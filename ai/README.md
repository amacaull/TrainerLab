# TrainerLab AI

Bots that play the battle engine in [`../engine`](../engine), and the tools to
measure them, in Python on top of a pybind11 binding of the engine's flat C++
API. The measurements are in [`docs/results.md`](../docs/results.md), the
study of why bots lose in [`docs/error-analysis.md`](../docs/error-analysis.md).

## Quick start

Requirements: Python 3.11+, CMake ≥ 3.20 and a C++20 compiler (the install
compiles the engine and its binding).

```bash
cd ai
python -m venv .venv && source .venv/bin/activate
pip install -e ".[dev]"                   # again after any engine change

pytest --cov                              # every test, 100 % line and branch coverage required
trainerlab                                # a menu: choose what to do, then each option
trainerlab play                           # play a battle against the default bot
trainerlab duel matcher hitter            # rule 3 against rule 1, 500 pairs
trainerlab bots                           # every bot and what it does
```

## One command

`trainerlab` with no argument opens a menu: the mode (play, duel, watch,
audit, dataset, train), then its options one at a time, each with a default
that Enter keeps. Before running, it prints the same command in one line,
which runs directly, as in scripts and in this README. `trainerlab --help`
lists the commands, `trainerlab duel --help` the options of one.

| Command | What it does |
|---|---|
| `play [--against BOT] [--seed N] [--team A,B,…]` | a battle against a bot in the terminal |
| `duel BOT_A BOT_B` | bot A's score against bot B, with its 95 % interval |
| `watch BOT_A BOT_B --losses` / `--seed N --side S` | the battles A lost, or one replayed with what each bot saw |
| `audit BOT_A BOT_B` | the mistakes of bot A that a program can spot |
| `usage BOT_A BOT_B` | the kinds of moves bot A plays (attacks, hazards, boosts, statuses…), and how often each was an option |
| `dataset BOT_A BOT_B` | every decision of the battles, as a learning dataset (`.npz`) |
| `train DATASET…` | a win probability model, fitted and scored on unseen battles |
| `bench` | how fast the bots play from Python (the C++ counterpart: `search_bench`) |

Every command that plays battles takes `--pairs N` (pairs of two battles, sides
swapped) and `--from SEED`, the seed of the first pair: `--pairs 5000 --from
30000` plays seeds 30000 to 34999. `duel` and `dataset` spread the pairs over
every core (`--jobs N` to choose); a pair depends on its seed alone, so the
results are the same whatever the number of processes.

In `play`, you see exactly what the bots see: the board, your moves tagged with
their type effectiveness (`x2`, `x1/2`…; `--no-hints` hides it), `?` for their
details, then the turn typed out (`--speed instant|fast|normal|slow`). A seed
replays the same battle; `--log FILE` keeps it as text.

## The bots

| Name | Rule |
|---|---|
| `random` | A uniformly random legal choice: the baseline. |
| `hitter` | **Rule 1.** The move with the highest power × STAB × type effectiveness × accuracy; the first Pokémon offered as replacement. |
| `dodger` | **Rule 2.** Rule 1, but leaves a super effective threat for a Pokémon that resists it. A measured negative result. |
| `matcher` | **Rule 3.** Rule 1, but replaces with the best matchup: damage it can deal minus damage it can take. |
| `survivor` | **Rule 4.** Rule 3, but leaves a healthy Pokémon the foe would knock out before it acts. |
| `calculator` | **Rule 5.** Rule 4, but plays the move with the highest damage the engine itself expects. |
| `forecaster` | **Rule 6.** Plays each option a turn ahead on a copy of what it sees, the foe playing as rule 5, and keeps the one the win probability model rates best. |
| `strategist` | **Rule 7.** Rule 6, but rates each option by the foe's best reply (minimax, one turn deep). |
| `nash` | **Rule 8.** Both sides choose at once: the odds after every pair (option, reply) make a zero-sum game, and it draws its option from that game's optimal mixed strategy. |
| `mcts` | **Rule 9.** A Monte Carlo tree search over simultaneous moves, three turns deep, guessing the foe's unseen Pokémon as legal teams; the model rates where each simulation stops. |

Rules 1 to 5 are each three choices assembled in a `RuleBot`: a switch it may
make before moving, its move, its replacement. `calculator()` is
`RuleBot(move=most_expected_damage, replacement=best_matchup, switch=escape)`.

`forecaster`, `strategist`, `nash` and `mcts` play with the logistic regression shipped in
`learning/models/win-logistic.npz`. Any command takes `forecaster:FILE.npz` to
play with another model instead.

## Method

- **Paired battles.** A pair is two battles with the same seed and the same
  two teams, the bots swapping sides: team strength and luck cancel out.
- **Random teams.** Every pair draws its own two legal teams of six: a result
  on one fixed matchup can be specific to it.
- **Score.** Wins plus half the draws, with a 95 % Wilson interval. A bot is
  better only if the whole interval is above 0.5.
- **Fixed before measuring.** A rule or a model is fixed before it is
  measured, once, on seeds that served for nothing else. When variants are
  compared, the choice is made on some seeds and confirmed on others.

## How it fits together

| Package | Role |
|---|---|
| `game/` | The referee (`environment`), the loop (`battle`), the contract a player honours (`player`), a pair of battles from its seed (`pairs`), every decision replayed (`decisions`), the event kinds (`events`), what a player can reckon of damage and speed (`estimate`), a playable state built from a view (`views`). |
| `learning/` | The features of a position (`features`), the win probability model (`model`), fitting and scoring it (`fitting`), the shipped model (`models/`). |
| `bots/` | Rules 1 to 5 (`rules`), rules 6 and 7 (`lookahead`), every bot by name (`roster`). |
| `experiments/` | Duels (`tournament`), spreading pairs over the cores (`parallel`), the audit of mistakes (`audit`), the datasets (`dataset`). |
| `terminal/` | A battle against a bot (`session`), a battle between bots told with what each saw (`commentary`), the human player, the text and the panels. |
| `commands/` | `trainerlab`: the dispatch, the menu, and one thin module per mode that reads the options, calls the domain and prints the result. |

Each package imports only those above it in the table (`terminal/`, only
`game/`); a test reads every import to keep it so. A bot never sees the real
state: it receives `observe(side)`, which hides the foe's Pokémon not yet seen.

The engine and the bots are deterministic, so two bots and a seed define a
pair of battles entirely. Nothing is stored; `decisions` replays them:

```python
from trainerlab.bots.roster import BOTS
from trainerlab.game.decisions import decisions

for d in decisions(BOTS["survivor"], BOTS["matcher"], seeds=range(20000, 21000)):
    d.view, d.legal, d.choice  # what the deciding side saw, its options, its choice
    d.side, d.by_a, d.turn  # who decided (bot A or bot B), and when
    d.outcome  # "win", "draw" or "loss", for the side that decided
```

## Learning

```bash
trainerlab dataset calculator survivor --pairs 2000 --from 40000
trainerlab train calculator-vs-survivor-40000-41999.npz \
  --out src/trainerlab/learning/models/win-logistic.npz      # rebuilds the shipped model
```

A dataset has one row per decision: 249 features of what the deciding side
saw, from its point of view (HP, status, stat stages and species of each
active, Pokémon alive and HP of each team, hazards, weather, the best damage
each active can expect, who is faster), and as label the battle's score for
that side (1, 0.5, 0). Rows of one battle look alike, so the train/test split
goes by seed: the seeds that are multiples of 5 are the test set.

The model is numpy alone: ReLU layers then a sigmoid, so with no hidden layer
it is a logistic regression (`--model logistic`, the default), trained by
Adam from a fixed seed. `--model network` adds a hidden layer of 64 units,
`network-stop` stops it early on held-out training battles.

For the networks of palier 6, `learning/encoding.py` reads a position as one
vector per Pokémon (153 numbers and 7 catalog ids each, twelve slots), the
field (17) and a mask of the ten options; computed in C++.

## Known limitations

- Rules 1 to 4 score moves by power × STAB × type × accuracy, which ignores
  abilities, multi-hit and fixed-damage moves. Rule 5 asks the engine for its
  move choice, but its escapes and replacements still use these estimates.
- The bots look one turn ahead at most; rule 6 guesses the foe, rule 7 assumes
  the worst. More data or a network did not make rule 6 play better: the
  limit is the search, not the evaluation.
