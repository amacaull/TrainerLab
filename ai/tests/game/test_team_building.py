import pytest

from trainerlab import _engine
from trainerlab.game.building import (
    POOL,
    SpeciesBuilder,
    bot_team,
    completable,
    cores,
    draw_pool,
    legal,
    random_builder,
    species,
    strengths,
)
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
    team = random_builder(["Gyarados"], 4, 3, species())

    assert team[0] == "Gyarados" and len(set(team)) == 4 and legal(team)
    assert random_builder(["Gyarados"], 4, 3, species()) == team
    assert any(random_builder(["Gyarados"], 4, s, species()) != team for s in range(4, 10))


def test_the_species_builder_adds_the_best_scored_species_it_may() -> None:
    scores = {"MegaSableye": 0.9, "Blissey": 0.8, "Toxapex": 0.7, "Snorlax": 0.6}
    build = SpeciesBuilder(scores)

    # Mega Sableye would be a second Mega: skipped.
    assert build(["MegaMawile"], 3, 0, species()) == ["MegaMawile", "Blissey", "Toxapex"]
    assert build(["Blissey"], 2, 0, species()) == ["Blissey", "MegaSableye"]


def test_the_species_builder_needs_enough_scored_species() -> None:
    with pytest.raises(ValueError, match="2 scored species cannot complete a team of 4"):
        SpeciesBuilder({"Blissey": 0.8, "Toxapex": 0.7})(["Gyarados"], 4, 0, species())


def test_a_pool_is_drawn_from_the_seed_without_the_cores() -> None:
    both = (["Gyarados"], ["Snorlax", "Blissey"])
    pool = draw_pool(7, 12, 6, both)

    assert len(set(pool)) == 12 and pool == draw_pool(7, 12, 6, both)
    assert pool != draw_pool(8, 12, 6, both)
    assert not {"Gyarados", "Snorlax", "Blissey"} & set(pool)
    assert draw_pool(7, 49, 6, both) == species()  # free choice: every species


def test_a_mega_or_a_legendary_in_a_core_leaves_none_in_the_pool() -> None:
    def kinds(pool: list[str]) -> tuple[bool, bool]:
        entries = [_engine.species_entry(_engine.find_species_id(name)) for name in pool]
        return any(e.mega for e in entries), any(e.legendary for e in entries)

    for seed in range(30):
        assert kinds(draw_pool(seed, 12, 6, (["MegaMawile"], ["Gyarados"])))[0] is False
        assert kinds(draw_pool(seed, 12, 6, (["Gyarados"], ["Azelf"])))[1] is False
    found = [kinds(draw_pool(seed, 12, 6, (["Gyarados"], ["Snorlax"]))) for seed in range(30)]
    assert any(mega for mega, _ in found) and any(legend for _, legend in found)


def test_every_core_can_be_completed_from_its_pool() -> None:
    # Five species for five places: a second Mega or legendary in the draw is drawn again.
    for seed in range(30):
        pool = draw_pool(seed, 5, 6, (["Gyarados"], ["Snorlax"]))
        assert completable(["Gyarados"], pool, 6) and completable(["Snorlax"], pool, 6)


def test_a_core_may_leave_no_legal_team_in_the_pool() -> None:
    megas = ["MegaBanette", "MegaGengar", "MegaSableye", "MegaSceptile"]

    assert not completable(["MegaMawile"], megas, 2)
    assert completable(["Gyarados"], megas, 2)


def test_the_builders_pick_in_the_pool_only() -> None:
    pool = ["Blissey", "Toxapex", "Snorlax", "Azumarill"]
    scores = {"Mimikyu": 0.9, "Toxapex": 0.7, "Snorlax": 0.6, "Blissey": 0.5}

    assert SpeciesBuilder(scores)(["Gyarados"], 3, 0, pool) == ["Gyarados", "Toxapex", "Snorlax"]
    for seed in range(10):
        assert set(random_builder(["Gyarados"], 3, seed, pool)[1:]) <= set(pool)


def test_a_species_never_seen_is_ranked_last() -> None:
    build = SpeciesBuilder({"Blissey": float("nan"), "Toxapex": 0.4, "Snorlax": 0.3})

    assert build.ranked == ["Toxapex", "Snorlax", "Blissey"]


def test_the_species_builder_keeps_the_team_completable() -> None:
    # Mega Rayquaza, a Mega and a legendary, would leave neither Mega Gengar nor Azelf.
    scores = {"MegaRayquaza": 0.9, "MegaGengar": 0.8, "Azelf": 0.7}
    pool = list(scores)

    assert SpeciesBuilder(scores)(["Gyarados"], 3, 0, pool) == ["Gyarados", "MegaGengar", "Azelf"]


def test_every_species_has_a_shipped_strength() -> None:
    assert set(strengths()) == set(species())
    assert all(0 < score < 1 for score in strengths().values())


def test_the_bot_completes_its_core_from_a_pool_with_the_species_strengths() -> None:
    both = cores(5, 6)
    pool = draw_pool(5, POOL, 6, both)

    team = bot_team(5, 6)

    assert team == SpeciesBuilder(strengths())(both[1], 6, 5, pool)
    assert legal(team) and bot_team(5, 6) == team
    assert len({tuple(bot_team(seed, 6)) for seed in range(10)}) > 5  # not the same team each time


def test_alone_the_bot_s_pokemon_is_its_core() -> None:
    assert bot_team(5, 1) == cores(5, 1)[1]
