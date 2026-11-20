# Benchmark

How fast the engine and the bots play: the reference every optimisation of
palier 5 is measured against. Measured on a MacBook (Apple M4), Release
builds.

```bash
search/build/search_bench      # C++, about 2 s per line
trainerlab bench               # Python, about 3 s per line
```

## 5.1: the starting point

| Line | Per second |
|---|---|
| random playout, Python loop | 48 803 turns |
| random playout, flat API (C++, as from Python) | 102 062 turns |
| random playout, C++ API | 881 435 turns |
| copy + one turn, C++ API (one lookahead step) | 732 198 turns |
| legal actions, both sides | 6 985 833 calls |
| observe | 20 861 248 calls |
| forecaster decision (rule 6) | **157 decisions** |

- **Python costs a factor of 2, the flat API a factor of 9.** The flat API
  validates and copies the state at every call: the search calls the engine's
  own C++ API.
- **The forecaster is the bottleneck**: 157 decisions a second, where the
  engine plays 730 000 lookahead steps.
- The two `expected damage` lines (12 279 calls in C++, 112 704 in Python) did
  not measure the same positions, and are left out. Since 5.2, both benchmarks
  play the same 64 positions (`search::openings`).

## 5.2: features in C++

The 249 features are computed in C++ too (`search/src/features.cpp`), value
for value the same as in Python, bit for bit on more than a thousand positions
(a test).

On the Mac, from Python and from C++ on the same 64 positions:

| Line | Per second |
|---|---|
| expected damage, C++ / Python | 21 056 / 20 707 calls |
| features of a position, C++ / Python | 2 759 / 2 534 positions |
| forecaster decision (rule 6) | 222 decisions |

The features of a position cost about the same in C++ as in Python: **both spend nearly all their time in the
eight `expected_damage` calls** a position needs. Porting the features alone
gains nothing; the cost is `expected_damage`.

Profiled with callgrind, `expected_damage` spends about a quarter of its time
looking up abilities by their name (a string hash, in the engine's damage
pipeline), a fifth allocating and freeing (events carry names as strings),
and a fifth in the walker of draws, which allocated two vectors per draw. The
walker now keeps a draw's outcomes on the stack, with the same arithmetic:
`expected_damage` is about 30 % faster, and its golden digest is unchanged.

## The engine's hot paths

Three changes inside the engine, each guarded by the golden tests (the same
battles and the same expected damage, bit for bit):

- a species' ability is resolved to its index once, by the loader, instead of
  being looked up by name, through a string hash, at every hit;
- `expected_damage` clears one event log between sequences of draws instead of
  building a new one each time;
- the catalog lookups (`moveByIndex`, `speciesByIndex`) are inline.

Measured with callgrind on the 64 positions (instructions, so free of the
machine's noise), `expected_damage` does **2.7 times less work** than before
5.2 (98 million instructions against 266). On the Mac:

| Line | 5.2 | Now |
|---|---|---|
| expected damage (C++) | 21 056 calls/s | 51 378 calls/s |
| features of a position (C++) | 2 719 positions/s | 6 875 positions/s |
| random playout, C++ API | 907 676 turns/s | 1 357 306 turns/s |
| forecaster decision | 222 decisions/s | 446 decisions/s |

The reference duel is unchanged (828 / 1 / 171): the bots play the same
moves, twice as fast.

## 5.3: rules 6 and 7 in C++

`forecaster` and `strategist` now decide in one call to C++
(`search/src/forecaster.cpp`): the turns played ahead, the foe's move as rule
5 plays it (with the bots' damage estimate, `search/src/estimate.cpp`), the
features and the model. The Python bot only hands over its view.

They make the same decisions as the Python version they replaced, kept by the
tests as the reference: every one of more than 2000 decisions checked, and the
reference duel unchanged (828 / 1 / 171). The odds themselves differ in the
last bit (numpy and C++ add the 249 terms in different orders), never enough
to change a choice on these decisions.

On this Linux machine, the reference duel (500 pairs) takes 44 s, against
2 min 05 s before 5.2. A decision is now dominated by the features of the
positions it rates: about 18 positions per decision, each with 8
`expected_damage` calls. That is where the next gain lies.

On the Mac: **659 forecaster decisions a second** (157 at 5.1, 4.2 times
more), and the reference duel in 5.6 s on its ten cores.

## 6.1: the encoding for networks

One vector per Pokémon, the field, the legal options (`search/src/encoding.cpp`).
It holds no expected damage: on this Linux machine it takes 1.6 million
positions a second, against 4500 for the features of the win model. The
network will have to learn damage from types, stats and moves, which a
network can; the time saved goes to playing more games.

## 6.2: the vector environment

256 battles at once, both sides played by the caller, a finished battle
starting again at once (`search/src/vec_env.cpp`). With random legal actions,
on this Linux machine, one core: about 217 000 battle steps a second in C++,
193 000 from Python (`learning/env.py`), some 3000 battles a second. The
environment will not be the bottleneck of self-play: the network will.

## 6.5b: the network in batches

Rule 11 spends nearly all its time in the network: two evaluations per
simulation, some 400 per decision. Three changes, in
`search/src/network.cpp`:

- **batches**: each layer reads its weights once for all the rows of a batch
  (the 12 Pokémon of a position, its 4 moves, its 6 switches, and both sides
  of a tree node) instead of once per row;
- **blocks sized for the registers**: 4 rows times 16 outputs kept in the 32
  NEON registers of the M4 (times 8 in SSE's 16 on x86), with vectors of four
  floats whose `+` and `*` are never fused;
- **computed once**: a foe not seen yet, or an empty place, always gives the
  network the same input; its output is computed when the network is built.

Each output still sums its inputs in order, one by one: a position gets the
same numbers alone or in any batch (a test), on the Mac and on Linux. Against
the version measured in rule 11, the logits move by at most 2e-5 and 239 of
240 decisions are the same (the other was a near tie): the bot is the same.

On the Mac (`search_bench`, random weights of the network's shapes, which
cost the same as trained ones; the planner timed from Python with
`selfplay.pt`):

| Line | Before | Now |
|---|---|---|
| planner decision, 200 simulations (rule 11) | 81.5 ms | **22.4 ms** |
| network evaluation | | 28 470 positions/s |
| network evaluation, batches of 16 | | 35 394 positions/s |

**The planner is 3.6 times faster** (1.8 on this Linux machine, whose SSE
blocks are half as wide). Larger batches gain little more: the network is now
limited by its arithmetic, not by reading its weights. Batching simulations of
one tree together would change the search for some 25 %; playing several games
at once, one per core, is the next lever.

## Palier 8: fused multiply-adds, Accelerate, battles side by side

All on the Mac M4 (10 cores), `search_bench 2`, and the planner and self-play
timed from Python with rule 12's network.

**8.1, fused multiply-adds.** On ARM, the kernel's multiply and add are one
instruction (`vfmaq_n_f32`, rounded once). The M4 and x86 no longer give the
same numbers bit for bit; each machine still gives a position the same numbers
alone or in any batch, and the 300 decisions checked were the same.

**8.2, Accelerate.** On macOS, the layers go through `cblas_sgemm` (Apple's
matrix unit), our kernel elsewhere (`-DSEARCH_BLAS=OFF` keeps it on the Mac).
BLAS sums in its own order: a position alone or in a batch differs by a float's
rounding, and the same batches still give the same numbers.

| Matrix product (GMAC/s) | positions | our kernel | Accelerate |
|---|---|---|---|
| Pokémon layer (265 → 128, 12 rows per position) | 1 | 57 | 445 |
| | 16 | 57 | 780 |
| trunk layer (529 → 256) | 1 | 18 | 188 |
| | 16 | 58 | 840 |

**8.3, battles side by side.** A search and a self-play battle now stop when
they wait on the network and resume with its outputs; each thread plays 16
battles side by side and evaluates what they all wait on in one batch, where
Accelerate is fastest. Battles 16g to 16g + 15 always go together: the rows are
the same for any number of threads, and with our kernel the same as battles
played one by one.

| Line | 8.0 | FMA (8.1) | + Accelerate (8.2) | + side by side (8.3) |
|---|---|---|---|---|
| planner decision, 1 thread (Python, 300 positions) | 16.4 ms | 12.1 ms | **5.9 ms** | 5.9 ms |
| network evaluation, batches of 16 | 35 503/s | 60 253/s | 285 392/s | |
| planner decisions, 10 threads | | 570/s | 622/s | |
| self-play, 10 threads (bench, 10 turns per battle) | | | 645/s | **1 643/s** |
| self-play, 160 full battles on 10 threads | | | 577/s | **1 162/s** |

Accelerate doubles one search but gains only 9 % on every core: the M4's matrix
units are shared and saturate. Batching across battles is what lifts them:
twice the self-play on every core. With 200 battles rather than 160, threads
wait for the last groups (1.79 times instead of 2.0): the loop plays a multiple
of 160 battles.

