# Testing

Every line and branch of the Python package runs under a test (`pytest --cov`,
100 % required). That says the code runs, not that a test would notice it
going wrong. Mutation testing checks the second: it changes the code one small
mutation at a time (a `<` for a `<=`, a `+` for a `-`, a constant, a
`return False` for a `return True`) and runs the tests; a mutant no test
catches survives. A survivor is either a test to write, or an equivalent
mutant: one that changes nothing a test could observe.

```bash
cd ai
mutmut run --paths-to-mutate src/trainerlab/experiments/teams.py \
  --runner "python -m pytest -x -q tests/experiments/test_team_effect.py" --tests-dir tests
mutmut results                            # the survivors
mutmut show 42                            # one of them
```

mutmut 2 (in the `dev` extras): it mutates the files in place, which an
editable install needs; mutmut 3 copies the sources aside, where the installed
package does not look. Stop it with Ctrl-C only between mutants, or restore the
file it was mutating with git.

## The first runs (after palier 7)

On the five modules written last, mutated one by one against their own tests:

| Module | Mutants | Survived before | Survived after |
|---|---|---|---|
| `experiments/teams.py` | 147 | 50 | 10 |
| `experiments/situations.py` | 94 | 18 | 9 |
| `experiments/judge.py` | 93 | 28 | 17 |
| `experiments/rollouts.py` | 118 | 34 | 10 |
| `rl/alphazero.py` | 96 | 40 | 25 |
| **all** | **548** | **170 (31 %)** | **71 (13 %)** |

Four more were killed after this run, each checked by applying the mutation by
hand; one unused constant was removed. What survives was read one by one and
is equivalent: tuning constants the tests set themselves (simulations,
worlds, batch size, learning rate), seeds whose only duty is to differ, frozen
dataclasses, a tolerance of `1e-12` against `2e-12`, `dim=+1` against `dim=-1`
on a two-dimensional tensor, a cache, the device on a CPU, and mutants that
break the syntax, which mutmut 2 counts as survivors.

The tests that were missing were real:

- **the AlphaZero loop** could learn with the sign of its policy loss or its
  value loss flipped, the network pushed away from the search and the
  results, and its reported losses could be any arithmetic: the losses are
  now checked exactly (learning nothing, they must equal the cross-entropy and
  squared error computed by hand, over several batches) and must fall when it
  learns;
- **the playing out of a blunder** could sum the two options instead of
  subtracting them, average wrongly, or have one bot play both sides: a fake
  battle now checks who plays which side, which option is forced, and the gap
  game by game;
- **the situations** were tested on side 0 only, where `1 - side` and
  `1 + side` both name side 1; a status move without a heal could count as a
  heal; the thresholds (a quarter of the HP, three quarters, a speed tie)
  were not tested at their exact values;
- **the judge's** loss could be twice the gap of values instead of half, and
  the numbers of a move and a switch could be swapped;
- **the team effect** could replay a matchup the same way twice, count a draw
  as 1.5 points, score the regression on the wrong pairs, or run a regression
  that never reached its optimum: the regression now has to find the
  strengths a simulated dataset was made with.

From now on, a test is written first and seen failing, then the code.
