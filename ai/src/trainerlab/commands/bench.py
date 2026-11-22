"""`trainerlab bench`: how fast the bots play from Python.

trainerlab bench               # about 3 s per line
trainerlab bench --seconds 1

The same lines in C++, through the engine's own API: search/build/search_bench.
"""

import argparse

from trainerlab.commands.console import Write, stdout
from trainerlab.experiments.bench import measure


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab bench", description="Measure how fast the bots play from Python."
    )
    parser.add_argument(
        "--seconds", type=float, default=3.0, help="time spent on each line (default: 3)"
    )
    args = parser.parse_args(argv)

    for line in measure(args.seconds):
        write(f"{line.what:<44} {line.per_second:>12.0f} {line.unit}/s\n")
    return 0
