# TrainerLab

[![CI](https://github.com/amacaull/TrainerLab/actions/workflows/ci.yml/badge.svg)](https://github.com/amacaull/TrainerLab/actions/workflows/ci.yml)

A Pokémon-style battle engine in C++20, and AI agents that play it, written
and measured in Python.

> **Status: version 1.** The engine is complete and tested. Twelve bots are
> measured, the best of them a tree search guided by a network trained on its
> own search, as AlphaZero; play against any of them with `trainerlab play`, or
> `trainerlab` for a menu. Next: measuring its strategic mistakes, a network
> that sees how the Pokémon relate, then teams built by the AI itself.

## Results so far

Every claim is a measurement: a score over 5000 pairs of battles on random
teams (10 000 battles), with its 95 % Wilson interval, reproducible with one
command.

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

`forecaster` beats every earlier bot; `strategist` beats it head to head but
does worse against every other bot. Tree searches (Nash, MCTS) brought nothing:
the limit was the win model judging the positions. Rule 10 lifts it: a network
that learned the worth of a position by playing millions of games against
itself beats `forecaster` and `strategist` by 17 points, without any search, and
is the first bot that looks ahead to lay hazards as often as chance does. And a
tree search guided by that network (rule 11) beats it by 26 more points: once
the positions are judged well, search pays. Trained on its own search, as
AlphaZero, the network lifts that search by 8 more points (rule 12): strong in
tactics, still weak in long-term strategy.

## Where to look

| | |
|---|---|
| [`engine/`](engine/) | The battle engine: stateless, deterministic, 309 tests. [README](engine/README.md), [API](engine/docs/api.md). |
| [`search/`](search/) | The C++ side of the AI: the tree searches, the network and self-play. [README](search/README.md). |
| [`ai/`](ai/) | The bots, the command `trainerlab`, the tournaments and the learning. [README](ai/README.md). |
| [`docs/results.md`](docs/results.md) | Every duel and every model, negative results included. |
| [`docs/benchmark.md`](docs/benchmark.md) | How fast the engine and the bots play, from Python and from C++. |
| [`docs/error-analysis.md`](docs/error-analysis.md) | Why a bot loses: losses studied by hand, mistakes counted automatically. |

## Origin

The engine was written for **ft_transcendence**, the final common-core
project at [42 Paris](https://42.fr), a Pokémon Showdown-style web game. This
repository keeps the engine and builds the AI on top of it.
