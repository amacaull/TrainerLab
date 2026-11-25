from pathlib import Path

import numpy as np
import pytest
import torch

from trainerlab import _engine
from trainerlab.experiments import arena, building, teams
from trainerlab.game.building import (
    Builder,
    SpeciesBuilder,
    cores,
    draw_pool,
    legal,
    random_builder,
    species,
)
from trainerlab.game.environment import DATA_DIR
from trainerlab.rl.team_net import PAD, TraitNet


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
    assert species.ranked == SpeciesBuilder(building.species_scores(path)).ranked
    with pytest.raises(ValueError, match="random, species:FILE.npz or net:FILE.pt"):
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
        return lambda core, size, seed, pool: [*core, f"{name}{seed}", *pool[:1]]

    monkeypatch.setattr(building, "builder_of", mark)

    result = building.play_block("a", "b", range(5, 7), bot="any", size=4, core=None, pool=12)

    core0, core1 = cores(5, 4)
    pool = draw_pool(5, 12, 4, (core0, core1))
    first = pool[0]  # the builders see the pool of the pair, the same for both sides
    assert built[0] == ([*core0, "a10", first], [*core1, "b11", first], 5)
    assert built[1] == ([*core0, "b10", first], [*core1, "a11", first], 5)
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

    building.play_block("random", "random", range(1), bot="any", size=2, core=["Gyarados"], pool=12)

    assert built == [["Gyarados"]] * 4


def test_free_choice_plays_the_teams_of_palier_12_1(monkeypatch: pytest.MonkeyPatch) -> None:
    built: list[tuple[list[str], list[str]]] = []

    class Env:
        def __init__(self, team0: list[str], team1: list[str], *, seed: int) -> None:
            built.append((team0, team1))

    monkeypatch.setattr(building, "BattleEnvironment", Env)
    monkeypatch.setattr(building, "factory", lambda bot: lambda seed: seed)
    monkeypatch.setattr(building, "play_battle", lambda env, b0, b1: 0)

    building.play_block("random", "random", range(3, 4), bot="any", size=4, core=None, pool=49)

    core0, core1 = cores(3, 4)
    every = draw_pool(3, 49, 4, (core0, core1))
    assert built[0] == (random_builder(core0, 4, 6, every), random_builder(core1, 4, 7, every))


def test_a_builders_duel_adds_up_its_blocks() -> None:
    result = building.duel("random", "random", range(4), 1, bot="hitter", size=2, pool=12)

    assert result.games == 8


def test_a_search_bot_plays_the_builders_battles_in_cpp(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    scores = "species:" + str(_scores_file(tmp_path))
    given: list[tuple[list[str], list[str], int]] = []

    def play(bot: object, battles: list, jobs: int, done: object = None) -> list:  # type: ignore[type-arg]
        given.extend(battles)
        if callable(done):
            done(len(battles))
        return [0] * len(battles)  # side 0 wins

    monkeypatch.setattr(arena, "play", play)
    parts: list[object] = []

    result = building.duel(
        "random",
        scores,
        range(5, 7),
        2,
        bot="alphazero",
        size=4,
        pool=12,
        on_part=parts.append,
    )

    core0, core1 = cores(5, 4)
    pool = draw_pool(5, 12, 4, (core0, core1))
    species = building.builder_of(scores)
    assert given[0] == (random_builder(core0, 4, 10, pool), species(core1, 4, 11, pool), 5)
    assert given[1] == (species(core0, 4, 10, pool), random_builder(core1, 4, 11, pool), 5)
    # A wins on side 0, loses on side 1, in both pairs.
    assert (result.wins, result.draws, result.losses) == (2, 0, 2)
    assert len(parts) == 1

    told: list[int] = []

    class Meter:
        def update(self, done: int) -> None:
            told.append(done)

        def finish(self) -> None:
            told.append(-1)

    building.duel(
        "random", "random", range(3), 1, bot="alphazero", size=2, pool=12, progress=Meter()
    )
    assert told == [3, -1]


class _ByIds(torch.nn.Module):
    """A team as strong as the sum of its species' engine ids: the highest ids are best."""

    def forward(self, species: torch.Tensor) -> torch.Tensor:
        return torch.where(species == PAD, 0, species).sum(dim=1).float()


def _ids(names: list[str]) -> list[int]:
    _engine.engine_init(str(DATA_DIR))
    return [_engine.find_species_id(name) for name in names]


def test_the_net_builder_completes_the_core_with_the_team_the_model_rates_best() -> None:
    build = building.NetBuilder(_ByIds())
    pool = ["Blissey", "Toxapex", "Snorlax", "Azumarill", "MegaGengar", "MegaSableye"]

    team = build(["MegaMawile"], 3, 0, pool)

    # The two highest ids among the pool, a second Mega aside.
    allowed = [n for n in pool if not n.startswith("Mega")]
    best = sorted(allowed, key=lambda n: -_ids([n])[0])[:2]
    assert team[0] == "MegaMawile" and sorted(team[1:]) == sorted(best)


def test_with_every_species_to_choose_from_the_builder_searches_a_beam() -> None:
    build = building.NetBuilder(_ByIds())

    team = build(["Gyarados"], 6, 0, species())

    assert legal(team) and len(team) == 6 and team[0] == "Gyarados"
    # Strengths that add up: the best legal team is the highest ids, one Mega and one legendary
    # at most; the beam must find it.
    _engine.engine_init(str(DATA_DIR))

    def kind(name: str) -> tuple[bool, bool]:
        entry = _engine.species_entry(_engine.find_species_id(name))
        return entry.mega, entry.legendary

    others = [n for n in species() if n != "Gyarados"]
    plain = sorted((n for n in others if kind(n) == (False, False)), key=lambda n: -_ids([n])[0])
    best: tuple[int, list[str]] = (-1, [])
    choices: list[list[str]] = [[]] + [[m] for m in others if kind(m) != (False, False)]
    choices += [
        [m, legend]
        for m in others
        for legend in others
        if kind(m) == (True, False) and kind(legend) == (False, True)
    ]
    for extra in choices:
        picked = extra + plain[: 5 - len(extra)]
        best = max(best, (sum(_ids(picked)), picked))
    assert sorted(team[1:]) == sorted(best[1])


def test_the_builder_s_quick_rule_is_the_engine_s() -> None:
    build = building.NetBuilder(_ByIds())
    rng = np.random.default_rng(0)
    every = species()
    for _ in range(500):
        team = [every[i] for i in rng.choice(len(every), size=4, replace=False)]
        assert build.legal(team) == legal(team)


def test_a_net_builder_is_named_by_its_file(tmp_path: Path) -> None:
    path = tmp_path / "evaluator.pt"
    TraitNet().save(path)

    found = building.builder_of(f"net:{path}")

    assert isinstance(found, building.NetBuilder)
    assert building.builder_of(f"net:{path}") is found  # loaded once
