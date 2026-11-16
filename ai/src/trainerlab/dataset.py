"""`trainerlab dataset`: turns the decisions of a series of battles into a learning dataset.

    trainerlab dataset calculator survivor --pairs 2000 --from 40000

Each decision becomes one row: features of what the deciding side saw, from its point of view
("my" side against the "foe"), and as label the score of its battle for that side: 1 for a win,
0.5 for a draw, 0 for a loss. Rows of one battle are far from independent, so the file keeps the
seed of each row: a train/test split must go by seed, never by row.
"""

import argparse
import sys
from collections.abc import Callable, Iterable
from pathlib import Path
from typing import Any

import numpy as np
import numpy.typing as npt

from trainerlab import _engine
from trainerlab.decisions import Decision, decisions
from trainerlab.features import Features, feature_names, features
from trainerlab.options import add_bots, add_pairs, seeds_of, show
from trainerlab.roster import BOTS

Write = Callable[[str], None]

SCORE = {"win": 1.0, "draw": 0.5, "loss": 0.0}


def build(series: Iterable[Decision]) -> dict[str, npt.NDArray[np.generic]]:
    """One row per decision: features `x`, label `y` (the score), and where the row comes from."""
    rows: list[Features] = []
    y, seed, side, a_side, turn = [], [], [], [], []
    for d in series:
        rows.append(features(d.view, d.side))
        y.append(SCORE[d.outcome])
        seed.append(d.seed)
        side.append(d.side)
        a_side.append(d.a_side)
        turn.append(d.turn)
    width = len(feature_names())
    return {
        "x": np.stack(rows) if rows else np.zeros((0, width), dtype=np.float32),
        "y": np.array(y, dtype=np.float32),
        "seed": np.array(seed, dtype=np.int64),
        "side": np.array(side, dtype=np.int8),
        "a_side": np.array(a_side, dtype=np.int8),
        "turn": np.array(turn, dtype=np.int32),
        "feature_names": np.array(feature_names()),
    }


def default_name(bot_a: str, bot_b: str, seeds: range) -> str:
    return f"{bot_a}-vs-{bot_b}-{show(seeds)}.npz"


def _stdout(text: str) -> None:
    sys.stdout.write(text)


def main(argv: list[str] | None = None, write: Write = _stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab dataset", description="Turn the decisions of battles into a dataset."
    )
    add_bots(parser)
    add_pairs(parser, 500)
    parser.add_argument(
        "--out", type=Path, help="the .npz file to write (default: BOT_A-vs-BOT_B-SEEDS.npz)"
    )
    args = parser.parse_args(argv)

    seeds = seeds_of(args)
    out = args.out or Path(default_name(args.bot_a, args.bot_b, seeds))
    data = build(decisions(BOTS[args.bot_a], BOTS[args.bot_b], seeds=seeds))
    origin = {
        "bots": np.array([args.bot_a, args.bot_b]),
        "catalog_fingerprint": np.uint64(_engine.catalog_fingerprint()),
    }
    payload: dict[str, Any] = {**data, **origin}  # Any: numpy's stub also matches allow_pickle
    np.savez_compressed(out, **payload)
    rows, width = data["x"].shape
    write(
        f"{args.bot_a} against {args.bot_b}, seeds {show(seeds)}: "
        f"{rows} decisions x {width} features, mean label {data['y'].mean():.3f} -> {out}\n"
    )
    return 0
