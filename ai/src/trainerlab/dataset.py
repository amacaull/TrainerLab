"""Command line entry point: turns the decisions of a series of battles into a learning dataset.

    trainerlab-dataset outlook survival --pairs 2000 --first-seed 40000 --out outlook-40000.npz

Each decision becomes one row: features of what the deciding side saw, from its point of view
("my" side against the "foe"), and as label the score of its battle for that side: 1 for a win,
0.5 for a draw, 0 for a loss. Rows of one battle are far from independent, so the file keeps the
seed of each row: a train/test split must go by seed, never by row.
"""

import argparse
import sys
from collections.abc import Callable, Iterable
from functools import cache
from pathlib import Path
from typing import Any

import numpy as np
import numpy.typing as npt

from trainerlab import _engine, estimate
from trainerlab.decisions import Decision, decisions
from trainerlab.environment import DATA_DIR
from trainerlab.tournament import BOTS

Write = Callable[[str], None]
Features = npt.NDArray[np.float32]

STATUSES = ["burn", "poison", "toxic", "paralysis", "sleep", "freeze"]  # 1-6, 0 is none
STAGES = ["atk", "def", "spa", "spd", "spe", "accuracy", "evasion"]
WEATHERS = ["rain", "sun", "sand", "snow", "strong_winds"]  # 1-5, 0 is none
SCORE = {"win": 1.0, "draw": 0.5, "loss": 0.0}


@cache
def feature_names() -> tuple[str, ...]:
    """The name of each column of `features`, in order."""
    _engine.engine_init(str(DATA_DIR))
    species = [_engine.species_id_string(i) for i in range(_engine.species_count())]
    names = ["replacement", "turn", "my_faster", "my_best_damage", "foe_best_damage"]
    names += [f"weather_{w}" for w in WEATHERS] + ["terrain_electric"]
    for p in ("my", "foe"):
        names += [f"{p}_active_hp"]
        names += [f"{p}_active_{s}" for s in STATUSES]
        names += [f"{p}_active_stage_{s}" for s in STAGES]
        names += [f"{p}_alive", f"{p}_hp_total", f"{p}_hidden"]
        names += [f"{p}_stealth_rock", f"{p}_spikes", f"{p}_toxic_spikes", f"{p}_aurora_veil"]
        names += [f"{p}_active_is_{s}" for s in species]
        names += [f"{p}_hp_of_{s}" for s in species]
    return tuple(names)


@cache
def _index() -> dict[str, int]:
    return {name: i for i, name in enumerate(feature_names())}


def features(view: _engine.BattleState, side: int) -> Features:
    """What `side` saw, as numbers between 0 and 1 (stat stages between -1 and 1).

    A foe Pokemon not yet seen is counted as alive at full HP, which it is, without its species.
    """
    index = _index()
    x = np.zeros(len(index), dtype=np.float32)
    me, foe = view.active(side), view.active(1 - side)

    x[index["replacement"]] = me.is_fainted()
    x[index["turn"]] = view.turn / 100
    lead = estimate.effective_speed(me) - estimate.effective_speed(foe)
    x[index["my_faster"]] = 1.0 if lead > 0 else 0.0 if lead < 0 else 0.5
    x[index["my_best_damage"]] = _best_damage(view, side)
    x[index["foe_best_damage"]] = _best_damage(view, 1 - side)
    if view.weather:
        x[index[f"weather_{WEATHERS[view.weather - 1]}"]] = 1
    x[index["terrain_electric"]] = view.terrain != 0

    for p, s in (("my", side), ("foe", 1 - side)):
        active = view.active(s)
        x[index[f"{p}_active_hp"]] = active.current_hp / active.stats.hp
        if active.status:
            x[index[f"{p}_active_{STATUSES[active.status - 1]}"]] = 1
        for stat, stage in zip(STAGES, active.stat_stages, strict=True):
            x[index[f"{p}_active_stage_{stat}"]] = stage / 6
        x[index[f"{p}_active_is_{_engine.species_id_string(active.species_id)}"]] = 1
        alive = hidden = 0
        hp_total = 0.0
        for i in range(view.team_size[s]):
            pokemon = view.pokemon(s, i)
            if pokemon.is_empty():
                hidden += 1
                continue
            share = pokemon.current_hp / pokemon.stats.hp
            alive += share > 0
            hp_total += share
            x[index[f"{p}_hp_of_{_engine.species_id_string(pokemon.species_id)}"]] = share
        x[index[f"{p}_alive"]] = (alive + hidden) / _engine.TEAM_SIZE
        x[index[f"{p}_hp_total"]] = (hp_total + hidden) / _engine.TEAM_SIZE
        x[index[f"{p}_hidden"]] = hidden / _engine.TEAM_SIZE
        hazards = view.hazards(s)
        x[index[f"{p}_stealth_rock"]] = hazards.stealth_rock
        x[index[f"{p}_spikes"]] = hazards.spikes / 3
        x[index[f"{p}_toxic_spikes"]] = hazards.toxic_spikes / 2
        x[index[f"{p}_aurora_veil"]] = view.aurora_veil_turns[s] / 8
    return x


def _best_damage(view: _engine.BattleState, side: int) -> float:
    """The highest damage the engine expects from this side's active, as a share of the foe's HP."""
    foe = view.active(1 - side)
    if foe.is_fainted():
        return 0.0
    attacker = view.active(side)
    slots = [i for i, move in enumerate(attacker.move_ids) if move != _engine.NO_MOVE]
    best = max(_engine.expected_damage(view, side, slot).mean for slot in slots)
    return best / foe.current_hp


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


def _stdout(text: str) -> None:
    sys.stdout.write(text)


def main(argv: list[str] | None = None, write: Write = _stdout) -> int:
    parser = argparse.ArgumentParser(description="Turn the decisions of battles into a dataset.")
    parser.add_argument("bot_a", choices=sorted(BOTS))
    parser.add_argument("bot_b", choices=sorted(BOTS))
    parser.add_argument("--pairs", type=int, default=500)
    parser.add_argument("--first-seed", type=int, default=0)
    parser.add_argument("--out", type=Path, required=True, help="the .npz file to write")
    args = parser.parse_args(argv)

    seeds = range(args.first_seed, args.first_seed + args.pairs)
    data = build(decisions(BOTS[args.bot_a], BOTS[args.bot_b], seeds=seeds))
    origin = {
        "bots": np.array([args.bot_a, args.bot_b]),
        "catalog_fingerprint": np.uint64(_engine.catalog_fingerprint()),
    }
    payload: dict[str, Any] = {**data, **origin}  # Any: numpy's stub also matches allow_pickle
    np.savez_compressed(args.out, **payload)
    rows, width = data["x"].shape
    write(
        f"{args.bot_a} against {args.bot_b}, seeds {seeds[0]}-{seeds[-1]}: "
        f"{rows} decisions x {width} features, mean label {data['y'].mean():.3f} -> {args.out}\n"
    )
    return 0
