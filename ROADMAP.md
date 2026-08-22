# Combat Engine — Roadmap

> Source de vérité pour le moteur de combat C++ du projet ft_transcendence.
> À mettre à jour à chaque PR mergée. Si une feature n'est pas listée ici, elle n'existe pas.

**Responsable** : Alex
**Langage** : C++20
**Build** : CMake
**Tests** : Catch2 v3
**Données** : JSON (nlohmann/json)
**Niveau de combat** : 100 (ADR #33)
**Dernière MAJ** : 2026-08-21 (phase 16a — préparatifs FFI livrés : borne obsolète sur `invulnerable_state` corrigée, `ItemDamageEvent` purgé des noms de talents et de moves, registry de talents indexé et gelé, CMake rendu autonome/offline/PIC avec règles `install` — 240/240 tests. Reste 16b : l'exposition FFI elle-même, bloquée sur les décisions D2 et D5 à trancher avec Taj le 2026-08-22, voir `FFI-CONTRACT.md`)

*Historique* : 2026-07-28 (phase 15 terminée : 49 espèces + 95 moves générés et vérifiés, validateTeam, puissance au poids, suite d'intégration, legacy supprimé — 239/239 tests. Le catalogue est stable : le gel des index FFI peut avoir lieu. Le contrat BattleState de Taj est à régénérer au gel : +6 champs POD de la phase 14, `wish[2]`, et `legendary`/`mega` côté Species)*

---

## Vision

Moteur de combat Pokémon tour-par-tour, formats **3v3 et 6v6**, **embarqué dans le backend Rust via FFI** (bindings type `cxx`). Pas de communication réseau entre Rust et le moteur : Rust détient le `BattleState`, appelle `resolveTurn()` directement sur l'objet C++ partagé en mémoire.

Contenu final : **49 espèces** (roster équipe du 2026-07-15, méta « chill compétitif »), sets **entièrement fixes** — chaque Pokémon vient avec ses 4 attaques, son talent, son objet, sa nature et ses EVs verrouillés. Le joueur compose son équipe, il ne configure pas ses Pokémon. Noms affichés en **français**.

**Principe directeur** : data-driven. Ajouter un Pokémon, une attaque ou un objet = ajouter un fichier JSON, jamais modifier le moteur. Le moteur ne connaît que des *effets* atomiques composables ; les attaques sont des listes d'effets décrites en JSON.

### Contrainte non-négociable

À chaque phase, le projet **compile, tourne et passe tous ses tests**. Pas de phase "moitié faite". On ajoute une couche, on la teste, on la merge, on passe à la suivante.

---

## Architecture (résumé)

```
┌─────────────────────────────────────────────────────────────┐
│                       BattleEngine                          │
│  • resolveTurn(state, action_p1, action_p2) -> EventLog     │
│  • orchestration : ordre, dégâts, effets, fin de tour       │
└──────────────┬──────────────────────────────────────────────┘
               │ utilise
               ▼
┌─────────────────────────────────────────────────────────────┐
│                       BattleState (POD pur)                 │
│  • teams[2][6], active[2], weather, terrain, screens,       │
│    hazards, turn                                            │
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
│              Abilities + Items (hooks, même pattern)        │
│  • on_switch_in, on_switch_out, on_before_move,             │
│    on_modify_damage, on_after_ko, on_residual, ...          │
│  • registry par string → singleton const stateless          │
│  • tout état par-combat vit dans le POD                     │
└─────────────────────────────────────────────────────────────┘
               ▲
               │ chargés par
               ▼
┌─────────────────────────────────────────────────────────────┐
│                       DataLoader                            │
│  • lit data/ au démarrage : types, moves, pokemon           │
│  • valide la cohérence (moveset, talents/objets existent)   │
└─────────────────────────────────────────────────────────────┘
```

### Règles d'architecture

1. **`BattleState` est un POD.** Aucune méthode autre que getters. Layout C-compatible pour FFI.
2. **Le moteur est stateless.** `resolveTurn` est une fonction pure (modulo RNG). Aucun match n'est stocké côté C++.
3. **Le backend Rust détient les `BattleState`.** Il les crée, les passe par référence au moteur qui les modifie en place, les sauvegarde en DB.
4. **Le RNG est injecté.** Jamais de `rand()` global. Tests = RNG déterministe.
5. **Le moteur ne connaît aucune attaque par son nom.** Il connaît des effets. *Unique exception : Lutte (Struggle), qui est une règle du jeu et non du contenu (ADR #35).*
6. **Les talents et objets sont des listeners sur des événements du moteur.** Pas de switch géant. Singletons const stateless ; l'état par-combat vit dans le POD.
7. **Toute donnée de jeu vit en JSON.** Code = règles génériques ; JSON = contenu. Le moteur charge ses JSON au démarrage (côté C++, transparent pour Rust). Les tables de règles (natures, multiplicateurs de stages) vivent en code.
8. **Arborescence en domaines, miroir strict include/src** (ADR #46) : `model/` (vocabulaire du jeu), `core/` (orchestration et état), `effects/`, `abilities/`, `items/` — les deux dernières sont des dossiers pour absorber l'éclatement en fichiers par familles des phases 13-14. La démo vit dans `demo/`, hors des sources de la lib. Les tests sont nommés par domaine (`test_items.cpp`), jamais par phase.
9. **IDs internes ASCII, affichage français.** Fichiers et IDs en PascalCase français sans accent ni séparateur (`OmbrePortee.json`) ; le français correct et accentué vit dans `displayName` (« Ombre Portée »). Le moteur, le FFI et l'IA ne manipulent que les IDs (ADR #40).

---

## Acquis — Phases 0 à 9 ✅ (mergées, 136/136 tests au 2026-07-09)

Le socle est complet et testé. Détail historique dans les ADRs #1-31 et l'historique git de `dev`. Capacités en place :

- **Fondations** : type chart 18×18, calcul de stats, catalogues indexés moves/espèces (lookup string → int au chargement seulement), `BattleState` POD strict prêt FFI, RNG injecté (Mersenne Twister + `FixedRNG` de test), `validateState()` défensive avec messages descriptifs (ADR #13), events exhaustifs pour le replay frontend.
- **Tour de jeu** : ordre priorité puis vitesse, speed ties aléatoires partout (ADR #31), switch avant attaques, remplacement post-KO hors tour via `resolveReplacement` (ADR #19), pivots U-Turn/Volt Switch avec cible déclarée (ADR #20), chain-stop des effets sur immunité/échec (ADR #24).
- **Statuts** : les 6 statuts canon gen 6+/Showdown, immunités de type, Clause Sleep avec exemption Rest (ADR #28), résiduels dans l'ordre de vitesse effective.
- **Stats de combat** : 7 stages [-6, +6], multiplicateurs canon, Acc/Eva branchés sur la précision avec leur propre table (ADR #18).
- **Field** : météo 5 tours (pluie/soleil/sable/grêle — grêle → neige en phase 12), hazards en champs fixes POD (ADR #23) avec application canon à l'entrée.
- **Talents** : registry de listeners stateless (ADR #9), 12 talents câblés (Intimidation, pinchs, Lévitation, Guts, ThickFat, Static, RoughSkin, SandStream, Drizzle...).
- **Moves avancés** : secondaires probabilistes en wrapper `chance` (ADR #26), crits Showdown (ADR #27), recul, recovery, flinch, Protect data-driven avec chaîne 1/3^n (ADR #30), moves deux-tours à continuation forcée (ADR #29), phazing aléatoire, Roost.

---

## Refonte « roster final » — contexte (2026-07-15)

L'équipe a livré le roster complet : **49 Pokémon avec objets tenus** — alors que le design initial excluait les objets. Décisions prises le 2026-07-15 (ADR #32-44), impact :

- Les objets, les Méga (statiques), les natures/EVs, les PP, la neige gen 9, le terrain Électrique et Voile Aurore entrent au scope.
- Le contenu de test des phases 0-9 (12 espèces, 45 moves) sera **remplacé** par le roster final en phase 14 ; les tests seront réécrits sur le contenu final.
- **Le FFI (ex-phase 10) devient la phase 15** : les index du catalogue gèlent au moment du FFI, donc tout le contenu doit être posé avant. Taj prévenu.
- Découpage : chaque phase reste mergeable avec tous ses tests verts ; l'ancien contenu n'est supprimé qu'en phase 14, quand le nouveau le remplace.

**En attente côté équipe** (bloque uniquement la saisie de données en phase 14, pas les phases 10-13) :
- ⬜ Doc complémentaire : PP, précision et poids par attaque/Pokémon (annoncé pour le 2026-07-15 au soir)
- ⬜ Liste officielle des **légendaires** (mythiques — Marshadow, Zarude, Hoopa — inclus ou non ?) pour la règle « 1 légendaire par équipe »
- ⬜ Correction des archétypes météo orphelins : Minotaupe (Baigne Sable) n'a **aucun poseur de sable** dans le roster, Méga-Laggron (Glissade) n'a **aucun poseur de pluie**. En attendant : attaque placeholder au choix d'Alex sur ces slots.
- ⬜ **Vérifier les noms de talents du doc équipe contre le canon** : plusieurs appellations utilisées dans nos échanges n'étaient pas canon (voir ADR #47) — à la saisie phase 14, les JSON référenceront les IDs du registry (`ChasseNeige`, `Tension`, `Impudence`, `Acharne`, `Benet`, `Incisif`, `Coloforce`, `CreaElec`, `SouffleDelta`, `UrneDuFleau`)
- ⬜ **Vérifier les natures « Pudique » du roster** : en canon, Pudique (Bashful) est une nature *neutre* — l'équivalent français de Bold (+Déf/−Atk) est **Assuré**. La table du moteur est canon ; si l'équipe voulait +Déf/−Atk, corriger les JSON concernés en phase 14.

---

## État d'avancement

Légende : ✅ fait, testé, mergé · 🚧 en cours · ⬜ pas commencé · 🔵 bloqué

### Phase 10 — Refonte stats + PP + Lutte + 6v6  ✅

Branche : `feat/battle-engine-stats-overhaul`. Design validé avec Alex le 2026-07-15.

**Stats canon niveau 100 (ADR #33) :**
- ✅ `computeStats(base, level, nature, evs)` : IV = 31 en constante, PV = ⌊(2×base + 31 + ⌊EV/4⌋)×N/100⌋ + N + 10, autres = (⌊(2×base + 31 + ⌊EV/4⌋)×N/100⌋ + 5) × nature
- ✅ Table des 25 natures **en code** (règle du jeu, pas du contenu), clés françaises. Utilisées par le roster : Jovial (+Vit/−AtqSpé), Rigide (+Atk/−AtqSpé), Timide (+Vit/−Atk), Modeste (+AtqSpé/−Atk), Pudique (+Déf/−Atk), Prudent (+DéfSpé/−AtqSpé), Calme (+DéfSpé/−Atk)
- ✅ JSON espèce : champs `nature` (string FR) et `evs` (objet à 6 clés, absentes = 0) ; validation ΣEV ≤ 510, EV ≤ 252 par stat
- ✅ Champ `poids` (kg) dans le JSON espèce — consommé par Balayette/Nœud Herbe en phase 14

**PP (ADR #35) :**
- ✅ Champ `pp` dans le JSON des moves ; `std::array<int, 4> pp` dans `BattlePokemon` (POD conservé), initialisé au chargement
- ✅ Décrément quand l'attaque **s'exécute** : pas de consommation si le tour est sauté (para/sommeil/gel/flinch), consommation même si l'attaque rate ou échoue ensuite ; moves deux-tours : décrément au tour de charge uniquement
- ✅ Hook prêt pour Pression (+1 PP quand on cible son porteur — branché en phase 13)
- ✅ `validateState` : `0 ≤ pp[i] ≤ pp_max` du move référencé, `0` pour les slots vides

**Lutte (ADR #35) :**
- ✅ Move hardcodé dans le moteur (exception assumée à la règle 5) : substitué à tout `UseMove` quand aucun slot n'a de PP utilisable (verrou Choix inclus dès la phase 11)
- ✅ Canon : physique, 50 de puissance, **sans type** (efficacité ×1 contre tout, Spectre compris, jamais de STAB), ne rate jamais, ne consomme aucun PP, recul de 25% des PV max si elle touche

**6v6 (ADR #36) :**
- ✅ `kTeamSize` 3 → 6 ; `team_size` couvre déjà le 3v3 (slots vides à `kNoSpecies`)
- ✅ Invariants et tests de bornes suivent mécaniquement

**Recalibration :**
- ✅ Les 136 tests existants recalibrés (anciennes espèces : nature Sérieux, EV 0, niveau 100) — un seul avait besoin d'une vraie correction : le test d'évasion enchaînait 100 tours de Growl (40 PP), Lutte never-miss prenait le relais au 41e et stoppait les miss ; les PP sont maintenant restaurés à chaque itération
- ✅ +10 tests dédiés dans `test_phase10.cpp` (facturation PP à l'exécution / tour sauté gratuit / raté et échec payants / charge seule payante, substitution Lutte + sans-type prouvé contre Spectre + recul 25% PV max, slot à 0 PP rejeté par `checkAction`, invariants PP de `validateState`, 6v6 complet, stats depuis nature/EVs) — **150/150**
- ✅ ADRs #32-44 consignés dans ce fichier (fait à la rédaction, à cocher au merge)

### Phase 11 — Objets tenus  ✅

Branche : `feat/battle-engine-items`. Architecture calquée sur les talents (ADR #34) : interface `Item` à hooks virtuels no-op, singletons const stateless, registry `itemByName`, état par-combat dans le POD.

- ✅ Interface `Item` + registry ; champ `objet` dans le JSON espèce (lock par espèce, pas de choix joueur) ; les Méga n'ont pas d'objet (champ absent)
- ✅ POD : `item_id` (`kNoItem = -1`), `item_consumed` (0/1), `locked_move_id` (`kNoMove` = pas de verrou) dans `BattlePokemon`
- ✅ **Les 12 objets du roster** :
  - `OrbeVie` — dégâts ×1,3, recul 10% PV max après une attaque qui touche
  - `Restes` — soin 1/16 en fin de tour
  - `Detritus` — soin 1/16 si type Poison, dégâts 1/8 sinon, fin de tour
  - `OrbeFlamme` — brûle son porteur en fin de tour (synergie Cran)
  - `BaieSitrus` — consommée à ≤ 50% PV, soin 25% PV max
  - `CeintureForce` — consommée : survit à 1 PV à un coup qui K.O. depuis PV pleins
  - `BandeauChoix` / `LunettesChoix` / `MouchoirChoix` — Atk / AtqSpé / Vit ×1,5, verrou sur la première attaque utilisée jusqu'au switch
  - `GrossesBottes` — immunité totale aux hazards à l'entrée
  - `DesPipes` — les multi-coups tapent 4-5 fois (plancher, branché en phase 14 avec les multi-hits)
  - `Massue` — Atk ×2 (le lock par espèce règle la restriction canon à Ossatueur)
  - `Lumargile` — Voile Aurore dure 8 tours au lieu de 5 (branché en phase 12)
- ✅ Interactions inverses posées comme hooks : retrait d'objet (pour Sabotage), présence d'objet requise (pour Poltergeist), blocage de baie (pour Cœur de Coq) — les moves/talents consommateurs arrivent en phases 13-14
- ✅ Verrou Choix : reset au switch ; un verrou sur un slot à 0 PP force Lutte
- ✅ Persistance au switch : `item_consumed` et objet retiré persistent (une Ceinture utilisée ne revient pas) ; `locked_move_id` reset
- ✅ `validateState` : `item_id` valide-ou-`kNoItem`, `locked_move_id` cohérent avec les slots, flags 0/1
- ✅ Events : `ItemTriggered`, `ItemConsumed`, `ItemKnockedOff` (posé pour Sabotage), plus `ItemDamage` (Orbe Vie, Détritus)
- ✅ Ordre canon vérifié par test d'ordre d'events : météo → soins d'objets → résiduels de statut → orbes (les Restes soignent avant le tick de poison ; la brûlure d'Orbe Flamme ne tape qu'au tour suivant)
- ✅ Baie Sitrus branchée sur *tous* les points d'application de dégâts (attaque, hazards à l'entrée, chip météo, résiduels, recul, Lutte)
- ✅ +17 tests dans `test_phase11.cpp`, dont l'intégration signature **Cran + Orbe Flamme** (le combo Bétochef, testé sur Machamp) — **167/167**

### Phase 12 — Field : neige gen 9, terrain Électrique, Voile Aurore  ✅

Branche : `feat/battle-engine-field`.

- ✅ **Grêle → Neige** (ADR #37) : même slot dans l'enum `Weather`, règles gen 9 — pas de chip, Déf ×1,5 pour les types Glace, Blizzard 100% de précision sous neige ; Alerte Neige la posera en phase 13, Baigne Neige inchangé dans son principe
- ✅ **Terrain Électrique** (ADR #38) : `terrain`/`terrain_turns_left` dans `BattleState`, façon météo, 5 tours ; Électrik ×1,3 pour les attaquants **au sol**, immunité sommeil pour les Pokémon au sol ; pas de généralisation aux autres terrains
- ✅ Notion de **grounded** partagée (déjà implicite pour les hazards/TSpikes) : extraite en helper unique, consommée par terrain, Spikes, TSpikes, et Lévitation
- ✅ **Voile Aurore** (ADR #39) : état d'écran **par camp** dans `BattleState` (`aurora_veil_turns[2]`), dégâts physiques ET spéciaux ×0,5 sur le camp protégé, 5 tours (8 avec Lumargile), activable uniquement sous neige, pas de stacking ; les crits l'ignorent (canon)
- ✅ `validateState` : bornes terrain/écrans, cohérence compteurs/état
- ✅ Events : `TerrainStarted` (posé pour la phase 13), `TerrainEnded`, `ScreenStarted`, `ScreenEnded`
- ✅ Précision météo-dépendante **générique** : champ JSON `accuracyInWeather` (`{"Snow": 0}` = infaillible sous neige) — Tonnerre/Vent Violent en phase 14 seront du pur JSON
- ✅ Repos échoue pour un utilisateur au sol sous terrain Électrique (canon) ; Lumargile devient un vrai objet (hook `screenDuration`, 5 → 8 tours)
- ✅ Fixtures : `Blizzard.json`, `VoileAurore.json` et l'espèce `Mammochon.json` (Glace/Sol, Isograisse) créés **directement sous leur nom français final** (ADR #40) — premiers fichiers du roster définitif ; catalogue de test : 13 espèces, 47 moves
- ✅ +12 tests dans `test_field.cpp` (premier fichier de la convention post-refactor) — **179/179**

### Phase 13 — Talents du roster  ✅

Branche : `feat/battle-engine-abilities`. **33 nouveaux talents** (45 au total), éclatés en 5 familles (`damage_mods`, `immunities`, `weather_abilities`, `switch_hooks`, `triggers`), chaque famille exposant sa fonction d'enregistrement au registry central. Noms **canon français ASCII** (ADR #40/#47) — les 12 talents legacy restent en anglais jusqu'à la bascule phase 14.

- ✅ **Nouveaux hooks d'interface** (défauts neutres) : `onSwitchOut`, `onAfterKO`, `onStatLoweredByOpponent`, `priorityBoost`, `bouncesStatusMoves`, `onHalfHpCrossed` (partagé avec la fenêtre Sitrus via `abilityHpCheck`, appelé à chaque site d'application de dégâts avec détection de franchissement), `opposingSpAMultiplier`, `pressuresPP`, `blocksOpposingBerries`, `statMultiplier`, `speedMultiplier`, `stabMultiplier`, `onMoveAbsorbed`, `blocksRecoil`, `blocksStatus`, `ignoresStages`, `hasDisguise`, `blocksStatDrop`, `onAfterDamagingMove`
- ✅ **damage_mods** : Coloforce (Atk ×2), Technicien (≤60 BP ×1,5), Adaptabilité (STAB ×2), Griffe Dure (contact ×1,3), Poing de Fer (flag `punch`, testé en phase 14 avec les vrais moves), Incisif (flag `slicing`, testé sur Air Slash), Benêt (stages adverses ignorés dans les deux sens), Urne du Fléau (AtqSpé adverse ×0,75)
- ✅ **immunities** : Absorbe-Volt (+25 % PV), Paratonnerre (+1 AtqSpé), Torche (immunité Feu + flag POD `flash_fire_active`, boost ×1,5, éteint au switch), Pare-Balles (flag `bulletproof`), Corps Sain
- ✅ **weather_abilities** : Alerte Neige, Créa-Élec (pose le terrain, event `TerrainStarted` enfin émis), **Souffle Delta** (ADR #47 : météo liée à la présence, `weather_turns_left = 0`, exemptée de l'upkeep, non-remplaçable, dissipée quand le poseur quitte le terrain K.O. compris, faiblesses de la composante Vol neutralisées), Glissade / Baigne Sable / Chasse-Neige (Vit ×2), Feuille Garde
- ✅ **switch_hooks** : Régé-Force (+1/3), Médic Nature, Repli Tactique (ADR #47 : auto-switch vers le premier remplaçant valide — divergence canon assumée, avec garde anti-réentrée dans `performSwitch`)
- ✅ **triggers** : Impudence, Acharné (+2 Atk via le chemin centralisé `applyOpposingStatDrop`, partagé avec Intimidation et Corps Sain), Colérique (coups directs seulement), Fantômasque (gen 8 : casse + 1/8 PV, flag POD persistant), Pression (branché sur la facturation PP), Tension, Magicien (vol d'objet), Farceur, **Miroir Magik** (flag data `reflectable` façon Showdown, ré-exécution rôles inversés, un Piège de Roc renvoyé se pose chez l'envoyeur), Tête de Roc (le recul de Lutte reste dû)
- ✅ Zoroark d'Hisui : champ `talent` vide toléré (ADR #43)
- ✅ Tests : +25 cas dans `test_abilities.cpp` via un helper d'override test-only (`overrideAbility`, const_cast confiné aux tests) ; 32/33 talents couverts par assertions directes (les cas groupés partagent leur setup ; Glissade/Baigne Sable/Chasse-Neige testés par instance pour garder le câblage nom → météo, pas seulement la classe partagée) ; seul Poing de Fer attend son premier move `punch` en phase 14 — **204/204**

### Phase 14 — Bascule anglaise + mécaniques d'attaques  ✅

Branche : `feat/battle-engine-mechanics`. Découpage 14/15 validé : les mécaniques d'abord (testées sur les fixtures), les ~190 fichiers de données ensuite.

**Bascule anglaise (ADR #48)** : ✅ talents (33), objets (13, indices gelés inchangés), natures (25, table canon — le « Pudique » du doc équipe = Bold), Struggle, clés de schéma (`weight`, `item`), fichiers renommés (`AuroraVeil.json`, `Mamoswine.json`). Zéro identifiant français survivant, vérifié par grep.

**Mécaniques livrées** (tout data-driven, `src/effects/move_mechanics.cpp`) :
- ✅ Multi-hit : `MultiHit` (2-5 canon 35/35/15/15, plancher 4 avec Dés Pipés, comptes fixes garantis par les Dés — règle équipe), Prolifération 10 coups avec jet par coup, Triple Axel 20/40/60, Draco-Flèches ×2 ; **Fantômasque n'absorbe qu'un seul coup d'une rafale** (canon gen 8, `multiHitIndex`)
- ✅ Stats croisées : champs `offenseStat`/`defenseStat`/`useTargetOffense` (Éclat de Corps, Psyko-Choc — les boosts météo Déf/DéfSpé sont désormais clés sur la stat de défense *résolue* —, Tricherie avec l'Atk + stages + objets de la cible)
- ✅ Dégâts fixes : `FixedDamage` niveau (Frappe Atlas, immunités du chart et Ceinture respectées, Destinée déclenchée) et moitié des PV courants (Fléau)
- ✅ Conditionnelles : `firstTurnOnly` + compteur POD `turns_on_field` (Bluff, Escarmouche), `failsIfTargetNotAttacking` via `executeAction(otherAction, targetAlreadyActed)` (Coup Bas), `requiresTargetItem` (Poltergeist)
- ✅ Sabotage : flag `boostedByTargetItem` (×1,5) + effet `KnockOff` (retrait définitif, le verrou Choix meurt avec)
- ✅ Drains (`Drain` : Vampipoing, Lame Funeste), Larcin Spectral (`StealBoosts` avant les dégâts), Taillade Continue (`HazardOnHit`), Cognobidon (`BellyDrum`, combo Sitrus testé sur PV pairs), Grand Nettoyage (ClearHazards deux camps + Atk/Vit +1)
- ✅ État différé : Vœu (`wish_turns[2]`/`wish_heal[2]` dans le BattleState, résolu après le terrain avant les soins d'objets), Destinée (`destiny_bond_active` + `last_move_id`, chain-fail canon gen 7+, entraîne le tueur sur dégâts directs et dégâts fixes)
- ✅ Blabla Dodo : flag `usableWhileAsleep` (le compteur de sommeil tique), appelle un autre move du set (sans précision, deux-tours exclus)
- ✅ Blockhaus : `Protect` paramétré `contactStatus` (empoisonne les attaquants au contact, immunités respectées)
- ✅ Revenant : `twoTurn: "disappear"` (état d'invulnérabilité 3, intouchable), release à travers Abri
- ✅ Précision : Tonnerre/Vent Violent en pur JSON `accuracyInWeather` (posé en phase 12), Vent Violent `hitsFly`, Toxik `alwaysHitsIfUserType: "Poison"`, Ébullition/Boutefeu `thawsUser` (dégèle l'utilisateur ET la cible)
- ✅ Malédiction : pur data (composite StatChange), Téléport : pur data (Pivot, priorité −6)
- ✅ **30 fichiers de moves finals créés** (fichiers du roster, testent enfin Poing de Fer et Dés Pipés) — catalogue de test : 77 moves
- ✅ +24 tests dans `test_move_mechanics.cpp` — **227/227**
- ⚠️ Divergence assumée : la **confusion** (secondaire de Vent Violent) n'est pas implémentée — seul move concerné du roster, à arbitrer avec l'équipe (l'ajouter = un volatile + un jet par action)

**Puissance au poids** (Balayette, Nœud Herbe) : reportée en phase 15 avec les données (paliers canon sur `weight` — champ déjà chargé).

### Phase 15 — Contenu du roster, catalogue livrable, validateTeam  ✅

Branche : `feat/battle-engine-roster-content`. **239 tests / 3204 assertions.**

**Données générées, pas saisies** : script de génération one-shot depuis le doc équipe, avec **vérification croisée des 294 stats** (chaque valeur du doc doit égaler notre formule — zéro écart) et échec bruyant sur tout pattern d'effet non couvert. Les 58 fichiers de moves préexistants ont été réconciliés avec l'index du doc : 5 divergences corrigées (Anti-Brume / Picots Toxik / Piège de Roc en précision 0, Tour Rapide 20→50, clé parasite sur Revenant).
- ✅ **49 espèces** (stats, EVs, natures, poids, talent, objet, moveset fixe) et **95 moves** — le catalogue livré est *exactement* le doc équipe
- ✅ Puissance au poids : paliers canon 20/40/60/80/100/120 sur `weight` (Balayette, Nœud Herbe), bornes testées au kg près
- ✅ Champs `legendary` et **`mega`** dans `Species` — le Méga est une **donnée**, pas une devinette sur le préfixe de l'id
- ✅ **`validateTeam`** : Species Clause, max 1 Méga, max 1 légendaire, et chaque attaque tirée du movepool de son espèce (ADR #50)
- ✅ **Suppression du legacy** : 12 espèces de test, les 49 du roster deviennent les fixtures ; les 18 instruments neutres des tests (Tackle, Growl, Abri…) sortent du catalogue livré (ADR #49)
- ✅ **Suite d'intégration** sur les objets réels (`buildLoadout`) : Cran+Orbe Flamme, Cognobidon+Baie Sitrus, Prolifération+Dés Pipés, Fantômasque vs rafale, Tête de Roc+Fracass'Tête, Alerte Neige+Voile Aurore+Chasse-Neige
- ✅ Démo rejouée sur le contenu réel (10 matchs, déterministe)

**Découvertes de la migration**, toutes conformes au canon et non des bugs : Mimiqui est **Spectre**, donc immunisé aux rafales Normal (le type prime sur le déguisement) ; Corviknight est **Vol**, donc immunisé aux Picots ; aucune espèce du roster n'est ×4 faible à Roche (le palier haut des tests est ×2) ; Mammochon et Dragapult tiennent un Bandeau Choix, ce qui verrouille les scripts de démo.

**En attente équipe** (n'a pas bloqué la livraison) :
- ⚠️ **Liste des légendaires** : tous les `legendary: false`. La règle « max 1 » est codée *et testée* sur des flags basculés — le jour où la liste arrive, c'est un flip de booléens en pur JSON, zéro code
- ⚠️ **Archétypes météo orphelins** : le roster ne contient **aucun move de météo** (ni Danse Pluie, ni Zénith, ni Tempête de Sable) et aucun talent poseur de sable ou de pluie. Baigne Sable (Minotaupe) et Glissade (M-Laggron) sont donc **inertes en partie réelle** ; la neige, elle, tourne (Alerte Neige de Feunard d'Alola). À trancher : ajouter un poseur, ou assumer deux talents morts
- ⚠️ Confusion (Vent Violent) toujours non implémentée (divergence de la phase 14)

### Phase 16a — Préparatifs FFI  ✅

Branche : `feat/battle-engine-ffi-prep`. **240 tests.** Tout ce qui devait être corrigé ou stabilisé *avant* d'exposer quoi que ce soit — aucune ligne de FFI, donc aucune décision d'architecture préemptée.

**Deux bugs latents, invisibles jusqu'ici parce que rien n'exerçait le chemin concerné :**
- ✅ `validateState` : suppression d'une seconde borne sur `invulnerable_state`, restée à `[0, 2]` depuis avant la phase 14. L'état `3` (Revenant, `TwoTurn::Disappear`) était donc **rejeté**. Latent parce que ni les tests ni la démo n'appellent `validateState` en cours de combat — le jour où Rust applique l'ADR #13 à la lettre, un Dragapult en plein Revenant faisait tomber la partie au tour suivant. Test de non-régression sur l'état 3 ajouté.
- ✅ `ItemDamageEvent` transportait trois espaces d'ids : un nom d'objet (Détritus, légitime), un nom de **talent** (Fantômasque) et un nom de **move** (Cognobidon). Incompatible avec un `name_id` typé par `kind` à la frontière. Fantômasque passe sur un `AbilityDamageEvent` dédié, Cognobidon sur `RecoilDamageEvent` (sans nom : le créneau du recul est exactement « un move coûte des PV à son utilisateur »). `ItemDamageEvent` ne porte plus que des objets.

**Registry de talents indexé (ADR #51) :**
- ✅ `AbilityMap` (`unordered_map`, sans ordre) → `AbilityTable` (`vector`, ordonné). Les cinq `registerX` font `push_back` ; l'ordre d'enregistrement **est** l'ordre des ids
- ✅ La map nom → index est *dérivée* de la table, jamais écrite à la main : un nom ne peut pas dériver de son index, et un doublon throw au lieu d'écraser silencieusement
- ✅ API alignée sur les objets : `abilityByIndex`, `findAbilityIdByName`, `abilityCount`, plus la façade `DataLoader::findAbilityId` / `isValidAbilityId` — les **quatre** catalogues (espèces, moves, objets, talents) sont désormais atteignables uniformément depuis le `DataLoader`
- ✅ `abilityByName` conserve son comportement exact (nullptr sur nom inconnu ou vide — Zoroark, ADR #43) : aucun appelant modifié
- ✅ Ordre gelé : `0-11` legacy · `12-19` damage_mods · `20-24` immunities · `25-31` weather · `32-34` switch_hooks · `35-44` triggers
- ✅ Test de gel (`[ffi]`) : count == 45, les 12 legacy nommément, la famille météo aux slots 27-30, round-trip id ↔ nom sur les 45, et les cas de miss

**CMake autonome (ADR #57) :**
- ✅ `BATTLE_ENGINE_BUILD_TESTS` / `BATTLE_ENGINE_BUILD_DEMO`, ON par défaut : le workflow local ne bouge pas, `build.rs` les met à OFF
- ✅ nlohmann/json en `find_package` avec FetchContent en repli ; Catch2 n'est plus téléchargé quand les tests sont OFF — **une image Docker n'a plus à joindre GitHub**
- ✅ `BATTLE_ENGINE_DATA_DIR` sort de `battle_engine_lib` (il n'était utilisé que par la démo et les tests) et va sur ces deux cibles via `BATTLE_ENGINE_DATA_PATH`
- ✅ `POSITION_INDEPENDENT_CODE ON` (sans quoi le link dans une cdylib Rust échoue) + règles `install` pour que `build.rs` récupère `.a` et headers depuis `OUT_DIR`

### Phase 16b — Exposition FFI  🔵 (bloquée sur D2 et D5, voir `FFI-CONTRACT.md`)

> Changement d'architecture validé avec l'équipe 2026-05-19 : pas de REST, le moteur est embarqué dans le backend Rust via FFI. Le service IA Python parle au backend Rust.

**Côté C++ (Alex) :**
- ⬜ `FfiAction` et `FfiEvent` plats + conversions (`EventLog` est un `vector<variant<35>>` avec des `std::string` : rien de C-compatible). **Le vrai gros morceau, absent de la roadmap jusqu'ici** — mutualisé avec le binding pybind11 du service IA
- ⬜ TU `src/ffi/ffi.cpp` : `engine_init(path)`, singletons `DataLoader`/`BattleEngine`, `validateState` en tête de chaque fonction exposée (ADR #13), try/catch en frontière
- ⬜ `makeCombatant(species_id, level)` exposé (Rust ne doit pas reconstruire un `BattlePokemon` à la main) + export du catalogue (`id`, `id_string`, `displayName`) pour le front et l'IA
- ⬜ `fasterSide(state, seed)` exposé — ordre des remplacements simultanés (D6)
- ⬜ Audit layout C-compatible de `BattleState` (PP, objets, terrain, écrans, `teams[2][6]`, `wish[2]`)
- ⬜ Tests C++ sur l'aller-retour event → `FfiEvent`

**Côté Rust (Taj) :**
- ⬜ `#[cxx::bridge]`, fonctions déclarées `Result<T>` pour que les exceptions deviennent des `Err`
- ⬜ `build.rs` + crate `cmake` (options tests/démo à OFF), link libstdc++/libc++
- ⬜ `data/` embarqué dans l'image Docker, chemin passé à `engine_init`
- ⬜ `serde` sur `BattleState` — définit *de facto* le schéma que le service IA Python lira aussi
- ⬜ Boucle de partie complète, phase de remplacement incluse
- ⬜ Tests d'intégration : rejouer un match scripté et comparer la séquence d'events à la démo C++ au même seed

**Décisions à trancher (détail, argumentaire et recommandations dans `FFI-CONTRACT.md`) :**
- ⬜ **D1** crate FFI · **D2** qui alloue le `BattleState` · **D3** obtention des index · **D4** intégration build
- ⬜ **D5** comment le RNG traverse la frontière — *absent de la liste initiale, bloquant*
- ⬜ **D6** ordre des remplacements simultanés · **D7** durée de vie du `DataLoader`
- ⬜ **D8** garantie forte sur `BattleState` : la couche FFI travaille sur une copie et ne commit qu'en sortie, sinon un throw en milieu de `resolveTurn` laisse l'état de Rust à moitié écrit — *découvert en cartographiant les sites de throw*
- ⬜ Convention d'erreurs : `cxx` ne transporte que `what()`, le **type** de l'exception est perdu. Préfixes stables (`E_STATE`, `E_TEAM`, `E_ACTION`, `E_DATA`) ou `FfiResult` typé ?

### Phase 17 — Polish  ⬜

- ⬜ Logging structuré, métriques (durée moyenne d'un tour)
- ⬜ Documentation Doxygen + spec écrite de l'API FFI

### Reporté explicitement (fin de projet, si le temps le permet)

- ⬜ **Illusion** (Zoroark-H) : c'est un problème de *protocole*, pas de moteur — le moteur devrait tracker « espèce affichée » et les events envoyés au front devraient mentir. À designer avec Taj si repris. En attendant Zoroark n'a pas de talent (ADR #43).
- ⬜ **Distorsion (Trick Room)** : aucun mon du roster ne porte l'attaque ; ajout purement additif plus tard (compteur POD + inversion du tri de vitesse). Rien à réserver maintenant.
- ⬜ Méga-évolution dynamique en combat : écartée (ADR #32), réévaluable seulement si le gameplay le réclame.
- ⬜ Mode « boss » M-Rayquaza : pour l'instant il compte comme un Méga normal dans la règle 1/équipe ; l'équipe réévaluera.

---

## Décisions d'architecture (ADR-light)

À mettre à jour si on revient sur une décision. ~~Barré~~ = remplacé (la ligne reste pour l'historique).

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
| 10 | **FFI Rust (au lieu de REST)** | pas de sérialisation JSON entre Rust et C++, intégration directe ; le backend Rust est le seul appelant du moteur (Python passe par Rust) | 2026-05-19 |
| 11 | **Rust détient le `BattleState`** (Option A) | struct de taille fixe, zéro allocation dynamique runtime, sauvegarde DB triviale côté Rust avec `serde`, debug simple, API FFI minimale — *taille passée de 3 à 6 par l'ADR #36* | 2026-05-19 |
| 12 | **Référencement par index numérique à la frontière FFI** (jamais par string) | `BattleState` 100% C-compatible, aucune allocation à l'interface, lookup O(1), pas de typo silencieuse ; les noms restent côté C++ dans le `DataLoader` (`unordered_map<string, int>` au chargement) | 2026-05-19 |
| 13 | **Politique d'erreurs FFI : validation défensive + conversion en `Result`** | aucune exception C++ ne traverse le FFI (UB sinon) ; chaque fonction exposée valide ses entrées au début, throw descriptif, attrapé et converti en `Result::Err` côté Rust via `cxx` ; en interne le moteur throw librement | 2026-05-19 |
| ~~14~~ | ~~Niveau fixe à 50~~ | remplacé par ADR #33 (niveau 100) | 2026-05-24 |
| ~~15~~ | ~~Pas d'IVs ni d'EVs~~ | remplacé par ADR #33 (IV31/EV/natures) | 2026-05-24 |
| ~~16~~ | ~~Roster de test 8 Pokémon distinct du roster final~~ | rempli son office phases 0-9 ; remplacé par le roster final en phase 14 (ADR #44) | 2026-05-24 |
| 17 | **Statuts : mécanique canon gen 6+/Showdown dès la phase 2** | immunités de type et de table incluses tout de suite ; paralysie ÷2 vitesse dans `computeOrder` ; dégel 20%/tour + move Feu qui touche ; sommeil roulé 1-3 tours ; probas via `RNG::chance(float)` pour que `FixedRNG` force/bloque les procs sans toucher la précision ; résiduels dans l'ordre de vitesse effective | 2026-06-12 |
| 18 | **Stages Acc/Eva stockés dès la phase 3, branchés en phase 8** | les 7 stages vivent dans `stat_stages` (ordre stable `StatIndex`) ; précision = calcul fin avec sa propre table `(3+n)/3`, stage combiné clampé | 2026-06-13 |
| 19 | **Remplacement après KO façon Showdown : `resolveReplacement` dédiée** | switch gratuit hors tour ; le moteur reste stateless : `resolveTurn` throw si un actif est K.O., le caller Rust demande au joueur puis appelle `resolveReplacement` ; pas d'auto-switch (choix stratégique, crucial pour l'IA) | 2026-07-05 |
| 20 | **Cible du pivot déclarée dans `UseMove.pivotTarget`** (-1 = auto) | garde la sémantique mid-turn canon en un seul appel stateless ; `PivotEffect` re-valide au moment du switch (fallback auto si K.O. entre-temps) ; POD conservé | 2026-07-05 |
| ~~21~~ | ~~Gyarados + stubs de talents inertes au roster de test~~ | obsolète avec l'ADR #44 (roster final) | 2026-07-05 |
| 22 | **Météo : durée fixe 5 tours, fin avant le chip du dernier tour** | timing Showdown : 4 ticks puis « subsides » en fin de 5e tour ; compteur dans le POD — *Roche Lisse absente du roster d'objets, la durée fixe tient toujours* | 2026-07-05 |
| 23 | **Hazards en struct POD à champs fixes (pas de map)** | `BattleState` traverse le FFI : 3 ints par camp suffisent, layout C trivial | 2026-07-05 |
| 24 | **`EffectContext.moveFailed` : le chain-stop des effets** | une immunité (0x), une cible K.O. ou un set raté posent le flag et coupent la chaîne ; corrige Volt Switch vs Sol, rend Rapid Spin bloquable par les Spectres | 2026-07-05 |
| ~~25~~ | ~~+Tyranitar, +Politoed, +Scizor au roster de test~~ | obsolète avec l'ADR #44 (roster final) | 2026-07-05 |
| 26 | **Effets secondaires = wrapper `SecondaryEffect` piloté par `"chance"`** | wrapper générique enrobe n'importe quel effet ; roulé via `RNG::chance(float)` ; composable avec tout effet futur | 2026-07-09 |
| 27 | **Crits : taux Showdown (1/24 base, 1/8 high-crit), ×1.5, ignorent les stages défavorables à l'attaquant** | aligné Showdown ; flag `highCrit` data-driven | 2026-07-09 |
| 28 | **Rest : champ POD `sleep_self_inflicted` exempté de la Clause Sleep** | le sommeil de Rest coexiste avec un dormeur adverse (canon) ; purgé au réveil et à tout nouveau statut ; validé | 2026-07-09 |
| 29 | **Moves multi-tours : continuation forcée par le moteur, flags data-driven** | pendant la charge le moteur **ignore** l'action fournie et rejoue le move — plus simple pour Rust et l'IA qu'un throw ; invulnérabilité et cas spéciaux pilotés par flags JSON (`twoTurn`, `solarCharge`, `hitsDig`) | 2026-07-09 |
| 30 | **Protect : chaîne 1/3^n + blocage data-driven `blockedByProtect`** | compteur `protect_chain` POD, reset en fin de tour sans Protect réussi ; blocage par flag (défaut : dégâts + status offensifs ; `selfOrField` exempte) plutôt qu'une heuristique fragile | 2026-07-09 |
| 31 | **Speed ties aléatoires partout ; `startBattle`/`computeOrder` prennent un `RNG&`** | `fasterSide(state, rng)` tranche par `rng.chance(0.5f)` : ordre du tour, résiduels, chips météo, `on_switch_in` simultanés | 2026-07-09 |
| 32 | **Méga = espèces statiques** (forme méga dès le tour 1, pas de transformation en combat) | zéro nouveau système (stats/types/talent en JSON), cohérent avec « pas d'objet sur les Méga » et la sélection d'équipe ; règle 1 Méga/équipe côté validation ; M-Ectoplasma passe en Lévitation (décision équipe, anti-trapping) ; M-Rayquaza compte comme un Méga | 2026-07-15 |
| 33 | **IV31 / EVs / natures, niveau 100** | l'équilibrage du roster repose sur les spreads 252/252 et les natures (qui outspeed qui) ; niveau 100 = standard Showdown singles, cohérent avec « Frappe Atlas inflige 100 PV » du doc équipe ; IV = 31 en constante (pas en JSON) ; table des 25 natures en code, clés FR ; formule canon complète dans `computeStats` | 2026-07-15 |
| 34 | **Objets tenus, lockés par espèce, architecture registry façon talents** | pas de choix joueur : le champ `objet` vit dans le JSON espèce ; `Item` = singletons const stateless + hooks, état par-combat dans le POD (`item_id`, `item_consumed`, `locked_move_id`) ; le lock par espèce règle gratuitement les restrictions canon (Massue) | 2026-07-15 |
| 35 | **PP + Lutte hardcodée** | `pp[4]` dans le POD, décrément à l'exécution (pas si tour sauté ; oui si raté/échoué ; charge des deux-tours seulement) ; Lutte est une *règle de repli obligatoire*, pas du contenu → en dur dans le moteur (unique exception à la règle d'archi 5) : sans type, 50 BP, never-miss, 0 PP, recul 25% PV max si touche | 2026-07-15 |
| 36 | **`kTeamSize` = 6 (formats 3v3 et 6v6)** | `team_size[side]` gère déjà le remplissage partiel donc le 3v3 est gratuit ; à faire avant le FFI (layout mémoire) | 2026-07-15 |
| 37 | **Neige gen 9 remplace la grêle** (même slot météo) | le roster est pensé gen 9 (Voile Aurore, Déf Glace ×1,5, pas de chip, Blizzard 100%) ; une seule variante de météo glacée, pas les deux | 2026-07-15 |
| 38 | **Terrain Électrique seul, implémenté façon météo** | un seul terrain dans tout le roster (Créateur Électrik) : enum + compteur POD, ×1,3 Électrik et anti-sommeil pour les Pokémon au sol ; pas de généralisation spéculative aux 4 terrains ; helper `grounded` unifié | 2026-07-15 |
| 39 | **Voile Aurore = seul écran** | personne n'a Protection/Mur Lumière ; état par camp dans le POD, phys+spé ×0,5, 5 tours (8 avec Lumargile), sous neige uniquement, ignoré par les crits | 2026-07-15 |
| 40 | **Nommage : IDs/fichiers PascalCase FR sans accent ni séparateur, `displayName` FR accentué** | tout le jeu est en français (décision équipe) mais accents/tirets/apostrophes/espaces dans des noms de fichiers et des strings FFI = champ de mines (leçon du crash « Duplicate move name ») ; `OmbrePortee.json` / « Ombre Portée » ; espèces désormais aussi en PascalCase (`Mimiqui.json`) | 2026-07-15 |
| 41 | **Sets et talents fixes : moveset = exactement les 4 attaques du roster** | un talent canonique par espèce, pas de variante ; simplifie le teambuilder, le schéma (`Species.ability` inchangé) et l'IA (scoring = pur problème de composition) | 2026-07-15 |
| 42 | **Règles d'équipe : Species Clause + max 1 Méga + max 1 légendaire** | dans `validateTeam` (moteur), exposée au FFI pour le teambuilder Rust ; flag `legendaire` en JSON, liste officielle à fournir par l'équipe | 2026-07-15 |
| 43 | **Zoroark d'Hisui sans talent ; Illusion et Distorsion reportées en fin de projet** | Illusion = problème de protocole (events qui mentent au front), complexe pour peu ; Distorsion : aucun porteur dans le roster, ajout additif trivial plus tard ; validation « tout talent référencé existe » assouplie pour tolérer le champ vide | 2026-07-15 |
| 44 | **Le roster final (49 espèces) remplace le contenu de test en phase 14** | tests réécrits de zéro sur le contenu final (décision Alex) ; anciennes espèces/moves supprimés au même moment ; d'ici là les tests existants sont recalibrés phase par phase pour garder la règle « tout vert à chaque merge » | 2026-07-15 |
| 45 | **Catalogue d'objets indexé en code, ordre d'enregistrement gelé** | contrairement aux talents (impliqués par l'espèce), `item_id` vit dans le POD et traversera le FFI : il faut des indices stables ; les objets sont des singletons enregistrés dans un ordre fixe (append-only) dans `item.cpp`, le `DataLoader` expose `findItemId`/`isValidItemId` comme façade, cohérent ADR #12 ; ajouter un objet = une classe + une ligne d'enregistrement | 2026-07-16 |
| 46 | **Arborescence par domaines : `model/` `core/` `effects/` `abilities/` `items/`, miroir include/src, démo dans `demo/`, tests nommés par domaine** | 24 headers à plat devenaient illisibles et les phases 13-14 vont tripler le volume (47 talents, ~110 moves) ; réorganiser avant l'afflux plutôt qu'après ; refactor pur, zéro logique, 167/167 comme preuve | 2026-07-16 |
| 47 | **Phase 13 : Souffle Delta lié à la présence, Repli Tactique en auto-switch, Fantômasque gen 8, noms de talents canon FR** | Souffle Delta : pas de compteur (`turns_left = 0`, exception `validateState`), setters normaux en échec, dissipation au départ du poseur ; Repli Tactique : le canon demande un choix joueur mid-turn, incompatible avec un `resolveTurn` stateless → auto-switch vers `firstHealthyBenched`, divergence documentée ; Fantômasque casse + 1/8 PV max (gen 8) ; correction des appellations non-canon employées jusqu'ici (Chasse-Neige, Tension, Impudence, Acharné, Benêt, Incisif, Coloforce, Créa-Élec, Souffle Delta, Urne du Fléau) — le doc équipe fera foi à la saisie phase 14 | 2026-07-17 |
| 50 | **`validateTeam` n'est pas appelée par `validateState`** | Species Clause, max 1 Méga, max 1 légendaire et l'appartenance au movepool sont des règles de *construction* d'équipe, pas des invariants d'état : une partie déjà lancée avec une équipe illégale n'est pas corrompue, elle aurait dû être refusée à la soumission. Les brancher dans `validateState` ferait payer ce coût à chaque tour (ADR #13) et casserait les fixtures qui réutilisent une espèce. Le teambuilder Rust l'appelle une fois via le FFI. Le flag `mega` est une donnée JSON, pas une heuristique sur le préfixe de l'id | 2026-07-28 |
| 49 | **Le catalogue livré est exactement le roster ; les instruments de test vivent hors de `data/`** | 18 des 19 moves devenus orphelins servaient de fixtures neutres aux tests (Tackle, Growl, Abri, Surf…) et aucun n'appartient à un movepool du roster. Les garder aurait fait entrer 19 moves injouables dans la table d'indices gelée en phase 16 — et les supprimer *après* le gel décalerait tous les indices suivants. Ils partent dans `tests/fixtures/moves/`, chargés par `loadExtraContent()` que seuls les tests appellent. Un test verrouille l'invariant : tout move livré appartient à au moins un movepool | 2026-07-28 |
| 51 | **Catalogue de talents indexé, ordre d'enregistrement gelé** | jusqu'ici les talents étaient impliqués par l'espèce et ne traversaient jamais la frontière — mais `AbilityTriggeredEvent` et `AbilityDamageEvent` transportent un nom, qui devient un `ability_id` dans `FfiEvent`. Même raisonnement que l'ADR #45 pour les objets : `AbilityTable` ordonnée, append-only, map nom → index *dérivée* de la table (un nom ne peut pas dériver de son index, un doublon throw). L'ordre gelé est celui hérité des phases 13-14, figé maintenant parce que rien ne l'a encore consommé | 2026-08-21 |
| 57 | **Le chemin de `data/` ne vit pas dans la lib ; aucun fallback** | `BATTLE_ENGINE_DATA_DIR` était une compile definition PUBLIC sur `battle_engine_lib` : un chemin absolu de machine de dev figé dans une archive statique que Rust linke. En Docker ça ne vaut rien, et surtout **un `data/` différent = des index différents** (ADR #12) : un repli silencieux ne crashe pas, il produit des `species_id` qui ne désignent plus les mêmes Pokémon, dans des `BattleState` déjà en base. `engine_init(path)` prend donc un chemin obligatoire et throw si le dossier manque. Le macro survit, mais seulement sur les cibles `demo` et `tests` | 2026-08-21 |
| 58 | **(proposé)** Empreinte de catalogue vérifiée par Rust | corollaire défensif de l'ADR #57 et de l'ADR #12 : exposer `species_count`, `move_count` et un hash des noms *dans l'ordre*, que Rust stocke à la création d'une partie et revérifie au chargement. Sans ça, un `data/` modifié après coup réinterprète silencieusement les parties sauvegardées. À trancher avec Taj (D3) | 2026-08-21 |
| 59 | **(proposé)** `FfiEvent` = une struct plate unique, pas 35 structs partagées | `EventLog` est un `vector<variant<35 types>>` avec des `std::string`. Aplatir en une struct à tag (`kind`, `side`, `slot`, `name_id`, `i0`, `i1`, `f0`, `flags`) fait perdre le typage fort mais simplifie massivement le pont — et le consommateur final (front TS) fait de toute façon un `switch` sur `kind`. Contrepartie obligatoire : un tableau documenté `kind` → sens de chaque champ, qui devient le contrat écrit. Aucun event ne porte deux `CombatantRef`, d'où un seul couple `side`/`slot`. À trancher (D2) | 2026-08-21 |
| 48 | **Bascule anglaise intégrale : moves, talents, objets, natures, espèces et clés de schéma en anglais canon** | Décision équipe : éliminer la classe d'erreurs des traductions approximatives (cf. ADR #47) ; le doc roster fournit les noms EN ; remplace le volet français de l'ADR #40 (PascalCase ASCII conservé) ; « Pudique » du doc équipe = Bold (+Déf/−Atk d'après leurs tables) ; indices d'objets gelés inchangés (seules les strings changent) | 2026-07-18 |

---

## Tooling

Outils en place pour la cohérence du code. À installer une fois par développeur.

### Build et tests

- **CMake** (≥ 3.20) configure le projet, FetchContent récupère nlohmann/json et Catch2 automatiquement.
- **Catch2 v3** pour les tests. Lancer avec `ctest --output-on-failure` depuis `build/`.

### Formatage : clang-format

- Style défini dans `.clang-format` à la racine de `battle-engine/` : **base LLVM + ColumnLimit 100**.
- Installation : `brew install clang-format` (macOS) ou `apt install clang-format` (Linux).
- Application manuelle : `clang-format -i <fichier>` ; recommandé : format-on-save via clangd.

### Hook pre-commit

- `tools/pre-commit` (racine du repo) auto-formate les `.cpp`/`.hpp` staged sous `backend/battle-engine/` au commit, re-stage transparent.
- À installer une fois après clone : `./tools/install-hooks.sh` (symlink `.git/hooks/pre-commit` → `tools/pre-commit`).

### Configuration éditeur

- `.clangd` à la racine de `battle-engine/` pointe vers `build/compile_commands.json`, désactive `UnusedIncludes`.
- **Setup VSCode** : désactiver l'extension Microsoft C/C++ pour ce workspace, installer **clangd** (LLVM), **ouvrir VSCode depuis `battle-engine/`** (sinon clangd ne trouve pas son `.clangd`).
- Format-on-save (`.vscode/settings.json`) :

```json
{
  "editor.formatOnSave": true,
  "[cpp]": { "editor.defaultFormatter": "llvm-vs-code-extensions.vscode-clangd" }
}
```

---

## Glossaire

- **Effect** : action atomique appliquée au `BattleState`. Une attaque est une *liste* d'effets.
- **Ability** (talent) : listener enregistré sur des hooks du moteur.
- **Item** (objet) : même pattern que les talents — listener stateless, un par espèce, verrouillé.
- **Hook** : point d'extension du moteur où talents/objets interviennent.
- **Stage** : niveau de boost/baisse de stat (-6 à +6). Reset au switch (les PV, statuts, PP et objets consommés persistent).
- **STAB** : ×1.5 si le move partage un type avec son utilisateur (×2 avec Adaptabilité).
- **Hazard** : piège posé sur le camp adverse (Piège de Roc, Picots...).
- **Grounded** : Pokémon « au sol » (ni type Vol, ni Lévitation, ni envol) — condition partagée par hazards, Pics Toxik et terrain.
- **Écran** : réduction de dégâts par camp à durée limitée (Voile Aurore).

### Conventions de données (JSON)

**Nommage (ADR #40)** : fichier = ID = PascalCase français **sans accent ni séparateur** (`DanseLames.json`, `BallOmbre.json`, `PoingEclair.json`, `FeunardAlola.json`, `MegaDracaufeuX.json`). Le français correct vit dans `displayName` (« Danse-Lames », « Ball'Ombre »). Espèces et moves suivent la même convention.

**Espèce** : `id`, `displayName`, stats de base, types, `nature` (string FR), `evs` (6 clés optionnelles, défaut 0), `poids` (kg), `talent` (string, vide autorisé — Zoroark), `objet` (string, absent pour les Méga), `legendaire` (bool, défaut false), `moveset` (exactement 4 IDs).

**Move — champs numériques** : `power`, `accuracy` (sentinelle `<= 0` = never-miss), `priority`, `pp`.

**Move — flags booléens optionnels** (défaut `false` sauf mention) :

- `contact` : déclenche Static, Peau Dure, Tough Claws...
- `highCrit` : +1 palier de crit (1/8 au lieu de 1/24).
- `bypassesProtect` : ignore Abri (Cyclone).
- `hitsDig` : touche une cible sous terre ×2 (Séisme).
- `solarCharge` : saute la charge au soleil, ×0.5 sous autre météo (Lance-Soleil).
- `blockedByProtect` : **défaut `true`** ; `"selfOrField": true` exempte (hazards, météo, soins, boosts self).
- `twoTurn` : `"charge"` / `"fly"` / `"dig"`.
- `chance` : sur un **effet**, l'enrobe en secondaire probabiliste (`10` = 10%).
- `punch` / `slicing` / `bulletproof` : familles de moves pour Poing de Fer, Tranchant, Pare-Balles (phase 13-14).

---

*Section workflow Git retirée le 2026-07-15 (redondante avec la pratique établie : une branche `feat/battle-engine-<nom>` par phase, un commit par PR, PR vers `dev`, jamais de push direct sur `dev`/`main`).*
