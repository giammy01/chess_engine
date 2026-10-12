#!/usr/bin/env python3
"""
gauntlet.py — measure engine strength against calibrated Stockfish opponents.

Why not just count wins: a 10-game match has an error bar of roughly +/-200
Elo, which is wide enough to "prove" almost anything. This script reports a
confidence interval alongside every estimate so you can see when a result is
real and when it's noise.

Two rules for trustworthy numbers:
  1. The estimate closest to a 50% score is the reliable one. Extrapolating
     from a 10% or 90% score is nearly meaningless.
  2. Different opponents should agree. If 1800 says 2100 and 2200 says 1700,
     the measurement is broken, not the engine.

Usage:
  python3 gauntlet.py --engine ./build/nitro --sweep 1800,2000,2200 --games 20
  python3 gauntlet.py --engine ./build/nitro --opponent 2200 --games 100 --tc 10+0.1
  python3 gauntlet.py --engine ./build/nitro --opponent 2200 --games 100 --concurrency 4
"""

from __future__ import annotations

import argparse
import math
import multiprocessing as mp
import os
import random
import sys
import time
from datetime import datetime

import chess
import chess.engine
import chess.pgn

# A handful of balanced openings. Playing every game from the start position
# means replaying the same game repeatedly once both engines are deterministic,
# which measures one line rather than overall strength.
OPENINGS = [
    "", "e4 e5", "e4 c5", "e4 e6", "e4 c6", "d4 d5", "d4 Nf6", "d4 e6",
    "c4 e5", "c4 Nf6", "Nf3 d5", "e4 d5", "d4 f5", "e4 Nf6", "c4 c5", "Nf3 Nf6",
]


def parse_tc(tc: str):
    """'10+0.1' -> (base_seconds, increment_seconds); '0.1' -> fixed movetime."""
    if "+" in tc:
        base, inc = tc.split("+")
        return float(base), float(inc), False
    return float(tc), 0.0, True


def elo_from_score(score: float, n: int, opp_elo: float):
    """Elo estimate plus a 95% interval, both relative to the opponent."""
    if n == 0:
        return None, None, None
    s = min(max(score / n, 0.5 / n), 1 - 0.5 / n)
    est = opp_elo + 400 * math.log10(s / (1 - s))
    # Standard error of the score fraction, propagated through the Elo curve.
    se = math.sqrt(max(s * (1 - s), 1e-9) / n)
    lo_s = min(max(s - 1.96 * se, 1e-6), 1 - 1e-6)
    hi_s = min(max(s + 1.96 * se, 1e-6), 1 - 1e-6)
    lo = opp_elo + 400 * math.log10(lo_s / (1 - lo_s))
    hi = opp_elo + 400 * math.log10(hi_s / (1 - hi_s))
    return est, lo, hi


def play_game(args):
    (engine_cmd, sf_path, opp_elo, game_idx, tc, hash_mb, seed) = args
    base, inc, fixed = tc
    random.seed(seed)

    me = chess.engine.SimpleEngine.popen_uci(engine_cmd)
    sf = chess.engine.SimpleEngine.popen_uci(sf_path)
    try:
        me.configure({"Hash": hash_mb})
    except Exception:
        pass
    sf.configure({"UCI_LimitStrength": True, "UCI_Elo": int(opp_elo)})

    board = chess.Board()
    opening = OPENINGS[game_idx % len(OPENINGS)]
    for san in opening.split():
        try:
            board.push_san(san)
        except Exception:
            break

    mine_white = (game_idx % 2 == 0)
    wclock = bclock = base
    plies = 0
    flagged = None

    while not board.is_game_over(claim_draw=True) and plies < 400:
        is_mine = board.turn == (chess.WHITE if mine_white else chess.BLACK)
        eng = me if is_mine else sf
        if fixed:
            limit = chess.engine.Limit(time=base)
        else:
            limit = chess.engine.Limit(
                white_clock=max(0.02, wclock), black_clock=max(0.02, bclock),
                white_inc=inc, black_inc=inc)
        t0 = time.time()
        try:
            res = eng.play(board, limit)
        except Exception:
            break
        used = time.time() - t0
        if res.move is None:
            break
        if not fixed:
            if board.turn == chess.WHITE:
                wclock -= used - inc
                if wclock <= 0: flagged = chess.WHITE; break
            else:
                bclock -= used - inc
                if bclock <= 0: flagged = chess.BLACK; break
        board.push(res.move)
        plies += 1

    if flagged is not None:
        result = "0-1" if flagged == chess.WHITE else "1-0"
    else:
        result = board.result(claim_draw=True)
        if result == "*":
            result = "1/2-1/2"    # hit the ply cap; score as a draw

    pt = 0.5 if result == "1/2-1/2" else (1.0 if (result == "1-0") == mine_white else 0.0)

    game = chess.pgn.Game.from_board(board)
    game.headers["Event"] = f"Gauntlet vs SF {opp_elo}"
    game.headers["White"] = "Nitro" if mine_white else f"Stockfish {opp_elo}"
    game.headers["Black"] = f"Stockfish {opp_elo}" if mine_white else "Nitro"
    game.headers["Result"] = result
    game.headers["Date"] = datetime.now().strftime("%Y.%m.%d")

    me.quit(); sf.quit()
    return pt, result, plies, str(game)


def run_match(engine_cmd, sf_path, opp_elo, games, tc, concurrency, hash_mb, pgn_file):
    tasks = [(engine_cmd, sf_path, opp_elo, i, tc, hash_mb, 1000 + i) for i in range(games)]
    score = 0.0
    w = d = l = 0
    done = 0
    t0 = time.time()
    pgns = []

    def report():
        est, lo, hi = elo_from_score(score, done, opp_elo)
        pct = 100.0 * score / done
        sys.stdout.write(
            f"\r  vs {opp_elo}: {done}/{games}  {w}W {d}D {l}L  "
            f"{score:.1f} ({pct:.0f}%)  est {est:.0f} [{lo:.0f}..{hi:.0f}]  "
            f"{time.time()-t0:.0f}s   ")
        sys.stdout.flush()

    if concurrency > 1:
        with mp.Pool(concurrency) as pool:
            for pt, result, plies, pgn in pool.imap_unordered(play_game, tasks):
                score += pt; done += 1
                w += pt == 1.0; d += pt == 0.5; l += pt == 0.0
                pgns.append(pgn)
                report()
    else:
        for t in tasks:
            pt, result, plies, pgn = play_game(t)
            score += pt; done += 1
            w += pt == 1.0; d += pt == 0.5; l += pt == 0.0
            pgns.append(pgn)
            report()

    print()
    if pgn_file:
        with open(pgn_file, "a") as f:
            for p in pgns:
                f.write(p + "\n\n")

    est, lo, hi = elo_from_score(score, games, opp_elo)
    return {"opp": opp_elo, "score": score, "games": games, "w": w, "d": d, "l": l,
            "est": est, "lo": lo, "hi": hi}


def auto_calibrate(engine_cmd, sf_path, games, tc, concurrency, hash_mb, pgn_file,
                   start=2200, lo=1320, hi=3190):
    """
    Find the opponent strength where the engine scores about 50%, then measure
    there properly.

    Why bother: an Elo estimate from a 90% score is a long extrapolation off the
    end of the curve, and small changes in score swing it by hundreds of points.
    At 50% the curve is flat and the estimate is stable, so a short match at the
    right level beats a long match at the wrong one.
    """
    print("=== Phase 1: bracketing (short matches, finding the 50% point) ===\n")
    probe = max(6, games // 3)
    level = start
    tried = {}

    for step in range(6):
        level = max(lo, min(hi, int(level)))
        if level in tried:
            break
        r = run_match(engine_cmd, sf_path, level, probe, tc, concurrency,
                      hash_mb, None)
        frac = r["score"] / r["games"]
        tried[level] = frac

        if 0.35 <= frac <= 0.65:
            print(f"\n  bracketed at {level} ({100*frac:.0f}%)\n")
            break
        # Move by roughly the Elo the score implies, damped, so a lopsided
        # result jumps far and a near-miss steps gently.
        s = min(max(frac, 0.05), 0.95)
        delta = 400 * math.log10(s / (1 - s))
        level = level + max(-500, min(500, delta * 0.8))
        if level >= hi:
            print(f"\n  Beyond Stockfish's ceiling ({hi}). "
                  f"Your engine is stronger than this test can measure.\n")
            level = hi
            break
        if level <= lo:
            level = lo
            break

    level = max(lo, min(hi, int(level)))
    print(f"=== Phase 2: measuring at {level} with {games} games ===\n")
    final = run_match(engine_cmd, sf_path, level, games, tc, concurrency,
                      hash_mb, pgn_file)
    return [final]


def main():
    ap = argparse.ArgumentParser(description="Measure engine strength vs Stockfish.")
    ap.add_argument("--engine", default="./build/nitro")
    ap.add_argument("--stockfish", default="stockfish")
    ap.add_argument("--opponent", type=int, default=0, help="single opponent Elo")
    ap.add_argument("--sweep", default="", help="comma list, e.g. 1800,2000,2200")
    ap.add_argument("--games", type=int, default=20)
    ap.add_argument("--tc", default="10+0.1",
                    help="'base+inc' in seconds, or a bare number for fixed movetime")
    ap.add_argument("--concurrency", type=int, default=1,
                    help="parallel games. Keep at 1 for the most accurate timing; "
                         "raise it for speed once you trust the result")
    ap.add_argument("--hash", type=int, default=64, help="engine hash in MB")
    ap.add_argument("--pgn", default="gauntlet.pgn", help="append games here ('' to skip)")
    ap.add_argument("--auto", action="store_true",
                    help="automatically find the opponent level where you score "
                         "~50%, then measure there. The reliable way to get a number.")
    args = ap.parse_args()

    if not os.path.exists(args.engine):
        sys.exit(f"engine not found: {args.engine}")

    # Resolve Stockfish before launching anything. Failing here with a clear
    # message beats an asyncio traceback from deep inside a worker process —
    # and on Ubuntu the apt package installs to /usr/games, which is not on
    # PATH for non-interactive shells.
    from shutil import which
    sf = which(args.stockfish) or (args.stockfish if os.path.exists(args.stockfish) else None)
    if sf is None:
        for cand in ("/usr/games/stockfish", "/usr/local/bin/stockfish",
                     "/usr/bin/stockfish", "/opt/homebrew/bin/stockfish"):
            if os.path.exists(cand):
                sf = cand
                break
    if sf is None:
        sys.exit("Stockfish not found. Install it (sudo apt install stockfish) "
                 "or pass --stockfish /path/to/stockfish")
    args.stockfish = sf
    print(f"Using Stockfish: {sf}")

    tc = parse_tc(args.tc)
    opponents = ([int(x) for x in args.sweep.split(",")] if args.sweep
                 else [args.opponent or 2000])

    print(f"Engine: {args.engine}   TC: {args.tc}   "
          f"{args.games} games/opponent   concurrency {args.concurrency}\n")

    if args.auto:
        results = auto_calibrate(args.engine, args.stockfish, args.games, tc,
                                 args.concurrency, args.hash, args.pgn,
                                 start=args.opponent or 2200)
    else:
        results = []
        for opp in opponents:
            results.append(run_match(args.engine, args.stockfish, opp, args.games,
                                     tc, args.concurrency, args.hash, args.pgn))

    print("\n================ SUMMARY ================")
    for r in results:
        print(f"  vs {r['opp']:4d}: {r['w']:3d}W {r['d']:3d}D {r['l']:3d}L  "
              f"{100*r['score']/r['games']:5.1f}%   "
              f"est {r['est']:.0f} Elo  (95%: {r['lo']:.0f}..{r['hi']:.0f})")

    # Pick the anchor nearest a 50% score — but only if one is actually near it.
    best = min(results, key=lambda r: abs(r["score"] / r["games"] - 0.5))
    best_pct = 100 * best["score"] / best["games"]
    # Saturation means the result carries almost no information about *how much*
    # stronger or weaker you are. A 33% score is a perfectly usable measurement,
    # so these thresholds have to sit near the ends, not near the middle.
    all_high = all(r["score"] / r["games"] > 0.85 for r in results)
    all_low = all(r["score"] / r["games"] < 0.15 for r in results)

    if all_high:
        # Every opponent lost badly: these are LOWER BOUNDS, not estimates, and
        # the nearest-to-50% rule would perversely report the weakest one.
        strongest = max(results, key=lambda r: r["opp"])
        print(f"\n  You beat every opponent tested. These are lower bounds, not "
              f"estimates —\n  the engine is clearly stronger than "
              f"{strongest['opp']}, best guess ~{strongest['est']:.0f} Elo.")
        nxt = int(round((strongest["est"] + 150) / 100.0) * 100)
        print(f"  Re-run against stronger opposition to pin it down:\n"
              f"      --sweep {nxt-200},{nxt},{nxt+200}")
    elif all_low:
        weakest = min(results, key=lambda r: r["opp"])
        print(f"\n  You lost to every opponent tested. These are upper bounds. "
              f"Try weaker\n  opposition: --sweep {max(1320, weakest['opp']-400)},"
              f"{max(1320, weakest['opp']-200)}")
    else:
        print(f"\n  Most reliable anchor: vs {best['opp']} "
              f"({best_pct:.0f}%)  ->  ~{best['est']:.0f} Elo")
        width = best["hi"] - best["lo"]
        if width > 150:
            need = int(best["games"] * (width / 100.0) ** 2)
            print(f"  Interval is +/-{width/2:.0f} Elo — wide. "
                  f"~{need} games would narrow it to about +/-50.")

    if len(results) > 1 and not (all_high or all_low):
        spread = max(r["est"] for r in results) - min(r["est"] for r in results)
        if spread > 250:
            print(f"  WARNING: estimates disagree by {spread:.0f} Elo. "
                  f"That usually means too few games, or a time-control problem.")
    if args.pgn:
        print(f"\n  Games saved to {args.pgn} — review losses to find weaknesses.")


if __name__ == "__main__":
    main()