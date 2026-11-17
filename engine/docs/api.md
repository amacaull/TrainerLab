# C++ API

The contract between the engine and whatever drives it: the flat API of
`include/engine/ffi/ffi.hpp`, its events, its errors, and what is frozen.
The Python binding `trainerlab._engine` exposes the same functions.

## Principle

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

## Call protocol

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

## Strong guarantee

Every exposed function validates the state, works on a **copy**, and writes
the result back into the caller's state only once the computation is done.
Either the whole turn applies, or nothing moves.

## `FfiAction`

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

## `FfiEvent`

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

## `name_id` sentinels

| Value | Meaning |
|---|---|
| `-1` | the event carries no name |
| `-2` | Struggle (no catalog id, `struggle_move_id()`) |
| `-3` | Struggle recoil (`struggle_recoil_ability_id()`) |

Any other negative `name_id` is a bug.

## API surface

```
// Initialisation and catalogs
engine_init(data_dir)              // idempotent; another path → E_INIT
engine_is_initialised() / engine_data_dir()
species_count() / move_count() / item_count() / ability_count()
find_species_id(id_string) / find_move_id(name) / find_item_id(name) / find_ability_id(name)
                                   // -1 if unknown: probing is legitimate
species_id_string(id) / species_display_name(id) / move_name(id) / item_name(id) / ability_name(id)
species_entry(id) -> SpeciesEntry  // id_string, display_name, types, weight, mega, legendary
move_entry(id)    -> MoveEntry     // name, type, category, power, accuracy, pp, priority,
                                   // power_from_target_weight, and effects: each effect's kind,
                                   // chance, stat and delta, status, fraction, hazard, weather
type_effectiveness(move_type, species_id) -> float   // chart multiplier, abilities ignored
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

// Decisions
legal_actions(state, side) -> FfiAction list  // what resolve_turn accepts (below)
legal_replacements(state, side) -> int list   // what resolve_replacement accepts
observe(state, side) -> BattleState           // what this player can see (below)
expected_damage(state, side, move_slot) -> FfiDamage   // min, max, mean (below); takes a view
```

## Legal actions

`legal_actions` returns exactly the actions `resolve_turn` accepts for one
side, **one entry per distinct outcome**, so that a player choosing uniformly
among them is not biased:

| Situation | Entries |
|---|---|
| Active fainted | none: use `legal_replacements` |
| Charging a two-turn move | one move entry naming the charging move; the engine ignores any other choice, switches included |
| No usable PP (Struggle) | one move entry, plus the valid switches |
| Locked by a Choice item | the locked move, plus the valid switches |
| Otherwise | every slot with a move and PP left, plus every valid switch |

`pivot_target` is always `-1` (automatic). The fuzzer checks, every turn,
that each entry is accepted and each accepted action is listed.

## The player's view

`observe(state, side)` returns a copy of the state with what this player
cannot see removed:

- **An opponent that never entered the field** is reset to an empty slot
  (`species_id == -1`). `team_size` stays: the player knows how many
  Pokémon are left to discover. Each `BattlePokemon` carries a `revealed`
  flag, set when it enters the field.
- **The remaining turns of sleep** read 0, on both sides: a player does not
  know its own counter either. Rest is the exception, since it always sleeps
  two turns.

Everything else stays exact, opponent HP included: the events show exact
damage, and sets are fixed per species in a public catalog.

The view is not a playable state: `validate_state` rejects its empty slots,
so it never goes back into the engine, `expected_damage` excepted. The caller computes the legal actions
from the real state and hands both to the player.

## Expected damage

`expected_damage(state, side, move_slot)` gives the HP the move would take
from the opposing active if this side used it now: `min`, `max` and `mean`
over every outcome of its random draws (accuracy, critical hit, damage roll,
number of hits, full paralysis, thaw).

```cpp
struct FfiDamage {
  int32_t min;
  int32_t max;
  double  mean;
  bool    exact;   // false: too many outcomes, the figures come from a sample
};
```

**The engine plays the move, it does not model it.** Each outcome runs the
move through the same code as a turn, on a copy of the state, with an RNG
(`OutcomeWalker`) that walks every sequence of draws once, with its
probability. Abilities, items, stat stages, weather, fixed damage, multi-hit
moves and moves that fail (Fake Out after the first turn, Poltergeist on a
foe with no item, a move into Levitate) are counted because they are played.
The damage is the HP the foe has lost once the move is over, so it never
exceeds its current HP, and a move it absorbs (Volt Absorb) is 0.

| Choice | Why |
|---|---|
| **The foe's action is not played** | The question is what this move does, not what the turn does |
| **A two-turn move counts its release** | Otherwise Phantom Force would be worth 0 on its charge turn |
| **Sucker Punch assumes the foe attacks**, if it has a damaging move | Otherwise it would always be 0 |
| **A miss counts as 0**: `min` is 0 for any move that can miss | `min` is a guarantee: a knock-out is certain only if `min` reaches the foe's HP |
| **Beyond 2048 sequences, a fixed sample of 1024 draws**, `exact = false` | A volley of 2–5 hits, each with a crit and a roll, has millions of sequences. Only Bullet Seed, Triple Axel and Population Bomb get there; the same state still always gives the same figures |

**It accepts a player's view**, the one exception to the rule of the previous section: a bot
gets only `observe`, and must be able to ask. On a view, only what the
player can see goes in: a forced switch draws among the foe's revealed
Pokémon, and a sleeping Pokémon whose counter is hidden is counted as waking
up.

The RNG gained two virtual draws for this, `bucket` (the hit count of a
2–5 volley, accuracy) and `damageRoll`; their default implementations make
exactly the draws of before, so a seed still replays the same battle (the
golden test is unchanged).

## Python binding

`ai/bindings/` wraps this API with pybind11 as the module
`trainerlab._engine`. Function names are unchanged; struct fields are in
`snake_case` (`current_hp`, `active_index`), enums are `int`, and
`FfiAction` / `FfiEvent` are `Action` / `Event`. Each error prefix becomes an
exception class deriving from `EngineError` (`ActionError`, `TeamError`…),
with the full message kept and the subcode in `ActionError.subcode`.

## Errors

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

## What is frozen

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
