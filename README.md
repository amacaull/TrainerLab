# TrainerLab

[![CI](https://github.com/amacaull/TrainerLab/actions/workflows/ci.yml/badge.svg)](https://github.com/amacaull/TrainerLab/actions/workflows/ci.yml)

A Pokémon-style battle engine in C++20, and AI agents that play it, written
and measured in Python.

> **Status: work in progress.** The engine is complete and tested. Seven bots
> are measured, the best of them with a learned win model; you can play
> against them in the terminal with `trainerlab play`, or `trainerlab` for a
> menu. Next: search (simultaneous-move Nash, MCTS), then reinforcement
> learning.

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

`forecaster` beats every earlier bot. `strategist` beats it head to head but
does worse against every other bot: guarding against the worst reply costs
the chances a predictable foe leaves.

## Where to look

| | |
|---|---|
| [`engine/`](engine/) | The battle engine: stateless, deterministic, 309 tests. [README](engine/README.md), [API](engine/docs/api.md). |
| [`search/`](search/) | The C++ side of the AI: playouts and benchmark now, search next. [README](search/README.md). |
| [`ai/`](ai/) | The bots, the command `trainerlab`, the tournaments and the learning. [README](ai/README.md). |
| [`docs/results.md`](docs/results.md) | Every duel and every model, negative results included. |
| [`docs/error-analysis.md`](docs/error-analysis.md) | Why a bot loses: losses studied by hand, mistakes counted automatically. |

## Origin

The engine was written for **ft_transcendence**, the final common-core
project at [42 Paris](https://42.fr), a Pokémon Showdown-style web game. This
repository keeps the engine and builds the AI on top of it.
