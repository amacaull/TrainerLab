# TrainerLab

[![CI](https://github.com/amacaull/TrainerLab/actions/workflows/ci.yml/badge.svg)](https://github.com/amacaull/TrainerLab/actions/workflows/ci.yml)

A Pokémon-style battle engine in C++20, and AI agents that play it, written
and measured in Python.

> **Status: work in progress.** The engine is complete and tested. Hand-written
> bots are measured; a terminal mode to play against them comes next.

## Results so far

Every claim is a measurement: a score over paired battles on random teams,
with its 95 % Wilson interval, reproducible with one command.

| Bot | Against | Score | 95 % interval |
|---|---|---|---|
| Rule 1: plays its highest expected damage | random bot | **0.970** | [0.957, 0.979] |
| Rule 3: + replaces with the best matchup | rule 1 | **0.580** | [0.549, 0.610] |
| Rule 4: + saves a healthy Pokémon about to fall first | rule 3 | **0.513** | [0.501, 0.526] |

Two rules were measured and dropped, and the traps found on the way are
documented: see [`ai/README.md`](ai/README.md) for the method, every
measurement and what went wrong.

## What is here

| Directory | Content |
|---|---|
| [`engine/`](engine/) | The battle engine: stateless, deterministic, 290 tests. See its [README](engine/README.md). |
| [`ai/`](ai/) | Python: the engine binding, a referee, bots, a statistical tournament harness. See its [README](ai/README.md). |

## Origin

The engine was written for **ft_transcendence**, the final common-core
project at [42 Paris](https://42.fr), a Pokémon Showdown-style web game. This
repository keeps the engine and builds the AI on top of it.
