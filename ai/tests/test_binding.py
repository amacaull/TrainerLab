"""The parts of the binding that no other test reaches."""

import subprocess
import sys

from trainerlab import _engine
from trainerlab.environment import DATA_DIR


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
