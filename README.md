# TrainerLab

[![CI](https://github.com/amacaull/TrainerLab/actions/workflows/ci.yml/badge.svg)](https://github.com/amacaull/TrainerLab/actions/workflows/ci.yml)

A Pokémon-style battle engine in C++20, and AI agents that play it, written
and measured in Python.

> **Status: version 2, finished.** The engine is complete and tested.
> Seventeen rules are measured, the best of them a tree search guided by a
> network trained on its own search, as AlphaZero. The bots build their own
> teams from the species' measured strengths. `trainerlab` opens a menu, in
> English or in French: play one of four levels, from easy to impossible, or
> measure the bots.

## Results

Every claim is a measurement: a score over paired battles (5000 pairs on random
teams up to rule 10, 2000 from rule 11 on), with its 95 % Wilson interval,
reproducible with one command. A rule is kept only if the whole interval is
above 0.5.

| Bot | Against | Score | 95 % interval |
|---|---|---|---|
| `hitter` (rule 1): plays its strongest move | random | **0.973** | [0.970, 0.976] |
| `matcher` (rule 3): + replaces with the best matchup | hitter | **0.586** | [0.576, 0.596] |
| `survivor` (rule 4): + saves a healthy Pokémon about to fall | matcher | **0.517** | [0.507, 0.527] |
| `calculator` (rule 5): + the damage the engine expects | survivor | **0.631** | [0.621, 0.640] |
| `forecaster` (rule 6): a turn ahead, rated by a learned win model | calculator | **0.844** | [0.837, 0.851] |
| `strategist` (rule 7): + rates each option by the foe's best reply | forecaster | **0.548** | [0.538, 0.558] |
| rule 10: a network trained by self-play, no search | forecaster | **0.672** | [0.663, 0.681] |
| rule 11: a tree search guided by that network | rule 10 | **0.763** | [0.750, 0.776] |
| rule 12: that search, its network trained on its own search (AlphaZero) | rule 11 | **0.578** | [0.562, 0.593] |
| rule 13: rule 12's network, trained further on deeper searches | rule 12 | **0.530** | [0.514, 0.545] |
| rule 14: a network with attention that sees the matchups, trained on its search | rule 13 | **0.539** | [0.523, 0.554] |
| rule 15: + searches its replacement after a knock-out | rule 14 | **0.549** | [0.534, 0.565] |
| rule 16: its network trained on those replacements | rule 15 | **0.540** | [0.524, 0.555] |
| `alphazero` (rule 17): trained on teams of 2 to 6 too | rule 16 | **0.517** | [0.502, 0.532] |

The four levels of `trainerlab play`, each against the one below (palier 14):

| Level | Bot | Its team | Against the level below |
|---|---|---|---|
| Easy | `matcher` | random | — |
| Medium | `forecaster` | random | **0.874** [0.863, 0.884] |
| Hard | `alphazero`, 200 simulations | random | **0.881** [0.870, 0.890] |
| Impossible | `alphazero`, 3200 simulations | built from the species' strengths | about 0.80 (estimated) |

## How it got here

Each step answered a question the one before had raised. The details, the
commands and every negative result are in [`docs/results.md`](docs/results.md).

**Rules by hand (rules 1 to 5).** How far does a heuristic go? Playing the
strongest move beats random play 97 % of the time. Then the replacement after
a knock-out turned out to matter more than the move (rule 3, +9 points), and
asking the engine for the damage it expects more than a hand-made estimate
(rule 5, +13). Dodging a threat by switching was measured negative (rule 2).

**A learned win model, a turn ahead (rules 6 and 7).** Rule 6 plays each option
a turn ahead on a copy of what it sees and rates the result with a logistic
regression learned from battles: **+34 points** over rule 5, the biggest step of
the rules. Rule 7, which assumes the foe's best reply, beats it head to head but
loses to every other bot: assuming the worst is a trap against foes that are
not perfect.

**Search without a better judge (rules 8 and 9): negative.** A mixed strategy
from the game's equilibrium, then a Monte Carlo tree search three turns deep,
gained nothing. More data or a hidden layer did not make the win model better
either. The limit was the judge of positions, not the search.

**A network, then a search it guides (rules 10 and 11).** A policy and value
network, learned by imitation of rule 6 then by self-play (PPO), beats rule 6 by
17 points with no search at all. A tree search guided by it (PUCT, as in
AlphaZero) wins 26 more: once positions are judged well, search pays. The
engine's hot paths, the features and then the search moved to C++ for this
(paliers 5 and 6).

**AlphaZero's loop (rules 12 to 17).** The network learns from its own search:
the search's choices as targets, the results and the search's value. Each rule
fixed what the measures showed: deeper targets (rule 13), attention between the
Pokémon and the arithmetic the network had to guess (rule 14), searched
replacements after a knock-out (rules 15 and 16), smaller teams in self-play
(rule 17). The gains shrank: +8, +3, +4, +5, +4, +1.7 points.

**Measuring the bot (palier 7).** Instead of improving, measure: the same team
wins both battles of a matchup two times in three, so teams decide much; four
times the simulations win 7 points; a judge searching 16 times deeper rates
decisions reliably on average but not one by one, so duels stay the measure.
Palier 8 then made the loop twice as fast on the Mac (Apple's matrix unit
through Accelerate, battles side by side), and palier 9 found two changes to
the search that gained nothing.

**Teams, built by the AI (palier 12).** If teams decide that much, can the AI
choose a better one? With a draft (a pool of 12 species shared by both sides,
around a core the seed imposes), adding the best measured species wins **21
points** over random picks. The true score of every team of two showed that a
team's worth is about 90 % the sum of its species. A network judging whole
teams predicted battles slightly better, but building with it won nothing: with
49 species, composition has reached its ceiling.

**The battle, with these teams (palier 13).** Choosing the lead gains at most a
point (kept in `play`, as a player chooses); guessing the foe's hidden Pokémon
by strength gains nothing; a loop learning from searches four times deeper
gains nothing either, as this network no longer learns what such searches see.
What pays is searching more at play time: 7 points for four times the
simulations, about 2.5 for each doubling after.

**Four levels (palier 14).** From the bots already measured, four levels with a
regular step: each wins about 87 % of its battles against the one below. The
last level adds the two things that still pay, a team built from the species'
strengths and 3200 simulations. Palier 11, between, had given `trainerlab` its
menu, in English and in French.

## What we learned

- **Paired battles.** Each pair plays the same teams and the same dice twice,
  the bots swapping sides: luck and team strength cancel, and 2000 pairs tell
  a 1.5-point difference apart.
- **Fixed before measuring.** Settings, seeds and the duel that decides are
  written down before running; a second attempt is declared as such and
  judged on fresh seeds. One duel decides, its whole interval above 0.5.
- **Negative results kept.** Many ideas did not work; each is in
  `docs/results.md` with its measure, and its code removed.
- **The judge before the search.** Search paid only once positions were judged
  well (rule 11), and the network paid only once it learned from search (rule
  12). When the loop stops learning, more search at play time still pays.
- **Determinism.** The engine and the bots are deterministic, and the engine is
  built without fused multiply-add: a seed replays a battle exactly, on macOS
  and on Linux. Tests first, every line and branch covered, mutation testing
  for the tests themselves.
- **Speed where it matters.** Profiling first, then C++ for what is hot: the
  engine, the features, the search, the network in batches, and a measure's
  battles side by side.

## Where to look

| | |
|---|---|
| [`engine/`](engine/) | The battle engine: stateless, deterministic, 309 tests. [README](engine/README.md), [API](engine/docs/api.md). |
| [`search/`](search/) | The C++ side of the AI: the tree searches, the network and self-play. [README](search/README.md). |
| [`ai/`](ai/) | The bots, the command `trainerlab`, the tournaments and the learning. [README](ai/README.md). |
| [`docs/results.md`](docs/results.md) | Every duel and every model, negative results included. |
| [`docs/benchmark.md`](docs/benchmark.md) | How fast the engine and the bots play, from Python and from C++. |
| [`docs/testing.md`](docs/testing.md) | How the tests are written and checked: coverage, and mutation testing. |
| [`docs/error-analysis.md`](docs/error-analysis.md) | Why a bot loses: losses studied by hand, mistakes counted automatically. |

## Origin

The engine was written for **ft_transcendence**, the final common-core
project at [42 Paris](https://42.fr), a Pokémon Showdown-style web game. This
repository keeps the engine and builds the AI on top of it.
