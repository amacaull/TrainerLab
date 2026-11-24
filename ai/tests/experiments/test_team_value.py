from pathlib import Path

import numpy as np
import pytest
import torch

from trainerlab import _engine
from trainerlab.experiments import team_value, teams, truth
from trainerlab.game.environment import DATA_DIR, random_team_pair
from trainerlab.rl.team_net import PAD


@pytest.fixture(autouse=True)
def engine() -> None:
    _engine.engine_init(str(DATA_DIR))


def _ids(team: list[str]) -> list[int]:
    return [_engine.find_species_id(name) for name in team] + [PAD] * (6 - len(team))


def _truth_file(tmp_path: Path, size: int, count: int, pairs: int, first: int) -> Path:
    index, points = truth.record("random", first=first, pairs=pairs, jobs=1, count=count, size=size)
    path = tmp_path / f"truth-{size}.npz"
    names = np.array(truth.teams_of(size, count))
    np.savez(path, team=index, points=points, first=first, pairs=pairs, names=names)
    return path


def _teams_file(tmp_path: Path, size: int, seeds: range) -> Path:
    seed, points = teams.record("random", seeds, jobs=1, size=size)
    path = tmp_path / f"teams-{size}.npz"
    np.savez(path, seed=seed, points=points, size=size)
    return path


def test_a_truth_file_gives_each_team_against_its_foes(tmp_path: Path) -> None:
    path = _truth_file(tmp_path, 2, count=3, pairs=2, first=50)

    battles = team_value.read([path])

    assert len(battles.y) == 12
    team, foe = truth.teams_of(2, 3)[0], random_team_pair(50, 2)[0]
    assert battles.a[0].tolist() == _ids(team) and battles.b[0].tolist() == _ids(foe)
    assert battles.a[1].tolist() == _ids(team) and battles.b[1].tolist() == _ids(foe)
    assert battles.b[2].tolist() == _ids(random_team_pair(51, 2)[0])
    assert battles.y.tolist() == np.load(path)["points"].tolist()
    assert battles.group.tolist() == [0] * 4 + [1] * 4 + [2] * 4
    assert battles.names[1] == truth.teams_of(2, 3)[1]
    assert set(battles.size.tolist()) == {2}


def test_a_teams_file_gives_its_battles_the_fifth_seeds_held_out(tmp_path: Path) -> None:
    path = _teams_file(tmp_path, 4, range(10, 16))

    battles = team_value.read([path])

    t0, t1 = random_team_pair(10, 4)
    assert battles.a[0].tolist() == _ids(t0) and battles.b[0].tolist() == _ids(t1)
    assert battles.group.tolist() == [-1] * 12
    assert battles.test.tolist() == [
        s % 5 == 0 for s in (10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15)
    ]


def test_a_fifth_of_the_measured_teams_are_held_out_with_every_battle_they_play() -> None:
    names = truth.teams_of(2, None)
    held = team_value.held_out(names)

    assert len(held) == round(0.2 * len(names)) and held == team_value.held_out(names)
    assert held <= {tuple(sorted(t)) for t in names}


def test_a_battle_is_held_out_if_either_team_is(tmp_path: Path) -> None:
    path = _truth_file(tmp_path, 2, count=40, pairs=1, first=0)
    battles = team_value.read([path])
    held = team_value.held_out(truth.teams_of(2, 40))

    for k in range(len(battles.y)):
        a = tuple(sorted(n for n in battles.names[battles.group[k]]))
        assert battles.test[k] == (a in held or team_value.key(battles.b[k]) in held)


class _Constant(torch.nn.Module):
    """Every team as strong as its first species' id: the lower ids win."""

    def forward(self, species: torch.Tensor) -> torch.Tensor:
        return -species[:, 0].float()


def test_the_report_scores_held_out_battles_and_teams_by_size(tmp_path: Path) -> None:
    battles = team_value.read(
        [_truth_file(tmp_path, 2, count=60, pairs=2, first=0), _teams_file(tmp_path, 4, range(20))]
    )

    report = team_value.report(_Constant(), battles)

    sizes = [row.size for row in report.battles]
    assert sizes == [2, 4]
    assert all(0 <= row.accuracy <= 1 and row.log_loss > 0 for row in report.battles)
    assert report.battles[1].count == int(battles.test[battles.size == 4].sum())
    (row,) = report.teams
    assert row.size == 2 and row.count == len(
        team_value.held_out(truth.teams_of(2, 60))
        & {tuple(sorted(t)) for t in truth.teams_of(2, 60)}
    )
    assert -1 <= row.rank_correlation <= 1 and row.error >= 0


def test_the_rank_correlation_of_two_orders() -> None:
    assert team_value.rank_correlation(np.array([1.0, 2, 3]), np.array([10.0, 20, 30])) == 1
    assert team_value.rank_correlation(np.array([1.0, 2, 3]), np.array([3.0, 2, 1])) == -1
