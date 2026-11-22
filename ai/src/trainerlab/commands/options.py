"""The options the commands share: two bots, how many pairs of battles, and the first seed."""

import argparse

from trainerlab.bots.roster import BOTS, WITH_MODEL, bot_spec
from trainerlab.experiments.parallel import cores
from trainerlab.i18n import t

PAIRS = 500  # the default length of a series: about a minute on a laptop
SCANNED_PAIRS = 50  # the pairs `watch --losses` looks through by default


def count(text: str) -> int:
    """A number of pairs: a whole number, at least 1."""
    if not text.isdigit() or int(text) < 1:
        raise argparse.ArgumentTypeError(f"not a number of pairs: {text!r} (e.g. 500)")
    return int(text)


def seed(text: str) -> int:
    """A seed: a whole number, 0 or more."""
    if not text.isdigit():
        raise argparse.ArgumentTypeError(f"not a seed: {text!r} (e.g. 30000)")
    return int(text)


def seeds_of(args: argparse.Namespace) -> range:
    """The seeds of the pairs asked for: one pair per seed, from --from upwards."""
    return range(args.first_seed, args.first_seed + args.pairs)


def add_bots(parser: argparse.ArgumentParser) -> None:
    names = ", ".join(BOTS)
    parser.add_argument(
        "bot_a", type=bot_spec, metavar="BOT_A", help=t("opt.bot_a", names=names, model=WITH_MODEL)
    )
    parser.add_argument("bot_b", type=bot_spec, metavar="BOT_B", help=t("opt.bot_b"))


def add_jobs(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--jobs",
        type=count,
        default=cores(),
        help=t("opt.jobs", cores=cores()),
    )


def add_pairs(parser: argparse.ArgumentParser, default: int = PAIRS) -> None:
    parser.add_argument(
        "--pairs", type=count, default=default, help=t("opt.pairs", default=default)
    )
    parser.add_argument(
        "--from",
        dest="first_seed",
        type=seed,
        default=0,
        metavar="SEED",
        help=t("opt.from"),
    )
