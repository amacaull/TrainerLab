import statistics

import pytest

from trainerlab import _engine
from trainerlab.environment import BattleEnvironment
from trainerlab.estimate import effective_speed, estimate_damage, strongest_hit

PARALYSIS = 4


def _leads(attacker: str, defender: str) -> tuple[_engine.BattlePokemon, _engine.BattlePokemon]:
    view = BattleEnvironment([attacker], [defender], seed=1).observe(0)
    return view.active(0), view.active(1)


def _without_item(pokemon: _engine.BattlePokemon) -> _engine.BattlePokemon:
    pokemon.item_consumed = 1
    return pokemon


def test_the_estimate_matches_the_engine_on_a_plain_hit() -> None:
    # Gyarados's Waterfall on Snorlax: no ability or item changes it. The engine's average
    # damage over many rolls must sit within 3 % of the estimate.
    dealt = []
    for seed in range(40):
        env = BattleEnvironment(["Gyarados"], ["Snorlax"], seed=seed)
        view = env.observe(0)
        guess = estimate_damage(view.active(0), view.active(1), 1)
        events = env.play_turn(_engine.Action.move(1), _engine.Action.move(0))  # Snorlax: Curse
        dealt += [e.i0 for e in events if e.kind == 1 and e.side == 1 and not e.flags & 2]

    assert statistics.mean(dealt) == pytest.approx(guess, rel=0.03)


def test_a_status_move_deals_no_damage() -> None:
    snorlax, blissey = _leads("Snorlax", "Blissey")

    assert estimate_damage(snorlax, blissey, 0) == 0  # Curse


def test_an_immunity_deals_no_damage() -> None:
    excadrill, gyarados = _leads("Excadrill", "Gyarados")

    assert estimate_damage(excadrill, gyarados, 0) == 0  # Earthquake on a Flying type


def test_attack_stages_scale_the_damage() -> None:
    gyarados, snorlax = _leads("Gyarados", "Snorlax")
    base = estimate_damage(gyarados, snorlax, 1)

    gyarados.stat_stages = (2, 0, 0, 0, 0, 0, 0)
    doubled = estimate_damage(gyarados, snorlax, 1)
    gyarados.stat_stages = (-1, 0, 0, 0, 0, 0, 0)
    reduced = estimate_damage(gyarados, snorlax, 1)

    # +2 doubles the attack and -1 cuts it to two thirds; the fixed "+2" of the formula keeps the
    # ratios just short of x2 and just above x2/3.
    assert 1.9 < doubled / base < 2.0
    assert 0.66 < reduced / base < 0.70


def test_defense_stages_of_the_defender_reduce_the_damage() -> None:
    gyarados, snorlax = _leads("Gyarados", "Snorlax")
    base = estimate_damage(gyarados, snorlax, 1)

    snorlax.stat_stages = (0, 2, 0, 0, 0, 0, 0)

    assert 0.5 < estimate_damage(gyarados, snorlax, 1) / base < 0.55


def test_special_moves_use_the_special_stats() -> None:
    # Starmie's Psychic is special: raising its Attack changes nothing.
    starmie, snorlax = _leads("Starmie", "Snorlax")
    base = estimate_damage(starmie, snorlax, 1)

    starmie.stat_stages = (2, 0, 0, 0, 0, 0, 0)

    assert estimate_damage(starmie, snorlax, 1) == base


@pytest.mark.parametrize(
    ("attacker", "slot", "boost"),
    [
        ("Excadrill", 2, 1.5),  # Choice Band, physical Stone Edge
        ("Inteleon", 0, 1.5),  # Choice Specs, special Hydro Pump
        ("Inteleon", 3, 1.0),  # Choice Specs does not boost the physical U-turn
        ("Infernape", 0, 1.3),  # Life Orb, physical Close Combat
        ("Azelf", 1, 1.3),  # Life Orb, special Psyshock
    ],
)
def test_held_items_multiply_the_damage(attacker: str, slot: int, boost: float) -> None:
    holder, snorlax = _leads(attacker, "Snorlax")
    with_item = estimate_damage(holder, snorlax, slot)

    without_item = estimate_damage(_without_item(holder), snorlax, slot)

    assert with_item / without_item == pytest.approx(boost)


def test_the_strongest_hit_is_the_best_of_the_four_moves() -> None:
    excadrill, gyarados = _leads("Excadrill", "Gyarados")

    assert strongest_hit(excadrill, gyarados) == max(
        estimate_damage(excadrill, gyarados, slot) for slot in range(4)
    )


def test_speed_follows_stages_scarf_and_paralysis() -> None:
    koko, snorlax = _leads("TapuKoko", "Snorlax")
    raw = koko.stats.speed

    assert effective_speed(koko) == raw * 1.5  # Choice Scarf
    assert effective_speed(_without_item(koko)) == raw
    snorlax.stat_stages = (0, 0, 0, 0, 1, 0, 0)
    assert effective_speed(snorlax) == snorlax.stats.speed * 1.5
    snorlax.stat_stages = (0, 0, 0, 0, -1, 0, 0)
    assert effective_speed(snorlax) == pytest.approx(snorlax.stats.speed * 2 / 3)
    snorlax.stat_stages = (0, 0, 0, 0, 0, 0, 0)
    snorlax.status = PARALYSIS
    assert effective_speed(snorlax) == snorlax.stats.speed / 2
