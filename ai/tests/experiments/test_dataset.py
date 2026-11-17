import subprocess
import sys
from pathlib import Path

import numpy as np
import pytest

from trainerlab import _engine
from trainerlab.bots.roster import BOTS
from trainerlab.commands.dataset import main
from trainerlab.experiments.dataset import build, default_name
from trainerlab.game.decisions import decisions
from trainerlab.game.environment import BattleEnvironment
from trainerlab.learning.features import feature_names, features

PARALYSIS, SAND = 4, 3


def _column(x: np.ndarray, name: str) -> float:
    return float(x[feature_names().index(name)])


def _view(team: list[str], foes: list[str], side: int = 0) -> _engine.BattleState:
    return BattleEnvironment(team, foes, seed=1).observe(side)


def test_every_feature_has_one_unique_name() -> None:
    x = features(_view(["Blissey"], ["Snorlax"]), 0)

    assert x.shape == (len(feature_names()),)
    assert len(set(feature_names())) == len(feature_names())
    assert x.dtype == np.float32


def test_a_foe_not_yet_seen_is_alive_at_full_hp_without_its_species() -> None:
    x = features(_view(["Blissey", "Toxapex"], ["Snorlax", "Gyarados", "KommoO"]), 0)

    assert _column(x, "foe_hidden") == pytest.approx(2 / 6)
    assert _column(x, "foe_alive") == pytest.approx(3 / 6)
    assert _column(x, "foe_hp_total") == pytest.approx(3 / 6)
    assert _column(x, "foe_active_is_Snorlax") == 1
    assert _column(x, "foe_hp_of_Snorlax") == 1
    assert _column(x, "foe_hp_of_Gyarados") == 0
    assert _column(x, "my_hidden") == 0
    assert _column(x, "my_hp_of_Toxapex") == 1


def test_the_point_of_view_is_the_deciding_side() -> None:
    x = features(_view(["Blissey"], ["Snorlax"], side=1), 1)

    assert _column(x, "my_active_is_Snorlax") == 1
    assert _column(x, "foe_active_is_Blissey") == 1


def test_best_damage_comes_from_the_engine_as_a_share_of_the_foe_hp() -> None:
    view = _view(["Blissey"], ["Snorlax"])
    snorlax = view.active(1)
    x = features(view, 0)

    assert _column(x, "my_best_damage") == pytest.approx(100 / snorlax.current_hp)  # Seismic Toss
    blissey = view.active(0)
    best = max(_engine.expected_damage(view, 1, slot).mean for slot in range(4))
    assert _column(x, "foe_best_damage") == pytest.approx(best / blissey.current_hp)


def test_a_replacement_row_has_no_damage_on_either_side() -> None:
    view = _view(["Blissey", "Toxapex"], ["Snorlax"])
    blissey = view.active(0)
    blissey.current_hp = 0
    view.set_pokemon(0, 0, blissey)
    x = features(view, 0)

    assert _column(x, "replacement") == 1
    assert _column(x, "my_best_damage") == 0
    assert _column(x, "foe_best_damage") == 0
    assert _column(x, "my_alive") == pytest.approx(1 / 6)


def test_status_stages_weather_hazards_and_screens_are_encoded() -> None:
    view = _view(["Blissey"], ["Snorlax"])
    blissey = view.active(0)
    blissey.status = PARALYSIS
    blissey.stat_stages = (2, 0, 0, 0, -6, 0, 0)
    blissey.current_hp = blissey.stats.hp // 2
    view.set_pokemon(0, 0, blissey)
    view.weather, view.weather_turns_left = SAND, 3
    view.terrain, view.terrain_turns_left = 1, 3
    hazards = view.hazards(1)
    hazards.stealth_rock, hazards.spikes, hazards.toxic_spikes = 1, 3, 1
    view.set_hazards(1, hazards)
    view.aurora_veil_turns = (4, 0)
    x = features(view, 0)

    assert _column(x, "my_active_paralysis") == 1
    assert _column(x, "my_active_burn") == 0
    assert _column(x, "my_active_stage_atk") == pytest.approx(2 / 6)
    assert _column(x, "my_active_stage_spe") == -1
    assert _column(x, "my_active_hp") == pytest.approx(0.5, abs=0.01)
    assert _column(x, "weather_sand") == 1
    assert _column(x, "terrain_electric") == 1
    assert (_column(x, "foe_stealth_rock"), _column(x, "foe_spikes")) == (1, 1)
    assert _column(x, "foe_toxic_spikes") == 0.5
    assert _column(x, "my_aurora_veil") == 0.5


def test_my_faster_is_one_zero_or_one_half_on_a_tie() -> None:
    assert _column(features(_view(["Dragapult"], ["Snorlax"]), 0), "my_faster") == 1
    assert _column(features(_view(["Snorlax"], ["Dragapult"]), 0), "my_faster") == 0
    assert _column(features(_view(["Snorlax"], ["Snorlax"]), 0), "my_faster") == 0.5


def test_each_row_is_a_decision_labelled_with_the_score_of_its_battle() -> None:
    series = list(decisions(BOTS["calculator"], BOTS["survivor"], seeds=[40000, 40001]))
    data = build(series)

    assert data["x"].shape == (len(series), len(feature_names()))
    assert list(data["y"]) == [{"win": 1.0, "draw": 0.5, "loss": 0.0}[d.outcome] for d in series]
    assert list(data["seed"]) == [d.seed for d in series]
    assert list(data["side"]) == [d.side for d in series]
    assert list(data["a_side"]) == [d.a_side for d in series]
    assert list(data["turn"]) == [d.turn for d in series]
    assert set(data["y"]) == {0.0, 1.0}
    assert np.array_equal(data["x"][0], features(series[0].view, series[0].side))


def test_no_decision_gives_an_empty_table_of_the_right_width() -> None:
    assert build([])["x"].shape == (0, len(feature_names()))


def test_the_command_writes_the_dataset_and_its_origin(tmp_path: Path) -> None:
    out = tmp_path / "data.npz"
    lines: list[str] = []

    main(
        ["calculator", "survivor", "--pairs", "2", "--from", "40000", "--out", str(out)],
        lines.append,
    )

    saved = np.load(out)
    expected = build(decisions(BOTS["calculator"], BOTS["survivor"], seeds=[40000, 40001]))
    assert np.array_equal(saved["x"], expected["x"])
    assert np.array_equal(saved["y"], expected["y"])
    assert list(saved["feature_names"]) == list(feature_names())
    assert list(saved["bots"]) == ["calculator", "survivor"]
    assert int(saved["catalog_fingerprint"]) == _engine.catalog_fingerprint()
    assert lines[0].startswith("calculator against survivor, seeds 40000-40001: ")
    assert f"x {len(feature_names())} features" in lines[0]


def test_the_command_prints_to_stdout_by_default(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    main(["hitter", "random", "--pairs", "1", "--out", str(tmp_path / "d.npz")])

    assert capsys.readouterr().out.startswith("hitter against random, seeds 0-0: ")


def test_the_number_of_processes_changes_no_row(tmp_path: Path) -> None:
    base = ["hitter", "random", "--pairs", "3"]
    main([*base, "--jobs", "1", "--out", str(tmp_path / "a.npz")], lambda text: None)
    main([*base, "--jobs", "2", "--out", str(tmp_path / "b.npz")], lambda text: None)

    alone, spread = np.load(tmp_path / "a.npz"), np.load(tmp_path / "b.npz")
    for key in ("x", "y", "seed", "side", "a_side", "turn", "feature_names"):
        assert np.array_equal(alone[key], spread[key])


def test_a_fresh_process_writes_a_dataset_played_in_other_processes(tmp_path: Path) -> None:
    # The battles are played in child processes: the parent never loads the catalog by itself.
    out = tmp_path / "fresh.npz"
    script = (
        "from trainerlab.commands.dataset import main\n"
        f"main(['hitter', 'random', '--pairs', '2', '--jobs', '2', '--out', {str(out)!r}])\n"
    )

    subprocess.run([sys.executable, "-c", script], check=True, capture_output=True)

    assert np.load(out)["x"].shape[0] > 0


def test_a_forecaster_with_its_own_model_names_the_file_after_it() -> None:
    assert default_name("forecaster:m/gen2.npz", "survivor", range(1, 3)) == (
        "forecaster-gen2-vs-survivor-1-2.npz"
    )
