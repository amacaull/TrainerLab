"""The decisions of a bot, encoded for the network: what it saw, what it could do, what it chose,
and how its battle ended. The data the network first learns from, by imitation.

Only bot A's decisions are kept: the bot imitated. Each row holds the encoding of what it saw
(learning/encoding.py), the mask of its options, the option it chose (0-9), the result of its
battle for it (1 a win, 0 a draw, -1 a loss) and the seed, so that train and test can be split by
battle. The Pokemon's numbers are stored in float16: one-hot flags and shares lose nothing that
matters, and the file is four times smaller.
"""

from pathlib import Path
from typing import Any

import numpy as np
import numpy.typing as npt

from trainerlab import _engine
from trainerlab.bots.roster import factory, slug
from trainerlab.experiments import parallel
from trainerlab.game.decisions import decisions
from trainerlab.game.pairs import span
from trainerlab.learning.encoding import ACTIONS, action_index, action_mask, encode

MOVES = 4
RESULT = {"win": 1.0, "draw": 0.0, "loss": -1.0}

Rows = dict[str, npt.NDArray[Any]]


def build_block(bot_a: str, bot_b: str, seeds: range) -> Rows:
    """The rows of one block of pairs; a process of its own when spread over the cores."""
    pokemon, ids, field, mask, action, result, seed = [], [], [], [], [], [], []
    for d in decisions(factory(bot_a), factory(bot_b), seeds=seeds):
        if not d.by_a:
            continue
        e = encode(d.view, d.side)
        pokemon.append(e.pokemon.astype(np.float16))
        ids.append(e.ids.astype(np.int16))
        field.append(e.field)
        if d.kind == "action":
            assert isinstance(d.choice, _engine.Action)
            mask.append(action_mask([a for a in d.legal if isinstance(a, _engine.Action)]))
            action.append(action_index(d.choice))
        else:
            options = np.zeros(ACTIONS, dtype=np.float32)
            options[[MOVES + i for i in d.legal if isinstance(i, int)]] = 1
            mask.append(options)
            assert isinstance(d.choice, int)
            action.append(MOVES + d.choice)
        result.append(RESULT[d.outcome])
        seed.append(d.seed)
    return {
        "pokemon": np.stack(pokemon),
        "ids": np.stack(ids),
        "field": np.stack(field),
        "mask": np.stack(mask),
        "action": np.array(action, dtype=np.int8),
        "result": np.array(result, dtype=np.float32),
        "seed": np.array(seed, dtype=np.int64),
    }


def generate(
    bot_a: str, bot_b: str, seeds: range, jobs: int, progress: parallel.Progress | None = None
) -> Rows:
    """Bot A's decisions over the pairs of these seeds, played on `jobs` processes."""
    parts = parallel.run(build_block, bot_a, bot_b, seeds, jobs, progress)
    return {key: np.concatenate([p[key] for p in parts]) for key in parts[0]}


def default_name(bot_a: str, bot_b: str, seeds: range) -> str:
    return f"imitate-{slug(bot_a)}-vs-{slug(bot_b)}-{span(seeds)}.npz"


def save(path: Path, rows: Rows) -> None:
    payload: dict[str, Any] = dict(rows)  # Any: numpy's stub also matches allow_pickle
    np.savez_compressed(path, **payload)
