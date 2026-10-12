// eval.hpp — classical tapered evaluation.
//
// "Tapered" means every term is computed twice, once with middlegame weights
// and once with endgame weights, then blended by how much material remains.
// This matters more than any single term: a king belongs behind a pawn shield
// on move 20 and marching to the centre on move 60, and an untapered eval has
// to pick one and be wrong half the game.
//
// Scores are ALWAYS from White's point of view inside this file; the search
// negates once at the end for the side to move.

#pragma once
#include "chess.hpp"
#include <algorithm>
#include <array>

namespace eval {

using namespace chess;

// ---------------------------------------------------------------- material

// Middlegame / endgame piece values. Pawns are worth relatively more in the
// endgame (promotion potential), minor pieces slightly less.
constexpr int MG_VALUE[6] = {  82, 337, 365, 477, 1025, 0 };
constexpr int EG_VALUE[6] = {  94, 281, 297, 512,  936, 0 };

// Phase weights: how much each piece contributes to "still a middlegame".
constexpr int PHASE_INC[6] = { 0, 1, 1, 2, 4, 0 };
constexpr int TOTAL_PHASE  = 24;   // 4 knights + 4 bishops + 4 rooks*2 + 2 queens*4

// ---------------------------------------------------------------- PSTs
// Piece-square tables, White's perspective, index 0 = a1 .. 63 = h8.
// These come from the well-known PeSTO tables, which are tuned rather than
// hand-guessed and are worth a few hundred Elo over naive tables.

constexpr int MG_PAWN[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
    -35,  -1, -20, -23, -15,  24,  38, -22,
    -26,  -4,  -4, -10,   3,   3,  33, -12,
    -27,  -2,  -5,  12,  17,   6,  10, -25,
    -14,  13,   6,  21,  23,  12,  17, -23,
     -6,   7,  26,  31,  65,  56,  25, -20,
     98, 134,  61,  95,  68, 126,  34, -11,
      0,   0,   0,   0,   0,   0,   0,   0,
};
constexpr int EG_PAWN[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
     13,   8,   8,  10,  13,   0,   2,  -7,
      4,   7,  -6,   1,   0,  -5,  -1,  -8,
     13,   9,  -3,  -7,  -7,  -8,   3,  -1,
     32,  24,  13,   5,  -2,   4,  17,  17,
     94, 100,  85,  67,  56,  53,  82,  84,
    178, 173, 158, 134, 147, 132, 165, 187,
      0,   0,   0,   0,   0,   0,   0,   0,
};
constexpr int MG_KNIGHT[64] = {
   -105, -21, -58, -33, -17, -28, -19, -23,
    -29, -53, -12,  -3,  -1,  18, -14, -19,
    -23,  -9,  12,  10,  19,  17,  25, -16,
    -13,   4,  16,  13,  28,  19,  21,  -8,
     -9,  17,  19,  53,  37,  69,  18,  22,
    -47,  60,  37,  65,  84, 129,  73,  44,
    -73, -41,  72,  36,  23,  62,   7, -17,
   -167, -89, -34, -49,  61, -97, -15,-107,
};
constexpr int EG_KNIGHT[64] = {
    -29, -51, -23, -15, -22, -18, -50, -64,
    -42, -20, -10,  -5,  -2, -20, -23, -44,
    -23,  -3,  -1,  15,  10,  -3, -20, -22,
    -18,  -6,  16,  25,  16,  17,   4, -18,
    -17,   3,  22,  22,  22,  11,   8, -18,
    -24, -20,  10,   9,  -1,  -9, -19, -41,
    -25,  -8, -25,  -2,  -9, -25, -24, -52,
    -58, -38, -13, -28, -31, -27, -63, -99,
};
constexpr int MG_BISHOP[64] = {
    -33,  -3, -14, -21, -13, -12, -39, -21,
      4,  15,  16,   0,   7,  21,  33,   1,
      0,  15,  15,  15,  14,  27,  18,  10,
     -6,  13,  13,  26,  34,  12,  10,   4,
     -4,   5,  19,  50,  37,  37,   7,  -2,
    -16,  37,  43,  40,  35,  50,  37,  -2,
    -26,  16, -18, -13,  30,  59,  18, -47,
    -29,   4, -82, -37, -25, -42,   7,  -8,
};
constexpr int EG_BISHOP[64] = {
    -23,  -9, -23,  -5,  -9, -16,  -5, -17,
    -14, -18,  -7,  -1,   4,  -9, -15, -27,
    -12,  -3,   8,  10,  13,   3,  -7, -15,
     -6,   3,  13,  19,   7,  10,  -3,  -9,
     -3,   9,  12,   9,  14,  10,   3,   2,
      2,  -8,   0,  -1,  -2,   6,   0,   4,
     -8,  -4,   7, -12,  -3, -13,  -4, -14,
    -14, -21, -11,  -8,  -7,  -9, -17, -24,
};
constexpr int MG_ROOK[64] = {
    -19, -13,   1,  17,  16,   7, -37, -26,
    -44, -16, -20,  -9,  -1,  11,  -6, -71,
    -45, -25, -16, -17,   3,   0,  -5, -33,
    -36, -26, -12,  -1,   9,  -7,   6, -23,
    -24, -11,   7,  26,  24,  35,  -8, -20,
     -5,  19,  26,  36,  17,  45,  61,  16,
     27,  32,  58,  62,  80,  67,  26,  44,
     32,  42,  32,  51,  63,   9,  31,  43,
};
constexpr int EG_ROOK[64] = {
     -9,   2,   3,  -1,  -5, -13,   4, -20,
     -6,  -6,   0,   2,  -9,  -9, -11,  -3,
     -4,   0,  -5,  -1,  -7, -12,  -8, -16,
      3,   5,   8,   4,  -5,  -6,  -8, -11,
      4,   3,  13,   1,   2,   1,  -1,   2,
      7,   7,   7,   5,   4,  -3,  -5,  -3,
     11,  13,  13,  11,  -3,   3,   8,   3,
     13,  10,  18,  15,  12,  12,   8,   5,
};
constexpr int MG_QUEEN[64] = {
     -1, -18,  -9,  10, -15, -25, -31, -50,
    -35,  -8,  11,   2,   8,  15,  -3,   1,
    -14,   2, -11,  -2,  -5,   2,  14,   5,
     -9, -26,  -9, -10,  -2,  -4,   3,  -3,
    -27, -27, -16, -16,  -1,  17,  -2,   1,
    -13, -17,   7,   8,  29,  56,  47,  57,
    -24, -39,  -5,   1, -16,  57,  28,  54,
    -28,   0,  29,  12,  59,  44,  43,  45,
};
constexpr int EG_QUEEN[64] = {
    -33, -28, -22, -43,  -5, -32, -20, -41,
    -22, -23, -30, -16, -16, -23, -36, -32,
    -16, -27,  15,   6,   9,  17,  10,   5,
    -18,  28,  19,  47,  31,  34,  39,  23,
      3,  22,  24,  45,  57,  40,  57,  36,
    -20,   6,   9,  49,  47,  35,  19,   9,
    -17,  20,  32,  41,  58,  25,  30,   0,
     -9,  22,  22,  27,  27,  19,  10,  20,
};
constexpr int MG_KING[64] = {
    -15,  36,  12, -54,   8, -28,  24,  14,
      1,   7,  -8, -64, -43, -16,   9,   8,
    -14, -14, -22, -46, -44, -30, -15, -27,
    -49,  -1, -27, -39, -46, -44, -33, -51,
    -17, -20, -12, -27, -30, -25, -14, -36,
     -9,  24,   2, -16, -20,   6,  22, -22,
     29,  -1, -20,  -7,  -8,  -4, -38, -29,
    -65,  23,  16, -15, -56, -34,   2,  13,
};
constexpr int EG_KING[64] = {
    -53, -34, -21, -11, -28, -14, -24, -43,
    -27, -11,   4,  13,  14,   4,  -5, -17,
    -19,  -3,  11,  21,  23,  16,   7,  -9,
    -18,  -4,  21,  24,  27,  23,   9, -11,
     -8,  22,  24,  27,  26,  33,  26,   3,
     10,  17,  23,  15,  20,  45,  44,  13,
    -12,  17,  14,  17,  17,  38,  23,  11,
    -74, -35, -18, -18, -11,  15,   4, -17,
};

inline const int* MG_PST[6] = { MG_PAWN, MG_KNIGHT, MG_BISHOP, MG_ROOK, MG_QUEEN, MG_KING };
inline const int* EG_PST[6] = { EG_PAWN, EG_KNIGHT, EG_BISHOP, EG_ROOK, EG_QUEEN, EG_KING };

// ---------------------------------------------------------------- terms

constexpr int BISHOP_PAIR_MG   = 30, BISHOP_PAIR_EG   = 50;
constexpr int ROOK_OPEN_MG     = 25, ROOK_OPEN_EG     = 12;
constexpr int ROOK_SEMIOPEN_MG = 12, ROOK_SEMIOPEN_EG =  6;
constexpr int DOUBLED_MG       = -8, DOUBLED_EG       = -18;
constexpr int ISOLATED_MG      =-12, ISOLATED_EG      = -14;
// Passed pawn bonus by rank (from the mover's side). Endgame values dominate:
// a passer on the 6th is often decisive with few pieces left.
constexpr int PASSED_MG[8] = { 0,  5, 10, 18, 32, 60,  90, 0 };
constexpr int PASSED_EG[8] = { 0, 12, 22, 38, 62, 102, 150, 0 };
constexpr int MOBILITY_MG[6] = { 0, 4, 4, 2, 1, 0 };
constexpr int MOBILITY_EG[6] = { 0, 3, 4, 4, 2, 0 };
constexpr int KING_SHIELD_MG = 9;
// Attack weight per attacking piece type near the enemy king.
constexpr int KING_ATTACK_W[6] = { 0, 2, 2, 3, 5, 0 };

inline int mirror(int sq) { return sq ^ 56; }  // flip rank for Black's PST lookup

// Files adjacent to a given file, for isolated-pawn detection.
inline Bitboard fileBB(int f) { return Bitboard(0x0101010101010101ULL << f); }
inline Bitboard adjacentFiles(int f) {
    Bitboard bb(0ULL);
    if (f > 0) bb |= fileBB(f - 1);
    if (f < 7) bb |= fileBB(f + 1);
    return bb;
}

// Squares in front of `sq` on its own and adjacent files — the "passed pawn"
// mask. If no enemy pawn sits in this zone, the pawn is passed.
inline Bitboard passedMask(Color c, Square sq) {
    const int f = static_cast<int>(sq.file());
    const int r = static_cast<int>(sq.rank());
    Bitboard span = fileBB(f) | adjacentFiles(f);
    Bitboard ahead(0ULL);
    if (c == Color::WHITE) {
        for (int rr = r + 1; rr <= 7; ++rr) ahead |= Bitboard(0xFFULL << (rr * 8));
    } else {
        for (int rr = r - 1; rr >= 0; --rr) ahead |= Bitboard(0xFFULL << (rr * 8));
    }
    return span & ahead;
}

struct Trace { int mg = 0, eg = 0; };

// Evaluate one side; caller negates for Black.
inline void evaluateSide(const Board& b, Color c, Trace& t, int& phase) {
    const Color them = ~c;
    const Bitboard occ = b.occ();
    const Bitboard ourPawns   = b.pieces(PieceType::PAWN, c);
    const Bitboard theirPawns = b.pieces(PieceType::PAWN, them);
    const Square theirKing    = b.kingSq(them);
    const Bitboard kingZone   = attacks::king(theirKing) | Bitboard::fromSquare(theirKing);

    int attackUnits = 0, attackerCount = 0;

    for (int pt = 0; pt < 6; ++pt) {
        Bitboard bb = b.pieces(static_cast<PieceType::underlying>(pt), c);
        while (bb) {
            const Square sq = bb.pop();
            const int idx = (c == Color::WHITE) ? sq.index() : mirror(sq.index());

            t.mg += MG_VALUE[pt] + MG_PST[pt][idx];
            t.eg += EG_VALUE[pt] + EG_PST[pt][idx];
            phase += PHASE_INC[pt];

            // --- mobility and king attacks (sliders + knights) ---
            Bitboard att(0ULL);
            switch (pt) {
                case 1: att = attacks::knight(sq); break;
                case 2: att = attacks::bishop(sq, occ); break;
                case 3: att = attacks::rook(sq, occ); break;
                case 4: att = attacks::queen(sq, occ); break;
                default: break;
            }
            if (pt >= 1 && pt <= 4) {
                const Bitboard moves = att & ~b.us(c);
                const int m = moves.count();
                t.mg += MOBILITY_MG[pt] * m;
                t.eg += MOBILITY_EG[pt] * m;
                if (att & kingZone) {
                    attackUnits += KING_ATTACK_W[pt] * (att & kingZone).count();
                    ++attackerCount;
                }
            }

            // --- pawn structure ---
            if (pt == 0) {
                const int f = static_cast<int>(sq.file());
                if ((fileBB(f) & ourPawns).count() > 1) {
                    t.mg += DOUBLED_MG; t.eg += DOUBLED_EG;
                }
                if (!(adjacentFiles(f) & ourPawns)) {
                    t.mg += ISOLATED_MG; t.eg += ISOLATED_EG;
                }
                if (!(passedMask(c, sq) & theirPawns)) {
                    const int r = static_cast<int>(sq.rank());
                    const int rel = (c == Color::WHITE) ? r : 7 - r;
                    t.mg += PASSED_MG[rel]; t.eg += PASSED_EG[rel];
                }
            }

            // --- rooks on open / semi-open files ---
            if (pt == 3) {
                const Bitboard f = fileBB(static_cast<int>(sq.file()));
                if (!(f & ourPawns)) {
                    if (!(f & theirPawns)) { t.mg += ROOK_OPEN_MG;     t.eg += ROOK_OPEN_EG; }
                    else                   { t.mg += ROOK_SEMIOPEN_MG; t.eg += ROOK_SEMIOPEN_EG; }
                }
            }
        }
    }

    // Bishop pair.
    if (b.pieces(PieceType::BISHOP, c).count() >= 2) {
        t.mg += BISHOP_PAIR_MG; t.eg += BISHOP_PAIR_EG;
    }

    // King safety: pawn shield in front of our own king (middlegame only).
    const Square ourKing = b.kingSq(c);
    const Bitboard shield = attacks::king(ourKing) & ourPawns;
    t.mg += KING_SHIELD_MG * shield.count();

    // Pressure on the enemy king. Only counts once several pieces join in —
    // a lone attacker is not an attack, and rewarding it makes the engine
    // throw pieces at the king pointlessly.
    if (attackerCount >= 2) {
        t.mg += attackUnits * attackUnits / 12;
    }
}

// Full evaluation, from the SIDE TO MOVE's point of view (negamax convention).
inline int evaluate(const Board& b) {
    Trace w, bl;
    int phase = 0;
    evaluateSide(b, Color::WHITE, w, phase);
    evaluateSide(b, Color::BLACK, bl, phase);

    const int mg = w.mg - bl.mg;
    const int eg = w.eg - bl.eg;

    // Blend by remaining material. phase == TOTAL_PHASE is a full board.
    const int p = std::min(phase, TOTAL_PHASE);
    int score = (mg * p + eg * (TOTAL_PHASE - p)) / TOTAL_PHASE;

    // Small tempo bonus: having the move is worth something.
    score += (b.sideToMove() == Color::WHITE) ? 10 : -10;

    return (b.sideToMove() == Color::WHITE) ? score : -score;
}

}  // namespace eval
