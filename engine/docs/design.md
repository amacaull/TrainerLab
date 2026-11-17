# Design decisions

The choices that explain the shape of the code, grouped by theme.

## Engine architecture

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

## Game rules

| Decision | Why |
|---|---|
| **Level 100, IV 31, EVs and natures** | Roster balance relies on EV spreads and natures |
| **Sets, abilities and items fixed per species** | Simple team building; the AI only has a team composition problem |
| **Megas are standalone species**, no in-battle transformation | No extra system: stats, types and ability are in the JSON |
| **Teams of 1 to 6** (`kTeamSize = 6`, `team_size` per side) | Every format with a single memory layout |
| **Replacement after a KO through a dedicated function**, no auto-switch | The replacement choice is strategic: it must go back to the player or the AI |
| **Two-turn moves: the engine forces the second turn** and ignores the action given | Simpler for callers and for the AI than an error |
| **Data-driven protection** (`selfOrField`) | No fragile heuristic to decide what Protect blocks |

## The flat API

| Decision | Why |
|---|---|
| **Numeric indices at the boundary**, never names | C-compatible `BattleState`, no allocation, no silent typo |
| **Strong guarantee**: each call works on a copy and commits only at the end | An error mid-turn never leaves a half-written state |
| **Prefixed errors** (`E_TEAM`, `E_ACTION`…) | Bindings often transport only the message: the prefix tells the caller whether it is a player error or a bug |
| **`FfiEvent` = one flat struct** rather than 36 types | Trivial to bind; clients `switch` on `kind` anyway |
| **Mandatory `data/` path, no fallback** | Another `data/` means other indices: a silent fallback would reinterpret stored battles |
| **Catalog fingerprint** | Detects a catalog whose names or order changed after a battle was stored |
