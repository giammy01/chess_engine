// main.cpp — UCI front end.
//
// Speaking UCI is what makes this engine usable: it plugs straight into
// cutechess-cli, Arena, En Croissant, and python-chess, so strength testing
// uses standard tooling instead of a bespoke harness.
//
// Extra non-UCI commands: `perft N`, `bench`, `selftest`, `eval`.

#include "chess.hpp"
#include "eval.hpp"
#include "search.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace chess;

static const char* ENGINE_NAME = "Nitro 1.0";
static const char* ENGINE_AUTH = "giammy";

static Board g_board;
static search::Search g_search;

// ---------------------------------------------------------------- perft

static uint64_t perft(Board& b, int depth) {
    Movelist moves;
    movegen::legalmoves(moves, b);
    if (depth == 1) return moves.size();
    uint64_t n = 0;
    for (const auto& m : moves) { b.makeMove(m); n += perft(b, depth - 1); b.unmakeMove(m); }
    return n;
}

static void runPerft(int depth) {
    auto t0 = std::chrono::steady_clock::now();
    uint64_t n = perft(g_board, depth);
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "perft(" << depth << ") = " << n
              << "  " << (uint64_t)(n / std::max(sec, 1e-9)) << " nps\n";
}

// ---------------------------------------------------------------- selftest

struct TacticTest { const char* fen; const char* best; const char* desc; };

// Every expected move below is a forced tactic, verified against Stockfish.
static const TacticTest TACTICS[] = {
    {"6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1", "a1a8", "back-rank mate in 1"},
    {"r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5Q2/PPPP1PPP/RNB1K1NR w KQkq - 4 4",
     "f3f7", "scholar's mate in 1"},
    {"rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq - 0 2",
     "d8h4", "fool's mate in 1"},
    {"2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1",
     "g3g6", "mate in 2"},
    {"6k1/pp3ppp/8/8/8/8/PP3PPP/2R3K1 w - - 0 1", "c1c8", "rook endgame mate in 1"},
    {"4k3/8/2p5/3p4/8/3R4/8/4K3 w - - 0 1", "!d3d5", "must NOT grab poisoned pawn"},
};

static void runSelftest(int movetime) {
    int passed = 0, failed = 0;
    std::cout << "Tactical self-test (" << movetime << "ms/move)\n" << std::endl;
    for (const auto& t : TACTICS) {
        Board b(t.fen);
        search::Limits lim; lim.movetime_ms = movetime;
        g_search.newGame();
        Move mv = g_search.think(b, lim, false);
        std::string got = uci::moveToUci(mv);
        bool ok;
        std::string want = t.best;
        if (!want.empty() && want[0] == '!') {          // "must not play X"
            ok = (got != want.substr(1));
        } else {
            ok = (got == want);
        }
        ok ? ++passed : ++failed;
        std::cout << "  [" << (ok ? "PASS" : "FAIL") << "] " << t.desc
                  << "  played " << got << "  want " << t.best << "\n";
    }
    std::cout << "\n  " << passed << " passed, " << failed << " failed\n";
}

static void runBench() {
    const char* fens[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r2q1rk1/pp2ppbp/2np1np1/2p3B1/2B1P3/2NP1N2/PPP2PPP/R2Q1RK1 w - - 0 1",
    };
    uint64_t total = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (const char* f : fens) {
        Board b(f);
        search::Limits lim; lim.movetime_ms = 3000;
        g_search.newGame();
        g_search.think(b, lim, true);
        total += g_search.nodes();
        std::cout << "---\n";
    }
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::cout << "bench: " << total << " nodes in " << sec << "s  "
              << (uint64_t)(total / sec) << " nps\n";
}

// ---------------------------------------------------------------- UCI

static void parsePosition(std::istringstream& is) {
    std::string token, fen;
    is >> token;
    if (token == "startpos") {
        g_board = Board(constants::STARTPOS);
        is >> token;                       // consume "moves" if present
    } else if (token == "fen") {
        while (is >> token && token != "moves") fen += token + " ";
        g_board = Board(fen);
    }
    if (token == "moves") {
        while (is >> token) {
            Move m = uci::uciToMove(g_board, token);
            if (m == Move::NO_MOVE) break;
            g_board.makeMove(m);
        }
    }
}

static void parseGo(std::istringstream& is) {
    search::Limits lim;
    std::string token;
    while (is >> token) {
        if      (token == "wtime")     is >> lim.wtime;
        else if (token == "btime")     is >> lim.btime;
        else if (token == "winc")      is >> lim.winc;
        else if (token == "binc")      is >> lim.binc;
        else if (token == "movestogo") is >> lim.movestogo;
        else if (token == "movetime")  is >> lim.movetime_ms;
        else if (token == "depth")     is >> lim.depth;
        else if (token == "infinite")  lim.infinite = true;
    }
    Move best = g_search.think(g_board, lim, true);
    std::cout << "bestmove " << uci::moveToUci(best) << std::endl;
}

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    g_board = Board(constants::STARTPOS);

    // Allow `./nitro bench` / `selftest` / `perft N` straight from the shell.
    if (argc > 1) {
        std::string cmd = argv[1];
        if (cmd == "bench")    { runBench(); return 0; }
        if (cmd == "selftest") { runSelftest(argc > 2 ? std::stoi(argv[2]) : 2000); return 0; }
        if (cmd == "perft")    { runPerft(argc > 2 ? std::stoi(argv[2]) : 5); return 0; }
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream is(line);
        std::string token;
        is >> token;

        if (token == "uci") {
            std::cout << "id name " << ENGINE_NAME << "\n"
                      << "id author " << ENGINE_AUTH << "\n"
                      << "option name Hash type spin default 64 min 1 max 4096\n"
                      << "uciok" << std::endl;
        } else if (token == "isready") {
            std::cout << "readyok" << std::endl;
        } else if (token == "ucinewgame") {
            g_search.newGame();
            g_board = Board(constants::STARTPOS);
        } else if (token == "setoption") {
            std::string name, value, w;
            is >> w;                       // "name"
            while (is >> w && w != "value") name += (name.empty() ? "" : " ") + w;
            is >> value;
            if (name == "Hash") g_search.setHash(std::stoul(value));
        } else if (token == "position") {
            parsePosition(is);
        } else if (token == "go") {
            parseGo(is);
        } else if (token == "stop") {
            g_search.stop();
        } else if (token == "quit") {
            break;
        } else if (token == "perft") {
            int d = 5; is >> d; runPerft(d);
        } else if (token == "bench") {
            runBench();
        } else if (token == "selftest") {
            int mt = 2000; is >> mt; runSelftest(mt);
        } else if (token == "eval") {
            std::cout << "eval " << eval::evaluate(g_board) << " cp (side to move)\n";
        } else if (token == "d") {
            std::cout << g_board << std::endl;
        }
    }
    return 0;
}
