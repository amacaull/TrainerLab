"""Every decision of a series of battles, replayed: the data of error analysis and of learning.

The engine and the bots are deterministic, so two bots and a seed define a pair of battles
entirely. Nothing is stored: the same call gives the same decisions, as long as the engine and
the bots are the same code. A dataset is therefore named by its bots, its seeds and a commit.
"""

from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from typing import Literal, NamedTuple

from trainerlab import _engine
from trainerlab.battle import play_battle
from trainerlab.bots import Bot
from trainerlab.environment import random_team_pair
from trainerlab.harness import BotFactory, Outcome, TeamSource, outcome_of, pair_battles

Kind = Literal["action", "replacement"]


@dataclass(frozen=True)
class Decision:
    """One choice a bot made, with what it saw, what it could do, and how its battle ended."""

    seed: int
    a_side: int  # the side bot A played in this battle
    side: int  # the side that decided
    turn: int
    kind: Kind
    view: _engine.BattleState  # what the deciding side saw: observe(side)
    legal: tuple[_engine.Action, ...] | tuple[int, ...]  # actions, or team indices to send in
    choice: _engine.Action | int
    winner: int | None  # None for a draw

    @property
    def by_a(self) -> bool:
        return self.side == self.a_side

    @property
    def outcome(self) -> Outcome:
        """How the battle ended for the side that decided."""
        return outcome_of(self.winner, self.side)


def decisions(
    make_a: BotFactory,
    make_b: BotFactory,
    *,
    seeds: Iterable[int],
    teams: TeamSource = random_team_pair,
) -> Iterator[Decision]:
    """Plays the pairs of these seeds as a tournament would, and gives every decision in order.

    Decisions come battle by battle, once each battle is over: a long series streams instead of
    filling the memory.
    """
    for seed in seeds:
        team0, team1 = teams(seed)
        for a_side, env, bot0, bot1 in pair_battles(
            make_a, make_b, seed=seed, team0=team0, team1=team1
        ):
            log: list[_Entry] = []
            winner = play_battle(env, _Recorder(bot0, log), _Recorder(bot1, log))
            for entry in log:
                yield Decision(
                    seed=seed,
                    a_side=a_side,
                    side=entry.side,
                    turn=entry.view.turn,
                    kind=entry.kind,
                    view=entry.view,
                    legal=entry.legal,
                    choice=entry.choice,
                    winner=winner,
                )


class _Entry(NamedTuple):
    kind: Kind
    side: int
    view: _engine.BattleState
    legal: tuple[_engine.Action, ...] | tuple[int, ...]
    choice: _engine.Action | int


class _Recorder:
    """Plays as the bot it wraps, and writes down each of its choices."""

    def __init__(self, bot: Bot, log: list[_Entry]) -> None:
        self._bot = bot
        self._log = log

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        choice = self._bot.choose_action(side, view, legal)
        self._log.append(_Entry("action", side, view, tuple(legal), choice))
        return choice

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        choice = self._bot.choose_replacement(side, view, options)
        self._log.append(_Entry("replacement", side, view, tuple(options), choice))
        return choice
