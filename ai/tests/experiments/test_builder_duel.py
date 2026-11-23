from pathlib import Path

import numpy as np
import pytest

from trainerlab.experiments import building, teams
from trainerlab.game.building import Builder, SpeciesBuilder, cores, random_builder


def _scores_file(tmp_path: Path) -> Path:
    seeds, points = teams.record("random", range(30), jobs=1, size=2)
    path = tmp_path / "teams-2.npz"
    np.savez(path, seed=seeds, points=points, size=2)
    return path


def test_the_species_scores_are_those_of_a_teams_measure(tmp_path: Path) -> None:
    path = _scores_file(tmp_path)
    saved = np.load(path)

    scores = building.species_scores(path)

    expected = teams.analyze(saved["seed"], saved["points"], size=2).species
    assert scores == {s.name: s.score for s in expected}


def test_a_builder_is_named_random_or_species_with_its_file(tmp_path: Path) -> None:
    path = _scores_file(tmp_path)

    assert building.builder_of("random") is random_builder
    species = building.builder_of(f"species:{path}")
    assert isinstance(species, SpeciesBuilder)
    assert species.ranked == sorted(
        building.species_scores(path), key=lambda n: -building.species_scores(path)[n]
    )
    with pytest.raises(ValueError, match="random, or species:FILE.npz"):
        building.builder_of("best")


def test_in_a_pair_the_cores_stay_and_the_builders_swap_sides(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    built: list[tuple[list[str], list[str], int]] = []
    winners = iter([0, 0, None, 1])

    class Env:
        def __init__(self, team0: list[str], team1: list[str], *, seed: int) -> None:
            built.append((team0, team1, seed))

    monkeypatch.setattr(building, "BattleEnvironment", Env)
    monkeypatch.setattr(building, "factory", lambda bot: lambda seed: seed)
    monkeypatch.setattr(building, "play_battle", lambda env, b0, b1: next(winners))

    def mark(name: str) -> Builder:
        return lambda core, size, seed: [*core, f"{name}{seed}"]

    monkeypatch.setattr(building, "builder_of", mark)

    result = building.play_block("a", "b", range(5, 7), bot="any", size=3, core=None)

    core0, core1 = cores(5, 3)
    assert built[0] == ([*core0, "a10"], [*core1, "b11"], 5)
    assert built[1] == ([*core0, "b10"], [*core1, "a11"], 5)
    # Seed 5: A wins on side 0, loses on side 1. Seed 6: a draw, then A wins on side 1.
    assert (result.wins, result.draws, result.losses) == (2, 1, 1)


def test_a_core_given_is_both_sides_core(monkeypatch: pytest.MonkeyPatch) -> None:
    built: list[list[str]] = []

    class Env:
        def __init__(self, team0: list[str], team1: list[str], *, seed: int) -> None:
            built.extend([team0[:1], team1[:1]])

    monkeypatch.setattr(building, "BattleEnvironment", Env)
    monkeypatch.setattr(building, "factory", lambda bot: lambda seed: seed)
    monkeypatch.setattr(building, "play_battle", lambda env, b0, b1: 0)

    building.play_block("random", "random", range(1), bot="any", size=2, core=["Gyarados"])

    assert built == [["Gyarados"]] * 4


def test_a_builders_duel_adds_up_its_blocks() -> None:
    result = building.duel("random", "random", range(4), 1, bot="hitter", size=2)

    assert result.games == 8
