from pathlib import Path

import numpy as np
import pytest

from trainerlab.commands import evaluator
from trainerlab.commands.cli import SECTIONS
from trainerlab.experiments import teams, truth
from trainerlab.rl.team_net import Additive, TeamNet, TraitNet, load


def test_both_models_learn_are_judged_and_kept(tmp_path: Path) -> None:
    index, points = truth.record("random", first=0, pairs=2, jobs=1, count=30)
    measured = tmp_path / "truth-2.npz"
    np.savez(
        measured, team=index, points=points, first=0, pairs=2, names=np.array(truth.teams_of(2, 30))
    )
    seed, points = teams.record("random", range(10), jobs=1, size=4)
    played = tmp_path / "teams-4.npz"
    np.savez(played, seed=seed, points=points, size=4)
    out = tmp_path / "evaluator.pt"
    shown: list[str] = []

    argv = [str(measured), str(played), "--epochs", "2", "--out", str(out)]
    assert evaluator.main(argv, shown.append) == 0

    assert shown[0].startswith("140 battles, ") and "held out" in shown[0]
    text = "".join(shown)
    assert "additive   truth-2.npz, teams of 2:" in text
    assert "attention  teams-4.npz, teams of 4:" in text and "traits     teams-4.npz" in text
    assert "traits against additive, held-out battles of teams-4.npz: log loss lower by " in text
    assert "held-out measured teams of 2: rank correlation" in text
    assert isinstance(load(out), TraitNet)
    assert isinstance(load(out.with_name("evaluator-additive.pt")), Additive)
    assert isinstance(load(out.with_name("evaluator-attention.pt")), TeamNet)
    assert shown[-1] == f"-> {out}\n"


def test_the_command_needs_a_file(capsys: pytest.CaptureFixture[str]) -> None:
    with pytest.raises(SystemExit):
        evaluator.main([])


def test_the_command_is_listed_with_the_training() -> None:
    assert "evaluator" in SECTIONS["train"]
