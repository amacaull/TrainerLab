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
