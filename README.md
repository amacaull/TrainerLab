# TrainerLab

[![CI](https://github.com/amacaull/TrainerLab/actions/workflows/ci.yml/badge.svg)](https://github.com/amacaull/TrainerLab/actions/workflows/ci.yml)

A Pokémon-style battle engine in C++20, and AI agents that play it, written
and measured in Python.

> **Status: work in progress.** The engine is complete and tested. Hand-written
> bots are measured, and you can play against them in the terminal:
> `trainerlab play`, or just `trainerlab` for a menu (see [`ai/README.md`](ai/README.md)).

## Results so far

Every claim is a measurement: a score over 5000 pairs of battles on random
teams (10 000 battles), with its 95 % Wilson interval, reproducible with one
command.

| Bot | Against | Score | 95 % interval |
|---|---|---|---|
| Rule 1: plays its highest expected damage | random bot | **0.973** | [0.970, 0.976] |
| Rule 3: + replaces with the best matchup | rule 1 | **0.586** | [0.576, 0.596] |
| Rule 4: + saves a healthy Pokémon about to fall first | rule 3 | **0.517** | [0.507, 0.527] |
| Rule 5: rule 4, with the damage expected by the engine itself | rule 4 | **0.631** | [0.621, 0.640] |
| Rule 6: plays each option a turn ahead, rated by a learned win model | rule 5 | **0.844** | [0.837, 0.851] |
| Rule 7: rule 6, rating each option by the foe's best reply | rule 6 | **0.548** | [0.538, 0.558] |

Rule 6 beats every earlier bot; rule 7 beats rule 6 head to head, but does
worse than rule 6 against every other bot: guarding against the worst reply
costs the chances a predictable foe leaves. Every duel, the models and the
negative results: [`docs/results.md`](docs/results.md); the method and the
earlier rules: [`ai/README.md`](ai/README.md).

## What is here

| Directory | Content |
|---|---|
| [`engine/`](engine/) | The battle engine: stateless, deterministic, 308 tests. See its [README](engine/README.md). |
| [`ai/`](ai/) | Python: the engine binding, a referee, bots, a statistical tournament harness. See its [README](ai/README.md). |

## Origin

The engine was written for **ft_transcendence**, the final common-core
project at [42 Paris](https://42.fr), a Pokémon Showdown-style web game. This
repository keeps the engine and builds the AI on top of it.
