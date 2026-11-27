"""`trainerlab duel`: plays paired battles between two bots and prints bot A's score.

trainerlab duel calculator survivor --pairs 5000 --from 30000

On a terminal: the duel in a frame, a progress bar with the score so far, then the verdict. In
a pipe or a log: one line, the same as ever, for the reports, and the progress on stderr if a
terminal watches it.
"""

import argparse
import time
from collections.abc import Callable

from rich.console import Console

from trainerlab.commands.options import add_bots, add_jobs, add_pairs, seeds_of
from trainerlab.experiments import parallel
from trainerlab.experiments.tournament import TEAMS, TournamentResult, duel
from trainerlab.game.pairs import span
from trainerlab.i18n import t
from trainerlab.terminal import reports
from trainerlab.terminal.progress import progress_bar


def kinds_of_teams(text: str) -> str:
    """Checks --teams (argparse type): one kind for both bots, or one per bot."""
    kinds = text.split(",")
    if len(kinds) > 2 or any(kind not in TEAMS for kind in kinds):
        raise argparse.ArgumentTypeError(
            f"{text!r}: random or species, or one per bot (species,random)"
        )
    return text


def on_teams(teams: str) -> str:
    """What the line of a duel says of its teams."""
    if "," in teams:
        a, b = teams.split(",")
        return f", {a} teams against {b}"
    return " on species teams" if teams == "species" else ""


def elapsed(seconds: float) -> str:
    minutes, secs = divmod(round(seconds), 60)
    return f"{minutes}:{secs:02d}"


def main(
    argv: list[str] | None = None,
    *,
    console: Console | None = None,
    clock: Callable[[], float] = time.monotonic,
) -> int:
    parser = argparse.ArgumentParser(prog="trainerlab duel", description=t("command.duel"))
    add_bots(parser)
    parser.add_argument("--teams", type=kinds_of_teams, default="random", help=t("opt.teams"))
    add_pairs(parser)
    add_jobs(parser)
    args = parser.parse_args(argv)
    console = console or Console(highlight=False)

    seeds = seeds_of(args)
    if not console.is_terminal:
        # In a pipe (| tee): the progress still goes to a terminal watching, on stderr.
        progress = parallel.progress_on_terminal(len(seeds))
        result = duel(args.bot_a, args.bot_b, seeds, args.jobs, progress, teams=args.teams)
        low, high = result.interval
        on = on_teams(args.teams)
        console.print(
            f"{args.bot_a} vs {args.bot_b}{on}: {result.score:.3f} [{low:.3f}, {high:.3f}] "
            f"({result.wins} wins, {result.draws} draws, {result.losses} losses, "
            f"seeds {span(seeds)})",
            highlight=False,
            markup=False,
        )
        return 0

    console.print(reports.duel_header(args.bot_a, args.bot_b, seeds))
    start = clock()
    so_far = [0, 0, 0]
    with progress_bar(console, t("duel.progress"), len(seeds)) as bar:

        def count(part: TournamentResult) -> None:
            so_far[0] += part.wins
            so_far[1] += part.draws
            so_far[2] += part.losses
            score = TournamentResult(*so_far).score
            bar.note(t("duel.running", bot=args.bot_a, score=score))

        result = duel(args.bot_a, args.bot_b, seeds, args.jobs, bar, count, args.teams)
    record = (result.wins, result.draws, result.losses)
    console.print(
        reports.duel_result(
            args.bot_a, args.bot_b, record, result.score, result.interval, elapsed(clock() - start)
        )
    )
    return 0
