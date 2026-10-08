# Battle engine

A stateless Pokémon battle engine in C++20, modelled on Pokémon Showdown. It
resolves singles battles turn by turn without keeping any state: the caller
passes a `BattleState`, two actions and a random seed, and gets back the
updated state plus the list of events that happened.

It was written for **ft_transcendence**, the final common-core project at
[42 Paris](https://42.fr), where a Rust server drove it through `cxx`. In
TrainerLab it is driven from Python instead, to play against and train AI
agents.

| | |
|---|---|
| Species | 49 (8 Megas) |
| Moves | 95 |
| Abilities | 45 |
| Held items | 13 |
| Types | 18, gen 6+ chart |
| Tests | 279 (Catch2), including a randomised battle fuzzer |

---

## Contents

1. [Quick start](#1-quick-start)
2. [What the engine does](#2-what-the-engine-does)
3. [Architecture](#3-architecture)
4. [Key decisions](#4-key-decisions)
5. [Quality: how we know it is right](#5-quality-how-we-know-it-is-right)
6. [C++ API](#6-c-api)
7. [Data and extension](#7-data-and-extension)
8. [Known limitations](#8-known-limitations)

---

## 1. Quick start

Requirements: CMake ≥ 3.20 and a C++20 compiler (clang or gcc). On the first
configure, CMake fetches nlohmann/json and Catch2 (`FetchContent`).

```bash
cd engine
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure   # the whole suite
./build/battle_engine_demo                   # a few commented battles
```

The demo plays scripted duels (STAB, 4× weakness, priority, recoil…) and
prints every event.

---

## 2. What the engine does

### Format

- **Teams of 1 to 6 Pokémon per side.** Both sides do not need the same size.
- **Everyone is level 100**, IVs at 31, EVs and natures fixed per species.
- **Fixed sets**: each species has exactly four moves, one ability and one
  item. Players pick their Pokémon, not their configuration.
- **Team rules**: no duplicate species, at most 1 Mega, at most 1 legendary.
- **Simultaneous turns**: both players choose, the engine orders by priority
  then speed, ties broken at random.

### Mechanics covered

All of them follow Showdown's behaviour (generation 9) unless listed in
[§8](#8-known-limitations).

- **Damage**: canonical formula, STAB, type effectiveness, critical hits
  (Showdown rates, high-crit moves), damage rolls, fixed damage (Seismic
  Toss), weight-based damage (Grass Knot, Low Kick), moves using Defense or
  the target's Attack (Body Press, Foul Play).
- **Accuracy and order**: accuracy and evasion stages, priority brackets,
  effective speed (paralysis, weather abilities).
- **Status**: burn, poison, toxic, paralysis, sleep (with Sleep Clause),
  freeze; type and ability immunities.
- **Stat stages** from −6 to +6, reset on switch-out.
- **Field**: snow (gen 9), strong winds (Delta Stream), Electric Terrain,
  Aurora Veil, Stealth Rock, Spikes, Toxic Spikes.
- **Move mechanics**: secondary effects, recoil, drain, healing, Wish,
  two-turn moves, multi-hit moves, protection (Protect, Baneful Bunker),
  forced switches (Roar, Dragon Tail), pivots (U-turn, Volt Switch,
  Teleport), Knock Off, Destiny Bond, Rest, Sleep Talk, Belly Drum, stat-boost
  stealing, first-turn-only moves.
- **PP and Struggle**: a Pokémon with no usable PP uses Struggle.
- **45 abilities** (Intimidate, Pressure, Regenerator, Magic Bounce,
  Prankster, Lightning Rod…) and **13 items** (Choice items, Leftovers, Life
  Orb, Focus Sash, Heavy-Duty Boots, Sitrus Berry…).
- **Replacement after a KO**: the engine stops and waits for the player's
  choice.

---

## 3. Architecture

### Layers

```
include/engine/ and src/ (strict mirror)
├── model/      the game's vocabulary: types, stats, status, species, moves
├── core/       orchestration: BattleState, BattleEngine, loading, validation
├── effects/    one class per move effect (Damage, StatChange, Pivot…)
├── abilities/  the 45 abilities, grouped by hook family
├── items/      the 13 items
└── ffi/        the flat API and its conversions
data/           game content, as JSON
tests/          one suite per domain, plus tests/fixtures/ for test-only content
demo/           the command-line demo
tools/          showdown-diff (§5)
```

Dependencies point one way:

```
                     ffi  ──►  BattleEngine (core)
                                   │ orchestrates
                                   ▼
   Effects ──transform──►  BattleState (POD, no logic)
       ▲                           ▲
       │ effect lists              │ per-battle state
   Moves (JSON)          Abilities / Items: listeners on hooks
                         (stateless singletons)
                            ▲
                     DataLoader: reads data/ at startup, validates
```

- **`BattleState` is a fixed-size POD struct**: two teams of six, each side's
  active Pokémon, weather, terrain, screens, hazards, turn number. No
  allocation, no pointer: it can be copied, stored or sent across a language
  boundary as is.
- **A move is a list of effects.** The engine knows no move by name, only
  generic effects configured by the JSON. The one exception is Struggle,
  which is a game rule rather than content.
- **Abilities and items are listeners** plugged into engine hooks (switch-in,
  switch-out, before the move, damage calculation, end of turn…). They are
  stateless singletons: everything that changes during a battle (consumed
  item, Choice lock, broken Disguise) lives in the `BattleState`.

### Anatomy of a turn

`BattleEngine::resolveTurn(state, action0, action1, rng)`:

1. **Both actions are validated** (`checkAction`) before any mutation:
   fainted active, empty slot, no PP, Choice lock, illegal switch.
2. **Turn order**: switches go first, then moves by priority, then by
   effective speed.
3. **Each action runs.** For a move: pre-move checks (sleep, freeze,
   paralysis, flinch), PP payment, protection, accuracy, ability immunities,
   then the effect chain, then the after-move window (Life Orb recoil,
   Magician, Moxie).
4. **End of turn**, in canonical order: weather, terrain, Wish, healing
   items, status damage, orbs, screens, clean-up of one-turn effects.

Each step appends events to an `EventLog`. That is all a client receives to
replay or display the battle.

---

## 4. Key decisions

The choices that explain the shape of the code, grouped by theme.

### Engine architecture

| Decision | Why |
|---|---|
| **Stateless engine**: all state lives in `BattleState`, owned by the caller | Testable, no battle stored on the C++ side, persistence is trivial |
| **Content in JSON, rules in C++** | Add a move or a species without recompiling |
| **Effects through virtual inheritance** rather than `std::variant` | Extensibility first: a new effect is a new class |
| **Secondary effects = one generic wrapper** driven by `"chance"` | Any effect becomes an X % secondary with no dedicated code |
| **Abilities and items as listener registries** | 45 abilities without a giant `switch` or touching the engine |
| **Injected RNG**, per-call seed, hand-written draws | Deterministic tests; the same seed replays the same battle on macOS and Linux (standard library distributions differ across platforms) |
| **Domain-based tree**, mirrored `include/` and `src/` | Readable at 49 species and 95 moves; tests are named by domain |
| **Canonical English names in ASCII PascalCase** (`ShadowBall`, `GiratinaOrigin`) | No accents or spaces in file names or API strings; localised display is the client's job |

### Game rules

| Decision | Why |
|---|---|
| **Level 100, IV 31, EVs and natures** | Roster balance relies on EV spreads and natures |
| **Sets, abilities and items fixed per species** | Simple team building; the AI only has a team composition problem |
| **Megas are standalone species**, no in-battle transformation | No extra system: stats, types and ability are in the JSON |
| **Teams of 1 to 6** (`kTeamSize = 6`, `team_size` per side) | Every format with a single memory layout |
| **Replacement after a KO through a dedicated function**, no auto-switch | The replacement choice is strategic: it must go back to the player or the AI |
| **Two-turn moves: the engine forces the second turn** and ignores the action given | Simpler for callers and for the AI than an error |
| **Data-driven protection** (`selfOrField`) | No fragile heuristic to decide what Protect blocks |

### The flat API

| Decision | Why |
|---|---|
| **Numeric indices at the boundary**, never names | C-compatible `BattleState`, no allocation, no silent typo |
| **Strong guarantee**: each call works on a copy and commits only at the end | An error mid-turn never leaves a half-written state |
| **Prefixed errors** (`E_TEAM`, `E_ACTION`…) | Bindings often transport only the message: the prefix tells the caller whether it is a player error or a bug |
| **`FfiEvent` = one flat struct** rather than 36 types | Trivial to bind; clients `switch` on `kind` anyway |
| **Mandatory `data/` path, no fallback** | Another `data/` means other indices: a silent fallback would reinterpret stored battles |
| **Catalog fingerprint** | Detects a catalog whose names or order changed after a battle was stored |

---

## 5. Quality: how we know it is right

Three complementary tools, each catching a different class of bugs.

### Unit and integration tests

One Catch2 suite per domain: types, stats, damage, status, stages, weather,
hazards, protection and two-turn moves, PP and Struggle, items, abilities,
field, move mechanics, validation, catalog, flat API. Scenarios use a
deterministic RNG that forces or forbids each draw (crit, secondary effect,
accuracy), so every rule is checked on an exact case.

### The fuzzer (`tests/test_fuzz.cpp`)

Plays complete battles with random teams and actions, through the flat API
only. After each call, it checks:

- that the state stays valid (`validate_state`, about forty invariants);
- that every event is well formed (known kind, side, slot and `name_id` in
  bounds);
- that a refused action changes nothing;
- **that the events are enough to rebuild the state**: HP, status, PP and
  item presence are replayed from the events alone, as a client would, then
  compared with the real state.

It runs with the normal suite (300 battles, under a second) and scales on
demand:

```bash
FUZZ_BATTLES=50000 ./build/tests/battle_engine_tests "[fuzz]"
FUZZ_SEED=1592590343 ./build/tests/battle_engine_tests "[fuzz]"   # replays one battle
```

Every anomaly is printed with the seed that reproduces it.

### Comparison with Showdown (`tools/showdown-diff`)

Compares our 95 moves and 49 species with Pokémon Showdown's data
(`@pkmn/dex`): power, accuracy, PP, priority, flags (contact, punch,
slicing…), secondary effects and their target, recoil, drain, healing,
protection, base stats, types, ability, weight, legendary. It reads the JSON
with the same rules as `data_loader.cpp`, so a loading bug shows up as a
difference.

```bash
cd tools/showdown-diff && npm i && npm run diff
# without Node: docker run --rm -v "$PWD/../..":/be -w /be/tools/showdown-diff node:24-alpine sh -c "npm i && npm run diff"
```

The four remaining differences are deliberate, listed in
[§8](#8-known-limitations).

### Formatting

`clang-format` (LLVM style, 100 columns), configured by `.clang-format` at
the repository root.

---

## 6. C++ API

### Principle

> The caller does not "call the engine".
> It **drives a state machine that it owns**.

The C++ side provides the **rules** and the **catalogs**. The caller provides
the **storage** (the `BattleState`), the **decisions** (players' actions,
replacements) and the **game loop**.

**The caller reimplements nothing**: not the stat formula, not team legality,
not turn order. If a computation is needed on the caller's side, it is a
function to expose, never to rewrite. A caller never assembles a
`BattlePokemon` field by field: it calls `make_combatant`.

The flat API lives in `include/engine/ffi/ffi.hpp` (namespace
`engine::ffi`). It only uses plain types, so any binding can wrap it.

### Call protocol

```
engine_init(data_dir)                ← once per process
find_*_id(...)                       ← once, ids cached

per battle:
  make_combatant(species_id) × team size
  validate_team(...)                 ← once, on submission
  start_battle(state, seed)          ← mandatory: fires entry abilities
  loop:
    is_over ? → end
    collect both actions
    resolve_turn(state, a0, a1, turn_seed)
    for each side whose active fainted (order given by faster_side):
      ask for the replacement → resolve_replacement(state, side, index)
```

Two non-negotiable rules:

1. **`start_battle` is not optional.** Without it, no entry ability fires
   (Intimidate, Snow Warning…).
2. **`resolve_turn` refuses to play if an active Pokémon is fainted.**
   Replacement is a mandatory step, not a detail.

The seed must change every turn (`turn_seed = hash(battle_seed, turn)`):
with a constant seed, crits and secondary effects would become periodic.

### Strong guarantee

Every exposed function validates the state, works on a **copy**, and writes
the result back into the caller's state only once the computation is done.
Either the whole turn applies, or nothing moves.

### `FfiAction`

```cpp
struct FfiAction {
  uint8_t kind;         // 0 = move, 1 = switch
  int32_t index;        // move: slot 0-3 | switch: team index 0-5
  int32_t pivot_target; // move only: replacement after a pivot, -1 = automatic
};
```

A move's `index` is its position in the Pokémon's `move_ids`.
`pivot_target` is not validated in advance (the target may faint during the
turn): the engine falls back to automatic if needed.

### `FfiEvent`

```cpp
struct FfiEvent {
  uint8_t kind;     // see the table, frozen numbering
  int8_t  side;     // -1 for field events
  int8_t  slot;     // team index, -1 for side events
  int32_t name_id;  // move_id | item_id | ability_id depending on kind, -1 if none
  int32_t i0, i1;   // payload, meaning defined by kind
  float   f0;       // type effectiveness only
  uint8_t flags;    // bit 0 = STAB, bit 1 = critical hit
};
```

**This table is the contract.** Its numbering is authoritative: it does not
depend on the internal order of event types.

| kind | Event | `side` / `slot` | `name_id` | `i0` | `i1` | `f0` | `flags` |
|---|---|---|---|---|---|---|---|
| 0 | MoveUsed | user | move_id | **PP spent** | — | — | — |
| 1 | DamageDealt | target | — | damage | — | effectiveness | STAB, crit |
| 2 | Fainted | fainted | — | — | — | — | — |
| 3 | Missed | user | move_id | — | — | — | — |
| 4 | StatusApplied | target | — | `Status` | — | — | — |
| 5 | StatusFailed | target | — | `Status` | — | — | — |
| 6 | StatusDamage | target | — | damage | `Status` | — | — |
| 7 | StatusCured | holder | — | `Status` | — | — | — |
| 8 | MoveSkipped | user | — | `SkipReason` | — | — | — |
| 9 | StatStageChanged | target | — | delta | `StatIndex` | — | — |
| 10 | StatChangeFailed | target | — | `StatIndex` | 1 = raise | — | — |
| 11 | SwitchedOut | leaving | — | — | — | — | — |
| 12 | SwitchedIn | entering | — | — | — | — | — |
| 13 | AbilityTriggered | holder | ability_id | — | — | — | — |
| 14 | MoveFailed | user | move_id | — | — | — | — |
| 15 | WeatherStarted | −1 / −1 | — | `Weather` | — | — | — |
| 16 | WeatherEnded | −1 / −1 | — | `Weather` | — | — | — |
| 17 | WeatherDamage | target | — | damage | `Weather` | — | — |
| 18 | HazardSet | affected side / −1 | — | `HazardKind` | layers after | — | — |
| 19 | HazardDamage | target | — | damage | `HazardKind` | — | — |
| 20 | HazardsCleared | cleared side / −1 | — | — | — | — | — |
| 21 | ToxicSpikesAbsorbed | absorber | — | — | — | — | — |
| 22 | Healed | holder | — | HP restored | — | — | — |
| 23 | RecoilDamage | holder | — | damage | — | — | — |
| 24 | Charging | user | move_id | **PP spent** | — | — | — |
| 25 | Protected | protected | — | — | — | — | — |
| 26 | ItemTriggered | holder | item_id | — | — | — | — |
| 27 | ItemConsumed | holder | item_id | — | — | — | — |
| 28 | ItemDamage | holder | item_id | damage | — | — | — |
| 29 | ItemKnockedOff | dispossessed | item_id | — | — | — | — |
| 30 | TerrainStarted | −1 / −1 | — | `Terrain` | — | — | — |
| 31 | TerrainEnded | −1 / −1 | — | `Terrain` | — | — | — |
| 32 | ScreenStarted | side / −1 | — | turns | — | — | — |
| 33 | ScreenEnded | side / −1 | — | — | — | — | — |
| 34 | DestinyBondTriggered | attacker taken down | — | — | — | — | — |
| 35 | AbilityDamage | holder | ability_id | damage | — | — | — |

What a client must know to replay the state from the events:

- **HP**: `DamageDealt` carries the **raw** damage, which can exceed the
  remaining HP. Display and apply `min(damage, current HP)`.
- **PP**: `i0` of `MoveUsed` and `Charging` is the actual cost (0, 1 or 2);
  `pp -= i0` on the slot of `name_id` is enough. Pressure (2 PP), the release
  turn of a two-turn move and the move called by Sleep Talk (0 PP) are
  already accounted for.
- **Magician**: the thief's `AbilityTriggered` immediately precedes the
  victim's `ItemKnockedOff`; that is when the thief gets the item.

### `name_id` sentinels

| Value | Meaning |
|---|---|
| `-1` | the event carries no name |
| `-2` | Struggle (no catalog id, `struggle_move_id()`) |
| `-3` | Struggle recoil (`struggle_recoil_ability_id()`) |

Any other negative `name_id` is a bug.

### API surface

```
// Initialisation and catalogs
engine_init(data_dir)              // idempotent; another path → E_INIT
engine_is_initialised() / engine_data_dir()
species_count() / move_count() / item_count() / ability_count()
find_species_id(id_string) / find_move_id(name) / find_item_id(name) / find_ability_id(name)
                                   // -1 if unknown: probing is legitimate
species_id_string(id) / species_display_name(id) / move_name(id) / item_name(id) / ability_name(id)
species_entry(id) -> SpeciesEntry  // id_string, display_name, types, weight, mega, legendary
move_entry(id)    -> MoveEntry     // name, type, category, power, accuracy, pp, priority
catalog_fingerprint() -> u64

// Construction
make_combatant(species_id) -> BattlePokemon   // stats, item, 4 moves, PP, full HP
validate_team(team, team_size)                // team building rules
validate_state(state)                         // state invariants

// Battle
start_battle(state, seed) -> events
resolve_turn(state, a0, a1, seed) -> events
resolve_replacement(state, side, team_index) -> events
faster_side(state, seed) -> int               // order of simultaneous replacements
is_over(state) / side_has_lost(state, side)
```

### Errors

> **An exception signals a contract violation, never a game event.** A
> missed move is a `Missed`, a move with no effect a `MoveFailed`.

Bindings often transport only the message, so every error starts with a
frozen prefix, checked by a test:

| Prefix | Meaning |
|---|---|
| `E_INIT` | engine not initialised, or re-initialised with another path |
| `E_DATA` | `data/` missing or invalid JSON |
| `E_ARG` | index outside the catalog |
| `E_STATE` | invalid `BattleState` |
| `E_TEAM` | team refused |
| `E_ACTION` | action refused |

An action refusal also carries a frozen **subcode**:
`E_ACTION:<SUBCODE>: <sentence for logs>`. Callers split on the first two
`:` and never parse the sentence.

| Subcode | Call | Meaning |
|---|---|---|
| `FAINTED` | `resolve_turn` | this side's active is fainted: `resolve_replacement` first |
| `INVALID_SWITCH` | `resolve_turn`, `resolve_replacement` | switch target outside the team, already active or fainted |
| `BAD_SLOT` | `resolve_turn` | move index outside 0-3 |
| `EMPTY_SLOT` | `resolve_turn` | empty move slot |
| `NO_PP` | `resolve_turn` | no PP left on this move |
| `CHOICE_LOCKED` | `resolve_turn` | locked into another move by a Choice item |
| `BAD_KIND` | `resolve_turn` | `FfiAction.kind` neither 0 nor 1 |
| `BAD_SIDE` | `resolve_replacement` | side neither 0 nor 1 |
| `NOT_FAINTED` | `resolve_replacement` | this side's active is not fainted |

An `E_STATE` or `E_ARG` from `resolve_turn` is always a bug on the caller's
side, never a normal outcome of a battle.

### What is frozen

| Table | Index order | Append at the end? |
|---|---|---|
| Species (49) | alphabetical order of the files in `data/pokemon/` | **no**, it shifts the following ones |
| Moves (95) | alphabetical order of the files in `data/moves/` | **no**, it shifts the following ones |
| Items (13) | registration order in `item.cpp` | yes |
| Abilities (45) | registration order in `registration.hpp` | yes |

Enums passed as `int`:

```
Type       : Normal=0, Fire, Water, Electric, Grass, Ice, Fighting, Poison,
             Ground, Flying, Psychic, Bug, Rock, Ghost, Dragon, Dark, Steel, Fairy
Status     : None=0, Burn, Poison, Toxic, Paralysis, Sleep, Freeze
StatIndex  : Atk=0, Def, SpA, SpD, Spe, Accuracy, Evasion
Weather    : None=0, Rain, Sun, Sand, Snow, StrongWinds
Terrain    : None=0, Electric
HazardKind : StealthRock=0, Spikes, ToxicSpikes
SkipReason : Asleep=0, Frozen, FullyParalyzed, Flinched
```

**Catalog fingerprint**: a hand-written 64-bit FNV-1a (`std::hash` is stable
neither across platforms nor across libstdc++ versions), over the four
catalogs in the order species, moves, items, abilities: the entry count, then
each name followed by `\0`. Current value: `0x9848D2D3F76497E5`, locked by a
test. Anything that stores catalog ids or a `BattleState` should store it
too and recheck it on load. It only covers **names and order**: a stat or
effect fix leaves it unchanged.

---

## 7. Data and extension

### Layout

```
data/
├── types.json          type chart (only values ≠ 1 are listed)
├── pokemon/<Id>.json   one species per file; the file name is the id
└── moves/<Name>.json   one move per file
tests/fixtures/moves/   neutral test-only moves (Tackle, Growl…),
                        outside the shipped catalog
```

### A species

```json
{
  "id": "Dragapult",
  "displayName": "Dragapult",
  "baseStats": { "hp": 88, "atk": 120, "def": 75, "specAtk": 100, "specDef": 75, "speed": 142 },
  "type1": "Dragon",
  "type2": "Ghost",
  "ability": "ClearBody",
  "nature": "Jolly",
  "evs": { "atk": 252, "def": 4, "speed": 252 },
  "weight": 50.0,
  "item": "ChoiceBand",
  "legendary": false,
  "movepool": ["DragonDarts", "PhantomForce", "UTurn", "FireFang"]
}
```

`item` is absent for Megas, `mega: true` marks them, `ability` may be empty.

### A move

```json
{
  "name": "PlayRough",
  "type": "Fairy",
  "category": "Physical",
  "power": 90,
  "accuracy": 90,
  "pp": 10,
  "contact": true,
  "effects": [
    { "kind": "Damage" },
    { "kind": "StatChange", "stat": "Atk", "delta": -1, "chance": 10 }
  ]
}
```

- `accuracy` ≤ 0: never misses.
- `chance` on an effect: makes it an X % secondary effect.
- `StatChange` targets the **target** by default; `"affectsUser": true` (or
  `"target": "user"`) targets the user.
- `selfOrField: true`: a move on the user or the field, which Protect does
  not block and Pressure does not tax.
- Optional flags: `priority`, `contact`, `punch`, `slicing`, `bulletproof`,
  `reflectable`, `highCrit`, `bypassesProtect`, `twoTurn`, `multiHit`,
  `firstTurnOnly`, and a few move-specific flags (`powerFromTargetWeight`,
  `useTargetOffense`…).
- The loader is **strict**: a key or value it does not know (typos included)
  stops startup with the offending file name, instead of being silently
  ignored.

### Adding a move or a species

1. Create the JSON. A move must be in some species' `movepool`: a test
   checks that everything shipped is playable.
2. Run `showdown-diff` and the test suite.
3. **The ids that follow alphabetically shift.** Update the fingerprint value
   in its test (a deliberate act: anything stored with the old catalog
   becomes unreadable).

A new effect, ability or item needs code: a class derived from `Effect`,
`Ability` or `Item`, then one registration line (at the end of the list for
abilities and items).

---

## 8. Known limitations

**Deliberate differences from the games** (the four `showdown-diff`
differences):

- **Confusion** is not implemented: Hurricane lacks its secondary effect.
- **Illusion** (Hisuian Zoroark, no ability): it is a protocol problem more
  than an engine one, the events sent to clients would have to lie about the
  species.
- **Mega Gengar** has Levitate instead of Shadow Tag (no trapping).
- **Mamoswine** has Slush Rush to benefit from snow, an ability it does not
  have in the games.

**Other differences**:

- **Emergency Exit** switches automatically (to the first valid
  replacement) instead of letting the player choose mid-turn, which would be
  incompatible with a turn resolved in a single call.

**Not exposed yet**:

- **`legal_actions(state, side)`**: callers must currently try an action and
  read the `E_ACTION`.
- **`observe(state, side)`**: a player's view of the state, without the
  information a human player could not see.

**Out of scope**: double battles, Trick Room, Substitute, in-battle Mega
Evolution.
