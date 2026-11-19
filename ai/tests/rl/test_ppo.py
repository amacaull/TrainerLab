import sys
from pathlib import Path

import numpy as np
import pytest
import torch

import trainerlab.rl
from trainerlab.commands import selfplay as command
from trainerlab.rl import ppo
from trainerlab.rl.net import PolicyValueNet, Shape

G, L = ppo.GAMMA, ppo.LAMBDA


def test_a_reward_after_a_step_without_decision_goes_to_the_decision_before() -> None:
    # One battle: decides at 0, waits at 1 (the foe replaces), wins at 1 and the battle ends.
    decided = np.array([[True], [False]])
    rewards = np.array([[0.0], [1.0]], dtype=np.float32)
    done = np.array([[False], [True]])
    values = np.array([[0.4], [0.0]], dtype=np.float32)

    adv = ppo.advantages(decided, rewards, done, values, np.array([9.0], dtype=np.float32))

    assert adv[0, 0] == pytest.approx(1.0 - 0.4)  # the end: no bootstrap from the next battle
    assert adv[1, 0] == 0.0


def test_advantages_chain_decisions_with_gae_and_bootstrap_at_the_end() -> None:
    decided = np.array([[True], [True]])
    rewards = np.zeros((2, 1), dtype=np.float32)
    done = np.zeros((2, 1), dtype=bool)
    values = np.array([[0.1], [0.2]], dtype=np.float32)

    adv = ppo.advantages(decided, rewards, done, values, np.array([0.5], dtype=np.float32))

    second = G * 0.5 - 0.2
    first = (G * 0.2 - 0.1) + G * L * second
    assert adv[1, 0] == pytest.approx(second)
    assert adv[0, 0] == pytest.approx(first)


def test_self_play_trains_against_itself_and_its_past(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(ppo, "SNAPSHOT_EVERY", 1)  # a past version from the first update on
    torch.manual_seed(0)
    net = PolicyValueNet(Shape.for_catalog())
    before = [p.clone() for p in net.parameters()]
    seen: list[ppo.Progress] = []

    trained = ppo.selfplay(net, 3, battles=4, steps=12, report=lambda p, _: seen.append(p))

    assert [p.update for p in seen] == [1, 2, 3]
    assert seen[-1].decisions > 0 and seen[-1].entropy > 0
    assert any(not torch.equal(a, b) for a, b in zip(before, trained.parameters(), strict=True))
    assert next(trained.parameters()).device.type == "cpu"
    assert ppo.selfplay(trained, 1, battles=2, steps=4) is trained  # no report: quiet


def test_the_command_trains_saves_and_measures(tmp_path: Path) -> None:
    torch.manual_seed(1)
    start, out = tmp_path / "start.pt", tmp_path / "out.pt"
    PolicyValueNet(Shape.for_catalog()).save(start)
    shown: list[str] = []
    argv = [str(start), "--updates", "3", "--every", "2", "--pairs", "1", "--out", str(out)]

    assert command.main([*argv, "--battles", "2", "--steps", "6"], shown.append) == 0

    assert shown[0].startswith(f"from {start}, on ")
    assert shown[1].startswith("update 1: ")
    assert shown[2].startswith("update 2: ")  # no measure after update 1
    assert shown[3].startswith("  against forecaster: ") and shown[3].endswith(f"-> {out}\n")
    assert shown[5].startswith("  against forecaster: ")  # the last update is measured too
    assert out.is_file()


def test_without_pytorch_the_command_says_how_to_get_it(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setitem(sys.modules, "trainerlab.rl.ppo", None)
    monkeypatch.delattr(trainerlab.rl, "ppo")
    shown: list[str] = []

    assert command.main([str(tmp_path / "start.pt")], shown.append) == 2
    assert "pip install" in shown[0]
