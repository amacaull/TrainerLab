"""A pair of battles: the unit of every measurement.

A pair is two battles with the same seed and the same two teams, the bots swapping sides: each
bot plays both teams and both sides under the same luck, which cancels most of the variance.
"""

from collections.abc import Callable, Iterator
from typing import Literal

from trainerlab.game.battle import play_battle
from trainerlab.game.environment import BattleEnvironment
from trainerlab.game.player import Bot

Outcome = Literal["win", "draw", "loss"]

# Builds a fresh bot from a seed, so that every battle starts from a known state.
BotFactory = Callable[[int], Bot]

# Gives the two teams of the pair played with a seed.
TeamSource = Callable[[int], tuple[list[str], list[str]]]


def span(seeds: range) -> str:
    """The seeds of a series of pairs, as written in every report: 30000-34999."""
    return f"{seeds[0]}-{seeds[-1]}"


def pair_battles(
    make_a: BotFactory,
    make_b: BotFactory,
    *,
    seed: int,
    team0: list[str],
    team1: list[str],
    follow: bool = False,
) -> Iterator[tuple[int, BattleEnvironment, Bot, Bot]]:
    """The two battles of a pair, ready to play: bot A's side, the referee, the bots of sides 0, 1.
    The teams stay on their sides; with `follow`, they follow the bots: A plays team0 on both
    sides, B team1 (palier 14: each level its own kind of team).

    The one place that decides how a seed sets up a battle, so that anything replaying a
    tournament plays exactly the battles it measured.
    """
    for a_side in (0, 1):
        bot_a, bot_b = make_a(2 * seed), make_b(2 * seed + 1)
        bot0, bot1 = (bot_a, bot_b) if a_side == 0 else (bot_b, bot_a)
        first, second = (team1, team0) if follow and a_side == 1 else (team0, team1)
        env = BattleEnvironment(led(bot0, first), led(bot1, second), seed=seed)
        yield a_side, env, bot0, bot1


def led(bot: Bot, team: list[str]) -> list[str]:
    """The team, the Pokemon the bot leads with first if it chooses (palier 13.1); as given
    otherwise."""
    choose = getattr(bot, "choose_lead", None)
    if choose is None:
        return team
    lead: int = choose(team)
    return [team[lead], *team[:lead], *team[lead + 1 :]]


def play_pair(
    make_a: BotFactory,
    make_b: BotFactory,
    *,
    seed: int,
    team0: list[str],
    team1: list[str],
    follow: bool = False,
) -> tuple[Outcome, Outcome]:
    """Plays one pair of battles and returns bot A's outcome in each."""
    outcomes = [
        outcome_of(play_battle(env, bot0, bot1), a_side)
        for a_side, env, bot0, bot1 in pair_battles(
            make_a, make_b, seed=seed, team0=team0, team1=team1, follow=follow
        )
    ]
    return outcomes[0], outcomes[1]


def outcome_of(winner: int | None, side: int) -> Outcome:
    """How a battle ended for `side`, given the winning side (None for a draw)."""
    if winner is None:
        return "draw"
    return "win" if winner == side else "loss"
