"""The judge: a far deeper search rates each decision of a bot (palier 7.4).

The checks of the audit count the mistakes we thought of. The judge counts all of them: rule 12's
network searches `SIMULATIONS` simulations over `WORLDS` guesses at the foe's unseen Pokemon (16
and 2 times what rule 12 plays with), trying every legal option at least `MIN_VISITS` times, so
that the option the bot chose gets a value even when the network dislikes it. A decision's loss
is what the judge's own choice is worth over the bot's, in points of the chance to win: the value
of an option is the mean result in [-1, 1], so half of it. Settings fixed before measuring.

Only the turns with a choice are judged: a replacement after a knock-out is the network's
favourite, without search, in rules 11 and 12. Needs PyTorch: pip install -e "ai/.[rl]".
"""

import math
from dataclasses import dataclass
from functools import cache
from pathlib import Path

from trainerlab import _engine, _search
from trainerlab.bots.roster import ALPHAZERO, factory
from trainerlab.experiments import parallel
from trainerlab.game.decisions import Decision, decisions
from trainerlab.rl.planner import cpp_network

NETWORK = ALPHAZERO  # rule 12's
SIMULATIONS = 3200
WORLDS = 16
MIN_VISITS = 50
INACCURACY = 0.05  # a loss of 5 points of the chance to win or more
BLUNDER = 0.10
MOVES = 4


@dataclass(frozen=True)
class Judged:
    seed: int
    a_side: int
    turn: int
    outcome: str  # how bot A's battle ended: win, draw or loss
    chosen: int  # the option played, numbered as in the encoding: moves 0-3, switches 4-9
    best: int  # the judge's: the option it visited most
    loss: float  # in points of the chance to win, 0 to 1
    active: str  # the Pokemon that chose, and the one in front of it
    foe: str
    chosen_label: str
    best_label: str


@cache
def _network(path: str) -> _search.Network:
    return cpp_network(Path(path))


def judge_decision(d: Decision) -> Judged:
    """The judge's look at one turn of bot A with a choice."""
    network = _network(str(NETWORK))
    seed = (d.seed * 2 + d.a_side) * 1000 + d.turn
    legal = [a for a in d.legal if isinstance(a, _engine.Action)]
    visits, value = network.judge(d.view, d.side, legal, seed, SIMULATIONS, WORLDS, MIN_VISITS)
    assert isinstance(d.choice, _engine.Action)
    chosen = _key(d.choice)
    best = int(visits.argmax())
    loss = max(0.0, float(value[best] - value[chosen]) / 2)
    active, foe = d.view.active(d.side), d.view.active(1 - d.side)
    return Judged(
        seed=d.seed,
        a_side=d.a_side,
        turn=d.turn,
        outcome=d.outcome,
        chosen=chosen,
        best=best,
        loss=loss,
        active=_engine.species_display_name(active.species_id),
        foe=_engine.species_display_name(foe.species_id),
        chosen_label=_label(d, chosen),
        best_label=_label(d, best),
    )


def _key(action: _engine.Action) -> int:
    return action.index if action.kind == _engine.ACTION_MOVE else MOVES + action.index


def _label(d: Decision, key: int) -> str:
    if key < MOVES:
        return _engine.move_name(d.view.active(d.side).move_ids[key])
    pokemon = d.view.pokemon(d.side, key - MOVES)
    return f"switch to {_engine.species_display_name(pokemon.species_id)}"


def judge_block(bot_a: str, bot_b: str, seeds: range) -> list[Judged]:
    """Every turn with a choice of bot A over these pairs, judged: a process of its own."""
    return [
        judge_decision(d)
        for d in decisions(factory(bot_a), factory(bot_b), seeds=seeds)
        if d.by_a and d.kind == "action" and len(d.legal) > 1
    ]


def judge_on_cores(
    bot_a: str, bot_b: str, seeds: range, jobs: int, progress: parallel.Progress | None = None
) -> list[Judged]:
    """`judge_block` over these seeds, spread over `jobs` processes, in the order of the seeds."""
    return [
        j for part in parallel.run(judge_block, bot_a, bot_b, seeds, jobs, progress) for j in part
    ]


@dataclass(frozen=True)
class Summary:
    decisions: int
    agreement: float  # share of decisions where the bot played the judge's choice
    mean_loss: float
    inaccuracies: float  # share of decisions losing INACCURACY or more (blunders included)
    blunders: float  # share losing BLUNDER or more
    loss_in_won: float  # mean loss over the decisions of battles bot A won
    loss_in_lost: float


def summarize(judged: list[Judged]) -> Summary:
    def mean(values: list[float]) -> float:
        return sum(values) / len(values) if values else math.nan

    losses = [j.loss for j in judged]
    return Summary(
        decisions=len(judged),
        agreement=mean([float(j.chosen == j.best) for j in judged]),
        mean_loss=mean(losses),
        inaccuracies=mean([float(loss >= INACCURACY) for loss in losses]),
        blunders=mean([float(loss >= BLUNDER) for loss in losses]),
        loss_in_won=mean([j.loss for j in judged if j.outcome == "win"]),
        loss_in_lost=mean([j.loss for j in judged if j.outcome == "loss"]),
    )
