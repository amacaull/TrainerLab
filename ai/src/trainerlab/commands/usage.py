"""`trainerlab usage`: what a bot does with its turns, and what it could have done.

trainerlab usage forecaster calculator --pairs 200 --from 96000
"""

import argparse
from collections.abc import Iterator

from rich.console import Console

from trainerlab.bots.roster import factory
from trainerlab.commands.options import add_bots, add_pairs, seeds_of
from trainerlab.experiments.usage import usage
from trainerlab.i18n import t
from trainerlab.terminal import reports
from trainerlab.terminal.progress import Bar, progress_bar

PAIRS = 200
ORDER = ["attack", "switch", "boost", "status", "recovery", "hazard", "other"]


def _counted(seeds: range, bar: Bar) -> Iterator[int]:
    for done, seed in enumerate(seeds):
        bar.update(done)
        yield seed
    bar.update(len(seeds))


def main(argv: list[str] | None = None, *, console: Console | None = None) -> int:
    parser = argparse.ArgumentParser(prog="trainerlab usage", description=t("command.usage"))
    add_bots(parser)
    add_pairs(parser, PAIRS)
    args = parser.parse_args(argv)
    console = console or Console(highlight=False)

    seeds = seeds_of(args)
    with progress_bar(console, t("duel.progress"), len(seeds)) as bar:
        result = usage(factory(args.bot_a), factory(args.bot_b), seeds=_counted(seeds, bar))
    console.print(
        reports.usage_report(
            args.bot_a,
            args.bot_b,
            len(seeds),
            result.turns,
            ORDER,
            result.available,
            result.played,
        )
    )
    return 0
