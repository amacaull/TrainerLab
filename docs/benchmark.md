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

On this Linux machine, the features of a position cost about the same in C++
as in Python (about 2000 a second): **both spend nearly all their time in the
eight `expected_damage` calls** a position needs. Porting the features alone
gains nothing; the cost is `expected_damage`.

Profiled with callgrind, `expected_damage` spends about a quarter of its time
looking up abilities by their name (a string hash, in the engine's damage
pipeline), a fifth allocating and freeing (events carry names as strings),
and a fifth in the walker of draws, which allocated two vectors per draw. The
walker now keeps a draw's outcomes on the stack, with the same arithmetic:
`expected_damage` is about 30 % faster, and its golden digest is unchanged.
