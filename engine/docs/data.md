# Data and extension

The game content is JSON, read and checked at startup: adding a species or a
move needs no recompiling.

## Layout

```
data/
├── types.json          type chart (only values ≠ 1 are listed)
├── pokemon/<Id>.json   one species per file; the file name is the id
└── moves/<Name>.json   one move per file
tests/fixtures/moves/   neutral test-only moves (Tackle, Growl…),
                        outside the shipped catalog
```

## A species

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

## A move

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

## Adding a move or a species

1. Create the JSON. A move must be in some species' `movepool`: a test
   checks that everything shipped is playable.
2. Run `showdown-diff` and the test suite.
3. **The ids that follow alphabetically shift.** Update the fingerprint value
   in its test (a deliberate act: anything stored with the old catalog
   becomes unreadable).

A new effect, ability or item needs code: a class derived from `Effect`,
`Ability` or `Item`, then one registration line (at the end of the list for
abilities and items).
