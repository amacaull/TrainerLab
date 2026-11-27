# Error analysis

Why does a good bot lose? Rule 4's losses against rule 3 were studied one by
one, then the mistakes a program can spot were counted over many battles. It
led to rule 5 ([results](results.md#rule-5-the-damage-the-engine-expects)).
Seeds 20000 and up: none of them served to choose or measure a rule.

This study covers the rule bots. For the network bots (rule 12 on), mistakes
were rated by a judge searching 16 times deeper, then checked by playing them
out: reliable on average, not decision by decision (palier 7, in
[results](results.md#palier-7-how-good-is-rule-12-and-where-it-goes-wrong)).

## By hand: 15 losses

`trainerlab watch survivor matcher --losses --pairs 50 --from 20000` lists the
losses; each was replayed with `trainerlab watch`, which shows at every
decision the bot's estimate of each option, then what really happened.

**The convention.** A loss often holds several mistakes. Its category is the
**last decision after which bot A could no longer win** except by luck; the
other mistakes are kept alongside. A piece of bad luck is the cause only if
the battle was lost for good after it. A loss with no such decision in its
last turns is `outclassed`: the foe's remaining team was simply stronger.

Every loss is in [`ai/analysis/rule4-losses.csv`](../ai/analysis/rule4-losses.csv),
with the turn and what would have won.

| Family | Categories | Losses | With the mistakes kept alongside |
|---|---|---|---|
| **Wrong damage estimate** | `move-mechanics`: fixed damage, multi-hit moves, moves that fail | **5** | 7, plus 3 `ability-ignored` |
| Who acts first ignored | `priority-ignored`, `replacement-ignores-speed` | 2 | 3 |
| No plan beyond one turn | `status-never-used`, `opponent-state-ignored` | 2 | 2 |
| Bad luck | `bad-luck` | 1 | 4 |
| No mistake in the endgame | `outclassed` | 5 | |

Of the 10 losses a bot decision caused, **half come from a wrong damage
estimate**: Blissey never plays Seismic Toss (fixed damage, estimated at 0),
Triple Axel is estimated as one hit of 20 power, Poltergeist is played on a
foe with no item and fails. The losses where bot A led at some point are the
ones decided by a mistake; those where it never led are mostly
`outclassed`.

**A rejected hypothesis.** In 22 of the 50 losses listed, bot A never led and
lost the first Pokémon, often on turn 1. Losing the opening matchup (the two
leads are not chosen) looked like a cause. In the 4 battles checked, the foe
lost its lead on turn 2 every time: openings are even trades, and these
battles are decided later.

**A bug found on the way.** In one loss, a Pokémon pulled in by Emergency
Exit used, that same turn, the move its predecessor had chosen. The engine
now leaves a Pokémon that enters mid-turn without an action, as in the games;
every duel was measured again after the fix.

## Automatically: 2000 battles

Some of these mistakes leave a trace that needs no judgement.
`trainerlab audit` counts them over bot A's decisions:

```bash
trainerlab audit survivor matcher --pairs 1000 --from 20000
```

| Check | What it spots | Per 1000 decisions | In lost battles | In won battles |
|---|---|---|---|---|
| `blind-choice` | a move played while every move is estimated at 0 damage | 143.5 | 37.1 % | 27.0 % |
| `failed` | a move that fails outright (Fake Out after the first turn, hazards already laid, a burn on a burned foe…) | 62.8 | 43.6 % | 26.4 % |
| `no-effect` | a move into an immunity, of type or of ability (Levitate…) | 37.1 | 25.3 % | 13.8 % |
| `fixed-damage-ko-missed` | Seismic Toss would knock out, a weaker choice is made | 4.4 | 4.0 % | 2.0 % |

2000 battles: 1025 won, 18 drawn, 957 lost; 49 716 decisions by rule 4.

- **One decision in seven is made blind**, every move estimated at zero: the
  bot then plays the first one in its list. One in ten is a move that fails
  or has no effect.
- **Every mistake is more frequent in lost battles**, 1.5 to 2 times. Lost
  and won battles are as long (25.0 and 24.8 decisions on average), so it is
  not a matter of more chances to err. It is an association: a mistake can
  also be the symptom of a lost position.
- Each check is tested on the mistakes found by hand: it must find them
  (20042-0, 20048-0, 20013-1…), and stay silent on look-alikes, such as a
  hit taken by Disguise, which is no immunity.

**What came next.** The first family called for a damage estimate computed
by the engine itself, which knows abilities, fixed damage, multi-hit moves
and the conditions under which a move fails, rather than a second, rougher
model of it in Python: that is rule 5.

## The bots' damage estimate

`estimate.py` gives the bots a rough idea of damage in HP, from visible
information only: stats, stat stages, power, STAB, types, the common damage
and speed items, and remaining HP. It leaves out abilities, weather, critical
hits and the damage roll (it takes the average roll). On 2374 real hits, the
engine's damage divided by the estimate has a median of 1.00, and 75 % of
hits are within ±20 %.
