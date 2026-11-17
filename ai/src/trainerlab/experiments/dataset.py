"""The decisions of a series of battles, turned into a learning dataset.

Each decision becomes one row: features of what the deciding side saw, from its point of view
("my" side against the "foe"), and as label the score of its battle for that side: 1 for a win,
0.5 for a draw, 0 for a loss. Rows of one battle are far from independent, so the file keeps the
seed of each row: a train/test split must go by seed, never by row.
"""

from collections.abc import Iterable
from pathlib import Path
from typing import Any

import numpy as np
import numpy.typing as npt

from trainerlab import _engine
from trainerlab.bots.roster import factory, slug
from trainerlab.experiments import parallel
from trainerlab.game.decisions import Decision, decisions
from trainerlab.game.environment import DATA_DIR
from trainerlab.game.pairs import span
from trainerlab.learning.features import Features, feature_names, features

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


def build_block(bot_a: str, bot_b: str, seeds: range) -> dict[str, npt.NDArray[np.generic]]:
    """The rows of one block of pairs; a process of its own when they are spread over the cores."""
    return build(decisions(factory(bot_a), factory(bot_b), seeds=seeds))


def default_name(bot_a: str, bot_b: str, seeds: range) -> str:
    return f"{slug(bot_a)}-vs-{slug(bot_b)}-{span(seeds)}.npz"


def generate(
    bot_a: str,
    bot_b: str,
    seeds: range,
    jobs: int,
    progress: parallel.Progress | None = None,
) -> dict[str, npt.NDArray[np.generic]]:
    """Every decision of the pairs of these seeds, played on `jobs` processes, as rows."""
    parts = parallel.run(build_block, bot_a, bot_b, seeds, jobs, progress)
    data = {key: np.concatenate([p[key] for p in parts]) for key in parts[0]}
    data["feature_names"] = parts[0]["feature_names"]
    return data


def save(path: Path, data: dict[str, npt.NDArray[np.generic]], bots: tuple[str, str]) -> None:
    """Writes the rows with where they come from: the two bots, and the catalog they used."""
    # The battles may all have been played in other processes: this one loads the catalog itself.
    _engine.engine_init(str(DATA_DIR))
    origin = {
        "bots": np.array(bots),
        "catalog_fingerprint": np.uint64(_engine.catalog_fingerprint()),
    }
    payload: dict[str, Any] = {**data, **origin}  # Any: numpy's stub also matches allow_pickle
    np.savez_compressed(path, **payload)
