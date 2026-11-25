"""The battles of a measure played in C++, side by side, when one search bot plays both sides.

A measure of teams (teams, build, truth) plays the same bot on both sides. When it searches with a
network (rule 11 and after), its battles go to C++ at once: each thread keeps 16 of them side by
side, the next taking the place of one that ends, and evaluates what they all wait on in one
batch, as self-play does (palier 8.3), without Python between the turns (palier 12.3). They play
as the Python referee does with the same seeds (search/include/search/self_play.hpp, playMatches);
only the searches' seeds come from the battle rather than from the bot's own draws, so a battle is
not the one the Python path played.

A battle is (team of side 0, team of side 1, its seed). TRAINERLAB_ONE_BY_ONE=1 keeps every
battle in Python, one at a time, as before palier 12.3: to time both, or to replay a battle the
Python path played.
"""

import os
from collections.abc import Callable, Sequence

from trainerlab import _engine
from trainerlab.bots.roster import Searcher, searcher
from trainerlab.rl.planner import REPLACEMENT_SHARE, cpp_network

LANES = 16  # battles side by side on a thread

Battle = tuple[list[str], list[str], int]


def searcher_of(bot: str) -> Searcher | None:
    """The search bot a measure plays in C++, side by side; None: in Python, one by one."""
    if os.environ.get("TRAINERLAB_ONE_BY_ONE") == "1":
        return None
    return searcher(bot)


def play(
    bot: Searcher,
    battles: Sequence[Battle],
    jobs: int,
    done: Callable[[int], None] | None = None,
) -> list[int | None]:
    """The winner of each battle, in order (None for a draw); `done` hears how many are over, at
    once and then every second. Ctrl-C stops them."""
    if not battles:
        return []
    network = cpp_network(bot.network)  # it starts the engine
    replacing = max(2, bot.simulations // REPLACEMENT_SHARE) if bot.replacements else 0

    def ids(team: list[str]) -> list[int]:
        return [_engine.find_species_id(name) for name in team]

    if done is not None:
        done(0)
    won, _ = network.play_matches(
        [ids(team0) for team0, _, _ in battles],
        [ids(team1) for _, team1, _ in battles],
        [seed for _, _, seed in battles],
        jobs,
        bot.simulations,
        replacing,
        LANES,
        progress=done,
    )
    if done is not None:
        done(len(battles))
    return [None if w < 0 else int(w) for w in won]
