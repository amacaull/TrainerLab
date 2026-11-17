# Search

The C++ side of the AI: what has to be fast. It plays on the battle engine
through the engine's own C++ API, never changes it, and is built and tested
on its own.

| | |
|---|---|
| `include/search/game.hpp` | The rules and the catalog loaded once; a random battle from a seed; a random playout to the end |
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

The Python counterpart, on comparable positions: `trainerlab bench`.
