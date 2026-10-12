// search.hpp — iterative-deepening alpha-beta.
//
// Ordered by how much Elo each part is worth, roughly:
//   1. move ordering (TT move, MVV-LVA, killers, history)  — the big one
//   2. quiescence search        — stops the engine hanging pieces
//   3. transposition table      — reuses work across iterations
//   4. null-move pruning        — big node reduction in the middlegame
//   5. late move reductions     — searches unlikely moves shallower
//   6. futility / delta pruning — skips hopeless quiet moves near the horizon
//
// Everything is negamax: scores are always from the side to move's point of
// view, and each ply negates.

#pragma once
#include "chess.hpp"
#include "eval.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <vector>

namespace search {

using namespace chess;

constexpr int MATE        = 32000;
constexpr int MATE_IN_MAX = MATE - 256;   // above this = a forced mate
constexpr int INF         = 32001;
constexpr int MAX_PLY     = 128;

// ---------------------------------------------------------------- TT

enum Bound : uint8_t { BOUND_NONE = 0, BOUND_EXACT, BOUND_LOWER, BOUND_UPPER };

struct TTEntry {
    uint64_t key   = 0;
    int32_t  score = 0;
    Move     move  = Move::NO_MOVE;
    int16_t  depth = -1;
    uint8_t  bound = BOUND_NONE;
};

class TranspositionTable {
public:
    void resize(size_t mb) {
        size_t bytes = mb * 1024 * 1024;
        count_ = std::max<size_t>(1024, bytes / sizeof(TTEntry));
        // Round down to a power of two so indexing is a mask, not a modulo.
        size_t p = 1;
        while (p * 2 <= count_) p *= 2;
        count_ = p;
        table_.assign(count_, TTEntry{});
    }
    void clear() { std::fill(table_.begin(), table_.end(), TTEntry{}); }

    TTEntry* probe(uint64_t key) {
        TTEntry& e = table_[key & (count_ - 1)];
        return e.key == key ? &e : nullptr;
    }
    void store(uint64_t key, int score, Move mv, int depth, uint8_t bound) {
        TTEntry& e = table_[key & (count_ - 1)];
        // Depth-preferred replacement, but always overwrite a different
        // position: a shallow entry for THIS position beats a deep entry for
        // some unrelated one that happens to collide.
        if (e.key != key || depth >= e.depth || bound == BOUND_EXACT) {
            e.key = key; e.score = score; e.move = mv;
            e.depth = static_cast<int16_t>(depth); e.bound = bound;
        }
    }
private:
    std::vector<TTEntry> table_;
    size_t count_ = 0;
};

// ---------------------------------------------------------------- limits

struct Limits {
    int64_t movetime_ms = 0;     // hard per-move budget
    int64_t wtime = 0, btime = 0, winc = 0, binc = 0;
    int     movestogo = 0;
    int     depth = 0;           // 0 = unlimited
    bool    infinite = false;
};

struct SearchInfo {
    uint64_t nodes = 0;
    int depth = 0, seldepth = 0, score = 0;
    std::vector<Move> pv;
};

// ---------------------------------------------------------------- engine

class Search {
public:
    Search() { tt_.resize(64); }

    void setHash(size_t mb) { tt_.resize(mb); }
    void newGame() { tt_.clear(); std::memset(history_, 0, sizeof(history_)); }
    void stop() { stop_ = true; }

    Move think(Board& board, const Limits& lim, bool print_info = true);

    uint64_t nodes() const { return nodes_; }

private:
    TranspositionTable tt_;
    std::atomic<bool> stop_{false};
    uint64_t nodes_ = 0;
    int seldepth_ = 0;
    std::chrono::steady_clock::time_point start_;
    int64_t budget_ms_ = 0;
    bool time_up_ = false;

    Move killers_[MAX_PLY][2];
    int  history_[2][64][64];
    Move pv_table_[MAX_PLY][MAX_PLY];
    int  pv_len_[MAX_PLY];

    int64_t elapsed() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start_).count();
    }
    bool checkTime() {
        if (stop_) { time_up_ = true; return true; }
        // Only poll the clock every 2048 nodes: calling steady_clock on every
        // node is itself a measurable slowdown.
        if ((nodes_ & 2047) == 0 && budget_ms_ > 0 && elapsed() >= budget_ms_) {
            time_up_ = true;
        }
        return time_up_;
    }

    int  quiescence(Board& b, int alpha, int beta, int ply);
    int  negamax(Board& b, int depth, int alpha, int beta, int ply, bool can_null);
    void scoreMoves(const Board& b, Movelist& moves, Move tt_move, int ply);
    int  see(const Board& b, Move m) const;
};

// ---------------------------------------------------------------- SEE

// Static Exchange Evaluation: is this capture actually winning material once
// both sides trade off on the square? Without it the engine happily plays
// QxP when the pawn is defended.
inline int Search::see(const Board& b, Move m) const {
    constexpr int V[7] = { 100, 320, 330, 500, 900, 20000, 0 };
    const Square to = m.to();

    auto pieceVal = [&](Square s) -> int {
        auto p = b.at(s);
        if (p == Piece::NONE) return 0;
        return V[static_cast<int>(p.type())];
    };

    int gain[32];
    int d = 0;
    gain[0] = (m.typeOf() == Move::ENPASSANT) ? V[0] : pieceVal(to);

    Bitboard occ = b.occ();
    Bitboard from_bb = Bitboard::fromSquare(m.from());
    occ &= ~from_bb;
    if (m.typeOf() == Move::ENPASSANT) {
        occ &= ~Bitboard::fromSquare(Square(to.index() ^ 8));
    }

    int captured = V[static_cast<int>(b.at(m.from()).type())];
    Color stm = ~b.sideToMove();

    // Recompute attackers to `to` for the current occupancy.
    auto attackersTo = [&](Square s, Bitboard occupancy) {
        return (attacks::pawn(Color::WHITE, s) & b.pieces(PieceType::PAWN, Color::BLACK)) |
               (attacks::pawn(Color::BLACK, s) & b.pieces(PieceType::PAWN, Color::WHITE)) |
               (attacks::knight(s) & b.pieces(PieceType::KNIGHT)) |
               (attacks::king(s)   & b.pieces(PieceType::KING))   |
               (attacks::bishop(s, occupancy) &
                    (b.pieces(PieceType::BISHOP) | b.pieces(PieceType::QUEEN))) |
               (attacks::rook(s, occupancy) &
                    (b.pieces(PieceType::ROOK) | b.pieces(PieceType::QUEEN)));
    };

    while (true) {
        Bitboard att = attackersTo(to, occ) & occ & b.us(stm);
        if (!att) break;

        // Always recapture with the least valuable attacker available.
        int bestType = -1;
        Square bestSq = Square::NO_SQ;
        for (int pt = 0; pt < 6 && bestType < 0; ++pt) {
            Bitboard cand = att & b.pieces(static_cast<PieceType::underlying>(pt), stm);
            if (cand) { bestType = pt; bestSq = cand.lsb(); }
        }
        if (bestType < 0) break;

        ++d;
        if (d >= 31) break;
        gain[d] = captured - gain[d - 1];
        captured = V[bestType];
        occ &= ~Bitboard::fromSquare(bestSq);
        stm = ~stm;
    }

    // Negamax back down the exchange: either side can stop capturing.
    while (--d > 0) gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
    return gain[0];
}

// ---------------------------------------------------------------- ordering

inline void Search::scoreMoves(const Board& b, Movelist& moves, Move tt_move, int ply) {
    for (auto& m : moves) {
        int s = 0;
        if (m == tt_move) {
            s = 10'000'000;                       // best move from a previous search
        } else if (b.isCapture(m)) {
            const int victim = (m.typeOf() == Move::ENPASSANT)
                ? 0 : static_cast<int>(b.at(m.to()).type());
            const int attacker = static_cast<int>(b.at(m.from()).type());
            const int mvvlva = 100 * (victim + 1) - attacker;
            // Losing captures get ordered AFTER quiet killers: trying QxP
            // (defended) before a killer move wastes the best cutoff slot.
            s = (see(b, m) >= 0 ? 8'000'000 : 2'000'000) + mvvlva;
        } else if (m.typeOf() == Move::PROMOTION) {
            s = 9'000'000 + static_cast<int>(m.promotionType());
        } else if (m == killers_[ply][0]) {
            s = 7'000'000;
        } else if (m == killers_[ply][1]) {
            s = 6'900'000;
        } else {
            s = history_[static_cast<int>(b.sideToMove())][m.from().index()][m.to().index()];
        }
        m.setScore(static_cast<int16_t>(std::clamp(s / 1000, -32000, 32000)));
        // Full-precision score kept in a parallel sort below.
    }
    // Sort by the same rule at full precision (setScore is only 16-bit).
    std::sort(moves.begin(), moves.end(), [&](const Move& a, const Move& c) {
        auto key = [&](const Move& m) -> int64_t {
            if (m == tt_move) return 10'000'000;
            if (b.isCapture(m)) {
                const int victim = (m.typeOf() == Move::ENPASSANT)
                    ? 0 : static_cast<int>(b.at(m.to()).type());
                const int attacker = static_cast<int>(b.at(m.from()).type());
                return (see(b, m) >= 0 ? 8'000'000 : 2'000'000)
                       + 100 * (victim + 1) - attacker;
            }
            if (m.typeOf() == Move::PROMOTION)
                return 9'000'000 + static_cast<int>(m.promotionType());
            if (m == killers_[ply][0]) return 7'000'000;
            if (m == killers_[ply][1]) return 6'900'000;
            return history_[static_cast<int>(b.sideToMove())][m.from().index()][m.to().index()];
        };
        return key(a) > key(c);
    });
}

// ---------------------------------------------------------------- quiescence

inline int Search::quiescence(Board& b, int alpha, int beta, int ply) {
    if (checkTime()) return 0;
    ++nodes_;
    seldepth_ = std::max(seldepth_, ply);
    if (ply >= MAX_PLY - 1) return eval::evaluate(b);

    const bool in_check = b.inCheck();
    int stand_pat = -INF;

    if (!in_check) {
        stand_pat = eval::evaluate(b);
        if (stand_pat >= beta) return stand_pat;
        if (stand_pat > alpha) alpha = stand_pat;
    }

    Movelist moves;
    if (in_check) movegen::legalmoves<movegen::MoveGenType::ALL>(moves, b);
    else          movegen::legalmoves<movegen::MoveGenType::CAPTURE>(moves, b);

    if (moves.empty()) {
        if (in_check) return -MATE + ply;      // checkmate
        return stand_pat;
    }

    scoreMoves(b, moves, Move::NO_MOVE, ply);

    for (const auto& m : moves) {
        if (!in_check) {
            // Skip captures that lose material outright.
            if (b.isCapture(m) && see(b, m) < 0) continue;
            // Delta pruning: even winning this piece wouldn't raise alpha.
            constexpr int V[7] = { 100, 320, 330, 500, 900, 20000, 0 };
            const int victim = (m.typeOf() == Move::ENPASSANT)
                ? 100 : V[static_cast<int>(b.at(m.to()).type())];
            if (stand_pat + victim + 200 < alpha && m.typeOf() != Move::PROMOTION) continue;
        }
        b.makeMove(m);
        const int score = -quiescence(b, -beta, -alpha, ply + 1);
        b.unmakeMove(m);
        if (time_up_) return 0;
        if (score >= beta) return score;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

// ---------------------------------------------------------------- negamax

inline int Search::negamax(Board& b, int depth, int alpha, int beta, int ply, bool can_null) {
    if (checkTime()) return 0;

    pv_len_[ply] = ply;

    if (ply > 0) {
        // Draw detection. Without this the engine will happily repeat moves in
        // a winning position, or fail to claim a draw when losing.
        if (b.isRepetition(1) || b.halfMoveClock() >= 100) return 0;
        if (b.isInsufficientMaterial()) return 0;

        // Mate distance pruning: if we already have a faster mate, don't look.
        alpha = std::max(alpha, -MATE + ply);
        beta  = std::min(beta,   MATE - ply - 1);
        if (alpha >= beta) return alpha;
    }

    const bool pv_node = (beta - alpha) > 1;
    const bool in_check = b.inCheck();
    if (in_check) ++depth;                      // check extension

    if (depth <= 0) return quiescence(b, alpha, beta, ply);

    ++nodes_;

    // --- transposition table ---
    Move tt_move = Move::NO_MOVE;
    if (TTEntry* e = tt_.probe(b.hash())) {
        tt_move = e->move;
        if (!pv_node && e->depth >= depth) {
            int s = e->score;
            if (s >  MATE_IN_MAX) s -= ply;
            if (s < -MATE_IN_MAX) s += ply;
            if (e->bound == BOUND_EXACT) return s;
            if (e->bound == BOUND_LOWER && s >= beta)  return s;
            if (e->bound == BOUND_UPPER && s <= alpha) return s;
        }
    }

    const int static_eval = in_check ? -INF : eval::evaluate(b);

    // --- reverse futility pruning ---
    // We're so far ahead that even giving back a chunk leaves us above beta.
    if (!pv_node && !in_check && depth <= 6 &&
        static_eval - 85 * depth >= beta && std::abs(beta) < MATE_IN_MAX) {
        return static_eval;
    }

    // --- null-move pruning ---
    // "If I pass and still can't be hurt, this node is good enough." Disabled
    // in check, in PV nodes, and in likely-zugzwang endgames where passing is
    // genuinely a good move and the assumption breaks.
    if (!pv_node && !in_check && can_null && depth >= 3 &&
        static_eval >= beta && std::abs(beta) < MATE_IN_MAX) {
        const Bitboard non_pawn =
            b.us(b.sideToMove()) & ~b.pieces(PieceType::PAWN, b.sideToMove()) &
            ~Bitboard::fromSquare(b.kingSq(b.sideToMove()));
        if (non_pawn) {
            const int R = 2 + depth / 4;
            b.makeNullMove();
            const int score = -negamax(b, depth - R - 1, -beta, -beta + 1, ply + 1, false);
            b.unmakeNullMove();
            if (time_up_) return 0;
            if (score >= beta) return std::abs(score) > MATE_IN_MAX ? beta : score;
        }
    }

    Movelist moves;
    movegen::legalmoves(moves, b);
    if (moves.empty()) return in_check ? -MATE + ply : 0;

    scoreMoves(b, moves, tt_move, ply);

    const int alpha_orig = alpha;
    int best = -INF;
    Move best_move = Move::NO_MOVE;
    int moves_searched = 0;

    for (const auto& m : moves) {
        const bool is_quiet = !b.isCapture(m) && m.typeOf() != Move::PROMOTION;

        // --- futility pruning: hopeless quiet moves near the horizon ---
        if (!pv_node && !in_check && is_quiet && depth <= 4 && best > -MATE_IN_MAX &&
            static_eval + 120 * depth + 100 < alpha) {
            continue;
        }

        b.makeMove(m);
        int score;

        if (moves_searched == 0) {
            score = -negamax(b, depth - 1, -beta, -alpha, ply + 1, true);
        } else {
            // --- late move reductions ---
            int R = 0;
            if (depth >= 3 && moves_searched >= 3 && is_quiet && !b.inCheck()) {
                R = 1 + (moves_searched > 6) + (depth > 6);
                if (pv_node) R = std::max(0, R - 1);
            }
            score = -negamax(b, depth - 1 - R, -alpha - 1, -alpha, ply + 1, true);
            // Re-search at full depth only if the reduced search beat alpha.
            if (score > alpha && R > 0)
                score = -negamax(b, depth - 1, -alpha - 1, -alpha, ply + 1, true);
            if (score > alpha && score < beta)
                score = -negamax(b, depth - 1, -beta, -alpha, ply + 1, true);
        }

        b.unmakeMove(m);
        if (time_up_) return 0;
        ++moves_searched;

        if (score > best) {
            best = score;
            best_move = m;
            if (score > alpha) {
                alpha = score;
                // Collect the principal variation for reporting.
                pv_table_[ply][ply] = m;
                for (int j = ply + 1; j < pv_len_[ply + 1]; ++j)
                    pv_table_[ply][j] = pv_table_[ply + 1][j];
                pv_len_[ply] = pv_len_[ply + 1];
            }
            if (alpha >= beta) {
                if (is_quiet) {
                    if (!(killers_[ply][0] == m)) {
                        killers_[ply][1] = killers_[ply][0];
                        killers_[ply][0] = m;
                    }
                    history_[static_cast<int>(b.sideToMove())]
                            [m.from().index()][m.to().index()] += depth * depth;
                }
                break;
            }
        }
    }

    // --- store ---
    int store = best;
    if (store >  MATE_IN_MAX) store += ply;
    if (store < -MATE_IN_MAX) store -= ply;
    const uint8_t bound = (best <= alpha_orig) ? BOUND_UPPER
                        : (best >= beta)       ? BOUND_LOWER : BOUND_EXACT;
    tt_.store(b.hash(), store, best_move, depth, bound);

    return best;
}

// ---------------------------------------------------------------- driver

inline Move Search::think(Board& board, const Limits& lim, bool print_info) {
    start_ = std::chrono::steady_clock::now();
    stop_ = false;
    time_up_ = false;
    nodes_ = 0;
    seldepth_ = 0;
    std::memset(killers_, 0, sizeof(killers_));
    std::memset(pv_len_, 0, sizeof(pv_len_));
    // History is decayed rather than cleared: it carries useful signal between
    // moves, but stale entries shouldn't dominate the new search.
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 64; ++i)
            for (int j = 0; j < 64; ++j) history_[c][i][j] /= 8;

    // --- time management ---
    if (lim.movetime_ms > 0) {
        budget_ms_ = lim.movetime_ms;
    } else if (lim.infinite || lim.depth > 0) {
        budget_ms_ = 0;
    } else {
        const bool white = board.sideToMove() == Color::WHITE;
        const int64_t t = white ? lim.wtime : lim.btime;
        const int64_t inc = white ? lim.winc : lim.binc;
        if (t > 0) {
            const int mtg = lim.movestogo > 0 ? lim.movestogo : 30;
            budget_ms_ = t / mtg + inc / 2;
            // Never burn more than a fifth of the clock on one move, and always
            // leave a safety margin so we don't flag.
            budget_ms_ = std::min(budget_ms_, t / 5);
            budget_ms_ = std::max<int64_t>(10, budget_ms_ - 30);
        } else {
            budget_ms_ = 1000;
        }
    }

    Movelist root_moves;
    movegen::legalmoves(root_moves, board);
    if (root_moves.empty()) return Move::NO_MOVE;
    Move best = root_moves[0];

    const int max_depth = lim.depth > 0 ? lim.depth : MAX_PLY - 2;
    int prev_score = 0;

    for (int depth = 1; depth <= max_depth; ++depth) {
        int alpha = -INF, beta = INF;
        int score;

        // --- aspiration windows ---
        // Assume this iteration lands near the previous one and search a narrow
        // window; widen only on failure. Cheaper than a full window every time.
        if (depth >= 4) {
            int delta = 25;
            alpha = prev_score - delta;
            beta  = prev_score + delta;
            while (true) {
                score = negamax(board, depth, alpha, beta, 0, true);
                if (time_up_) break;
                if (score <= alpha)      { alpha = std::max(-INF, alpha - delta); beta = (alpha + beta) / 2; }
                else if (score >= beta)  { beta  = std::min( INF, beta  + delta); }
                else break;
                delta += delta / 2;
                if (delta > 800) { alpha = -INF; beta = INF; }
            }
        } else {
            score = negamax(board, depth, alpha, beta, 0, true);
        }

        if (time_up_ && depth > 1) break;

        prev_score = score;
        if (pv_len_[0] > 0) best = pv_table_[0][0];

        if (print_info) {
            std::string pv;
            for (int i = 0; i < pv_len_[0]; ++i)
                pv += uci::moveToUci(pv_table_[0][i]) + " ";
            const int64_t ms = std::max<int64_t>(1, elapsed());
            std::cout << "info depth " << depth << " seldepth " << seldepth_;
            if (std::abs(score) > MATE_IN_MAX) {
                const int mate_in = (score > 0 ? (MATE - score + 1) : -(MATE + score)) / 2;
                std::cout << " score mate " << mate_in;
            } else {
                std::cout << " score cp " << score;
            }
            std::cout << " nodes " << nodes_
                      << " nps " << (nodes_ * 1000 / ms)
                      << " time " << ms
                      << " pv " << pv << std::endl;
        }

        if (std::abs(score) > MATE_IN_MAX) break;   // forced mate found
        // If most of the budget is gone, another iteration won't finish.
        if (budget_ms_ > 0 && elapsed() > budget_ms_ * 0.55) break;
    }

    return best;
}

}  // namespace search
