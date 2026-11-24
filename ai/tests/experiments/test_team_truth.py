import math

import numpy as np
import pytest

from trainerlab.experiments import arena, truth
from trainerlab.game.building import legal
from trainerlab.game.environment import random_team_pair


def test_every_legal_team_of_two_is_listed_once() -> None:
    teams = truth.every_team()

    assert len(teams) == 1093
    assert all(legal(team) for team in teams)
    assert len({tuple(sorted(team)) for team in teams}) == 1093


def test_each_team_plays_its_pairs_against_random_teams_on_both_sides(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    built: list[tuple[list[str], list[str], int]] = []
    winners = iter([0, 0, None, 1])

    class Env:
        def __init__(self, first: list[str], second: list[str], *, seed: int) -> None:
            built.append((first, second, seed))

    monkeypatch.setattr(truth, "BattleEnvironment", Env)
    monkeypatch.setattr(truth, "factory", lambda bot: lambda seed: seed)
    monkeypatch.setattr(truth, "play_battle", lambda env, b0, b1: next(winners))

    # Seeds 100-101: the second team of the list (pairs=1 each, from seed 99).
    points = truth.play_block("any", "", range(100, 102), first=99, pairs=1)

    team, foe = truth.every_team()[1], random_team_pair(100, 2)[0]
    assert built[:2] == [(team, foe, 200), (foe, team, 201)]
    assert built[2][0] == truth.every_team()[2]
    # The team wins on side 0, loses on side 1 (side 0 wins); a draw, then a win on side 1.
    assert points == [1.0, 0.0, 0.5, 1.0]


def test_record_lists_each_team_s_battles_in_order() -> None:
    index, points = truth.record("random", first=0, pairs=2, jobs=1, count=2)

    assert index.tolist() == [0, 0, 0, 0, 1, 1, 1, 1]
    assert len(points) == 8


def test_the_analysis_separates_the_teams_from_the_dice() -> None:
    index = np.repeat(np.arange(3), 4)
    points = np.array([1, 1, 1, 0, 0.5, 0.5, 0.5, 0.5, 0, 0, 0, 1], dtype=float)

    result = truth.analyze(index, points)

    assert [t.score for t in result.teams] == [0.75, 0.5, 0.25]
    assert result.teams[0].names == truth.every_team()[0]
    assert result.teams[0].low < 0.75 < result.teams[0].high
    assert result.spread == pytest.approx(np.std([0.75, 0.5, 0.25]))
    # The error of a score over n battles, s(1 - s) / n, as if every battle were won or lost.
    noise = math.sqrt(np.mean([0.75 * 0.25 / 4, 0.5 * 0.5 / 4, 0.25 * 0.75 / 4]))
    assert result.noise == pytest.approx(noise)
    assert result.explained == pytest.approx(1 - noise**2 / result.spread**2)


def test_a_search_bot_plays_every_team_s_battles_in_cpp(monkeypatch: pytest.MonkeyPatch) -> None:
    given: list[tuple[list[str], list[str], int]] = []

    def play(bot: object, battles: list, jobs: int, done: object = None) -> list:  # type: ignore[type-arg]
        given.extend(battles)
        if callable(done):
            done(len(battles))
        return [0] * len(battles)  # side 0 wins

    monkeypatch.setattr(arena, "play", play)
    told: list[int] = []

    class Meter:
        def update(self, done: int) -> None:
            told.append(done)

        def finish(self) -> None:
            told.append(-1)

    index, points = truth.record("alphazero", first=40, pairs=2, jobs=2, count=2, progress=Meter())

    team, foe = truth.every_team()[0], random_team_pair(40, 2)[0]
    assert given[:2] == [(team, foe, 80), (foe, team, 81)]
    assert len(given) == 8
    assert index.tolist() == [0, 0, 0, 0, 1, 1, 1, 1]
    assert points.tolist() == [1.0, 0.0] * 4
    assert told == [4, -1]
    assert truth.record("alphazero", first=0, pairs=1, jobs=1, count=1)[1].tolist() == [1.0, 0.0]
