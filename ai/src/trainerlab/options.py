"""The options the commands share: two bots, how many pairs of battles, and the first seed."""

import argparse

from trainerlab.roster import BOTS


def count(text: str) -> int:
    """A number of pairs: a whole number, at least 1."""
    if not text.isdigit() or int(text) < 1:
        raise argparse.ArgumentTypeError(f"not a number of pairs: {text!r} (e.g. 500)")
    return int(text)


def seed(text: str) -> int:
    if not text.isdigit():
        raise argparse.ArgumentTypeError(f"not a seed: {text!r} (e.g. 30000)")
    return int(text)


def show(seeds: range) -> str:
    return f"{seeds[0]}-{seeds[-1]}"


def seeds_of(args: argparse.Namespace) -> range:
    """The seeds of the pairs asked for: one pair per seed, from --from upwards."""
    return range(args.first_seed, args.first_seed + args.pairs)


def add_bots(parser: argparse.ArgumentParser) -> None:
    names = ", ".join(BOTS)
    parser.add_argument(
        "bot_a", choices=list(BOTS), metavar="BOT_A", help=f"the bot measured: {names}"
    )
    parser.add_argument("bot_b", choices=list(BOTS), metavar="BOT_B", help="its opponent")


def add_pairs(parser: argparse.ArgumentParser, default: int, what: str = "to play") -> None:
    parser.add_argument(
        "--pairs",
        type=count,
        default=default,
        help=f"pairs of battles {what}, two battles each, sides swapped (default: {default})",
    )
    parser.add_argument(
        "--from",
        dest="first_seed",
        type=seed,
        default=0,
        metavar="SEED",
        help="the seed of the first pair, the next ones follow (default: 0)",
    )
