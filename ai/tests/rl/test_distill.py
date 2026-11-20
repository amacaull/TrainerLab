from pathlib import Path
from typing import Any

import numpy as np
import pytest
import torch

from trainerlab.bots.roster import SELFPLAY
from trainerlab.commands import distill as command
from trainerlab.rl import alphazero, distill
from trainerlab.rl.net import PolicyValueNet

# The teacher plays small: 2 battles per round, 4 simulations, fast searches of 2, all recorded.
SMALL: dict[str, Any] = {"games": 2, "simulations": 4, "worlds": 2, "fast": 2, "share": 1.0}


def test_the_teacher_plays_rounds_until_it_leaves_enough_decisions() -> None:
    seen: list[tuple[int, int]] = []
    rows = distill.generate(
        PolicyValueNet.load(SELFPLAY),
        60,
        threads=2,
        report=lambda r, n: seen.append((r, n)),
        **SMALL,
    )

    n = len(rows["result"])
    assert n >= 60 and set(rows) == set(alphazero.KEYS)
    assert rows["pokemon"].shape[:2] == (n, 12) and rows["policy"].shape == (n, 10)
    assert [r for r, _ in seen] == list(range(1, len(seen) + 1)) and seen[-1][1] == n
    assert all(a < b for (_, a), (_, b) in zip(seen, seen[1:], strict=False))


@pytest.fixture(scope="module")
def taught() -> dict[str, np.ndarray]:
    return distill.generate(PolicyValueNet.load(SELFPLAY), 200, threads=2, **SMALL)


def test_the_student_learns_the_teacher_s_search_and_reports_each_pass(
    taught: dict[str, np.ndarray],
) -> None:
    torch.manual_seed(0)
    student = distill.student(layers=1)
    before = distill.losses(student, taught)
    epochs: list[distill.Epoch] = []

    distill.teach(student, taught, epochs=3, report=epochs.append, batch=64)

    assert [e.epoch for e in epochs] == [1, 2, 3]
    after = distill.losses(student, taught)
    assert after[0] < before[0] and after[1] < before[1]
    assert all(e.held_policy > 0 and e.policy > 0 for e in epochs)
    assert not student.training and next(student.parameters()).device.type == "cpu"


def test_the_learning_rate_falls_from_its_start_to_its_floor() -> None:
    assert distill.rate(0, 100) == pytest.approx(distill.RATE)
    assert distill.rate(100, 100) == pytest.approx(distill.FLOOR)
    assert distill.FLOOR < distill.rate(50, 100) < distill.RATE


def test_the_command_generates_once_then_teaches_from_the_saved_decisions(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(distill, "GENERATION", SMALL)
    out, data = tmp_path / "student.pt", tmp_path / "data.npz"
    argv = [str(SELFPLAY), "--rows", "40", "--epochs", "1", "--layers", "1", "--threads", "2"]
    shown: list[str] = []

    assert command.main([*argv, "--out", str(out), "--data", str(data)], shown.append) == 0
    assert data.is_file() and PolicyValueNet.load(out).shape.layers == 1
    assert any(line.startswith("round 1: ") for line in shown)
    assert any(line.startswith("pass 1: ") for line in shown)

    def never(*args: object, **kwargs: object) -> object:
        raise AssertionError("the decisions are on disk")

    monkeypatch.setattr(distill, "generate", never)
    shown.clear()
    assert command.main([*argv, "--out", str(out), "--data", str(data)], shown.append) == 0
    assert shown[0].startswith(f"{len(np.load(data)['result'])} decisions from {data}")


def test_without_pytorch_the_command_says_how_to_get_it(monkeypatch: pytest.MonkeyPatch) -> None:
    import sys

    import trainerlab.rl

    monkeypatch.setitem(sys.modules, "trainerlab.rl.distill", None)
    monkeypatch.delattr(trainerlab.rl, "distill", raising=False)
    shown: list[str] = []

    assert command.main([str(SELFPLAY)], shown.append) == 2
    assert "pip install" in shown[0]
