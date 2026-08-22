# Contrat FFI — moteur de combat C++ ↔ backend Rust

> **Document de référence.** Toutes les décisions de la session du 2026-08-22
> y sont actées. Ce qui est marqué **gelé** ne bouge plus sans invalider les
> parties déjà en base.
>
> Numérotation des ADR : #52-56 sont réservés au service IA Python, d'où le
> saut à #57.

---

## 1. Le principe directeur

> La frontière n'est pas « Rust appelle C++ ».
> C'est **« Rust pilote une machine à états qu'il possède »**.

Le C++ fournit des **règles** (`resolveTurn`) et des **catalogues** (index →
contenu). Rust fournit le **stockage** (`BattleState` en DB), les **décisions**
(actions des joueurs, choix des remplaçants) et la **boucle de partie**.

Corollaire, et c'est le point le plus important du document : **Rust ne
réimplémente rien.** Ni la formule de stats, ni la légalité d'équipe, ni la
table des types, ni le calcul de l'ordre du tour. Si un de ces calculs est
nécessaire côté Rust, c'est une fonction à exposer — jamais à réécrire. Deux
implémentations d'une même règle divergent toujours, et la divergence se
découvre en production.

**Rust n'assemble jamais un `BattlePokemon` champ par champ.** Il appelle
`make_combatant`, qui applique nature, EVs, objet, moveset et PP.

---

## 2. Décisions actées

| # | Décision | Retenu |
|---|---|---|
| D1 | Crate FFI | **`cxx`** — conversion automatique exception → `Result::Err` |
| D2 | Allocation du `BattleState` | **Rust alloue, C++ mute en place** (ADR #11) |
| D3 | Obtention des index | `find_*_id()` au démarrage, cachés côté Rust + **empreinte de catalogue** (ADR #58) |
| D4 | Intégration build | `build.rs` + crate `cmake`, options tests/démo à OFF |
| D5 | RNG | `seed: u64` par appel ; Rust dérive `turn_seed = hash(battle_seed, state.turn)` |
| D6 | Remplacements simultanés | `faster_side(state, seed)` exposé, Rust ordonne |
| D7 | Durée de vie du `DataLoader` | singletons C++, `engine_init(path)` idempotent |
| D8 | Garantie forte | la couche FFI travaille sur une **copie**, commit en sortie |
| ADR #59 | Forme des events | **struct plate unique à 8 champs**, pas 36 shared structs |

### D5 — les deux pièges

L'état d'un `mt19937_64` fait ~2,5 Ko : il n'est **pas** dans le `BattleState`.

1. **Si Rust passe le même seed à chaque tour, le RNG rejoue la même
   séquence** — crits et secondaires deviennent périodiques. Le seed doit
   dépendre du numéro de tour.
2. `fasterSide()` est recalculé à chaque étape de fin de tour (chip météo,
   résiduels, orbes) et consomme un tirage en cas d'égalité de vitesse. La
   consommation RNG d'un tour dépend donc de la configuration : **le replay se
   fait au tour entier**, jamais à la demi-mesure.

### D8 — pourquoi la copie

`checkAction` protège l'entrée du tour : il valide les deux actions *avant*
toute mutation, donc un tour rejeté laisse l'état intact. Mais au-delà, un
throw au milieu de `resolveTurn` laisserait le `BattleState` de Rust à moitié
écrit — PP décrémentés, dégâts appliqués, upkeep non fait — et Rust le
persisterait en croyant l'avoir laissé intact.

```cpp
std::vector<FfiEvent> resolve_turn(BattleState &state, FfiAction a0,
                                   FfiAction a1, uint64_t seed) {
  validateState(state, loader());   // ADR #13
  BattleState scratch = state;      // POD de taille fixe : un memcpy
  MersenneRNG rng(seed);
  auto events = engine().resolveTurn(scratch, toAction(a0), toAction(a1), rng);
  state = scratch;                  // commit seulement si on arrive ici
  return flatten(events);
}
```

Garantie forte par construction : **soit le tour s'applique entièrement, soit
rien ne bouge.** Rust n'a aucun snapshot à gérer.

---

## 3. Les trois couches, et ce qu'est le shim

`ffi.hpp` n'inclut **aucun header `cxx`**. Ce n'est pas de la coquetterie :
`cxx` *génère* la définition C++ des shared structs, et dans `build.rs`,
`cmake::Config::build()` tourne **avant** `cxx_build::bridge()`. Au moment où
CMake compile `ffi.cpp`, le header généré n'existe pas encore. En dépendre
serait une impasse d'ordre de build — sans compter que pybind11 doit consommer
le même header (ADR #52).

```
engine::ffi::FfiEvent        ← notre struct, C++ pur, dans ffi.hpp
        ↕   le shim  (engine_shim.cc, dans la crate Rust)
RsEvent                      ← shared struct du bridge, générée par cxx
        ↕   impl Serialize   (ou From<RsEvent> vers une struct serde)
JSON vers le front
```

Le shim ne fait que recopier des champs :

```cpp
// src/engine_shim.cc — côté Rust
rust::Vec<RsEvent> resolve_turn(BattleState &state, RsAction a0,
                                RsAction a1, uint64_t seed) {
  engine::ffi::FfiAction ca0{a0.kind, a0.index, a0.pivot_target};
  engine::ffi::FfiAction ca1{a1.kind, a1.index, a1.pivot_target};

  std::vector<engine::ffi::FfiEvent> cpp =
      engine::ffi::resolve_turn(state, ca0, ca1, seed);

  rust::Vec<RsEvent> out;
  out.reserve(cpp.size());
  for (const auto &e : cpp)
    out.push_back(RsEvent{e.kind, e.side, e.slot, e.name_id,
                          e.i0, e.i1, e.f0, e.flags});
  return out;
}
```

**Aucune logique ne descend dans le shim.** Ce qui s'y glisserait deviendrait
invisible pour le binding Python.

À savoir côté Rust : **une shared struct `cxx` n'accepte pas
`#[derive(Serialize, Deserialize)]`** (seuls `Clone`, `Debug`, `Default`,
`PartialEq`… sont supportés). `RsEvent` étant générée dans la crate, la règle
d'orphelin ne s'applique pas : un `impl Serialize for RsEvent` écrit à la main
évite une troisième représentation.

Le projet est en **C++20** (`CMAKE_CXX_STANDARD 20`, `STANDARD_REQUIRED ON`,
`EXTENSIONS OFF`). `cxx_build` doit donc être en `.std("c++20")` — compiler le
shim en C++17 est au mieux une erreur de compilation, au pire une violation
d'ODR silencieuse.

---

## 4. Surface d'API

### Initialisation et catalogues

```
engine_init(data_dir: &str) -> Result<()>     // idempotent ; autre chemin => E_INIT
engine_is_initialised() -> bool
engine_data_dir() -> String

species_count() -> i32
move_count() -> i32
item_count() -> i32
ability_count() -> i32

find_species_id(id_string: &str) -> Result<i32>   // Ok(-1) si absent
find_move_id(name: &str)         -> Result<i32>
find_item_id(name: &str)         -> Result<i32>
find_ability_id(name: &str)      -> Result<i32>

species_id_string(id) / species_display_name(id) / move_name(id)
item_name(id) / ability_name(id)                  // E_ARG si id invalide

species_entry(id) -> Result<SpeciesEntry>   // id_string, displayName, type1, type2,
                                            // weightKg, mega, legendary
move_entry(id)    -> Result<MoveEntry>      // name, type, category, power,
                                            // accuracy, pp, priority
catalog_fingerprint() -> u64                // ADR #58
struggle_move_id() -> i32                   // sentinelle, voir section 7
```

Les `find_*` renvoient `Ok(-1)` sur un nom inconnu : sonder est légitime.
**Un `Err` de ces fonctions signifie « moteur non initialisé », jamais « nom
introuvable ».**

### Construction

```
make_combatant(species_id: i32) -> Result<BattlePokemon>
validate_team(team: &[BattlePokemon; 6], team_size: i32) -> Result<()>
validate_state(state: &BattleState) -> Result<()>
```

`make_combatant` rend un Pokémon **complet** : stats calculées, objet de
l'espèce, **les quatre attaques du movepool et leurs PP**, PV pleins. Il n'y a
ni paramètre de niveau (fixe à 100, ADR #33) ni pose d'attaques séparée : le
movepool des 49 espèces est de taille 4 exactement, donc il n'y a rien à
choisir.

`validate_team` reste utile même si aucun choix n'est possible : elle est la
ligne de défense contre un appelant qui court-circuiterait `make_combatant`
(ADR #13). Elle est appelée **une fois, à la soumission** (ADR #50).

### Combat

```
start_battle(state, seed) -> Result<Vec<FfiEvent>>
resolve_turn(state, a0, a1, seed) -> Result<Vec<FfiEvent>>
resolve_replacement(state, side, team_index) -> Result<Vec<FfiEvent>>
faster_side(state, seed) -> Result<i32>
is_over(state) -> bool
side_has_lost(state, side) -> bool
```

---

## 5. `FfiAction`

```cpp
struct FfiAction {
  uint8_t kind;         // 0 = UseMove, 1 = Switch
  int32_t index;        // kind 0 : slot d'attaque 0-3 | kind 1 : team index 0-5
  int32_t pivot_target; // kind 0 seulement, -1 = auto
};
```

`pivot_target` n'est **pas** validé par `checkAction` : il peut devenir
obsolète en cours de tour si la cible tombe. `PivotEffect` re-valide et
retombe sur l'auto. Rust peut donc envoyer `-1` sans réfléchir.

Un `kind` inconnu lève `E_ACTION` — c'est le seul cas qu'un `std::variant` C++
ne peut pas produire, et donc le seul que l'aplatissement introduit.

---

## 6. `FfiEvent`

```cpp
struct FfiEvent {
  uint8_t kind;     // voir tableau, GELÉ
  int8_t  side;     // -1 pour les events de terrain
  int8_t  slot;     // index dans l'équipe, -1 si sans objet
  int32_t name_id;  // move_id | item_id | ability_id selon kind, -1 si aucun
  int32_t i0, i1;   // charge utile, sens défini par kind
  float   f0;       // efficacité de type uniquement
  uint8_t flags;    // bit 0 = STAB, bit 1 = crit
};
```

Une struct unique plutôt que 36 shared structs : on perd le typage fort, on
gagne un pont trivial — et le consommateur final (front TS) fait de toute
façon un `switch` sur `kind`. **Le tableau ci-dessous EST le contrat** : sans
lui, `i0` est un nombre sans signification.

Fait qui rend la forme viable : **aucun des 36 events ne porte deux
`CombatantRef`**, d'où un seul couple `side`/`slot`. Huit n'en portent aucun
(météo, terrain, écrans, hazards) et laissent `side`/`slot` à `-1`, sauf les
events de camp qui renseignent `side` seul.

**La numérotation ci-dessous est autoritative** — elle ne dérive pas de
l'ordre du `std::variant`, c'est `flatten` qui doit s'y conformer.

| kind | Event | `side`/`slot` | `name_id` | `i0` | `i1` | `f0` | `flags` |
|---|---|---|---|---|---|---|---|
| 0 | MoveUsed | user | move_id | — | — | — | — |
| 1 | DamageDealt | target | — | damage | — | effectiveness | STAB, crit |
| 2 | Fainted | K.O. | — | — | — | — | — |
| 3 | Missed | user | move_id | — | — | — | — |
| 4 | StatusApplied | target | — | `Status` | — | — | — |
| 5 | StatusFailed | target | — | `Status` | — | — | — |
| 6 | StatusDamage | target | — | damage | `Status` | — | — |
| 7 | StatusCured | who | — | `Status` | — | — | — |
| 8 | MoveSkipped | user | — | `SkipReason` | — | — | — |
| 9 | StatStageChanged | target | — | **delta** | `StatIndex` | — | — |
| 10 | StatChangeFailed | target | — | `StatIndex` | 1 = hausse | — | — |
| 11 | SwitchedOut | sortant | — | — | — | — | — |
| 12 | SwitchedIn | entrant | — | — | — | — | — |
| 13 | AbilityTriggered | porteur | **ability_id** | — | — | — | — |
| 14 | MoveFailed | user | move_id | — | — | — | — |
| 15 | WeatherStarted | −1 / −1 | — | `Weather` | — | — | — |
| 16 | WeatherEnded | −1 / −1 | — | `Weather` | — | — | — |
| 17 | WeatherDamage | target | — | damage | `Weather` | — | — |
| 18 | HazardSet | **camp qui subit**, slot −1 | — | `HazardKind` | layers après pose | — | — |
| 19 | HazardDamage | target | — | damage | `HazardKind` | — | — |
| 20 | HazardsCleared | camp nettoyé, slot −1 | — | — | — | — | — |
| 21 | ToxicSpikesAbsorbed | absorbeur | — | — | — | — | — |
| 22 | Healed | who | — | amount | — | — | — |
| 23 | RecoilDamage | who | — | damage | — | — | — |
| 24 | Charging | who | move_id | — | — | — | — |
| 25 | Protected | who | — | — | — | — | — |
| 26 | ItemTriggered | porteur | **item_id** | — | — | — | — |
| 27 | ItemConsumed | porteur | **item_id** | — | — | — | — |
| 28 | ItemDamage | porteur | **item_id** | damage | — | — | — |
| 29 | ItemKnockedOff | dépossédé | **item_id** | — | — | — | — |
| 30 | TerrainStarted | −1 / −1 | — | `Terrain` | — | — | — |
| 31 | TerrainEnded | −1 / −1 | — | `Terrain` | — | — | — |
| 32 | ScreenStarted | camp, slot −1 | — | turns | — | — | — |
| 33 | ScreenEnded | camp, slot −1 | — | — | — | — | — |
| 34 | DestinyBondTriggered | **l'attaquant entraîné** | — | — | — | — | — |
| 35 | AbilityDamage | porteur | **ability_id** | damage | — | — | — |

Les kinds 13, 26-29 et 35 sont la raison pour laquelle la phase 16a a dû
assainir `ItemDamageEvent` : trois espaces d'ids partageaient un même type
d'event, ce qui rendait `name_id` intypable.

Un seul type d'écran existe dans le roster (Voile Aurore), d'où l'absence de
`ScreenKind`.

---

## 7. Sentinelles

Lutte est codée en dur (ADR #35) : il n'y a pas de `Struggle.json`, donc pas
d'id de catalogue. Or `-1` signifie déjà « cet event ne porte aucun nom ». Deux
constantes négatives réservées, exposées par le catalogue :

| Valeur | Sens |
|---|---|
| `-1` | l'event ne porte aucun nom |
| `-2` | Lutte (`struggle_move_id()`) |
| `-3` | `StruggleRecoil`, le recul de Lutte |

Rust les mappe vers ses libellés d'affichage. Tout autre `name_id` négatif est
un bug.

---

## 8. Protocole d'appel

```
engine_init(data_dir)                    ← une fois au démarrage du process
cache des index                          ← une fois, puis plus jamais de string
catalog_fingerprint()                    ← stocké à la création d'une partie

par partie :
  make_combatant(species_id) × team_size
  validate_team(...)                     ← une fois, à la soumission (ADR #50)
  validate_state(...)
  start_battle(state, seed)              ← OBLIGATOIRE
  boucle :
     is_over ? → fin
     collecter les deux actions (joueur / IA)
     resolve_turn(state, a0, a1, turn_seed)
     pour chaque camp dont l'actif est K.O. et partie non finie, dans l'ordre
       donné par faster_side :
         demander le remplaçant → resolve_replacement(state, side, index)
     concaténer les EventLog dans l'ordre pour le replay front
     persister
```

**Deux règles non négociables :**

1. **`start_battle` n'est pas optionnel.** Sans lui, aucun talent d'entrée ne
   se déclenche : pas d'Intimidation, pas d'Alerte Neige. La partie démarre
   dans un état subtilement faux.
2. **`resolve_turn` throw si un actif est K.O. au moment de l'appel.** La phase
   de remplacement est une transition d'état obligatoire (ADR #19), pas un
   détail cosmétique. Pas d'auto-switch : le choix du remplaçant est une
   décision stratégique qui doit remonter au joueur — et à l'IA.

---

## 9. Politique d'erreurs

### Le principe

> **Une exception signale une violation de contrat, jamais un événement de jeu.**

Une attaque qui rate → `MissedEvent`. Un Volt Switch contre un Sol →
`MoveFailedEvent`. Une équipe illégale → `invalid_argument`. Un `move_id = 999`
→ `out_of_range`.

**Conséquence pour Rust : un `Err` en retour de `resolve_turn` est TOUJOURS un
bug côté appelant, jamais une issue normale de partie.** Ça se logge en
`error!` et remonte en 500 — jamais en message joueur. Traiter les `Err` comme
« le coup n'est pas passé » masquerait des bugs pendant des semaines.

### Les trois couches de validation

1. **`validateState`** en tête de chaque fonction exposée (ADR #13). ~40
   invariants, message descriptif.
2. **`validateTeam`**, délibérément hors de `validateState` (ADR #50) : règles
   de **construction**, appelées une fois par le teambuilder.
3. **`checkAction`**, interne à `resolveTurn` : actif K.O., cible de switch
   illégale, slot vide, 0 PP, verrou Choix violé. **Throw avant toute
   mutation.**

### Le mécanisme `cxx`

**Une exception C++ qui atteint une fonction `extern "C"` appelle
`std::terminate`.** Le processus meurt : pas de panic Rust, pas d'unwinding,
pas de log. C'est la raison principale du choix de `cxx` (D1).

Déclarer `-> Result<T>` dans le bridge fait générer par `cxx` un shim
`noexcept` qui enveloppe l'appel dans un `try/catch` et convertit en
`Err(cxx::Exception)`.

> ⚠️ **Le `noexcept` est sur le shim.** Une fonction déclarée **sans** `Result`
> n'obtient pas de `try/catch` : l'exception atteint le `noexcept` et c'est
> `terminate`. **Toute fonction du bridge susceptible de throw doit être
> déclarée `Result<T>`.** À vérifier ligne par ligne.

### Préfixes — GELÉS

`cxx` ne transporte que `what()` : **le type de l'exception est perdu**. Le
préfixe est le seul canal permettant à Rust de distinguer une erreur client
d'un déploiement cassé.

| Préfixe | Sens | HTTP suggéré |
|---|---|---|
| `E_INIT` | `engine_init` non appelé, ou rappelé avec un autre chemin | 500 |
| `E_DATA` | `data/` absent ou JSON malformé | 500 |
| `E_ARG` | index hors catalogue passé par l'appelant | 500 |
| `E_STATE` | `validateState` a rejeté le `BattleState` | 500 |
| `E_TEAM` | `validateTeam` a rejeté une équipe | 400 |
| `E_ACTION` | `checkAction` a rejeté une action | 400 |

```
E_STATE: side 0 slot 2: pp[1] out of range [0, 15], got 20
E_TEAM:  species clause violated (Snorlax appears twice)
```

Faiblesse assumée : **un préfixe est une convention que rien ne compile.** Elle
est donc verrouillée par un test C++ qui vérifie que tout message levé par la
couche FFI commence par un préfixe de cette liste — la convention devient un
invariant testé, comme l'ordre du registry de talents.

### Côté Python (ADR #52)

`pybind11` fait le même travail dans l'autre sens, mais **le type survit** :
`invalid_argument` → `ValueError`, `out_of_range` → `IndexError`,
`runtime_error` → `RuntimeError`. Le service IA n'a pas besoin des préfixes.

---

## 10. Ce qui est gelé

### Les quatre catalogues

| Table | Source de l'ordre | Ajouter en fin ? |
|---|---|---|
| Espèces (49) | ordre alphabétique des fichiers de `data/pokemon/` | ❌ **non** — insère au milieu |
| Moves (95) | ordre alphabétique des fichiers de `data/moves/` | ❌ **non** — insère au milieu |
| Objets (13) | ordre d'enregistrement dans `item.cpp` (ADR #45) | ✅ append-only |
| Talents (45) | ordre d'enregistrement, `registration.hpp` (ADR #51) | ✅ append-only |

Les deux premières lignes sont le vrai risque : **ajouter un Pokémon ou une
attaque décale les ids de tous ceux qui suivent alphabétiquement.** À dire à
toute l'équipe, pas seulement au backend.

### Les enums qui traversent en `int`

```cpp
Type       : Normal=0, Fire, Water, Electric, Grass, Ice, Fighting, Poison,
             Ground, Flying, Psychic, Bug, Rock, Ghost, Dragon, Dark, Steel, Fairy
Status     : None=0, Burn, Poison, Toxic, Paralysis, Sleep, Freeze
StatIndex  : Atk=0, Def, SpA, SpD, Spe, Accuracy, Evasion
Weather    : None=0, Rain, Sun, Sand, Snow, StrongWinds
Terrain    : None=0, Electric
HazardKind : StealthRock=0, Spikes, ToxicSpikes
SkipReason : Asleep=0, Frozen, FullyParalyzed, Flinched
```

Plus la numérotation des `kind` de la section 6, et les sentinelles de la
section 7.

### `catalog_fingerprint()` — ADR #58

**FNV-1a 64 bits écrit à la main.** Pas `std::hash` : instable entre
plateformes et entre versions de libstdc++, donc inutilisable ici.

- base `14695981039346656037`, premier `1099511628211`
- on alimente les quatre catalogues dans l'ordre **espèces, moves, objets,
  talents** ; pour chacun : le nombre d'entrées en décimal, puis chaque nom
  dans l'ordre des index, chacun suivi d'un `\0`
- espèces par `id_string`, jamais par `displayName`

Le séparateur `\0` évite qu'un `AB`+`C` collisionne avec `A`+`BC`. Objets et
talents sont inclus bien qu'append-only : coût nul, et ça attrape une
réorganisation accidentelle du registry.

Rust stocke la valeur à la création d'une partie et la revérifie au
chargement. Un test C++ verrouille la valeur courante : la mettre à jour doit
être un acte conscient, avec la question « et les parties en base ? » posée au
bon moment.

### Ce qui se stocke en base

- **Les ids ne se stockent jamais**, sauf à l'intérieur d'un `BattleState`
  sérialisé — et c'est précisément ce que l'empreinte protège.
- **Les `id_string` se stockent** : équipes sauvegardées, historiques, replays.

---

## 11. Dettes connues, sans rapport avec le FFI

- **Légendaires** : tous les flags à `false`, la liste officielle n'est jamais
  arrivée. La règle « max 1 » est codée *et testée*. Le jour où la liste tombe,
  c'est un flip de booléens en pur JSON, zéro code.
- **Archétypes météo orphelins** : le roster ne contient aucun move de météo ni
  aucun talent poseur de sable ou de pluie. Baigne Sable (Minotaupe) et
  Glissade (M-Laggron) sont donc inertes en partie réelle. La neige tourne
  (Alerte Neige de Feunard d'Alola). À trancher : ajouter un poseur, ou assumer
  deux talents morts.
- **Confusion** (secondaire de Vent Violent) : non implémentée, divergence
  assumée depuis la phase 14.
- **Illusion** (Zoroark-H) et **Distorsion** : reportées. Illusion est un
  problème de *protocole* — le moteur devrait tracker « espèce affichée » et
  les events envoyés au front devraient mentir. À designer avec Taj si on la
  reprend.
