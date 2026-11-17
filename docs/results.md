# Results of the learned bots

Every line is a score over **5000 pairs of battles (10 000 battles)** on
random teams, with its 95 % Wilson interval, reproducible with the command
above its table. Since the engine is compiled without fused multiply-add
(see the engine README, section 5), each line gives the same record, battle
for battle, on macOS and on Linux.

## Rule 5 and rule 6 against every other bot

```bash
trainerlab duel calculator <bot> --pairs 5000 --from 55000
trainerlab duel forecaster <bot> --pairs 5000 --from 55000   # --from 50000 against calculator
```

| Bot ↓ against → | calculator | survivor | matcher | hitter | dodger | random |
|---|---|---|---|---|---|---|
| **forecaster** (rule 6) | **0.844** [0.837, 0.851] | **0.859** [0.852, 0.866] | **0.864** [0.858, 0.871] | **0.887** [0.880, 0.893] | **0.894** [0.888, 0.900] | 0.985 [0.982, 0.987] |
| **calculator** (rule 5) | | 0.631 [0.621, 0.640] | 0.652 [0.642, 0.661] | 0.730 [0.721, 0.739] | 0.774 [0.765, 0.782] | **0.990** [0.988, 0.992] |

`calculator` against `survivor` is on seeds 30000–34999, the others on
55000–59999.

| Duel | Record (W / D / L) |
|---|---|
| forecaster vs calculator | 8430 / 17 / 1553 |
| forecaster vs survivor | 8580 / 27 / 1393 |
| forecaster vs matcher | 8630 / 28 / 1342 |
| forecaster vs hitter | 8857 / 17 / 1126 |
| forecaster vs dodger | 8924 / 28 / 1048 |
| forecaster vs random | 9845 / 7 / 148 |
| calculator vs survivor | 6289 / 37 / 3674 |
| calculator vs matcher | 6498 / 38 / 3464 |
| calculator vs hitter | 7280 / 45 / 2675 |
| calculator vs dodger | 7719 / 36 / 2245 |
| calculator vs random | 9898 / 6 / 96 |

- **Each bot beats every earlier one, the more clearly as the gap in rules
  grows.** `forecaster` beats every bot measured against it.
- **`forecaster` does not win by knowing the foe's move.** It assumes the
  foe plays as `calculator`, which is exactly true against `calculator`
  (0.844), and only partly against `survivor` and `matcher`, where it scores
  more (0.859, 0.864): those foes are weaker.
- **Against the random bot, `calculator` does slightly better** (0.990
  against 0.985): a foe that plays at random misleads a bot that assumes it
  plays well.

## Rule 7: minimax against exploitation

```bash
trainerlab duel strategist forecaster --pairs 5000 --from 70000
trainerlab duel strategist calculator --pairs 5000 --from 50000
trainerlab duel strategist survivor --pairs 5000 --from 55000
```

| Duel | Score | 95 % interval | Record (W / D / L) | forecaster, same seeds |
|---|---|---|---|---|
| strategist vs forecaster | **0.548** | [0.538, 0.558] | 5462 / 35 / 4503 | |
| strategist vs calculator | 0.739 | [0.730, 0.748] | 7373 / 36 / 2591 | 0.844 |
| strategist vs survivor | 0.812 | [0.804, 0.820] | 8110 / 23 / 1867 | 0.859 |

**There is no single best bot.** Head to head, `strategist` beats
`forecaster`: `forecaster` expects `calculator`'s move, which `strategist`
does not play, while `strategist` always expects the worst reply and is
never caught out. Against every other bot, `forecaster` does clearly better:
it exploits a foe it can predict, whereas `strategist` guards against
replies those foes never play and lets chances go. Exploiting a known foe or
guarding against an unknown one: neither dominates.

## The models

```bash
trainerlab train calculator-vs-survivor-40000-41999.npz [--model network|network-stop]
trainerlab train calculator-vs-survivor-40000-41999.npz \
  forecaster-vs-calculator-60000-61999.npz forecaster-vs-survivor-62000-63999.npz \
  [--model network-stop]
```

Test: the battles whose seed is a multiple of 5, never seen in training.

| Data | Model | Log loss (test) | Brier (test) | Accuracy (test) | Log loss (train) |
|---|---|---|---|---|---|
| calculator vs survivor | constant | 0.693 | 0.248 | 0.513 | |
| | logistic regression | **0.532** | **0.178** | **0.723** | 0.507 |
| | network, 10 passes | 0.632 | 0.206 | 0.699 | 0.351 |
| | network, stopped early (best after 2 passes) | 0.540 | 0.182 | 0.714 | 0.460 |
| the three datasets | constant | 0.692 | 0.249 | 0.520 | |
| | logistic regression (`gen2`) | **0.394** | **0.126** | **0.816** | 0.409 |
| | network, stopped early (best after 2 passes, `gen2-net`) | 0.400 | 0.129 | 0.810 | 0.362 |

Stopping early cures the network's overfitting, but it stops after two
passes and still does no better than the logistic regression: with these
features, a hidden layer brings nothing.

## The data loop and the network, in play

```bash
trainerlab duel forecaster:gen2.npz forecaster --pairs 5000 --from 65000
trainerlab duel forecaster:gen2-net.npz forecaster:gen2.npz --pairs 5000 --from 75000
```

| Duel | Score | 95 % interval | Record (W / D / L) |
|---|---|---|---|
| gen2 (also trained on forecaster's games) vs the shipped model | 0.505 | [0.495, 0.515] | 5040 / 17 / 4943 |
| gen2-net (network) vs gen2 (logistic, same data) | 0.473 | [0.463, 0.482] | 4716 / 20 / 5264 |

Two negative results:

- **More data, and data from `forecaster`'s own games, does not make it play
  better** (a draw), although `gen2` predicts the winner of those games
  better. The bot only uses the model to rank positions one turn apart, and
  for that both models lean on the same things, Pokémon alive and HP left:
  they nearly always pick the same option.
- **The network plays worse than the logistic regression** trained on the
  same data, the whole interval below 0.5.

Both point to the same limit: the bot's search (one turn, a guessed foe), not
its evaluation.
