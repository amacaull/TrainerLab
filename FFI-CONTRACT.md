# Contrat FFI — moteur de combat C++ ↔ backend Rust

> **Document de travail de la session du 2026-08-22 (Alex + Taj).**
> Il n'est pas encore validé : les sections marquées **[À TRANCHER]** attendent
> une décision commune. On annote directement dans ce fichier, et ce qui en
> sort devient la spec de la phase 16b.
>
> État du moteur : phase 16a mergée, 240 tests verts, catalogue stable.
> Numérotation des ADR : #52-56 sont réservés au service IA Python, d'où le
> saut à #57.

---

## 1. Le principe directeur

> La frontière n'est pas « Rust appelle C++ ».
> C'est **« Rust pilote une machine à états qu'il possède »**.

Le C++ fournit des **règles** (`resolveTurn`) et des **catalogues** (index → contenu).
Rust fournit le **stockage** (`BattleState` en DB), les **décisions** (actions des
joueurs, choix des remplaçants) et la **boucle de partie**.

Corollaire, et c'est le point le plus important du document : **Rust ne
réimplémente rien.** Ni la formule de stats, ni la légalité d'équipe, ni la
table des types, ni le calcul de l'ordre du tour. Si un de ces calculs est
nécessaire côté Rust, c'est une fonction à exposer — jamais à réécrire. Deux
implémentations d'une même règle divergent toujours, et la divergence se
découvre en production.

---

## 2. Répartition du travail

### Côté C++ (Alex)

| Item | Description | Coût |
|---|---|---|
| ~~16a~~ | ~~bugs latents, registry indexé, CMake autonome~~ | ✅ livré |
| A | `FfiAction` / `FfiEvent` plats + conversions | 3-4 h |
| B | `src/ffi/ffi.cpp` : `engine_init`, singletons, validation, try/catch | 2 h |
| C | `makeCombatant` exposé + export du catalogue | 1 h 30 |
| D | `fasterSide` exposé (D6) | 20 min |
| E | Tests C++ d'aller-retour sur l'aplatissement | 1 h |

**≈ 1,5 jour effectif.** On ne finit donc pas le pont aujourd'hui — l'objectif
de la journée est le *contrat*, pas l'implémentation.

### Côté Rust (Taj)

| Item | Description |
|---|---|
| F | `#[cxx::bridge]`, fonctions déclarées `Result<T>` |
| G | `build.rs` + crate `cmake`, options tests/démo à OFF, link libstdc++/libc++ |
| H | `data/` embarqué dans l'image Docker, chemin passé à `engine_init` |
| I | `serde` sur `BattleState` |
| J | Boucle de partie, phase de remplacement incluse |
| K | Tests d'intégration : match scripté rejoué, events comparés à la démo C++ |

**Trois pièges à connaître avant de commencer G et H :**

1. La lib **doit être construite dans le même environnement que le backend**.
   Pas de `.a` compilé sur un macOS arm64 qu'on glisserait dans une image Linux.
2. Le dossier `data/` doit exister **au runtime**. Le moteur lit ses JSON au
   démarrage. Voir ADR #57 : plus aucun chemin par défaut, plus aucun repli.
3. Les **exceptions doivent rester activées** à la compilation. C'est le défaut,
   mais ne pas les désactiver « pour optimiser » : tout le contrat d'erreur
   repose dessus.

**Bonne nouvelle à donner tout de suite :** une fois chargé, le `DataLoader`
est en lecture seule et le moteur est stateless. **N combats en parallèle sur
des `BattleState` distincts sont thread-safe sans verrou.** Inutile de prévoir
un mutex global.

---

## 3. Les décisions **[À TRANCHER]**

### D1 — Crate FFI ✅ **TRANCHÉ : `cxx`** (2026-08-21)

La conversion automatique d'une exception C++ en `Result::Err` vaut à elle
seule le choix (ADR #13). `bindgen`/`cc` auraient imposé d'écrire à la main la
couche de traduction d'erreurs.

**Où vit l'adaptateur `rust::Str`.** `ffi.hpp` prend des `const std::string &`
et n'inclut aucun header `cxx`. C'est délibéré : `rust/cxx.h` est généré par la
crate au build Rust, donc en faire une dépendance de `battle_engine_lib`
inverserait le couplage — le moteur dépendrait de l'écosystème Rust alors qu'il
doit aussi servir pybind11 (ADR #52).

L'adaptateur est donc un `.cc` **dans la crate Rust**, compilé par `cxx_build`
via `.file("src/engine_shim.cc")`. Il convertit `rust::Str` ↔ `std::string` et
`std::string` → `rust::String`, et n'a aucune logique. Côté Rust, le bridge
écrit `fn engine_init(data_dir: &str) -> Result<()>` — plus de
`let_cxx_string!`.

### D2 — Qui alloue le `BattleState` ? **(bloquant)**

L'ADR #11 dit Rust, par référence. **À valider par un prototype avant tout le
reste** : `BattleState` contient `std::array<std::array<BattlePokemon, 6>, 2>`,
donc des tableaux imbriqués de structs. `cxx` gère `[T; N]` dans les shared
structs, mais ça se vérifie en 20 minutes.

- **Plan A** (ADR #11) : shared struct, Rust alloue, C++ mute en place.
- **Plan B** si A coince : type opaque côté C++, `UniquePtr<BattleState>` côté
  Rust, plus une paire `serialize`/`deserialize` pour la DB. Plus lourd, mais
  ça marche.

**C'est la première chose de la journée.** Le reste du document suppose A.

> Décision : ................................................................

### D3 — Comment Rust obtient les index

**Reco :** `find_*_id()` appelés une fois au démarrage et cachés, **plus** un
export de catalogue (`id`, `id_string`, `displayName`) que le front et l'IA
consomment. Pas de fichier d'index généré à maintenir en parallèle.

**Le point à bien faire entendre :** les index sont **l'ordre alphabétique des
noms de fichiers dans `data/`** (`sortedJsonFiles()` dans `data_loader.cpp`).
Ajouter `Aegislash.json` décale tout le catalogue d'espèces. Le gel des index
n'est pas une métaphore, c'est un gel de l'arborescence `data/`.

Corollaire proposé (**ADR #58**) : exposer `species_count`, `move_count` et un
hash des noms dans l'ordre, que Rust stocke à la création d'une partie et
revérifie au chargement. Sans ça, un `data/` modifié après coup réinterprète
silencieusement les parties sauvegardées.

> Décision : ................................................................

### D4 — Intégration build

**Reco :** `build.rs` + crate `cmake`. Depuis la phase 16a, tout est en place :
options à OFF, `POSITION_INDEPENDENT_CODE`, règles `install`.

```bash
cmake -S . -B $OUT_DIR -DBATTLE_ENGINE_BUILD_TESTS=OFF -DBATTLE_ENGINE_BUILD_DEMO=OFF
cmake --build $OUT_DIR -j
cmake --install $OUT_DIR --prefix $OUT_DIR
```

> Décision : ................................................................

### D5 — Comment le RNG traverse la frontière **(bloquant, absent de la liste initiale)**

L'état d'un `mt19937_64` fait ~2,5 Ko : le mettre dans le `BattleState`
ruinerait la sérialisation. Il n'y est donc pas.

**Reco :** `resolve_turn(state, a0, a1, seed: u64)`. Rust stocke un
`battle_seed` par partie et dérive `turn_seed = hash(battle_seed, state.turn)`.
Deux `u64` en DB, replay parfait, POD inchangé.

**Le piège :** si Rust passe le *même* seed à chaque tour, le RNG rejoue la même
séquence — les crits et les secondaires deviennent périodiques. Le seed doit
dépendre du numéro de tour.

**Détail à connaître :** `fasterSide()` est recalculé à chaque étape de fin de
tour (chip météo, résiduels, orbes) et consomme un tirage en cas d'égalité de
vitesse. La consommation RNG d'un tour dépend donc de la configuration : on ne
peut pas « rejouer un demi-tour ». Le replay se fait au tour entier.

> Décision : ................................................................

### D6 — Ordre des remplacements simultanés

Quand les deux camps doivent remplacer un K.O., l'ordre compte : une
Intimidation à l'entrée n'a pas le même effet selon qui arrive en premier.
`resolveReplacement` est par camp, donc c'est Rust qui décide. Aujourd'hui
c'est **indéfini**.

**Reco :** exposer `faster_side(state, seed)` et laisser Rust l'appeler, plutôt
que de figer la règle dans le moteur. Canon = le plus rapide d'abord.

> Décision : ................................................................

### D7 — Durée de vie du `DataLoader`

**Reco :** `DataLoader` et `BattleEngine` en singletons côté C++, initialisés
une fois par `engine_init(path)`, qui throw si le dossier est absent (ADR #57).
Lecture seule ensuite, donc thread-safe.

> Décision : ................................................................

### D8 — Garantie forte sur `BattleState` **(découle du plan A de D2)**

Si Rust possède le `BattleState` et le passe en `&mut`, le C++ **mute le buffer
de Rust en place**. `checkAction` protège l'entrée du tour — il valide les deux
actions *avant* toute mutation, donc un tour rejeté laisse l'état intact. Mais
au-delà de ce point, un throw au milieu de `resolveTurn` (un `out_of_range`
depuis `moveByIndex`, par exemple) laisserait le `BattleState` **à moitié
écrit** : PP décrémentés, dégâts appliqués, upkeep non fait. Rust reçoit un
`Err`, croit son état intact, et le persiste.

**Reco :** la couche FFI travaille sur une copie et ne valide qu'en sortie.
`BattleState` étant un POD de taille fixe, c'est un `memcpy` d'environ 1,5 Ko —
négligeable devant un tour de combat.

```cpp
rust::Vec<FfiEvent> resolve_turn(BattleState &state, FfiAction a0,
                                 FfiAction a1, uint64_t seed) {
  validateState(state, loader());   // ADR #13
  BattleState scratch = state;      // copie
  MersenneRNG rng(seed);
  auto events = engine().resolveTurn(scratch, toAction(a0), toAction(a1), rng);
  state = scratch;                  // commit seulement si on arrive ici
  return flatten(events);
}
```

La frontière offre alors la **garantie forte par construction** : soit le tour
s'applique entièrement, soit rien ne bouge. Rust n'a aucun snapshot à gérer,
aucune transaction à annuler.

> Décision : ................................................................

---

## 4. Surface d'API proposée

### Initialisation et catalogues

```
engine_init(data_dir: &str) -> Result<()>        // throw si data_dir absent
species_count() -> i32
move_count() -> i32
item_count() -> i32
ability_count() -> i32

find_species_id(id_string: &str) -> i32          // -1 si absent
find_move_id(name: &str) -> i32                  // -1 si absent
find_item_id(name: &str) -> i32                  // -1 si absent
find_ability_id(name: &str) -> i32               // -1 si absent

species_entry(id: i32) -> Result<SpeciesEntry>   // id_string, displayName, types, mega, legendary
move_entry(id: i32) -> Result<MoveEntry>         // name, type, category, power, accuracy, pp, priority
catalog_fingerprint() -> u64                     // ADR #58, si retenu
```

Les `find_*` renvoient `-1` plutôt que de throw : c'est du *probing*, pas une
erreur. Les `*_entry` throw sur index invalide.

### Construction

```
make_combatant(species_id: i32, level: i32) -> Result<BattlePokemon>
set_move(p: &mut BattlePokemon, slot: i32, move_id: i32) -> Result<()>   // pose aussi les PP
validate_team(team: &[BattlePokemon; 6], team_size: i32) -> Result<()>   // ADR #50, à la soumission
validate_state(state: &BattleState) -> Result<()>
```

`make_combatant` applique la nature, les EVs, l'objet verrouillé de l'espèce et
les PP. **Rust ne doit jamais assembler un `BattlePokemon` champ par champ** —
c'est la porte ouverte à une divergence de formule de stats.

### Combat

```
start_battle(state: &mut BattleState, seed: u64) -> Result<Vec<FfiEvent>>
resolve_turn(state: &mut BattleState, a0: FfiAction, a1: FfiAction, seed: u64) -> Result<Vec<FfiEvent>>
resolve_replacement(state: &mut BattleState, side: i32, team_index: i32) -> Result<Vec<FfiEvent>>
faster_side(state: &BattleState, seed: u64) -> i32
is_over(state: &BattleState) -> bool
side_has_lost(state: &BattleState, side: i32) -> bool
```

---

## 5. `FfiAction`

```cpp
struct FfiAction {
  uint8_t kind;         // 0 = UseMove, 1 = Switch
  int32_t index;        // kind 0 : slot d'attaque 0-3 | kind 1 : team index 0-5
  int32_t pivot_target; // kind 0 seulement, -1 = auto (premier remplaçant valide)
};
```

`pivot_target` n'est **pas** validé par `checkAction` : il peut devenir obsolète
en cours de tour si la cible tombe. `PivotEffect` re-valide et retombe sur
l'auto. Rust peut donc envoyer `-1` sans réfléchir.

---

## 6. `FfiEvent` — la struct plate **[À TRANCHER — ADR #59]**

```cpp
struct FfiEvent {
  uint8_t kind;     // voir tableau ci-dessous
  int8_t  side;     // -1 pour les events de terrain
  int8_t  slot;     // index dans l'équipe, -1 si sans objet
  int32_t name_id;  // move_id | item_id | ability_id selon kind, -1 si aucun
  int32_t i0;       // charge utile numérique principale (dégâts, soin, delta...)
  int32_t i1;       // charge utile secondaire (rarement utilisée)
  float   f0;       // efficacité de type uniquement
  uint8_t flags;    // bit 0 = STAB, bit 1 = crit
};
```

Une struct unique plutôt que 35 structs partagées : on perd le typage fort, on
gagne massivement en simplicité de pont — et le consommateur final (front TS)
fait de toute façon un `switch` sur `kind`. **Contrepartie obligatoire : le
tableau ci-dessous est le contrat.** S'il n'est pas tenu à jour, la struct
devient illisible.

Fait notable qui simplifie tout : **aucun des 35 events ne porte deux
`CombatantRef`**, d'où un seul couple `side`/`slot`.

| kind | Event | `side`/`slot` | `name_id` | `i0` | `i1` | `f0` | `flags` |
|---|---|---|---|---|---|---|---|
| 0 | MoveUsed | utilisateur | move_id | — | — | — | — |
| 1 | DamageDealt | cible | — | dégâts | — | efficacité | STAB, crit |
| 2 | Fainted | K.O. | — | — | — | — | — |
| 3 | Missed | utilisateur | move_id | — | — | — | — |
| 4 | StatusApplied | cible | — | `Status` | — | — | — |
| 5 | StatusFailed | cible | — | `Status` | — | — | — |
| 6 | StatusDamage | cible | — | dégâts | `Status` | — | — |
| 7 | StatusCured | concerné | — | `Status` | — | — | — |
| 8 | MoveSkipped | utilisateur | — | `SkipReason` | — | — | — |
| 9 | StatStageChanged | cible | — | delta appliqué | `StatIndex` | — | — |
| 10 | StatChangeFailed | cible | — | `StatIndex` | 1 = hausse | — | — |
| 11 | SwitchedOut | sortant | — | — | — | — | — |
| 12 | SwitchedIn | entrant | — | — | — | — | — |
| 13 | AbilityTriggered | porteur | **ability_id** | — | — | — | — |
| 14 | MoveFailed | utilisateur | move_id | — | — | — | — |
| 15 | WeatherStarted | −1 | — | `Weather` | — | — | — |
| 16 | WeatherEnded | −1 | — | `Weather` | — | — | — |
| 17 | WeatherDamage | cible | — | dégâts | `Weather` | — | — |
| 18 | HazardSet | camp visé, slot −1 | — | `HazardKind` | couches | — | — |
| 19 | HazardDamage | cible | — | dégâts | `HazardKind` | — | — |
| 20 | HazardsCleared | camp, slot −1 | — | — | — | — | — |
| 21 | ToxicSpikesAbsorbed | absorbeur | — | — | — | — | — |
| 22 | Healed | soigné | — | montant | — | — | — |
| 23 | RecoilDamage | encaisseur | — | dégâts | — | — | — |
| 24 | Charging | utilisateur | move_id | — | — | — | — |
| 25 | Protected | protégé | — | — | — | — | — |
| 26 | ItemTriggered | porteur | **item_id** | — | — | — | — |
| 27 | ItemConsumed | porteur | **item_id** | — | — | — | — |
| 28 | ItemDamage | porteur | **item_id** | dégâts | — | — | — |
| 29 | ItemKnockedOff | dépossédé | **item_id** | — | — | — | — |
| 30 | TerrainStarted | −1 | — | `Terrain` | — | — | — |
| 31 | TerrainEnded | −1 | — | `Terrain` | — | — | — |
| 32 | ScreenStarted | camp, slot −1 | — | tours | — | — | — |
| 33 | ScreenEnded | camp, slot −1 | — | — | — | — | — |
| 34 | DestinyBondTriggered | entraîné | — | — | — | — | — |
| 35 | **AbilityDamage** | porteur | **ability_id** | dégâts | — | — | — |

Les kinds 13, 26-29 et 35 sont exactement la raison pour laquelle la phase 16a
a dû nettoyer `ItemDamageEvent` : trois espaces d'ids se partageaient un même
type d'event, ce qui rendait `name_id` intypable.

**L'ordre des kinds est gelé au même titre que les index de catalogue.**

---

## 7. Protocole d'appel

C'est plus qu'une boucle sur `resolve_turn` :

```
engine_init(data_dir)                    ← une fois au démarrage du process
cache des index                          ← une fois, puis plus jamais de string

par partie :
  make_combatant(...) × team_size
  validate_team(...)                     ← une fois, à la soumission (ADR #50)
  validate_state(...)
  start_battle(state, seed)               ← OBLIGATOIRE
  boucle :
     is_over ? → fin
     collecter les deux actions (joueur / IA)
     resolve_turn(state, a0, a1, turn_seed)
     pour chaque camp dont l'actif est K.O. et partie non finie :
         demander le remplaçant → resolve_replacement(state, side, index)
     concaténer les EventLog dans l'ordre pour le replay front
     persister
```

**Deux règles non négociables :**

1. **`start_battle` n'est pas optionnel.** Sans lui, aucun talent d'entrée ne
   se déclenche : pas d'Intimidation, pas d'Alerte Neige. La partie démarre
   dans un état subtilement faux.
2. **`resolve_turn` throw si un actif est K.O. au moment de l'appel.** La phase
   de remplacement est une transition d'état obligatoire du protocole
   (ADR #19), pas un détail cosmétique. Pas d'auto-switch : le choix du
   remplaçant est une décision stratégique qui doit remonter au joueur — et à
   l'IA.

---

## 8. Politique d'erreurs (ADR #13)

### 8.1 Le principe

> **Une exception signale une violation de contrat, jamais un événement de jeu.**

Une attaque qui rate → `MissedEvent`. Un Volt Switch contre un Sol →
`MoveFailedEvent`. Une équipe illégale → `invalid_argument`. Un `move_id = 999`
→ `out_of_range`.

**Conséquence directe pour Rust, et c'est le point le plus important de cette
section : un `Result::Err` en retour de `resolve_turn` est TOUJOURS un bug côté
appelant, jamais une issue normale de partie.** Ça se logge en `error!` et ça
remonte en 500 — jamais en message joueur. Traiter les `Err` comme « le coup
n'est pas passé » masquerait des bugs pendant des semaines.

### 8.2 Les trois couches de validation

Dans cet ordre :

1. **`validateState`** en tête de chaque fonction exposée. ~40 invariants,
   message descriptif :
   `"side 0 slot 2: pp[1] out of range [0, 15], got 20"`.
2. **`validateTeam`**, délibérément *hors* de `validateState` (ADR #50) :
   Species Clause, max 1 Méga, max 1 légendaire, appartenance au movepool. Ce
   sont des règles de **construction**, appelées une fois par le teambuilder.
   Une partie déjà lancée avec une équipe illégale n'est pas corrompue — elle
   aurait dû être refusée à la soumission.
3. **`checkAction`**, interne à `resolveTurn` : actif K.O., cible de switch
   illégale, slot vide, 0 PP, verrou Choix violé. **Throw avant toute
   mutation** — un `resolveTurn` rejeté laisse l'état intact.

### 8.3 Ce que le moteur lance

Trois types, et le découpage n'est pas arbitraire :

| Type | Sites | Signifie |
|---|---|---|
| `std::invalid_argument` | `validate.cpp`, `checkAction`, parsing d'enums | l'appelant a fourni une entrée illégale |
| `std::out_of_range` | `moveByIndex`, `speciesByIndex`, `typeName`, `statusName` | index hors catalogue |
| `std::runtime_error` | `data_loader.cpp` (14 sites), `computeSpeciesStats` | données disque cassées ou absentes |

S'y ajoutent les types de nlohmann (`json::parse_error`, `json::type_error`),
qui dérivent de `std::exception` et ne surviennent qu'à `engine_init`.

### 8.4 Le mécanisme exact à la frontière

**Une exception C++ qui atteint une fonction `extern "C"` appelle
`std::terminate`.** Le processus meurt : pas de panic Rust, pas d'unwinding,
pas de log applicatif. C'est la raison principale du choix de `cxx` (D1).

Quand une fonction est déclarée avec `-> Result<T>` dans le bridge :

```rust
#[cxx::bridge]
mod ffi {
    unsafe extern "C++" {
        fn resolve_turn(state: &mut BattleState, a0: FfiAction,
                        a1: FfiAction, seed: u64) -> Result<Vec<FfiEvent>>;
    }
}
```

`cxx` génère un shim C++ `noexcept` qui enveloppe l'appel dans un `try/catch`
et convertit l'exception en `Err(cxx::Exception)` côté Rust.

> ⚠️ **Le `noexcept` est sur le shim.** Une fonction déclarée **sans** `Result`
> n'obtient pas de `try/catch` : l'exception atteint le `noexcept` et c'est
> `terminate`. **Toute fonction du bridge susceptible de throw doit être
> déclarée `Result<T>`.** À vérifier ligne par ligne, pas au jugé.

### 8.5 Ce qu'on perd, et la convention de préfixes **[À TRANCHER]**

`cxx` ne transporte que `what()`. **Le type de l'exception est perdu** : côté
Rust, `invalid_argument` et `runtime_error` sont indiscernables. Or Taj doit
distinguer « équipe illégale soumise par le joueur » (→ 400) de « `data/`
corrompu » (→ 500).

`validate.cpp` a déjà l'amorce de la solution (`fail()` préfixe tout par
`"validateState: "`). Proposition : généraliser en préfixe stable et
documenté, sur lequel Rust matche.

```
E_STATE:  side 0 slot 2: pp[1] out of range [0, 15], got 20
E_TEAM:   species clause violated (Snorlax appears twice)
E_ACTION: side 1: move slot 2 has 0 pp
E_DATA:   data/moves/Blizzard.json: missing key 'power'
```

**Alternative** : retourner un `struct FfiResult { code: i32, message: String }`
au lieu d'un `Result`. Typé et propre, mais plus verbeux à tous les sites
d'appel et on perd le `?` de Rust.

> Décision : ................................................................

### 8.6 Côté Python (ADR #52)

`pybind11` fait le même travail dans l'autre sens, mais **le type survit** :
`std::invalid_argument` → `ValueError`, `std::out_of_range` → `IndexError`,
`std::runtime_error` → `RuntimeError`. Le service IA distinguera donc les cas
sans avoir besoin de la convention de préfixes du §8.5.

---

## 9. Ce qui gèle aujourd'hui

Une fois qu'un `BattleState` est en base, ces quatre tables ne peuvent plus
bouger sans invalider les parties sauvegardées :

| Table | Source de l'ordre | Ajouter en fin ? |
|---|---|---|
| Espèces (49) | ordre alphabétique des fichiers de `data/pokemon/` | ❌ **non** — insère au milieu |
| Moves (95) | ordre alphabétique des fichiers de `data/moves/` | ❌ **non** — insère au milieu |
| Objets (13) | ordre d'enregistrement dans `item.cpp` (ADR #45) | ✅ oui, append-only |
| Talents (45) | ordre d'enregistrement, `registration.hpp` (ADR #51) | ✅ oui, append-only |

Les deux premières lignes sont le vrai risque : **ajouter un Pokémon ou une
attaque décale les ids de tous ceux qui suivent alphabétiquement.** C'est
l'argument central en faveur de l'ADR #58 (empreinte de catalogue).

S'y ajoutent l'ordre des `kind` de `FfiEvent` (§6) et la valeur numérique des
enums qui traversent : `Type`, `Status`, `StatIndex`, `Weather`, `Terrain`,
`HazardKind`. Tous portent déjà un commentaire « do not reorder » dans les
headers.

---

## 10. Plan de la journée

| Créneau | Quoi |
|---|---|
| 9 h 00 – 9 h 30 | Alex : visite de la frontière. Les 3 fonctions, le protocole §7, le fait que `EventLog` et `Action` ne sont pas des PODs |
| 9 h 30 – 10 h 30 | Taj : prototype `cxx` sur un `BattleState` bidon à tableaux imbriqués → **verdict D2** |
| 10 h 30 – 12 h 00 | À deux : trancher D1, D3-D8, la convention d'erreurs du §8.5, et figer le §6. **C'est le livrable de la journée** |
| Après-midi | Taj : `build.rs` + squelette de bridge sur des stubs C++ qui renvoient du vide. Alex : items A et B |
| Fin de journée | Un appel trivial (`engine_init` + `find_species_id`) qui traverse réellement depuis un test Rust |

**Definition of done :** la spec est écrite et validée à deux, le build
Rust→CMake fonctionne, un appel traverse. Le combat complet, c'est pour la
suite. Un seul appel qui marche vaut mieux que dix qui compilent.

---

## 11. Dettes connues, sans rapport avec le FFI

À mentionner en fin de journée pour qu'elles ne se perdent pas — aucune ne
bloque la phase 16b :

- **Légendaires** : tous les flags à `false`, la liste officielle n'est jamais
  arrivée. La règle « max 1 » est codée *et testée*. Le jour où la liste
  tombe, c'est un flip de booléens en pur JSON, zéro code.
- **Archétypes météo orphelins** : le roster ne contient aucun move de météo ni
  aucun talent poseur de sable ou de pluie. Baigne Sable (Minotaupe) et
  Glissade (M-Laggron) sont donc **inertes en partie réelle**. La neige, elle,
  tourne (Alerte Neige de Feunard d'Alola). À trancher : ajouter un poseur, ou
  assumer deux talents morts.
- **Confusion** (secondaire de Vent Violent) : non implémentée, divergence
  assumée depuis la phase 14. Coût si on la veut : un volatile + un jet par
  action.
- **Illusion** (Zoroark-H) et **Distorsion** : reportées explicitement. Illusion
  est un problème de *protocole* — le moteur devrait tracker « espèce
  affichée » et les events envoyés au front devraient mentir. À designer avec
  Taj si on la reprend.
