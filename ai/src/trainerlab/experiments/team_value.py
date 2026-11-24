"""What the strength of a team is learned from, and how well it is learned (palier 12.4).

The battles come from the measures' files: `trainerlab truth` (each measured team against random
teams of its size, a team's points) and `trainerlab teams` (two random teams, the first's points).
Each battle is a row: team a, team b, a's points.

Held out, decided before learning: a fifth of the teams each truth file measures, drawn from its
list, with every battle where either side is one of them; and the battles of the teams files whose
seed is a multiple of 5, as in experiments/teams.py. A model is judged on what it never saw:
per team size, how often it calls the winner and its log loss on held-out battles; and for the
held-out teams of a truth file, how well its expected score ranks them against their measured one.
"""

import random
from collections.abc import Sequence
from dataclasses import dataclass
from functools import cache
from pathlib import Path

import numpy as np
import numpy.typing as npt
import torch

from trainerlab import _engine
from trainerlab.experiments.truth import teams_of
from trainerlab.game.environment import DATA_DIR, random_team_pair
from trainerlab.rl.team_net import PAD, TEAM, log_loss, win_probability

HELD_OUT = 0.2  # of the teams a truth file measures
TEST_EVERY = 5  # the teams files' held-out seeds
BATCH = 8192  # rows per evaluation

Ints = npt.NDArray[np.int64]


@dataclass(frozen=True)
class Battles:
    a: Ints  # rows x 6: team a's species ids, PAD after its last
    b: Ints
    y: npt.NDArray[np.float32]  # a's points: 1, 0.5, 0
    size: Ints  # the teams' size
    test: npt.NDArray[np.bool_]  # held out
    group: Ints  # the measured team a is, in `names`; -1 for a teams file's battle
    names: list[list[str]]  # the measured teams of the truth files, in order


@cache
def _names() -> list[str]:
    _engine.engine_init(str(DATA_DIR))
    return [_engine.species_id_string(i) for i in range(_engine.species_count())]


def _ids(team: Sequence[str]) -> list[int]:
    index = {name: i for i, name in enumerate(_names())}
    return [index[name] for name in team] + [PAD] * (TEAM - len(team))


def key(team: Sequence[str] | Ints) -> tuple[str, ...]:
    """A team whatever its order: its species' names, sorted (from names or from a row of ids)."""
    if isinstance(team, np.ndarray):
        team = [_names()[i] for i in team if i != PAD]
    return tuple(sorted(team))


def held_out(names: list[list[str]]) -> set[tuple[str, ...]]:
    """A fifth of these measured teams, the same for the same list."""
    size = len(names[0])
    drawn = random.Random(f"held-out-{size}").sample(names, round(HELD_OUT * len(names)))
    return {key(team) for team in drawn}


def _truth_names(saved: np.lib.npyio.NpzFile) -> list[list[str]]:
    # Files of palier 12.2 keep no names: every team of two, in order.
    return [list(t) for t in saved["names"]] if "names" in saved else teams_of(2, None)


def read(paths: list[Path]) -> Battles:
    """Every battle of these files, as rows."""
    files = [np.load(path) for path in paths]
    held: set[tuple[str, ...]] = set()
    for saved in files:
        if "first" in saved:
            held |= held_out(_truth_names(saved))
    a: list[list[int]] = []
    b: list[list[int]] = []
    y: list[float] = []
    size: list[int] = []
    test: list[bool] = []
    group: list[int] = []
    names: list[list[str]] = []
    for saved in files:
        if "first" in saved:
            listed = _truth_names(saved)
            first = int(saved["first"])  # pair k // 2 of the file plays seed first + k // 2
            for k, (team, points) in enumerate(zip(saved["team"], saved["points"], strict=True)):
                mine = listed[int(team)]
                foe = random_team_pair(first + k // 2, len(mine))[0]
                a.append(_ids(mine))
                b.append(_ids(foe))
                y.append(float(points))
                size.append(len(mine))
                test.append(key(mine) in held or key(foe) in held)
                group.append(len(names) + int(team))
            names += listed
        else:
            teams_size = int(saved["size"]) if "size" in saved else TEAM
            for seed, points in zip(saved["seed"], saved["points"], strict=True):
                first_team, second_team = random_team_pair(int(seed), teams_size)
                a.append(_ids(first_team))
                b.append(_ids(second_team))
                y.append(float(points))
                size.append(teams_size)
                test.append(
                    int(seed) % TEST_EVERY == 0
                    or key(first_team) in held
                    or key(second_team) in held
                )
                group.append(-1)
    return Battles(
        np.array(a, dtype=np.int64),
        np.array(b, dtype=np.int64),
        np.array(y, dtype=np.float32),
        np.array(size, dtype=np.int64),
        np.array(test, dtype=np.bool_),
        np.array(group, dtype=np.int64),
        names,
    )


@dataclass(frozen=True)
class BattleRow:
    size: int
    count: int  # held-out battles
    accuracy: float  # winners called, draws aside
    log_loss: float


@dataclass(frozen=True)
class TeamRow:
    size: int
    count: int  # held-out measured teams
    rank_correlation: float  # of their expected and measured scores
    error: float  # mean gap between the two, in points of score


@dataclass(frozen=True)
class Report:
    battles: list[BattleRow]
    teams: list[TeamRow]


def rank_correlation(x: npt.NDArray[np.float64], y: npt.NDArray[np.float64]) -> float:
    """Spearman's: the correlation of the two ranks."""
    rx, ry = np.argsort(np.argsort(x)), np.argsort(np.argsort(y))
    return float(np.corrcoef(rx, ry)[0, 1])


def chances(model: torch.nn.Module, a: Ints, b: Ints) -> npt.NDArray[np.float64]:
    """The chance that team a beats team b, per row."""
    out = []
    with torch.no_grad():
        for start in range(0, len(a), BATCH):
            ta = torch.from_numpy(a[start : start + BATCH])
            tb = torch.from_numpy(b[start : start + BATCH])
            out.append(win_probability(model(ta), model(tb)).numpy())
    return np.concatenate(out).astype(np.float64) if out else np.zeros(0)


def report(model: torch.nn.Module, battles: Battles) -> Report:
    """How well the model does on what was held out."""
    test = battles.test
    p = chances(model, battles.a[test], battles.b[test])
    y = battles.y[test].astype(np.float64)
    sizes = battles.size[test]
    rows = []
    for size in sorted(set(sizes.tolist())):
        mine = sizes == size
        decided = mine & (y != 0.5)
        accuracy = float(((p[decided] > 0.5) == (y[decided] == 1)).mean()) if decided.any() else 0.0
        loss = log_loss(torch.from_numpy(p[mine]), torch.from_numpy(y[mine]))
        rows.append(BattleRow(size, int(mine.sum()), accuracy, loss))

    # The held-out measured teams: every battle of theirs held out (a team whose foe alone was
    # held out had its other battles learned).
    groups = battles.group[test]
    known = battles.group >= 0
    played = np.bincount(battles.group[known], minlength=len(battles.names))
    seen = np.bincount(groups[groups >= 0], minlength=len(battles.names))
    expected_sum = np.bincount(groups[groups >= 0], weights=p[groups >= 0], minlength=len(played))
    measured_sum = np.bincount(groups[groups >= 0], weights=y[groups >= 0], minlength=len(played))
    held: dict[int, tuple[list[float], list[float]]] = {}
    for g in np.flatnonzero((seen == played) & (played > 0)):
        expected, measured = held.setdefault(len(battles.names[g]), ([], []))
        expected.append(float(expected_sum[g] / seen[g]))
        measured.append(float(measured_sum[g] / seen[g]))
    team_rows = []
    for size, (expected, measured) in sorted(held.items()):
        e, m = np.array(expected), np.array(measured)
        team_rows.append(TeamRow(size, len(e), rank_correlation(e, m), float(np.abs(e - m).mean())))
    return Report(rows, team_rows)


def describe(report: Report, model: str) -> list[str]:
    """The report's lines, for the model named."""
    lines = [
        f"  {model:<10} teams of {row.size}: {row.count} held-out battles, winner called "
        f"{row.accuracy:.3f}, log loss {row.log_loss:.3f}"
        for row in report.battles
    ]
    lines += [
        f"  {model:<10} {row.count} held-out measured teams of {row.size}: rank correlation "
        f"{row.rank_correlation:.3f}, mean error {row.error:.3f}"
        for row in report.teams
    ]
    return [line + "\n" for line in lines]
