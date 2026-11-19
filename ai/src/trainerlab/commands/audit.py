"""`trainerlab audit`: counts the mistakes of a bot that a program can spot on its own.

trainerlab audit survivor matcher --pairs 1000 --from 20000

The strategic checks come after the first ones; the boost checks also as a share of the boosts.
"""

import argparse

from trainerlab.commands.console import quiet_on_closed_pipe, stdout
from trainerlab.commands.options import add_bots, add_jobs, add_pairs, seeds_of
from trainerlab.experiments import parallel
from trainerlab.experiments.audit import CHECKS, audit_on_cores
from trainerlab.game.pairs import span
from trainerlab.terminal.screen import Write


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab audit", description="Count a bot's mistakes a program can spot."
    )
    add_bots(parser)
    add_pairs(parser)
    add_jobs(parser)
    args = parser.parse_args(argv)
    if write is stdout:
        quiet_on_closed_pipe()

    seeds = seeds_of(args)
    progress = parallel.progress_on_terminal(len(seeds))
    result = audit_on_cores(args.bot_a, args.bot_b, seeds, args.jobs, progress)
    won, drawn, lost = (result.battles[o] for o in ("win", "draw", "loss"))
    write(
        f"{args.bot_a} against {args.bot_b}, seeds {span(seeds)}: "
        f"{won + drawn + lost} battles ({won} won, {drawn} drawn, {lost} lost), "
        f"{result.decisions} decisions by {args.bot_a}.\n\n"
    )
    write(f"{'check':<24}{'per 1000 decisions':>20}{'in lost':>10}{'in won':>10}\n")
    for name, count in result.checks.items():
        if name not in CHECKS and name == "boost":
            write("strategic\n")
        rate = 1000 * count.occurrences / result.decisions
        write(
            f"{name:<24}{rate:>20.1f}"
            f"{_share(count.battles['loss'], lost):>10}{_share(count.battles['win'], won):>10}\n"
        )
    boosts = result.checks["boost"].occurrences
    shares = ", ".join(
        f"{name.removeprefix('boost-')} {_share(result.checks[name].occurrences, boosts)}"
        for name in ("boost-wasted", "boost-into-phazer", "boost-into-ko")
    )
    write(f"\nof the {boosts} boosts: {shares}\n")
    return 0


def _share(part: int, whole: int) -> str:
    return f"{100 * part / whole:.1f}%" if whole else "-"
