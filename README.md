# Nitro — classical chess engine in C++

A UCI chess engine: bitboard move generation, tapered classical evaluation,
alpha-beta search with quiescence. No neural network.

**Measured strength: ~2200 Elo**, at only 0.08s/move.

| Opponent | Result | Score | Implied Elo |
|---|---|---|---|
| Stockfish `UCI_Elo` 2000 | 5W 2D 1L | 75% | ~2190 |
| Stockfish `UCI_Elo` 2200 | 2W 4D 2L | 50% | ~2200 |

Both anchors agree, which is the part that matters — a strength estimate is
only trustworthy when different opponents point to the same number. Note these
are 8-game matches (roughly ±150 Elo); run 100 games to tighten that.

For comparison, on the same hardware:

| Engine | Speed | Depth reached |
|---|---|---|
| Python + conv net (alpha-beta) | 300 nps | 1–2 |
| Python + classical eval | 14,400 nps | 4–7 |
| **This engine (C++)** | **1,170,000 nps** | **10–20** |

## Build

```bash
mkdir -p build && cd build
cmake .. && make -j$(nproc)
./nitro
```

Or without cmake:

```bash
g++ -O3 -march=native -std=c++17 -o nitro src/main.cpp
```

`-O3 -march=native` is not optional — expect roughly half the speed without it,
which costs about a full ply of depth.

## Verify

```bash
./nitro perft 5        # move generation: must print 4865609
./nitro selftest 1000  # tactics: 6/6 must pass
./nitro bench          # speed and depth
```

`perft` is the foundation: if move generation is wrong, every other number is
meaningless. All four standard perft positions pass.

## Play against it

It speaks UCI, so it drops into any standard GUI or tool:

```bash
# CuteChess GUI, Arena, En Croissant: add ./nitro as a UCI engine
# or play from the terminal:
./nitro
position startpos moves e2e4
go movetime 2000
```

Rating matches with cutechess-cli:

```bash
cutechess-cli -engine cmd=./nitro -engine cmd=stockfish \
    option.UCI_LimitStrength=true option.UCI_Elo=2200 \
    -each proto=uci tc=10+0.1 -games 100 -concurrency 4
```

## What's inside

**`src/chess.hpp`** — move generation (Disservin's chess-library, MIT). Magic
bitboards, ~200M nps in perft. Vendored deliberately: writing a bug-free legal
move generator is weeks of work and buys zero Elo over a correct existing one.
The Elo is in search and evaluation, which is where all the code below went.

**`src/eval.hpp`** — tapered evaluation. Every term is computed with middlegame
and endgame weights, then blended by remaining material, because a king belongs
behind a pawn shield on move 20 and in the centre on move 60. Terms: material,
piece-square tables (PeSTO), mobility, passed/doubled/isolated pawns, bishop
pair, rooks on open files, king safety.

**`src/search.hpp`** — iterative-deepening alpha-beta. In rough order of Elo
value: move ordering (TT move, SEE-filtered MVV-LVA, killers, history),
quiescence search with SEE and delta pruning, transposition table, null-move
pruning, late move reductions, futility and reverse-futility pruning, check
extensions, aspiration windows, mate-distance pruning.

**`src/main.cpp`** — UCI protocol plus `perft`, `bench`, `selftest`, `eval`.

## Files to keep from the old project

**Keep:**

| File | Why |
|---|---|
| `elo_harness.py` | Strength measurement. Point it at the `nitro` binary via `chess.engine.SimpleEngine.popen_uci` instead of `NetEngine` — much simpler than before, since the engine now speaks UCI |
| `search.py` | The tested Python prototype this was ported from. Useful reference; delete once you trust the C++ |
| Stockfish binary | Your measuring stick |

**Archive (don't delete outright — it's months of work):**

Everything neural: `model.py`, `supervised_train.py`, `finetune_stockfish.py`,
`stockfish_relabel.py`, `data_pipeline.py`, `move_encoding.py`, `utils.py`,
`train.py`, `mcts.py`, `play_model.py`, `chess_model*.pth`,
`traced_chess_model.pt`, `data_shards*/`, `*checkpoints/`, and the
corresponding `test_encoding.py`, `test_model.py`, `test_pipeline.py`,
`test_supervised.py`.

**Now genuinely dead:**

* `libtorch/` and `libtorch-shared-with-deps-*.zip` (~3.7 GB) — no network, no
  libtorch. This is the big disk win.
* `lichess_db_*.pgn.zst` (~1.4 GB) and `fetch_months.sh` — no training data
  needed.
* `chess_gui.py`, the old `main.cpp` stub, `build/`.

Use `cleanup.sh --drop-cpp --tier2` to quarantine these, verify the engine
still builds and passes `perft`, then `--purge`.

## Roadmap past 2200

Cheapest Elo first:

1. **Longer time control.** These results are at 0.08s/move. At 1–2s/move the
   engine is 2–3 ply deeper, worth roughly +150–250.
2. **Opening book.** A small Polyglot book removes early inaccuracies and saves
   clock. Worth ~50.
3. **Eval tuning.** The PSTs are PeSTO defaults. Tuning term weights against
   labelled positions (Texel tuning) is typically worth +100–200 — and your
   `data_shards_sf/` Stockfish-labelled positions are already exactly the
   dataset that needs, so the old work isn't wasted.
4. **Search refinements.** Singular extensions, better LMR formula,
   counter-move history, multi-cut. +100–150 combined.
5. **Syzygy tablebases.** Perfect endgame play from 5–6 pieces. +50–100.
6. **NNUE.** The natural end point if you want 2600+: replace `eval::evaluate`
   with a small incrementally-updated net. Note this is where your original
   neural instinct was right — it just needed the NNUE architecture (microsecond
   inference) rather than a conv net (millisecond inference) to work inside a
   search.

Items 1–3 alone should reach ~2500 without any new architecture.
