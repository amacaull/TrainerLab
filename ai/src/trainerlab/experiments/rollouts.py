"""Palier 7.6: is a blunder of the judge one? Both options, played out to the end.

The judge rates decisions with a search guided by rule 12's network, and could share that
network's blind spots. Its worst blunders are checked here against results: the battle is replayed
to the decision, its real state copied, and played on to the end `games` times with the option the
bot chose and as many times with the judge's, by the same two bots. The k-th game of each option
rolls the same dice and seeds the bots alike, so that the two options differ in nothing but the
choice. The real state holds what the bot could not see (the foe's unseen Pokemon): this measures
what each option would have brought in that very battle, the same for both options.

A blunder is confirmed if the 95 % interval of the gap (the judge's option's share of points
minus the bot's) lies above 0, refuted if below, unclear otherwise.
"""

import math
import multiprocessing
from concurrent.futures import ProcessPoolExecutor, as_completed
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from trainerlab import _engine
from trainerlab.bots.roster import factory
from trainerlab.experiments import parallel
from trainerlab.experiments.tournament import Z_95
from trainerlab.game.battle import play_battle
from trainerlab.game.environment import BattleEnvironment, random_team_pair
from trainerlab.game.pairs import outcome_of, pair_battles
from trainerlab.game.player import Bot

MOVES = 4
POINTS = {"win": 1.0, "draw": 0.5, "loss": 0.0}
SEEDS = 7_000_000  # the rollouts' seeds start here, far from every series of pairs


class _ReachedError(Exception):
    def __init__(self, state: _engine.BattleState) -> None:
        self.state = state


class _StopAt:
    """Plays as bot A until it is asked to choose at `turn`: then copies the real state, stops."""

    def __init__(self, bot: Bot, env: BattleEnvironment, turn: int) -> None:
        self._bot, self._env, self._turn = bot, env, turn

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        if view.turn == self._turn:
            raise _ReachedError(self._env.snapshot())
        return self._bot.choose_action(side, view, legal)

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        return self._bot.choose_replacement(side, view, options)


def position(bot_a: str, bot_b: str, *, seed: int, a_side: int, turn: int) -> _engine.BattleState:
    """The real state of that battle of the pair when bot A chose its action at `turn`."""
    team0, team1 = random_team_pair(seed)
    for side, env, bot0, bot1 in pair_battles(
        factory(bot_a), factory(bot_b), seed=seed, team0=team0, team1=team1
    ):
        if side != a_side:
            continue
        bots = [bot0, bot1]
        bots[a_side] = _StopAt(bots[a_side], env, turn)
        try:
            play_battle(env, bots[0], bots[1])
        except _ReachedError as reached:
            return reached.state
    raise ValueError(f"no decision of bot A at turn {turn} in seed {seed}, side {a_side}")


def _observe(state: _engine.BattleState, side: int) -> _engine.BattleState:
    return _engine.observe(state, side)


def _action(key: int) -> _engine.Action:
    return _engine.Action.move(key) if key < MOVES else _engine.Action.switch(key - MOVES)


def rollout(
    bot_a: str, bot_b: str, state: _engine.BattleState, a_side: int, key: int, *, seed: int
) -> float:
    """Bot A's points from `state`: it plays option `key` now, then both bots play to the end."""
    env = BattleEnvironment.resume(state, seed=SEEDS + seed)
    bots = [factory(bot_a)(SEEDS + 2 * seed), factory(bot_b)(SEEDS + 2 * seed + 1)]
    if a_side == 1:
        bots.reverse()
    foe = 1 - a_side
    actions = [_action(key), _action(key)]
    actions[foe] = bots[foe].choose_action(foe, env.observe(foe), env.legal_actions(foe))
    env.play_turn(actions[0], actions[1])
    return POINTS[outcome_of(play_battle(env, bots[0], bots[1]), a_side)]


@dataclass(frozen=True)
class Blunder:
    seed: int
    a_side: int
    turn: int
    chosen: int
    best: int
    loss: float  # as the judge rated it, in points of the chance to win


@dataclass(frozen=True)
class Verification:
    seed: int
    a_side: int
    turn: int
    chosen: int
    best: int
    loss: float
    gap: float  # the judge's option's share of points minus the bot's, over the games
    low: float
    high: float
    games: int
    verdict: str  # confirmed, refuted or unclear


def interval(differences: list[float]) -> tuple[float, float]:
    """The 95 % interval of the mean of these paired differences."""
    n = len(differences)
    mean = sum(differences) / n
    sd = math.sqrt(sum((d - mean) ** 2 for d in differences) / (n - 1)) if n > 1 else 0.0
    half = Z_95 * sd / math.sqrt(n)
    return mean - half, mean + half


def verdict_of(low: float, high: float) -> str:
    if low > 0:
        return "confirmed"
    if high < 0:
        return "refuted"
    return "unclear"


def verify(bot_a: str, bot_b: str, blunder: Blunder, games: int) -> Verification:
    """Both options of one blunder, played out `games` times each with the same dice."""
    state = position(bot_a, bot_b, seed=blunder.seed, a_side=blunder.a_side, turn=blunder.turn)
    differences = [
        rollout(bot_a, bot_b, state, blunder.a_side, blunder.best, seed=k)
        - rollout(bot_a, bot_b, state, blunder.a_side, blunder.chosen, seed=k)
        for k in range(games)
    ]
    low, high = interval(differences)
    return Verification(
        blunder.seed, blunder.a_side, blunder.turn, blunder.chosen, blunder.best, blunder.loss,
        sum(differences) / games, low, high, games, verdict_of(low, high),
    )  # fmt: skip


def verify_all(
    bot_a: str,
    bot_b: str,
    blunders: list[Blunder],
    games: int,
    jobs: int,
    progress: parallel.Meter | None = None,
) -> list[Verification]:
    """`verify` on each blunder, one process each on `jobs` cores, in the order given."""
    results: dict[int, Verification] = {}
    if progress is not None:
        progress.update(0)
    if jobs == 1:
        for i, b in enumerate(blunders):
            results[i] = verify(bot_a, bot_b, b, games)
            if progress is not None:
                progress.update(len(results))
    else:
        context = multiprocessing.get_context("spawn")
        with ProcessPoolExecutor(min(jobs, len(blunders)), mp_context=context) as pool:
            futures = {
                pool.submit(verify, bot_a, bot_b, b, games): i for i, b in enumerate(blunders)
            }
            for future in as_completed(futures):
                results[futures[future]] = future.result()
                if progress is not None:
                    progress.update(len(results))
    if progress is not None:
        progress.finish()
    return [results[i] for i in range(len(blunders))]


def worst_of(judged: Path, count: int) -> list[Blunder]:
    """The `count` decisions the judge rated worst, in a file `trainerlab judge` wrote."""
    saved = np.load(judged)
    order = np.argsort(-saved["loss"], kind="stable")[:count]
    return [
        Blunder(
            int(saved["seed"][i]),
            int(saved["a_side"][i]),
            int(saved["turn"][i]),
            int(saved["chosen"][i]),
            int(saved["best"][i]),
            float(saved["loss"][i]),
        )  # fmt: skip
        for i in order
    ]


@dataclass(frozen=True)
class Summary:
    confirmed: float  # shares of the blunders checked
    refuted: float
    mean_loss: float  # what the judge said they lose, on average
    mean_gap: float  # what playing them out says


def summarize(checked: list[Verification]) -> Summary:
    if not checked:
        return Summary(0.0, 0.0, math.nan, math.nan)
    n = len(checked)
    return Summary(
        sum(v.verdict == "confirmed" for v in checked) / n,
        sum(v.verdict == "refuted" for v in checked) / n,
        sum(v.loss for v in checked) / n,
        sum(v.gap for v in checked) / n,
    )
