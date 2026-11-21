import sys
from collections.abc import Callable
from pathlib import Path
from typing import Any

import numpy as np
import pytest
import torch

import trainerlab.rl
from trainerlab.bots.roster import SELFPLAY
from trainerlab.commands import alphazero as command
from trainerlab.rl import alphazero, distill
from trainerlab.rl.net import PolicyValueNet

# A small search (4 simulations, 2 worlds, fast searches of 2): rule 10's network plays the battles
# quickly.
SMALL: dict[str, Any] = {"simulations": 4, "worlds": 2, "fast": 2}


def test_self_play_gives_a_policy_and_a_result_per_decision() -> None:
    rows = alphazero.play(PolicyValueNet.load(SELFPLAY), 2, seed=1, threads=2, **SMALL)

    n = len(rows["result"])
    assert n > 0
    assert rows["pokemon"].shape[:2] == (n, 12) and rows["ids"].shape == (n, 12, 7)
    assert rows["policy"].shape == rows["mask"].shape == (n, 10)
    assert np.allclose(rows["policy"].sum(1), 1)
    assert (rows["policy"][rows["mask"] == 0] == 0).all()
    assert set(np.unique(rows["result"])) <= {-1.0, 0.0, 1.0}
    assert len(rows["turns"]) == len(rows["winner"]) == 2
    assert rows["foe"].shape == (n,) and rows["foe_mask"].shape == (n, 10)
    assert (rows["foe_mask"][np.arange(n), rows["foe"]] == 1).all()


def test_a_network_with_a_foe_head_learns_what_the_foe_played() -> None:
    torch.manual_seed(0)
    rows = alphazero.play(PolicyValueNet.load(SELFPLAY), 2, seed=4, threads=2, **SMALL, share=1.0)
    net = distill.student(layers=0)
    assert net.shape.foe_head
    optimizer = torch.optim.Adam(net.parameters(), lr=1e-3)
    rng = np.random.default_rng(0)

    before = alphazero.foe_loss(net, rows)
    alphazero.learn(net, optimizer, rows, epochs=5, rng=rng)
    assert alphazero.foe_loss(net, rows) < before


def test_only_the_full_searches_are_recorded() -> None:
    net = PolicyValueNet.load(SELFPLAY)
    none = alphazero.play(net, 2, seed=1, threads=2, **SMALL, share=0.0)
    every = alphazero.play(net, 2, seed=1, threads=2, **SMALL, share=1.0)
    some = alphazero.play(net, 2, seed=1, threads=2, **SMALL)  # a quarter

    assert len(none["result"]) == 0 and len(none["turns"]) == 2
    assert 0 < len(some["result"]) < len(every["result"])


def test_learning_moves_the_policy_toward_the_search() -> None:
    torch.manual_seed(0)
    net = PolicyValueNet.load(SELFPLAY)
    rows = alphazero.play(net, 2, seed=2, threads=2, **SMALL, share=1.0)
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
        net, 2, games=2, threads=2, report=lambda p, _: seen.append(p), **SMALL, share=1.0
    )

    assert [p.iteration for p in seen] == [1, 2]
    assert all(p.games == 2 and p.decisions > 0 and p.turns > 0 for p in seen)
    assert 0 <= seen[0].draws <= 1
    assert any(not torch.equal(a, b) for a, b in zip(before, trained.parameters(), strict=True))
    assert next(trained.parameters()).device.type == "cpu"
    assert alphazero.loop(trained, 1, games=1, **SMALL) is trained  # no report: quiet


def test_an_empty_iteration_reports_no_turns() -> None:
    seen: list[alphazero.Progress] = []
    alphazero.loop(
        PolicyValueNet.load(SELFPLAY),
        1,
        games=0,
        report=lambda p, _: seen.append(p),
        **SMALL,
    )

    assert seen[0].decisions == 0 and seen[0].turns == 0.0 and seen[0].draws == 0.0
    assert seen[0].fresh_value == 0.0
    assert seen[0].entropy == 0.0


def test_the_command_trains_saves_each_iteration_and_measures(tmp_path: Path) -> None:
    out = tmp_path / "az.pt"
    shown: list[str] = []
    argv = [str(SELFPLAY), "--iterations", "1", "--games", "2", "--simulations", "4", "--fast", "2"]

    assert (
        command.main([*argv, "--pairs", "1", "--threads", "2", "--out", str(out)], shown.append)
        == 0
    )

    assert shown[0].startswith(f"from {SELFPLAY}, learning on ")
    assert shown[1].startswith("iteration 1: 2 battles, ")
    assert shown[2].startswith(f"  against plan:{SELFPLAY}: ") and shown[2].endswith("az-1.pt\n")
    assert out.is_file() and (tmp_path / "az-1.pt").is_file()


def test_the_command_measures_every_few_iterations_and_after_the_last(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    measured: list[str] = []

    opponents: list[str] = []

    def duel(first: str, second: str, seeds: object, *, jobs: int) -> object:
        measured.append(first)
        opponents.append(second)
        return type("Result", (), {"score": 0.5, "games": 2})()

    def loop(
        net: object, iterations: int, *, report: Callable[..., None], **kwargs: object
    ) -> None:
        for i in range(1, iterations + 1):
            report(alphazero.Progress(i, 2, 4, 10.0, 0.0, 1.2, 1.4, 0.45, 0.5, 0.3), net)

    monkeypatch.setattr(command, "duel", duel)
    monkeypatch.setattr(alphazero, "loop", loop)
    shown: list[str] = []
    out = tmp_path / "az.pt"
    argv = [str(SELFPLAY), "--iterations", "7", "--follow", "3", "--out", str(out)]

    assert command.main(argv, shown.append) == 0

    assert measured == [f"plan:{tmp_path / f'az-{i}.pt'}" for i in (3, 6, 7)]
    assert opponents == [f"plan:{SELFPLAY}"] * 3  # the network it started from
    assert "entropy 1.200" in shown[1] and "fresh 1.400, 0.450" in shown[1]
    assert (tmp_path / "az-1.pt").is_file()  # saved each iteration

    opponents.clear()
    assert command.main([*argv, "--iterations", "1", "--against", "planner"], shown.append) == 0
    assert opponents == ["planner"]
    assert shown[-1].startswith("  against planner: ")
    with pytest.raises(SystemExit):
        command.main([*argv, "--against", "nobody"], shown.append)


def test_without_pytorch_the_command_says_how_to_get_it(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setitem(sys.modules, "trainerlab.rl.alphazero", None)
    monkeypatch.delattr(trainerlab.rl, "alphazero", raising=False)
    shown: list[str] = []

    assert command.main([str(tmp_path / "start.pt")], shown.append) == 2
    assert "pip install" in shown[0]


def _losses(net: PolicyValueNet, rows: dict[str, np.ndarray]) -> tuple[float, float]:
    """The policy's cross-entropy against the search's visits, and the value's squared error."""
    with torch.no_grad():
        logits, value = net(
            torch.from_numpy(rows["pokemon"].astype(np.float32)),
            torch.from_numpy(rows["ids"].astype(np.int64)),
            torch.from_numpy(rows["field"].astype(np.float32)),
            torch.from_numpy(rows["mask"].astype(np.float32)),
        )
        policy = torch.from_numpy(rows["policy"].astype(np.float32))
        result = torch.from_numpy(rows["result"].astype(np.float32))
        cross_entropy = -(policy * torch.log_softmax(logits, dim=-1)).sum(-1).mean()
        return float(cross_entropy), float(((value - result) ** 2).mean())


@pytest.fixture(scope="module")
def played() -> dict[str, np.ndarray]:
    return alphazero.play(PolicyValueNet.load(SELFPLAY), 2, seed=2, threads=2, **SMALL, share=1.0)


def test_the_losses_reported_are_the_cross_entropy_and_the_squared_error_of_every_row(
    played: dict[str, np.ndarray], monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(alphazero, "BATCH", 16)  # several batches, of unequal sizes
    assert len(played["result"]) % 16 != 0
    net = PolicyValueNet.load(SELFPLAY)
    still = torch.optim.SGD(net.parameters(), lr=0.0)  # learns nothing: the losses stay put

    reported = alphazero.learn(net, still, played, epochs=1, rng=np.random.default_rng(0))

    assert reported == pytest.approx(_losses(net, played), rel=1e-5)
    assert reported[0] > 0  # a cross-entropy


def test_learning_brings_the_policy_to_the_search_and_the_value_to_the_result(
    played: dict[str, np.ndarray],
) -> None:
    torch.manual_seed(0)
    net = PolicyValueNet.load(SELFPLAY)
    policy_before, value_before = _losses(net, played)

    optimizer = torch.optim.Adam(net.parameters(), lr=1e-3)
    alphazero.learn(net, optimizer, played, epochs=5, rng=np.random.default_rng(0))

    policy_after, value_after = _losses(net, played)
    assert policy_after < policy_before
    assert value_after < value_before


def test_the_loop_learns_from_the_last_iterations_and_reports_their_battles(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    seeds: list[int] = []
    learned: list[int] = []

    def play(net: object, games: int, *, seed: int, **kwargs: object) -> dict[str, np.ndarray]:
        seeds.append(seed)
        n = len(seeds)  # iteration n leaves n decisions
        rows = {key: np.zeros(n) for key in alphazero.KEYS}
        rows["turns"] = np.array([10.0, 30.0])
        rows["winner"] = np.array([-1, 0], dtype=np.int8)  # one draw, one win for side 0
        rows["policy"] = np.tile([0.5, 0.5, 0, 0, 0, 0, 0, 0, 0, 0], (n, 1))  # ln 2 each
        return rows

    def learn(
        net: object, optimizer: object, rows: dict[str, np.ndarray], **kwargs: object
    ) -> tuple[float, float]:
        learned.append(len(rows["result"]))
        return 0.1, 0.2

    order: list[str] = []

    def measure(net: object, rows: dict[str, np.ndarray], **kwargs: object) -> tuple[float, float]:
        order.append(f"measure {len(rows['result'])}")
        return 1.5, 0.25

    def learning(*args: object, **kwargs: object) -> tuple[float, float]:
        order.append("learn")
        return learn(*args, **kwargs)  # type: ignore[arg-type]

    monkeypatch.setattr(alphazero, "play", play)
    monkeypatch.setattr(alphazero, "learn", learning)
    monkeypatch.setattr(alphazero, "measure", measure)
    seen: list[alphazero.Progress] = []

    alphazero.loop(PolicyValueNet.load(SELFPLAY), 6, seed=3, report=lambda p, _: seen.append(p))

    assert seeds == [3001, 3002, 3003, 3004, 3005, 3006]
    window = alphazero.WINDOW
    assert learned == [sum(range(max(1, n - window + 1), n + 1)) for n in range(1, 7)]
    assert seen[0].turns == 20.0 and seen[0].draws == 0.5
    assert (seen[0].policy_loss, seen[0].value_loss) == (0.1, 0.2)
    assert seen[0].entropy == pytest.approx(np.log(2))
    # The new decisions are measured before they are learned: how well the network does on
    # battles it has not learned from.
    assert order[:4] == ["measure 1", "learn", "measure 2", "learn"]
    assert (seen[0].fresh_policy, seen[0].fresh_value) == (1.5, 0.25)
