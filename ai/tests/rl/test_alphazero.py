import sys
from pathlib import Path

import numpy as np
import pytest
import torch

import trainerlab.rl
from trainerlab.bots.roster import SELFPLAY
from trainerlab.commands import alphazero as command
from trainerlab.rl import alphazero
from trainerlab.rl.net import PolicyValueNet

# A small search (4 simulations, 2 worlds): rule 10's network plays the battles quickly.


def test_self_play_gives_a_policy_and_a_result_per_decision() -> None:
    rows = alphazero.play(
        PolicyValueNet.load(SELFPLAY), 2, seed=1, threads=2, simulations=4, worlds=2
    )

    n = len(rows["result"])
    assert n > 0
    assert rows["pokemon"].shape[:2] == (n, 12) and rows["ids"].shape == (n, 12, 7)
    assert rows["policy"].shape == rows["mask"].shape == (n, 10)
    assert np.allclose(rows["policy"].sum(1), 1)
    assert (rows["policy"][rows["mask"] == 0] == 0).all()
    assert set(np.unique(rows["result"])) <= {-1.0, 0.0, 1.0}
    assert len(rows["turns"]) == len(rows["winner"]) == 2


def test_learning_moves_the_policy_toward_the_search() -> None:
    torch.manual_seed(0)
    net = PolicyValueNet.load(SELFPLAY)
    rows = alphazero.play(net, 2, seed=2, threads=2, simulations=4, worlds=2)
    optimizer = torch.optim.Adam(net.parameters(), lr=1e-3)
    rng = np.random.default_rng(0)

    first, _ = alphazero.learn(net, optimizer, rows, epochs=1, rng=rng)
    later, _ = alphazero.learn(net, optimizer, rows, epochs=5, rng=rng)

    assert later < first
    assert not net.training


def test_the_loop_plays_learns_and_reports_each_iteration() -> None:
    seen: list[alphazero.Progress] = []
    net = PolicyValueNet.load(SELFPLAY)
    before = [p.clone() for p in net.parameters()]

    trained = alphazero.loop(
        net, 2, games=2, threads=2, report=lambda p, _: seen.append(p), simulations=4, worlds=2
    )

    assert [p.iteration for p in seen] == [1, 2]
    assert all(p.games == 2 and p.decisions > 0 and p.turns > 0 for p in seen)
    assert 0 <= seen[0].draws <= 1
    assert any(not torch.equal(a, b) for a, b in zip(before, trained.parameters(), strict=True))
    assert next(trained.parameters()).device.type == "cpu"
    assert (
        alphazero.loop(trained, 1, games=1, simulations=4, worlds=2) is trained
    )  # no report: quiet


def test_an_empty_iteration_reports_no_turns() -> None:
    seen: list[alphazero.Progress] = []
    alphazero.loop(
        PolicyValueNet.load(SELFPLAY),
        1,
        games=0,
        report=lambda p, _: seen.append(p),
        simulations=4,
        worlds=2,
    )

    assert seen[0].decisions == 0 and seen[0].turns == 0.0 and seen[0].draws == 0.0


def test_the_command_trains_saves_each_iteration_and_measures(tmp_path: Path) -> None:
    out = tmp_path / "az.pt"
    shown: list[str] = []
    argv = [str(SELFPLAY), "--iterations", "1", "--games", "2", "--simulations", "4"]

    assert (
        command.main([*argv, "--pairs", "1", "--threads", "2", "--out", str(out)], shown.append)
        == 0
    )

    assert shown[0].startswith(f"from {SELFPLAY}, learning on ")
    assert shown[1].startswith("iteration 1: 2 battles, ")
    assert shown[2].startswith("  against rule 11: ") and shown[2].endswith("az-1.pt\n")
    assert out.is_file() and (tmp_path / "az-1.pt").is_file()


def test_without_pytorch_the_command_says_how_to_get_it(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setitem(sys.modules, "trainerlab.rl.alphazero", None)
    monkeypatch.delattr(trainerlab.rl, "alphazero", raising=False)
    shown: list[str] = []

    assert command.main([str(tmp_path / "start.pt")], shown.append) == 2
    assert "pip install" in shown[0]
