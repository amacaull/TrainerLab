# TrainerLab

[![CI](https://github.com/amacaull/TrainerLab/actions/workflows/ci.yml/badge.svg)](https://github.com/amacaull/TrainerLab/actions/workflows/ci.yml)

A Pokémon-style battle engine in C++20, and AI agents that play it, written
and measured in Python.

> **Status: work in progress.** The engine is complete and tested. Hand-written
> bots are measured, and you can play against them in the terminal:
> `trainerlab-play` (see [`ai/README.md`](ai/README.md)).

## Results so far

Every claim is a measurement: a score over 5000 pairs of battles on random
teams (10 000 battles), with its 95 % Wilson interval, reproducible with one
command.

| Bot | Against | Score | 95 % interval |
|---|---|---|---|
| Rule 1: plays its highest expected damage | random bot | **0.973** | [0.970, 0.976] |
| Rule 3: + replaces with the best matchup | rule 1 | **0.586** | [0.576, 0.596] |
| Rule 4: + saves a healthy Pokémon about to fall first | rule 3 | **0.517** | [0.507, 0.527] |

Rule 4 is the best bot: it beats every other one. Two attempts did not pay
off, and the traps found on the way are documented: see
[`ai/README.md`](ai/README.md) for the method, every bot against every other,
and what went wrong.

## What is here

| Directory | Content |
|---|---|
| [`engine/`](engine/) | The battle engine: stateless, deterministic, 293 tests. See its [README](engine/README.md). |
| [`ai/`](ai/) | Python: the engine binding, a referee, bots, a statistical tournament harness. See its [README](ai/README.md). |

## Origin

The engine was written for **ft_transcendence**, the final common-core
project at [42 Paris](https://42.fr), a Pokémon Showdown-style web game. This
repository keeps the engine and builds the AI on top of it.
