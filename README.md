# Moteur de combat — ft_transcendence

Moteur de combat Pokémon en C++20, inspiré de Pokémon Showdown. Il résout les
tours d'un combat en simple, sans garder d'état : le serveur Rust lui passe un
`BattleState`, deux actions, une graine aléatoire, et récupère l'état modifié
avec la liste des événements à mettre en scène.

| | |
|---|---|
| Espèces | 49 (dont 8 Méga) |
| Attaques | 95 |
| Talents | 45 |
| Objets tenus | 13 |
| Types | 18, table gen 6+ |
| Tests | 279 (Catch2), dont un test de combats aléatoires en masse |

---

## Sommaire

1. [Lancer en deux minutes](#1-lancer-en-deux-minutes)
2. [Ce que fait le moteur](#2-ce-que-fait-le-moteur)
3. [Architecture](#3-architecture)
4. [Décisions structurantes](#4-décisions-structurantes)
5. [Qualité : comment on sait que c'est juste](#5-qualité--comment-on-sait-que-cest-juste)
6. [Contrat FFI](#6-contrat-ffi)
7. [Données et extension](#7-données-et-extension)
8. [Limites connues](#8-limites-connues)

---

## 1. Lancer en deux minutes

Prérequis : CMake ≥ 3.20 et un compilateur C++20 (clang ou gcc). Au premier
configure, CMake télécharge nlohmann/json et Catch2 (`FetchContent`).

```bash
cd backend/battle-engine
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure   # toute la suite
./build/battle_engine_demo                   # quelques combats commentés
```

La démo joue des duels scénarisés (STAB, faiblesse ×4, priorité, recul…) et
affiche chaque événement.

En production, le moteur n'est jamais lancé seul : il est compilé en
bibliothèque statique par le `build.rs` du backend Rust et appelé via `cxx`
(voir [§6](#6-contrat-ffi)).

---

## 2. Ce que fait le moteur

### Le format

- **Équipes de 1 à 6 Pokémon par camp.** Les deux camps n'ont pas besoin
  d'avoir la même taille : c'est le matchmaking du serveur qui n'apparie que
  des équipes de même taille.
- **Niveau 100 pour tous**, IV à 31, EV et natures fixés par espèce.
- **Sets fixes** : chaque espèce a exactement quatre attaques, un talent et un
  objet. Le joueur choisit ses Pokémon, pas leur configuration.
- **Règles d'équipe** : pas de doublon d'espèce, au plus 1 Méga, au plus
  1 légendaire.
- **Tours simultanés** : les deux joueurs choisissent, le moteur ordonne par
  priorité puis vitesse, égalités tranchées au hasard.

### Les mécaniques couvertes

Toutes suivent le comportement de Showdown (génération 9), sauf mention
contraire en [§8](#8-limites-connues).

- **Dégâts** : formule canon, STAB, efficacités, coups critiques (taux
  Showdown, attaques à taux élevé), variance, dégâts fixes (Frappe Atlas),
  dégâts selon le poids (Nœud Herbe, Balayette), attaques utilisant la
  Défense ou l'Attaque adverse (Big Splash, Tricherie).
- **Précision et ordre** : paliers de précision et d'esquive, tranches de
  priorité, vitesse effective (paralysie, talents de météo).
- **Statuts** : brûlure, poison, poison grave, paralysie, sommeil (avec la
  Clause Sommeil), gel ; immunités de type et de talent.
- **Crans de stat** de −6 à +6, remis à zéro au changement de Pokémon.
- **Terrain de jeu** : neige (gen 9), vent mystérieux (Souffle Delta), Champ
  Électrifié, Voile Aurore, Piège de Roc, Picots, Pics Toxik.
- **Mécaniques d'attaques** : effets secondaires, recul, drain, soins, Souhait,
  attaques en deux tours, coups multiples, protection (Abri, Blockhaus),
  changements forcés (Hurlement, Draco-Queue), pivots (Demi-Tour, Change Éclair,
  Téléport), Sabotage, Lien du Destin, Repos, Blabla Dodo, Cognobidon,
  vol de boosts, attaques réservées au premier tour.
- **PP et Lutte** : un Pokémon sans PP utilise Lutte.
- **45 talents** (Intimidation, Pression, Régé-Force, Miroir Magik, Farceur,
  Paratonnerre…) et **13 objets** (objets Choix, Restes, Orbe Vie, Ceinture
  Force, Grosses Bottes, Baie Sitrus…).
- **Remplacement après K.O.** : le moteur s'arrête et attend le choix du
  joueur.

---

## 3. Architecture

### Les couches

```
include/engine/ et src/ (miroir strict)
├── model/      vocabulaire du jeu : types, stats, statuts, espèces, attaques
├── core/       orchestration : BattleState, BattleEngine, chargement, validation
├── effects/    une classe par effet d'attaque (Damage, StatChange, Pivot…)
├── abilities/  les 45 talents, regroupés par famille de hooks
├── items/      les 13 objets
└── ffi/        la frontière avec Rust : API plate et conversions
data/           le contenu du jeu, en JSON
tests/          une suite par domaine, et tests/fixtures/ pour les instruments de test
demo/           la démo en ligne de commande
tools/          showdown-diff (§5)
```

Les dépendances vont dans un seul sens :

```
          ffi  ──►  BattleEngine (core)
                         │ orchestre
                         ▼
   Effects ──transforment──►  BattleState (POD, zéro logique)
       ▲                           ▲
       │ listes d'effets           │ état par combat
   Moves (JSON)          Abilities / Items : listeners sur des hooks
                         (singletons sans état)
                         ▲
                     DataLoader : lit data/ au démarrage, valide
```

- **`BattleState` est une struct POD** de taille fixe : deux équipes de six,
  l'actif de chaque camp, la météo, le terrain, les écrans, les pièges, le
  numéro du tour. Aucune allocation, aucun pointeur : elle traverse la
  frontière FFI telle quelle et se sérialise en base côté Rust.
- **Une attaque est une liste d'effets.** Le moteur ne connaît aucune attaque
  par son nom, seulement des effets génériques configurés par le JSON. Seule
  exception : Lutte, qui est une règle du jeu et non du contenu.
- **Talents et objets sont des listeners** branchés sur des hooks du moteur
  (entrée, sortie, avant l'attaque, calcul des dégâts, fin de tour…). Ce sont
  des singletons sans état : tout ce qui change pendant un combat (objet
  consommé, verrou Choix, Déguisement cassé) vit dans le `BattleState`.

### Le déroulé d'un tour

`BattleEngine::resolveTurn(state, action0, action1, rng)` :

1. **Validation des deux actions** (`checkAction`), avant toute modification :
   actif K.O., emplacement vide, 0 PP, verrou Choix, changement illégal.
2. **Ordre du tour** : les changements de Pokémon passent en premier, puis
   les attaques par priorité, puis par vitesse effective.
3. **Exécution de chaque action.** Pour une attaque : obstacles avant l'attaque
   (sommeil, gel, paralysie, peur), paiement des PP, protection, précision,
   immunités de talent, puis la chaîne d'effets, puis la fenêtre après
   l'attaque (recul d'Orbe Vie, Magicien, Impudence).
4. **Fin de tour**, dans l'ordre canon : météo, terrain, Souhait, objets de
   soin, dégâts de statut, orbes, écrans, nettoyage des effets d'un tour.

Chaque étape ajoute des événements à un `EventLog`. C'est tout ce que le
front reçoit pour mettre le combat en scène.

---

## 4. Décisions structurantes

Les choix qui expliquent la forme du code, regroupés par thème.

### Architecture du moteur

| Décision | Pourquoi |
|---|---|
| **Moteur sans état** : tout l'état vit dans `BattleState`, détenu par Rust | Testable, aucun combat stocké côté C++, sauvegarde en base triviale |
| **Contenu en JSON, règles en C++** | Ajouter une attaque ou une espèce sans recompiler |
| **Effets en héritage virtuel** plutôt qu'en `std::variant` | L'extensibilité prime : un nouvel effet est une nouvelle classe |
| **Effets secondaires = un wrapper générique** piloté par `"chance"` | N'importe quel effet devient un secondaire à X %, sans code dédié |
| **Talents et objets en registres de listeners** | 45 talents sans `switch` géant ni modification du moteur |
| **RNG injecté**, graine par appel, tirages écrits à la main | Tests déterministes ; la même graine rejoue le même combat sur macOS comme sous Linux (les distributions de la bibliothèque standard diffèrent d'une plateforme à l'autre) |
| **Arborescence par domaines**, miroir `include/` et `src/` | Lisible à 49 espèces et 95 attaques ; les tests sont nommés par domaine |
| **Noms canon anglais en PascalCase ASCII** (`ShadowBall`, `GiratinaOrigin`) | Pas d'accents ni d'espaces dans les fichiers et les chaînes FFI ; l'affichage français est l'affaire du front |

### Règles du jeu

| Décision | Pourquoi |
|---|---|
| **Niveau 100, IV 31, EV et natures** | L'équilibrage du roster repose sur les répartitions d'EV et les natures |
| **Sets, talents et objets fixés par espèce** | Teambuilder simple, et l'IA n'a qu'un problème de composition d'équipe |
| **Méga = espèces à part entière**, sans transformation en combat | Aucun système supplémentaire : stats, types et talent sont dans le JSON |
| **Équipes de 1 à 6** (`kTeamSize = 6`, `team_size` par camp) | Tous les formats avec un seul layout mémoire |
| **Remplacement après K.O. par une fonction dédiée**, sans auto-switch | Le choix du remplaçant est stratégique, il doit remonter au joueur ou à l'IA |
| **Attaques en deux tours : le moteur force la suite** et ignore l'action du 2e tour | Plus simple pour le serveur et l'IA qu'une erreur |
| **Protection pilotée par la donnée** (`selfOrField`) | Pas d'heuristique fragile pour savoir ce qu'Abri bloque |

### Frontière avec Rust

| Décision | Pourquoi |
|---|---|
| **Appel direct par FFI (`cxx`)** plutôt qu'un service REST | Pas de sérialisation entre Rust et C++, un seul appelant |
| **Index numériques à la frontière**, jamais de noms | `BattleState` C-compatible, aucune allocation, pas de faute de frappe silencieuse |
| **Garantie forte** : chaque appel travaille sur une copie et ne valide qu'à la fin | Une erreur en plein tour ne laisse jamais un état à moitié écrit |
| **Erreurs préfixées** (`E_TEAM`, `E_ACTION`…) | `cxx` ne transporte que le message : le préfixe dit à Rust si c'est l'erreur du joueur ou un bug |
| **`FfiEvent` = une struct plate unique** plutôt que 36 types | Pont trivial ; le front fait de toute façon un `switch` sur `kind` |
| **Chemin de `data/` obligatoire, aucun repli** | Un autre `data/` donne d'autres index : un repli silencieux réinterpréterait les parties en base |
| **Empreinte du catalogue** vérifiée par Rust au chargement d'une partie | Détecte un catalogue dont les noms ou l'ordre ont changé après la sauvegarde d'un combat |

---

## 5. Qualité : comment on sait que c'est juste

Trois outils complémentaires, chacun attrape une classe de bugs différente.

### Les tests unitaires et d'intégration

Une suite Catch2 par domaine : types, stats, dégâts, statuts, crans, météo,
pièges, protection et attaques en deux tours, PP et Lutte, objets, talents,
terrain, mécaniques d'attaques, validation, catalogue, FFI. Les scénarios
utilisent un RNG déterministe qui force ou interdit chaque tirage (critique,
effet secondaire, précision), donc chaque règle est vérifiée sur un cas exact.

### Le fuzz (`tests/test_fuzz.cpp`)

Joue des combats complets avec des équipes et des actions tirées au hasard,
uniquement par l'API FFI, comme le serveur. Après chaque appel, il vérifie :

- que l'état reste valide (`validate_state`, une quarantaine d'invariants) ;
- que chaque événement est bien formé (kind connu, camp, emplacement et
  `name_id` dans les bornes) ;
- qu'une action refusée ne modifie rien ;
- **que les événements suffisent à reconstituer l'état** : PV, statut, PP et
  présence de l'objet sont rejoués à partir des seuls événements, comme le
  fait le front, puis comparés à l'état réel.

Il tourne avec la suite normale (300 combats, moins d'une seconde) et
s'étend à la demande :

```bash
FUZZ_BATTLES=50000 ./build/tests/battle_engine_tests "[fuzz]"
FUZZ_SEED=1592590343 ./build/tests/battle_engine_tests "[fuzz]"   # rejoue un combat
```

Chaque anomalie est affichée avec la graine qui la reproduit.

### La comparaison avec Showdown (`tools/showdown-diff`)

Compare nos 95 attaques et 49 espèces aux données de Pokémon Showdown
(`@pkmn/dex`) : puissance, précision, PP, priorité, drapeaux (contact,
coup de poing, tranchant…), effets secondaires et leur cible, recul, drain,
soin, protection, stats de base, types, talent, poids, légendaire. Il lit les
JSON avec les mêmes règles que `data_loader.cpp`, donc un bug de chargement
apparaît comme un écart.

```bash
cd tools/showdown-diff && npm i && npm run diff
# sans Node : docker run --rm -v "$PWD/../..":/be -w /be/tools/showdown-diff node:24-alpine sh -c "npm i && npm run diff"
```

Les quatre écarts restants sont des choix assumés, listés en
[§8](#8-limites-connues).

### Formatage

`clang-format` (LLVM, 100 colonnes), appliqué au commit par un hook
pre-commit : `./tools/install-hooks.sh` depuis la racine du dépôt.

---

## 6. Contrat FFI

### Le principe

> La frontière n'est pas « Rust appelle C++ ».
> C'est **« Rust pilote une machine à états qu'il possède »**.

Le C++ fournit les **règles** et les **catalogues**. Rust fournit le
**stockage** (le `BattleState` en base), les **décisions** (actions des
joueurs, remplaçants) et la **boucle de partie**.

**Rust ne réimplémente rien** : ni la formule de stats, ni la légalité d'une
équipe, ni l'ordre du tour. Si un calcul est nécessaire côté Rust, c'est une
fonction à exposer, jamais à réécrire. Il n'assemble jamais un `BattlePokemon`
champ par champ : il appelle `make_combatant`.

### Protocole d'appel

```
engine_init(data_dir)                ← une fois au démarrage du processus
find_*_id(...)                       ← une fois, index mis en cache
catalog_fingerprint()                ← stocké à la création de chaque partie

par partie :
  make_combatant(species_id) × taille d'équipe
  validate_team(...)                 ← une fois, à la soumission
  start_battle(state, seed)          ← obligatoire : déclenche les talents d'entrée
  boucle :
    is_over ? → fin
    collecter les deux actions
    resolve_turn(state, a0, a1, turn_seed)
    pour chaque camp dont l'actif est K.O. (ordre donné par faster_side) :
      demander le remplaçant → resolve_replacement(state, side, index)
    persister, envoyer les événements au front
```

Deux règles non négociables :

1. **`start_battle` n'est pas optionnel.** Sans lui, aucun talent d'entrée ne
   se déclenche (Intimidation, Alerte Neige…).
2. **`resolve_turn` refuse de jouer si un actif est K.O.** Le remplacement est
   une étape obligatoire, pas un détail.

La graine doit changer à chaque tour (`turn_seed = hash(battle_seed, turn)`) :
avec une graine constante, critiques et effets secondaires deviendraient
périodiques.

### Garantie forte

Chaque fonction exposée valide l'état, travaille sur une **copie**, et ne
recopie le résultat dans l'état de Rust qu'une fois le calcul terminé. Soit le
tour s'applique entièrement, soit rien ne bouge.

### `FfiAction`

```cpp
struct FfiAction {
  uint8_t kind;         // 0 = attaque, 1 = changement
  int32_t index;        // attaque : emplacement 0-3 | changement : index d'équipe 0-5
  int32_t pivot_target; // attaque seulement : remplaçant d'un pivot, -1 = automatique
};
```

`index` d'une attaque désigne la position dans `move_ids` du Pokémon.
`pivot_target` n'est pas validé à l'avance (la cible peut tomber pendant le
tour) : le moteur retombe sur l'automatique si besoin.

### `FfiEvent`

```cpp
struct FfiEvent {
  uint8_t kind;     // voir la table, numérotation gelée
  int8_t  side;     // -1 pour les événements de terrain
  int8_t  slot;     // index d'équipe, -1 pour les événements de camp
  int32_t name_id;  // move_id | item_id | ability_id selon kind, -1 si aucun
  int32_t i0, i1;   // charge utile, sens défini par kind
  float   f0;       // efficacité de type uniquement
  uint8_t flags;    // bit 0 = STAB, bit 1 = critique
};
```

**Cette table est le contrat.** Sa numérotation est autoritative : elle ne
dépend pas de l'ordre interne des types d'événements.

| kind | Événement | `side` / `slot` | `name_id` | `i0` | `i1` | `f0` | `flags` |
|---|---|---|---|---|---|---|---|
| 0 | MoveUsed | lanceur | move_id | **PP dépensés** | — | — | — |
| 1 | DamageDealt | cible | — | dégâts | — | efficacité | STAB, crit |
| 2 | Fainted | K.O. | — | — | — | — | — |
| 3 | Missed | lanceur | move_id | — | — | — | — |
| 4 | StatusApplied | cible | — | `Status` | — | — | — |
| 5 | StatusFailed | cible | — | `Status` | — | — | — |
| 6 | StatusDamage | cible | — | dégâts | `Status` | — | — |
| 7 | StatusCured | concerné | — | `Status` | — | — | — |
| 8 | MoveSkipped | lanceur | — | `SkipReason` | — | — | — |
| 9 | StatStageChanged | cible | — | variation | `StatIndex` | — | — |
| 10 | StatChangeFailed | cible | — | `StatIndex` | 1 = hausse | — | — |
| 11 | SwitchedOut | sortant | — | — | — | — | — |
| 12 | SwitchedIn | entrant | — | — | — | — | — |
| 13 | AbilityTriggered | porteur | ability_id | — | — | — | — |
| 14 | MoveFailed | lanceur | move_id | — | — | — | — |
| 15 | WeatherStarted | −1 / −1 | — | `Weather` | — | — | — |
| 16 | WeatherEnded | −1 / −1 | — | `Weather` | — | — | — |
| 17 | WeatherDamage | cible | — | dégâts | `Weather` | — | — |
| 18 | HazardSet | camp qui subit / −1 | — | `HazardKind` | couches après pose | — | — |
| 19 | HazardDamage | cible | — | dégâts | `HazardKind` | — | — |
| 20 | HazardsCleared | camp nettoyé / −1 | — | — | — | — | — |
| 21 | ToxicSpikesAbsorbed | absorbeur | — | — | — | — | — |
| 22 | Healed | concerné | — | PV rendus | — | — | — |
| 23 | RecoilDamage | concerné | — | dégâts | — | — | — |
| 24 | Charging | lanceur | move_id | **PP dépensés** | — | — | — |
| 25 | Protected | protégé | — | — | — | — | — |
| 26 | ItemTriggered | porteur | item_id | — | — | — | — |
| 27 | ItemConsumed | porteur | item_id | — | — | — | — |
| 28 | ItemDamage | porteur | item_id | dégâts | — | — | — |
| 29 | ItemKnockedOff | dépossédé | item_id | — | — | — | — |
| 30 | TerrainStarted | −1 / −1 | — | `Terrain` | — | — | — |
| 31 | TerrainEnded | −1 / −1 | — | `Terrain` | — | — | — |
| 32 | ScreenStarted | camp / −1 | — | tours | — | — | — |
| 33 | ScreenEnded | camp / −1 | — | — | — | — | — |
| 34 | DestinyBondTriggered | l'attaquant entraîné | — | — | — | — | — |
| 35 | AbilityDamage | porteur | ability_id | dégâts | — | — | — |

Ce que le front doit savoir pour rejouer l'état à partir des événements :

- **PV** : `DamageDealt` porte le dégât **brut**, qui peut dépasser les PV
  restants. Il faut afficher et appliquer `min(dégâts, PV actuels)`.
- **PP** : `i0` de `MoveUsed` et de `Charging` est le coût réel (0, 1 ou 2) ;
  il suffit de faire `pp -= i0` sur l'emplacement de `name_id`. Pression
  (2 PP), la libération d'une attaque en deux tours et l'attaque appelée par
  Blabla Dodo (0 PP) sont déjà comptées.
- **Magicien** : l'`AbilityTriggered` du voleur précède immédiatement
  l'`ItemKnockedOff` de la victime ; c'est à ce moment que le voleur récupère
  l'objet.

### Sentinelles de `name_id`

| Valeur | Sens |
|---|---|
| `-1` | l'événement ne porte aucun nom |
| `-2` | Lutte (pas d'id de catalogue, `struggle_move_id()`) |
| `-3` | le recul de Lutte (`struggle_recoil_ability_id()`) |

Tout autre `name_id` négatif est un bug.

### Surface d'API

```
// Initialisation et catalogues
engine_init(data_dir)              // idempotent ; un autre chemin → E_INIT
engine_is_initialised() / engine_data_dir()
species_count() / move_count() / item_count() / ability_count()
find_species_id(id_string) / find_move_id(name) / find_item_id(name) / find_ability_id(name)
                                   // -1 si inconnu : sonder est légitime
species_id_string(id) / species_display_name(id) / move_name(id) / item_name(id) / ability_name(id)
species_entry(id) -> SpeciesEntry  // id_string, display_name, types, poids, mega, legendary
move_entry(id)    -> MoveEntry     // name, type, category, power, accuracy, pp, priority
catalog_fingerprint() -> u64

// Construction
make_combatant(species_id) -> BattlePokemon   // stats, objet, 4 attaques, PP, PV pleins
validate_team(team, team_size)                // règles de construction
validate_state(state)                         // invariants d'état

// Combat
start_battle(state, seed) -> Vec<FfiEvent>
resolve_turn(state, a0, a1, seed) -> Vec<FfiEvent>
resolve_replacement(state, side, team_index) -> Vec<FfiEvent>
faster_side(state, seed) -> i32               // ordre des remplacements simultanés
is_over(state) / side_has_lost(state, side)
```

### Erreurs

> **Une exception signale une violation de contrat, jamais un événement de
> jeu.** Une attaque ratée est un `Missed`, une attaque sans effet un
> `MoveFailed`.

`cxx` ne transporte que le message, donc chaque erreur commence par un préfixe
gelé, vérifié par un test :

| Préfixe | Sens | Réponse HTTP suggérée |
|---|---|---|
| `E_INIT` | moteur non initialisé, ou réinitialisé avec un autre chemin | 500 |
| `E_DATA` | `data/` absent ou JSON invalide | 500 |
| `E_ARG` | index hors catalogue | 500 |
| `E_STATE` | `BattleState` invalide | 500 |
| `E_TEAM` | équipe refusée | 400 |
| `E_ACTION` | action refusée | 400 |

Un refus d'action porte en plus un **sous-code**, lui aussi gelé :
`E_ACTION:<SOUS_CODE>: <phrase pour les logs>`. Rust découpe sur les deux
premiers `:` et ne lit jamais la phrase.

| Sous-code | Appel | Sens |
|---|---|---|
| `FAINTED` | `resolve_turn` | l'actif de ce camp est K.O. : `resolve_replacement` d'abord |
| `INVALID_SWITCH` | `resolve_turn`, `resolve_replacement` | cible de changement hors équipe, déjà active ou K.O. |
| `BAD_SLOT` | `resolve_turn` | index d'attaque hors de 0-3 |
| `EMPTY_SLOT` | `resolve_turn` | emplacement d'attaque vide |
| `NO_PP` | `resolve_turn` | plus de PP sur cette attaque |
| `CHOICE_LOCKED` | `resolve_turn` | verrouillé par un objet Choix sur une autre attaque |
| `BAD_KIND` | `resolve_turn` | `FfiAction.kind` ni 0 ni 1 |
| `BAD_SIDE` | `resolve_replacement` | camp ni 0 ni 1 |
| `NOT_FAINTED` | `resolve_replacement` | l'actif de ce camp n'est pas K.O. |

Un `E_STATE` ou un `E_ARG` en retour de `resolve_turn` est toujours un bug
côté appelant, jamais une issue normale de partie.

Toute fonction du bridge qui peut lever une exception doit être déclarée
`Result<T>` côté Rust : sinon l'exception atteint un shim `noexcept` et le
processus s'arrête sans log.

### Ce qui est gelé

| Table | Ordre des index | Ajouter en fin ? |
|---|---|---|
| Espèces (49) | ordre alphabétique des fichiers de `data/pokemon/` | **non**, ça décale les suivants |
| Attaques (95) | ordre alphabétique des fichiers de `data/moves/` | **non**, ça décale les suivants |
| Objets (13) | ordre d'enregistrement dans `item.cpp` | oui |
| Talents (45) | ordre d'enregistrement dans `registration.hpp` | oui |

Enums transmis en `int` :

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

**Empreinte du catalogue** : FNV-1a 64 bits écrit à la main (`std::hash`
n'est stable ni entre plateformes ni entre versions de libstdc++), sur les
quatre catalogues dans l'ordre espèces, attaques, objets, talents : le nombre
d'entrées, puis chaque nom suivi de `\0`. Valeur actuelle :
`0x9848D2D3F76497E5`, verrouillée par un test. Rust la stocke à la création
d'une partie et la revérifie au chargement. Elle ne couvre que les **noms et
l'ordre** : une correction de stats ou d'effet la laisse inchangée, et les
parties en cours continuent simplement avec les nouvelles données.

**En base**, on stocke les `id_string`, jamais les index, sauf à l'intérieur
d'un `BattleState` sérialisé, que l'empreinte protège.

---

## 7. Données et extension

### Arborescence

```
data/
├── types.json          table des types (seules les valeurs ≠ 1 sont listées)
├── pokemon/<Id>.json   une espèce par fichier ; le nom du fichier est l'id
└── moves/<Name>.json   une attaque par fichier
tests/fixtures/moves/   attaques neutres réservées aux tests (Tackle, Growl…),
                        hors du catalogue livré
```

### Une espèce

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

`item` est absent pour les Méga, `mega: true` les marque, `ability` peut être
vide.

### Une attaque

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

- `accuracy` ≤ 0 : ne rate jamais.
- `chance` sur un effet : en fait un effet secondaire à X %.
- `StatChange` vise la **cible** par défaut ; `"affectsUser": true` (ou
  `"target": "user"`) vise le lanceur.
- `selfOrField: true` : attaque sur soi ou sur le terrain, qu'Abri ne bloque
  pas et que Pression ne taxe pas.
- Drapeaux optionnels : `priority`, `contact`, `punch`, `slicing`,
  `bulletproof`, `reflectable`, `highCrit`, `bypassesProtect`, `twoTurn`,
  `multiHit`, `firstTurnOnly`, et quelques drapeaux propres à une attaque
  (`powerFromTargetWeight`, `useTargetOffense`…).
- Le chargeur est **strict** : une clé ou une valeur qu'il ne connaît pas
  (faute de frappe comprise) arrête le démarrage avec le nom du fichier en
  cause, au lieu d'être ignorée en silence.

### Ajouter une attaque ou une espèce

1. Créer le JSON. Pour une attaque, il faut qu'une espèce l'ait dans son
   `movepool` : un test vérifie que tout ce qui est livré est jouable.
2. Lancer `showdown-diff` et la suite de tests.
3. **Les index qui suivent alphabétiquement sont décalés.** Il faut donc :
   - mettre à jour la valeur d'empreinte dans son test (acte volontaire : les
     parties en base deviennent illisibles) ;
   - régénérer la table du front : `node scripts/gen-catalog.mjs` dans
     `frontend/`.

Un nouvel effet, talent ou objet demande du code : une classe dérivée
d'`Effect`, d'`Ability` ou d'`Item`, puis une ligne d'enregistrement (en fin
de liste pour les talents et les objets).

---

## 8. Limites connues

**Écarts assumés avec le jeu** (les quatre écarts de `showdown-diff`) :

- **Confusion** non implémentée : Vent Violent n'a pas son effet secondaire.
- **Illusion** (Zoroark d'Hisui, sans talent) : c'est un problème de
  protocole plus que de moteur, les événements envoyés au front devraient
  mentir sur l'espèce.
- **Méga-Ectoplasma** a Lévitation au lieu de Marque Ombre (décision
  d'équipe, pas de piégeage).
- **Mammochon** a Chasse-Neige pour profiter de la neige, un talent qu'il n'a
  pas dans les jeux.

**Autres divergences** :

- **Escampette** change de Pokémon automatiquement (vers le premier
  remplaçant valide) au lieu de laisser choisir le joueur en plein tour, ce
  qui serait incompatible avec un tour résolu en un seul appel.
- **Baigne Sable et Glissade** sont inertes : aucun Pokémon du roster ne pose
  le sable ou la pluie.

**Non exposé par la frontière** :

- **`legal_actions(state, side)`** : le front et l'IA doivent aujourd'hui
  essayer une action et lire l'éventuel `E_ACTION`.
- **Binding Python** (pybind11) pour un service d'IA : prévu, non fait.

**Hors périmètre** : combats en double, Distorsion, Clone, méga-évolution en
cours de combat.
