import pytest

from trainerlab import _engine
from trainerlab.game.building import SpeciesBuilder, cores, legal, random_builder
from trainerlab.game.environment import DATA_DIR


@pytest.fixture(autouse=True)
def engine() -> None:
    _engine.engine_init(str(DATA_DIR))


def test_a_team_is_legal_with_one_mega_at_most() -> None:
    assert legal(["Gyarados", "MegaMawile"])
    assert not legal(["MegaMawile", "MegaSableye"])


def test_each_side_gets_a_core_of_one_or_two_drawn_from_the_seed() -> None:
    assert cores(7, 6) == cores(7, 6)
    sizes = set()
    for seed in range(40):
        for core in cores(seed, 6):
            assert legal(core) and len(set(core)) == len(core)
            sizes.add(len(core))
    assert sizes == {1, 2}


def test_a_core_leaves_a_choice_to_make() -> None:
    assert all(len(core) == 1 for seed in range(20) for core in cores(seed, 2))


def test_the_random_builder_completes_the_core_into_a_legal_team() -> None:
    team = random_builder(["Gyarados"], 4, seed=3)

    assert team[0] == "Gyarados" and len(set(team)) == 4 and legal(team)
    assert random_builder(["Gyarados"], 4, seed=3) == team
    assert any(random_builder(["Gyarados"], 4, seed=s) != team for s in range(4, 10))


def test_the_species_builder_adds_the_best_scored_species_it_may() -> None:
    scores = {"MegaSableye": 0.9, "Blissey": 0.8, "Toxapex": 0.7, "Snorlax": 0.6}
    build = SpeciesBuilder(scores)

    # Mega Sableye would be a second Mega: skipped.
    assert build(["MegaMawile"], 3, seed=0) == ["MegaMawile", "Blissey", "Toxapex"]
    assert build(["Blissey"], 2, seed=0) == ["Blissey", "MegaSableye"]


def test_the_species_builder_needs_enough_scored_species() -> None:
    with pytest.raises(ValueError, match="2 scored species cannot complete a team of 4"):
        SpeciesBuilder({"Blissey": 0.8, "Toxapex": 0.7})(["Gyarados"], 4, seed=0)
