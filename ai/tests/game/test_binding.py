"""The parts of the binding that no other test reaches."""

import subprocess
import sys

import pytest

from trainerlab import _engine
from trainerlab.game.environment import DATA_DIR, BattleEnvironment


def test_a_missing_data_directory_is_reported_as_a_data_error() -> None:
    # engine_init loads the catalog once per process: a fresh interpreter is needed.
    script = (
        "from trainerlab import _engine\n"
        "try:\n"
        "    _engine.engine_init('/no/such/directory')\n"
        "except _engine.DataError:\n"
        "    print('DataError')\n"
    )

    output = subprocess.run(
        [sys.executable, "-c", script], capture_output=True, text=True, check=True
    ).stdout

    assert output.strip() == "DataError"


def test_type_effectiveness_is_available_from_python() -> None:
    _engine.engine_init(str(DATA_DIR))
    electric = 3
    gyarados = _engine.find_species_id("Gyarados")

    assert _engine.type_effectiveness(electric, gyarados) == 4.0


def test_move_effects_are_available_from_python() -> None:
    _engine.engine_init(str(DATA_DIR))
    body_slam = _engine.move_entry(_engine.find_move_id("BodySlam"))
    paralysis = 4

    assert [effect.kind for effect in body_slam.effects] == ["Damage", "ApplyStatus"]
    assert (body_slam.effects[1].status, body_slam.effects[1].chance) == (paralysis, 30)
    assert _engine.move_entry(_engine.find_move_id("GrassKnot")).power_from_target_weight


def test_expected_damage_takes_a_view_and_refuses_an_empty_slot() -> None:
    env = BattleEnvironment(["Blissey"], ["Snorlax", "Gyarados"], seed=1)
    view = env.observe(0)
    seismic_toss = 3

    damage = _engine.expected_damage(view, 0, seismic_toss)

    assert (damage.min, damage.max, damage.mean, damage.exact) == (100, 100, 100.0, True)
    assert repr(damage) == "Damage(min=100, max=100, mean=100.000000)"
    assert "sampled" in repr(_engine.expected_damage(_maushold_state(), 0, 1))  # Bullet Seed
    blissey = view.pokemon(0, 0)
    blissey.move_ids = (blissey.move_ids[0], blissey.move_ids[1], blissey.move_ids[2], -1)
    blissey.pp = (blissey.pp[0], blissey.pp[1], blissey.pp[2], 0)
    view.set_pokemon(0, 0, blissey)
    with pytest.raises(_engine.ArgError):
        _engine.expected_damage(view, 0, seismic_toss)


def _maushold_state() -> _engine.BattleState:
    return BattleEnvironment(["Maushold"], ["Snorlax"], seed=1).observe(0)
