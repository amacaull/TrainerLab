# Search

The C++ side of the AI: what has to be fast. It plays on the battle engine
through the engine's own C++ API, never changes it, and is built and tested
on its own.

| | |
|---|---|
| `include/search/game.hpp` | The rules and the catalog loaded once; a random battle from a seed; a random playout to the end; the benchmark's positions |
| `include/search/features.hpp` | The 249 features of a position, the same as `trainerlab.learning.features`, bit for bit |
| `include/search/estimate.hpp` | The bots' damage and speed estimate, the same as `trainerlab.game.estimate` |
| `include/search/forecaster.hpp` | Rules 6 to 8, and the win model rule 9 rates with; 6 and 7 make the same decisions as their former Python version |
| `include/search/matrix_game.hpp` | The exact solution of a small zero-sum game (simplex): rule 8's mixed strategy |
| `include/search/mcts.hpp` | Rule 9: a Monte Carlo tree search over simultaneous moves (decoupled UCT), with guessed teams for the foe's unseen Pokémon |
| `include/search/encoding.hpp` | A position for a network: one vector per Pokémon, the field, the legal options (palier 6) |
| `include/search/vec_env.hpp` | Many battles at once for self-play: observe both sides, step, rewards, a battle that ends starts again |
| `include/search/network.hpp` | The policy and value network of `trainerlab.rl.net`, evaluated in C++ from its PyTorch weights, in batches; its layers through Accelerate on macOS |
| `include/search/planner.hpp` | Rule 11: the tree search over simultaneous moves guided by the network (PUCT), also step by step, for batching |
| `include/search/self_play.hpp` | The AlphaZero loop's games: battles side by side on every core, their network queries batched; deep searches recorded, fast ones not |
| `bench/bench.cpp` | `search_bench`: how fast the engine plays, through each of its APIs |
| `tests/` | Catch2 tests |

The flat API (`engine/docs/api.md`) validates and copies the state at every
call: right at a language boundary, too slow for a search that simulates
millions of turns. The search calls `BattleEngine` directly, and uses the flat
API only to set a battle up.

```bash
cmake -S search -B search/build          # Release by default
cmake --build search/build -j
ctest --test-dir search/build --output-on-failure
search/build/search_bench                # about 2 s per line
```

On macOS the network's layers go through Accelerate (`SEARCH_BLAS`, on by
default there); `-DSEARCH_BLAS=OFF` keeps the network's own kernel, as on
Linux, where `-DSEARCH_BLAS=ON` takes any CBLAS.

The Python counterpart, on the same positions: `trainerlab bench`. From
Python, `trainerlab._search` exposes the features, the positions and the
forecaster. The
measurements: [`docs/benchmark.md`](../docs/benchmark.md).
