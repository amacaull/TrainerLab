# Battle engine

A stateless Pokémon battle engine in C++20, modelled on Pokémon Showdown. It
resolves singles battles turn by turn without keeping any state: the caller
passes a `BattleState`, two actions and a random seed, and gets back the
updated state plus the list of events that happened.

It was written for **ft_transcendence**, the final common-core project at
[42 Paris](https://42.fr), where a Rust server drove it through `cxx`. In
TrainerLab it is driven from Python, to play against and train AI agents.

| | |
|---|---|
| Species | 49 (8 Megas) |
| Moves | 95 |
| Abilities | 45 |
| Held items | 13 |
| Types | 18, gen 6+ chart |
| Tests | 309 (Catch2), including a randomised battle fuzzer |

| Document | Content |
|---|---|
| [`docs/api.md`](docs/api.md) | The flat C++ API: call protocol, events, errors, expected damage, what is frozen |
| [`docs/design.md`](docs/design.md) | The design decisions and why |
| [`docs/data.md`](docs/data.md) | The JSON format of species and moves, and how to add one |

## Quick start

Requirements: CMake ≥ 3.20 and a C++20 compiler (clang or gcc). On the first
configure, CMake fetches nlohmann/json and Catch2.

```bash
cd engine
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure   # the whole suite
./build/battle_engine_demo                   # a few commented battles
```

## What the engine does

- **Format**: teams of 1 to 6 per side, everyone level 100, fixed sets (four
  moves, one ability, one item per species), at most one Mega and one
  legendary, no duplicate species. Both players choose, then the engine orders
  by priority, then speed.
- **Mechanics**, as in Showdown (generation 9) unless listed below: the damage
  formula with STAB, types, critical hits and rolls; accuracy and priority;
  the six statuses; stat stages; weather, Electric Terrain, Aurora Veil and
  hazards; recoil, drain, healing, two-turn and multi-hit moves, protection,
  forced switches, pivots, Knock Off, Destiny Bond, Rest, Sleep Talk; PP and
  Struggle; 45 abilities and 13 items.
- **Replacement after a knock-out** goes back to the caller: the engine stops
  and waits for the player's choice.

## Architecture

```
include/engine/ and src/ (strict mirror)
├── model/      the game's vocabulary: types, stats, status, species, moves
├── core/       orchestration: BattleState, BattleEngine, loading, validation
├── effects/    one class per move effect (Damage, StatChange, Pivot…)
├── abilities/  the 45 abilities, grouped by hook family
├── items/      the 13 items
└── ffi/        the flat API and its conversions
data/           game content, as JSON
tests/          one suite per domain
tools/          showdown-diff
```

- **`BattleState` is a fixed-size POD struct**: no allocation, no pointer. It
  can be copied, stored or sent across a language boundary as is.
- **A move is a list of effects** configured by its JSON. The engine knows no
  move by name, Struggle excepted.
- **Abilities and items are stateless listeners** on engine hooks
  (switch-in, damage calculation, end of turn…). Whatever changes in battle
  lives in the `BattleState`.

A turn validates both actions before any change, orders them, runs each one
(pre-move checks, PP, protection, accuracy, the effect chain), then the end
of turn in canonical order. Every step appends to the event log, which is all
a client needs to replay the battle.

## How we know it is right

- **Unit tests**, one Catch2 suite per domain, with an RNG that forces or
  forbids each draw, so that every rule is checked on an exact case.
- **A fuzzer** (`tests/test_fuzz.cpp`) plays random battles through the flat
  API and checks after every call: about forty state invariants, well-formed
  events, that a refused action changes nothing, that the legal actions are
  exactly what the engine accepts, that `observe` hides what it must, and that
  the events alone rebuild the state. It runs 300 battles with the suite;
  `FUZZ_BATTLES=50000` scales it, `FUZZ_SEED=…` replays one.
- **Golden tests** pin the digest of a full seeded battle and of every
  expected damage, bit for bit. The random draws are hand-written, and the
  engine is compiled with `-ffp-contract=off` (clang on Apple Silicon fused
  `a * b + c`, gcc on x86-64 did not): a seed gives the same battle on macOS
  and Linux. CI runs them on Linux; run them on macOS before merging an
  engine change.
- **`tools/showdown-diff`** compares every move and species with Showdown's
  data (`@pkmn/dex`), reading the JSON with the loader's own rules:
  `cd tools/showdown-diff && npm i && npm run diff`.

Formatting: `clang-format` (LLVM style, 100 columns), `.clang-format` at the
repository root.

## Known limitations

Deliberate differences from the games (the four `showdown-diff` differences):

- **Confusion** is not implemented: Hurricane lacks its secondary effect.
- **Illusion** (Hisuian Zoroark, no ability): the events sent to clients
  would have to lie about the species.
- **Mega Gengar** has Levitate instead of Shadow Tag (no trapping).
- **Mamoswine** has Slush Rush, to benefit from snow.

**Emergency Exit** switches automatically to the first valid replacement,
since a turn is resolved in one call; the Pokémon that comes in does not act
that turn, as in the games.

Out of scope: double battles, Trick Room, Substitute, in-battle Mega
Evolution.
