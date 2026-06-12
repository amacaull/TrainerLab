# Combat Engine — Roadmap

> Source de vérité pour le moteur de combat C++ du projet ft_transcendence.
> À mettre à jour à chaque PR mergée. Si une feature n'est pas listée ici, elle n'existe pas.

**Responsable** : Alex
**Langage** : C++20
**Build** : CMake
**Tests** : Catch2 v3
**Données** : JSON (nlohmann/json)
**Dernière MAJ** : 2026-06-12 (phase 2 statuts mergée ; 54/54 tests)

---

## Vision

Moteur de combat Pokémon 3v3 tour-par-tour, **embarqué dans le backend Rust via FFI** (bindings type `cxx`). Pas de communication réseau entre Rust et le moteur : Rust détient le `BattleState`, appelle `resolveTurn()` directement sur l'objet C++ partagé en mémoire.
**Principe directeur** : data-driven. Ajouter un Pokémon ou une attaque = ajouter un fichier JSON, jamais modifier le moteur. Le moteur ne connaît que des *effets* atomiques composables ; les attaques sont des listes d'effets décrites en JSON.

### Contrainte non-négociable

À chaque phase, le projet **compile, tourne et passe tous ses tests**. Pas de phase "moitié faite". On ajoute une couche, on la teste, on la merge, on passe à la suivante.

---

## Architecture (résumé)

```
┌─────────────────────────────────────────────────────────────┐
│                       BattleEngine                          │
│  • resolveTurn(state, action_p1, action_p2) -> new_state    │
│  • orchestration : ordre, dégâts, effets, fin de tour       │
└──────────────┬──────────────────────────────────────────────┘
               │ utilise
               ▼
┌─────────────────────────────────────────────────────────────┐
│                       BattleState (POD pur)                 │
│  • teams[2], active[2], weather, hazards, turn, rng_seed    │
│  • layout C-compatible (FFI Rust), zéro logique             │
└─────────────────────────────────────────────────────────────┘
               ▲
               │ transformé par
               ▼
┌─────────────────────────────────────────────────────────────┐
│                    Effects (polymorphes)                    │
│  • Damage, ApplyStatus, StatChange, Recoil, Recovery,       │
│    Hazard, Weather, ForceSwitch, Pivot, Protect, ...        │
│  • chaque Move = vector<unique_ptr<Effect>>                 │
└─────────────────────────────────────────────────────────────┘
               ▲
               │ enregistrés dans
               ▼
┌─────────────────────────────────────────────────────────────┐
│                    Abilities (hooks)                        │
│  • on_switch_in, on_before_move, on_modify_damage,          │
│    on_after_move, on_residual, on_hazard_apply, ...         │
│  • registry par string → factory                            │
└─────────────────────────────────────────────────────────────┘
               ▲
               │ chargés par
               ▼
┌─────────────────────────────────────────────────────────────┐
│                       DataLoader                            │
│  • lit data/ au démarrage : types, moves, pokemon, abil.    │
│  • valide la cohérence (movepool, talents existent...)      │
└─────────────────────────────────────────────────────────────┘
```

### Règles d'architecture

1. **`BattleState` est un POD.** Aucune méthode autre que getters. Layout C-compatible pour FFI.
2. **Le moteur est stateless.** `resolveTurn` est une fonction pure (modulo RNG). Aucun match n'est stocké côté C++.
3. **Le backend Rust détient les `BattleState`.** Il les crée, les passe par référence au moteur qui les modifie en place, les sauvegarde en DB.
4. **Le RNG est injecté.** Jamais de `rand()` global. Tests = RNG déterministe.
5. **Le moteur ne connaît aucune attaque par son nom.** Il connaît des effets.
6. **Les talents sont des listeners sur des événements du moteur.** Pas de switch géant.
7. **Toute donnée de jeu vit en JSON.** Code = règles génériques ; JSON = contenu. Le moteur charge ses JSON au démarrage (côté C++, transparent pour Rust).

---

## État d'avancement

Légende :
- ✅ Fait, testé, mergé
- 🚧 En cours
- ⬜ Pas commencé
- 🔵 Bloqué (en attente d'une autre tâche)

### Phase 0 — Fondations (vertical slice minimal)  ✅

Objectif : un combat 1v1 Dracaufeu vs Florizarre avec Lance-Flammes et Fouet Lianes tourne bout en bout. Toute l'archi est en place, juste minimaliste en contenu.

- ✅ Structure de projet + CMake + nlohmann/json + Catch2
- ✅ `Type` (enum 18 types) + `TypeChart` chargée depuis JSON
- ✅ `Stats` (struct) + calcul de stats à partir de stats de base et niveau
- ✅ `Effect` (interface) + premier effet `Damage`
- ✅ `Move` (struct : name, type, category, power, accuracy, priority, effects)
- ✅ `Species` (espèce Pokémon) + `BattlePokemon` (instance en combat)
- ✅ `Action` (variant : UseMove, Switch)
- ✅ `BattleState` (POD : teams, active, turn)
- ✅ `RNG` (interface + impl Mersenne Twister + impl déterministe pour tests)
- ✅ `DataLoader` : charge `data/types.json`, `data/moves/*.json`, `data/pokemon/*.json`
- ✅ `BattleEngine::resolveTurn` : ordre par priorité puis vitesse, applique effets, gère KO
- ✅ 2 Pokémon en JSON, 2 moves en JSON, type chart minimaliste
- ✅ `main.cpp` qui simule un combat et print les events
- ✅ Premier test Catch2 : "Lance-Flammes sur Florizarre fait ×2"
- ✅ 8/8 tests passent au 2026-05-19

### Dette technique de la phase 0 (à régler en phase 1)

- ✅ `g_typeChart` est une globale ; déplacé dans `EffectContext` (passé via `DataLoader`)
- ⬜ Speed tie déterministe (camp 0 d'abord) ; devrait être aléatoire
- ⬜ Pas de gestion du switch dans `executeAction` (placeholder qui no-op)
- ⬜ `accuracy > 100` non géré spécialement (sera utile pour les moves qui ne ratent jamais)

### Phase 1 — Données complètes de base + prep FFI

Mostly du JSON et des tests de cohérence, plus un petit refactor pour préparer le FFI.

**Données :**
- ✅ Type chart complet (18×18)
- ✅ Stats de base validées pour 8 Pokémon de test (cf. ADR #16)
- ✅ 15 attaques de dégâts pur couvrant tous les types présents dans le roster
- 🔵 Test : tous les Pokémon référencent des talents qui existent (reporté phase 5 — pas d'ability registry avant)
- ✅ Test : tous les Pokémon ont un movepool valide (test_catalog.cpp + validation au chargement par DataLoader)
- ✅ Test : matrice de types complète et valeurs canon vérifiées (immunités, ×2, ×4, ×0.25, ×0)

**Refactor pour préparer le FFI (cf. ADR #11 et #12) :**
- ✅ Catalogue de moves : `std::vector<Move>` indexé + `unordered_map<string, int>` pour le lookup au chargement
- ✅ Catalogue d'espèces : idem (`species_id_to_index_`)
- ✅ `BattlePokemon` : `std::array<int, 4> move_ids` + `int species_id` (plus de `Species*` ni de `vector<string>`)
- ✅ `BattleState` : `std::array<std::array<BattlePokemon, 3>, 2> teams` (taille fixe) + `int team_size[2]`
- ✅ Plus aucune indirection dans `BattlePokemon`/`BattleState` (POD strict, prêt FFI)
- ✅ Tous les tests passent toujours après le refactor (36/36)

**Validation défensive (cf. ADR #13) :**
- ✅ Fonction `validateState(const BattleState&, const DataLoader&)` qui throw `std::invalid_argument` avec message descriptif (`include/engine/validate.hpp`)
- ✅ Invariants vérifiés (à compléter à chaque phase qui ajoute des champs) :
  - `current_hp >= 0` et `current_hp <= stats.hp` pour chaque Pokémon
  - `level >= 1` et `level <= 100`
  - `team_size[side]` dans `[1, kTeamSize]`
  - `active_index[side]` dans `[0, team_size[side])`
  - `move_ids[i]` valides (dans le catalogue) ou égal à `kNoMove` (slot vide)
  - `species_id` valide (dans le catalogue)
  - `turn >= 0`
- ⬜ Appeler `validateState` au début de toute fonction exposée au FFI (déjà appelée dans `main.cpp` ; à systématiser en phase 10)
- ✅ Tests : 13 cas dans `test_validate.cpp` (states invalides + qualité des messages d'erreur)

### Phase 2 — Statuts  ✅

Ajout d'effets et de hooks `on_residual` (fin de tour) et `on_before_move`.

- ✅ Effet `ApplyStatus` (Burn, Poison, Toxic, Paralysis, Sleep, Freeze) — `effects/apply_status.{hpp,cpp}` ; probas (10% burn sur Flamethrower...) reportées phase 8 comme prévu
- ✅ Champ `status` dans `BattlePokemon` (+ `status_turns` : tours de sommeil restants OU compteur Toxic — un seul statut à la fois, pas de conflit ; POD conservé)
- ✅ Hook `on_residual` : dégâts brûlure (1/16), poison (1/8), toxic incrémental (n/16) — fonction interne `applyResidual` dans `engine.cpp`, appliquée camp le plus rapide d'abord (vitesse effective)
- ✅ Hook `on_before_move` : skip si paralysé (25%)/gelé (dégel 20%/tour)/endormi (1-3 tours roulés à l'application) — fonction interne `passesBeforeMove`
- ✅ Brûlure réduit les dégâts physiques de moitié (modificateur final ×0.5, canon gen 5+)
- ✅ Clause Sleep (1 seul endormi non-K.O. par camp ; `ApplyStatus(Sleep)` échoue sinon)
- ✅ Immunités de type : Feu ≠ burn, Électrik ≠ para, Poison/Acier ≠ poison ; immunité de la table des types bloque aussi les moves Status (ThunderWave vs Sol) — cf. ADR #17
- ✅ Paralysie ÷2 la vitesse effective dans `computeOrder` dès maintenant (cf. ADR #17)
- ✅ Dégel canon Showdown : un move Feu offensif qui touche dégèle la cible (cf. ADR #17)
- ✅ 5 nouveaux moves Status : WillOWisp, ThunderWave, Toxic, Spore, PoisonPowder (catalogue : 20 moves)
- ✅ 5 nouveaux events : StatusApplied, StatusFailed, StatusDamage, StatusCured, MoveSkipped (raison : Asleep/Frozen/FullyParalyzed)
- ✅ Tests : chaque statut individuellement + interactions (18 nouveaux cas, 54/54 au total)
- ✅ **`validateState`** : étendu pour vérifier `status` (valeur d'enum valide) et `status_turns >= 0`

### Notes phase 2 (à brancher plus tard)

- Phase 4 : reset du compteur Toxic (`status_turns`) au switch-out (TODO déjà posé dans `executeAction`)
- Phase 5 : `passesBeforeMove` et `applyResidual` sont des fonctions internes ; le système de hooks génériques des talents viendra s'y greffer sans changer leur contrat
- Phase 8 : si Rest est ajouté, exempter le sommeil auto-infligé de la Clause Sleep

### Phase 3 — Boosts de stats

- ⬜ Effet `StatChange` (+1, +2, -1, -2 sur Atk/Def/SpA/SpD/Spe/Acc/Eva)
- ⬜ Stages dans `BattlePokemon` (clamp à -6/+6)
- ⬜ Multiplicateurs appliqués au calcul de dégâts et à la vitesse
- ⬜ Boost reset au switch
- ⬜ Tests : Swords Dance × Earthquake = dégâts ×2, etc.
- ⬜ **`validateState`** : étendre pour vérifier `stat_stages[i]` dans `[-6, +6]`

### Phase 4 — Switch et Pivot

- ⬜ `Action::Switch{index}` traitée par le moteur
- ⬜ Switch joue toujours avant les attaques (priorité spéciale)
- ⬜ Effet `Pivot` (U-Turn, Volt Switch : dégâts puis switch forcé du user)
- ⬜ Hook `on_switch_in` (préparation pour talents/hazards)
- ⬜ Reset du compteur Toxic (`status_turns`) au switch-out (TODO posé en phase 2)
- ⬜ Tests : switch en mid-turn, switch après KO, double KO

### Phase 5 — Talents

- ⬜ Interface `Ability` avec tous les hooks (`on_switch_in`, `on_modify_damage`, etc.)
- ⬜ Registry par string → factory
- ⬜ `Species` référence un talent par nom
- ⬜ Premiers talents : Intimidation, Torrent, Brasier, Engrais, Lévitation
- ⬜ Tests : Intimidation baisse l'attaque adverse au switch-in, etc.

### Phase 6 — Météo

- ⬜ Enum `Weather` + champ + tours restants dans `BattleState`
- ⬜ Effet `SetWeather` (Dance Pluie, Zénith, Tempête de Sable, Grêle)
- ⬜ Hook `on_modify_damage` (Pluie × Eau ×1.5, etc.)
- ⬜ Hook `on_residual` (dégâts Sable/Grêle aux types non-immunisés)
- ⬜ Talents-météo (Sécheresse, Crachin) au switch-in
- ⬜ Tests
- ⬜ **`validateState`** : étendre pour vérifier `weather` (enum valide) et `weather_turns_left >= 0`

### Phase 7 — Hazards

- ⬜ Enum `Hazard` + map<Hazard, int> par camp dans `BattleState`
- ⬜ Effets `SetHazard` (Stealth Rock, Spikes, Toxic Spikes)
- ⬜ Hook `on_switch_in` : application des hazards
- ⬜ Effets `RemoveHazards` (Rapid Spin, Defog)
- ⬜ Lévitation / type Vol ignorent Spikes/Toxic Spikes
- ⬜ Tests
- ⬜ **`validateState`** : étendre pour vérifier les compteurs de hazards par camp (≥ 0, ≤ max canon : Stealth Rock 1, Spikes 3, Toxic Spikes 2)

### Phase 8 — Effets secondaires probabilistes + crits + recul + recovery

- ⬜ Probabilité dans `ApplyStatus` (10% brûlure sur Flamethrower, etc.)
- ⬜ Effet `Flinch` (10% sur Iron Head, etc.)
- ⬜ Crits (proba de base + boosts, ignore les baisses de défense)
- ⬜ Effet `Recoil{fraction}` (1/3 sur Brave Bird, 1/4 sur Flare Blitz)
- ⬜ Effet `Recovery{fraction}` (1/2 sur Recover, Roost...)
- ⬜ Roost retire temporairement le type Vol
- ⬜ Tests avec RNG déterministe

### Phase 9 — Mécaniques avancées de moves

- ⬜ Effet `ForceSwitch` (Whirlwind, Roar, Dragon Tail)
- ⬜ Effet `Protect` + flag "used last turn" (échoue si chaîné)
- ⬜ Multi-tour : `MultiTurnCharge` (Solar Beam, Fly, Dig)
- ⬜ Invulnérabilité partielle pendant Fly/Dig
- ⬜ Tests

### Phase 10 — Exposition FFI (intégration avec le backend Rust)

> **Changement d'architecture (validé avec l'équipe 2026-05-19)** : on n'expose plus le moteur via REST. Il est embarqué dans le backend Rust via FFI. Le backend détient les `BattleState`, le moteur les modifie en place. Le service IA Python parle au backend Rust (pas directement au moteur).

- ⬜ Choix de la crate côté Rust : `cxx` (recommandé, type-safe, moderne) ou `bindgen`/`cc`
- ⬜ Couche `extern "C"` ou `#[cxx::bridge]` pour exposer :
  - `resolveTurn(state*, action_p0, action_p1, rng_seed) -> EventLog`
  - Constructeurs/getters pour `BattleState`, `BattlePokemon`, etc.
- ⬜ Assurer que `BattleState` a un layout C-compatible (audit des types : pas de `std::vector` directement exposé, prévoir des vues `span`-like ou des accesseurs)
- ⬜ Intégration build : `build.rs` côté Cargo qui invoque CMake, ou lib statique précompilée
- ⬜ Tests d'intégration côté Rust (un petit binaire de test qui crée un state, appelle le moteur, vérifie les events)
- ⬜ Documentation de l'API FFI (ce que Rust peut/doit appeler)

### Points à clarifier avec Taj avant d'attaquer

- ⬜ Crate FFI : `cxx` ou autre ?
- ⬜ Qui alloue le `BattleState` : Rust (passé par ref) ou C++ (via factory, handle opaque côté Rust) ?
- ⬜ Format de l'intégration build : `build.rs` qui appelle CMake, ou lib statique précompilée livrée en binaire ?

### Phase 11 — Polish

- ⬜ Logging structuré
- ⬜ Métriques (durée moyenne d'un tour, etc.)
- ⬜ Documentation Doxygen
- ⬜ Spec écrite de l'API FFI (contrat avec Taj)

---

## Décisions d'architecture (ADR-light)

À mettre à jour si on revient sur une décision.

| # | Décision | Justification | Date |
|---|----------|---------------|------|
| 1 | C++20 | concepts, ranges, designated initializers, support large | 2026-05-19 |
| 2 | CMake | standard de l'écosystème, joue bien avec Docker | 2026-05-19 |
| 3 | nlohmann/json | single-header, syntaxe propre, large adoption | 2026-05-19 |
| 4 | Catch2 v3 | tests, intégration CMake propre | 2026-05-19 |
| 5 | Effets en héritage virtuel (pas variant) | extensibilité primée sur la perf | 2026-05-19 |
| 6 | Données en JSON, pas en C++ | ajouter contenu sans recompiler | 2026-05-19 |
| 7 | Moteur stateless | testable, scale, l'état vit côté backend (en DB) | 2026-05-19 |
| 8 | RNG injecté | tests déterministes | 2026-05-19 |
| 9 | Talents = registry de listeners | scale à 50+ talents sans modifier le moteur | 2026-05-19 |
| 10 | **FFI Rust (au lieu de REST)** | pas de sérialisation JSON entre Rust et C++, intégration directe, simplicité de déploiement ; le backend Rust est le seul appelant du moteur (Python passe par Rust) | 2026-05-19 |
| 11 | **Rust détient le `BattleState`** (Option A) | struct de taille fixe (3v3 fixé), zéro allocation dynamique runtime, sauvegarde DB triviale côté Rust avec `serde`, debug simple, API FFI minimale | 2026-05-19 |
| 12 | **Référencement par index numérique à la frontière FFI** (jamais par string) | `BattleState` 100% C-compatible, aucune allocation à l'interface, lookup O(1), pas de typo silencieuse ; les noms (`"Flamethrower"`) restent uniquement côté C++ dans le `DataLoader` interne (`unordered_map<string, int>` pour le chargement initial des JSON) | 2026-05-19 |
| 13 | **Politique d'erreurs FFI : validation défensive + conversion en `Result`** | aucune exception C++ ne traverse la frontière FFI (UB sinon) ; chaque fonction exposée valide ses entrées (HP ≥ 0, level ∈ [1,100], indices dans les bornes, etc.) au début, throw une erreur descriptive si invalide, attrapée et convertie en `Result::Err` côté Rust via `cxx` ; en interne le moteur peut throw librement ; les segfaults (déréférences, etc.) restent fatals — pour les éviter, validation systématique des invariants en entrée | 2026-05-19 |
| 14 | **Niveau fixe à 50** (champ `level` conservé dans le POD pour flexibilité de test) | gameplay équilibré sans config de niveau côté frontend, mais on garde la possibilité de tester d'autres niveaux | 2026-05-24 |
| 15 | **Pas d'IVs ni d'EVs** (équivalent IVs=0, EVs=0, nature neutre) | équilibrage par stats de base + types, lisible pour le joueur, pas d'UI de config nécessaire ; `computeStats` reste paramétrée par stats de base + niveau, suffisant | 2026-05-24 |
| 16 | **Roster de test (8 Pokémon)** distinct du roster final (48 Pokémon, à définir) | la phase 1 vérifie le moteur, pas le contenu final ; roster de test : Charizard, Venusaur, Blastoise, Pikachu, Snorlax, Gengar, Machamp, Garchomp (couvre 8 types primaires + rôles offensifs/défensifs/rapides/lents) | 2026-05-24 |
| 17 | **Statuts : mécanique canon gen 6+/Showdown dès la phase 2** | immunités de type (Feu≠burn, Électrik≠para, Poison/Acier≠poison) et immunité de la table des types pour les moves Status (ThunderWave vs Sol) incluses tout de suite ; paralysie ÷2 vitesse appliquée dans `computeOrder` sans attendre les multiplicateurs de la phase 3 ; dégel = 20%/tour + move Feu offensif qui touche (canon Showdown) ; sommeil roulé 1-3 tours à l'application ; probas de statut via `RNG::chance(float)` (et non `chancePct`) pour que `FixedRNG` force/bloque les procs sans toucher aux jets de précision ; résiduels appliqués dans l'ordre de vitesse effective | 2026-06-12 |

---

## Points en attente (à clarifier avec l'équipe)

### Pour la phase 10 (FFI)

- ⬜ **Crate FFI** : `cxx` (recommandé) ou `bindgen`/`cc` ?
  → **À trancher au début de la phase 10.** N'impacte pas le travail sur les phases 1-9.

- ⬜ **Comment Rust obtient les indices des moves/espèces** :
  - Option 1 : le moteur expose `find_move_id_by_name(name) -> int` et `find_species_id_by_name(name) -> int`. Rust appelle ça une fois au démarrage du backend, cache les résultats, après plus jamais de strings à l'interface.
  - Option 2 : on génère un fichier partagé (genre `data/index.json` ou un `enum MoveId` en Rust) que les deux côtés lisent au démarrage.

  → **À trancher avant la phase 10.** Idéalement quand le catalogue commence à se remplir (phase 1 ou 2), pour que Taj puisse commencer à construire ses équipes côté Rust avec un mécanisme stable. **Crucial** dès qu'il y a plus de 10 attaques/espèces, parce qu'au-delà ça devient pénible de hardcoder les indices manuellement.

- ⬜ **Intégration build** : `build.rs` qui invoque CMake, ou lib statique précompilée ?
  → **À trancher au début de la phase 10.**

### Pour les phases de contenu (1 et suivantes)

- ✅ **Roster** : 48 Pokémon, à proposer par l'équipe (cf. recap projet). Roster de test phase 1 décidé par Alex (cf. ADR #16).
- ✅ **Movepool par Pokémon** : décidé par Alex pour la phase 1 (test), à arbitrer en équipe pour le jeu final.
- ✅ **Niveau** : fixe à 50 (cf. ADR #14).
- ✅ **IVs / EVs** : pas implémentés (cf. ADR #15).

### Pour l'équipe globale

- ⬜ **CI/CD** : GitHub Actions ? GitLab CI ?

---

## Tooling

Outils en place pour la cohérence du code et le workflow Git. À installer une fois par développeur.

### Build et tests

- **CMake** (≥ 3.20) configure le projet, FetchContent récupère nlohmann/json et Catch2 automatiquement.
- **Catch2 v3** pour les tests unitaires et d'intégration. Lancer avec `ctest --output-on-failure` depuis `build/`.

### Formatage : clang-format

- Style défini dans `.clang-format` à la racine de `battle-engine/` : **base LLVM + ColumnLimit 100**.
- Installation locale : `brew install clang-format` (macOS) ou `apt install clang-format` (Linux).
- Application manuelle : `clang-format -i <fichier>`.
- Recommandé : activer le format-on-save dans l'éditeur via clangd (cf. config éditeur ci-dessous).

### Hook pre-commit

- Le hook `tools/pre-commit` (à la racine du repo) auto-formate les fichiers `.cpp`/`.hpp` staged sous `backend/battle-engine/` au moment du `git commit`. Les fichiers reformatés sont re-stagés transparente, le commit procède avec le message d'origine.
- **À installer une seule fois après clone** :

```bash
  ./tools/install-hooks.sh
```

  Crée un symlink `.git/hooks/pre-commit` → `tools/pre-commit`. Comme ça les mises à jour du hook (versionnées dans le repo) sont automatiquement actives.
- Si jamais on veut bypasser ponctuellement (rare) : `git commit --no-verify`.

### Configuration éditeur

- `.clangd` à la racine de `battle-engine/` configure clangd (LSP officiel C++) : pointe vers `build/compile_commands.json`, désactive les warnings `UnusedIncludes` (faux positifs sur les headers orphelins).
- **Setup VSCode recommandé** :
  - Désinstaller / désactiver l'extension Microsoft C/C++ pour ce workspace.
  - Installer l'extension **clangd** (publisher : LLVM).
  - **Ouvrir VSCode depuis `battle-engine/`**, pas depuis la racine du repo, sinon clangd ne trouve pas son `.clangd`.
- Pour activer le format-on-save dans VSCode, ajouter dans `.vscode/settings.json` :

```json
  {
    "editor.formatOnSave": true,
    "[cpp]": { "editor.defaultFormatter": "llvm-vs-code-extensions.vscode-clangd" }
  }
```

---

## Convention de commit

```
feat(engine): ajout de l'effet Recoil
fix(loader): segfault si data/moves est vide
test(status): ajoute test brûlure × attaque physique
docs(roadmap): coche Phase 2 statuts
refactor(types): extrait TypeChart dans son propre header
```

## Convention de PR

- Une PR = une feature ou une couche d'une phase
- Toujours mettre à jour `ROADMAP.md` dans la PR
- Tests obligatoires pour toute nouvelle feature
- Pas de merge dans `dev` sans review d'au moins 1 membre

---

## Glossaire

- **Effect** : action atomique appliquée au `BattleState` (faire des dégâts, poser un statut, etc.). Une attaque est une *liste* d'effets.
- **Ability** (talent) : listener qui s'enregistre sur des événements du moteur (`on_switch_in`, etc.).
- **Hook** : point d'extension du moteur où les talents/objets/conditions peuvent intervenir.
- **Stage** : niveau de boost/baisse de stat (-6 à +6).
- **STAB** : Same Type Attack Bonus, ×1.5 si le move partage un type avec son utilisateur.
- **Hazard** : entry hazard, piège posé sur le terrain adverse (Stealth Rock, Spikes...).

---

## Workflow GitHub

Repo : `bhyant/42-Transcendence` (monorepo)
Branches protégées : `main` et `dev` (PR + 1 review obligatoires)

### Règle d'or

**On ne push JAMAIS directement sur `main` ou `dev`.** Tout passe par une branche de feature + PR vers `dev`. Quand `dev` est stable, on merge `dev` → `main` via une PR séparée.

### Convention de nommage des branches

Une branche par phase ou sous-feature. Toujours préfixée par le service quand on est dans le monorepo :

```
feat/battle-engine-<nom-court-descriptif>
fix/battle-engine-<bug>
test/battle-engine-<scope>
docs/battle-engine-<scope>
```

Exemples concrets pour les phases à venir :
- `feat/battle-engine-foundations` (phase 0 ✅)
- `feat/battle-engine-type-chart` (phase 1)
- `feat/battle-engine-status` (phase 2)
- `feat/battle-engine-boosts` (phase 3)
- `feat/battle-engine-switch` (phase 4)
- `feat/battle-engine-abilities` (phase 5)
- `feat/battle-engine-weather` (phase 6)
- `feat/battle-engine-hazards` (phase 7)
- `feat/battle-engine-crit-recoil` (phase 8)
- `feat/battle-engine-advanced-moves` (phase 9)
- `feat/battle-engine-ffi-rust` (phase 10)

### Démarrer une nouvelle phase

```bash
# 1. Toujours partir de dev a jour
git checkout dev
git pull origin dev

# 2. Creer la branche de feature
git checkout -b feat/battle-engine-<nom>

# 3. Verifier qu'on est bien sur la nouvelle branche
git branch
# * feat/battle-engine-<nom>  <- doit etre selectionnee
```

### Pendant le développement

Commit souvent, en petits morceaux logiques. Suit la convention de commit ci-dessus.

```bash
# Voir ce qui a change
git status
git diff

# Ajouter precisement (eviter git add . sans regarder)
git add battle-engine/src/effects/status.cpp
git add battle-engine/include/engine/effects/status.hpp
git status  # revalider avant de commit

# Commit
git commit -m "feat(engine): ajoute l'effet ApplyStatus

- Nouveau hook on_residual pour les degats de brulure/poison
- Implementation Burn, Poison, Toxic, Paralysis, Sleep, Freeze
- Tests pour chaque statut"

# Push (la premiere fois sur cette branche : -u)
git push -u origin feat/battle-engine-<nom>

# Push les fois suivantes
git push
```

### Garder sa branche à jour avec `dev`

Si Taj/Ilan/Laid ont mergé des trucs dans `dev` pendant que tu bossais, il faut récupérer ces changements régulièrement pour éviter les conflits monstrueux à la fin :

```bash
# Sauvegarder ton travail en cours
git status                       # verifier qu'il n'y a rien d'oublie
git stash                        # OU commit tes changements

# Recuperer dev
git checkout dev
git pull origin dev

# Revenir sur sa branche et merger dev dedans
git checkout feat/battle-engine-<nom>
git merge dev
# (resoudre les conflits si besoin)

# Repush
git push

# Recuperer les changements stashes si on en a fait
git stash pop
```

### Ouvrir la PR

Après le `git push -u`, GitHub te répond avec une URL :
```
remote: Create a pull request for 'feat/battle-engine-<nom>' on GitHub by visiting:
remote:      https://github.com/bhyant/42-Transcendence/pull/new/feat/battle-engine-<nom>
```

Sur la page de création de PR :
1. **Vérifier `base: dev`** (PAS `main` — GitHub propose `main` par défaut, faut le changer)
2. Compare : ta branche
3. Titre : reprendre le commit principal
4. Description : remplir le template (voir ci-dessous)
5. Reviewers : ajouter au moins 1 membre de l'équipe
6. Labels : `battle-engine` si vous en avez configuré

### Template de description de PR

```markdown
## Quoi
[Description en 2-3 phrases de ce que la PR apporte]

## Pourquoi
[Lien avec le ROADMAP : "Implémente la phase X" ou "Résout le point Y"]

## Comment tester
```bash
cd battle-engine/build
cmake ..
make -j
./battle_engine_demo
ctest --output-on-failure
```

## Checklist
- [ ] Le code compile sans warning sur clang 17
- [ ] Tous les tests passent (X/X)
- [ ] Nouveaux tests ajoutés pour les nouvelles features
- [ ] ROADMAP.md mis à jour (cases cochées + dette technique notée)
- [ ] Pas de fichier de build commit par erreur (`build/`, `*.o`, etc.)
```

### Après la review

Si le reviewer demande des changements :
```bash
# Faire les modifs sur la meme branche
# ... edit ... edit ...
git add <fichiers>
git commit -m "fix(engine): corrige ce que <reviewer> a demande"
git push
# La PR se met a jour automatiquement
```

### Après le merge

```bash
# Revenir sur dev
git checkout dev
git pull origin dev

# Supprimer la branche locale (elle ne sert plus)
git branch -d feat/battle-engine-<nom>

# Optionnel : supprimer la branche distante si GitHub ne l'a pas fait
git push origin --delete feat/battle-engine-<nom>
```

### Cas particuliers

**J'ai commité par erreur sur `main` ou `dev` (sans avoir push)** :
```bash
# Annuler le dernier commit en gardant les changements
git reset --soft HEAD~1
# Creer la bonne branche
git checkout -b feat/battle-engine-<nom>
# Commit a nouveau au bon endroit
git commit -m "..."
```

**J'ai oublié un fichier dans mon dernier commit (non-pushé)** :
```bash
git add <fichier-oublie>
git commit --amend --no-edit
# Si deja pushe : git push --force-with-lease
```

**Je veux annuler mon dernier commit pushé sur ma branche de feature** :
```bash
# Sur ta branche de feature seulement, JAMAIS sur dev/main
git reset --hard HEAD~1
git push --force-with-lease
```

**Conflits au merge avec `dev`** :
1. Git va lister les fichiers en conflit dans `git status`
2. Ouvrir chaque fichier, chercher les marqueurs `<<<<<<<`, `=======`, `>>>>>>>`
3. Résoudre manuellement (garder la bonne version)
4. `git add <fichier-resolu>` puis `git commit` (sans message, Git met "Merge branch...")
5. `git push`

### Checklist avant CHAQUE push

- [ ] `git status` ne montre rien d'inattendu
- [ ] Pas de `build/`, `_deps/`, `.DS_Store` ou autre fichier généré
- [ ] Le code compile (`make` dans `build/`)
- [ ] Les tests passent (`ctest`)
- [ ] Le ROADMAP est à jour si la PR coche/débloque des items
- [ ] Le message de commit suit la convention

### Quand demander à merger `dev` → `main`

- Plusieurs phases complètes mergées dans `dev`
- Tous les tests passent
- La démo tourne bout en bout
- Un point logique du projet (genre fin de Phase 4 = "switch et pivot fonctionnels")

Ouvrir une PR `dev` → `main` avec un titre style `Release: phases 0-4` et un changelog dans la description listant tout ce qui a été ajouté depuis la dernière release.
