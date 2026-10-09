# TrainerLab

A Pokémon-style battle engine in C++20, and AI agents that play it, written
and measured in Python.

> **Status: work in progress.** The engine is complete and tested; the Python
> side is being built.

## What is here

| Directory | Content |
|---|---|
| [`engine/`](engine/) | The battle engine: stateless, deterministic, 287 tests. See its [README](engine/README.md). |
| `ai/` | Python: the engine binding (done), bots, a statistical tournament harness and a terminal mode to play against the AI (in progress). |

## Goal

Play against the AI from a terminal, and back every claim about a bot with a
measurement: win rates over paired games, with confidence intervals.

## Origin

The engine was written for **ft_transcendence**, the final common-core
project at [42 Paris](https://42.fr), a Pokémon Showdown-style web game. This
repository keeps the engine and builds the AI on top of it.
