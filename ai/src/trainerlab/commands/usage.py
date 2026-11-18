"""`trainerlab usage`: what a bot does with its turns, and what it could have done.

trainerlab usage forecaster calculator --pairs 200 --from 96000
"""

import argparse

from trainerlab.bots.roster import factory
from trainerlab.commands.console import stdout
from trainerlab.commands.options import add_bots, add_pairs, seeds_of
from trainerlab.experiments.usage import KINDS, usage
from trainerlab.game.pairs import span
from trainerlab.terminal.screen import Write

PAIRS = 200


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab usage", description="Count the kinds of moves a bot plays."
    )
    add_bots(parser)
    add_pairs(parser, PAIRS)
    args = parser.parse_args(argv)

    seeds = seeds_of(args)
    result = usage(factory(args.bot_a), factory(args.bot_b), seeds=seeds)
    write(
        f"{args.bot_a} against {args.bot_b}, seeds {span(seeds)}: "
        f"{result.turns} turns by {args.bot_a}.\n\n"
    )
    write(f"{'kind':<10}{'an option':>12}{'played':>10}{'when an option':>16}\n")
    for kind in KINDS:
        available, played = result.available[kind], result.played[kind]
        when = f"{100 * played / available:.1f}%" if available else "-"
        write(
            f"{kind:<10}{_share(available, result.turns):>12}"
            f"{_share(played, result.turns):>10}{when:>16}\n"
        )
    return 0


def _share(part: int, whole: int) -> str:
    return f"{100 * part / whole:.1f}%" if whole else "-"
