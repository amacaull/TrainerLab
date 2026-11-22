"""`trainerlab audit`: counts the mistakes of a bot that a program can spot on its own.

trainerlab audit survivor matcher --pairs 1000 --from 20000

The strategic checks come after the first ones; the boost checks also as a share of the boosts.
"""

import argparse

from rich.console import Console

from trainerlab.commands.console import quiet_on_closed_pipe
from trainerlab.commands.options import add_bots, add_jobs, add_pairs, seeds_of
from trainerlab.experiments.audit import CHECKS, audit_on_cores
from trainerlab.i18n import t
from trainerlab.terminal import reports
from trainerlab.terminal.progress import progress_bar

BOOSTS = ("boost-wasted", "boost-into-phazer", "boost-into-ko")


def main(argv: list[str] | None = None, *, console: Console | None = None) -> int:
    parser = argparse.ArgumentParser(prog="trainerlab audit", description=t("command.audit"))
    add_bots(parser)
    add_pairs(parser)
    add_jobs(parser)
    args = parser.parse_args(argv)
    if console is None:
        quiet_on_closed_pipe()
    console = console or Console(highlight=False)

    seeds = seeds_of(args)
    with progress_bar(console, t("duel.progress"), len(seeds)) as bar:
        result = audit_on_cores(args.bot_a, args.bot_b, seeds, args.jobs, bar)
    rows = [
        (
            name,
            1000 * count.occurrences / result.decisions,
            count.battles["loss"],
            count.battles["win"],
            name not in CHECKS,
        )
        for name, count in result.checks.items()
        if name != "boost" and name not in BOOSTS
    ]
    boosts = {name: result.checks[name].occurrences for name in BOOSTS}
    console.print(
        reports.audit_report(
            args.bot_a,
            args.bot_b,
            len(seeds),
            {str(outcome): count for outcome, count in result.battles.items()},
            result.decisions,
            rows,
            result.checks["boost"].occurrences,
            boosts,
        )
    )
    return 0
