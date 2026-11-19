"""Palier 7.6: the judge's worst blunders, played out to the end from the real position."""

import math
from pathlib import Path

import numpy as np
import pytest

from trainerlab.bots.roster import factory
from trainerlab.commands import verify as command
from trainerlab.experiments import parallel, rollouts
from trainerlab.game.decisions import decisions


def _turn(bot_a: str, bot_b: str, seed: int, a_side: int, index: int = 2) -> tuple[int, object]:
    """The turn of bot A's index-th decision with a choice in that battle, and its view."""
    mine = [
        d
        for d in decisions(factory(bot_a), factory(bot_b), seeds=[seed])
        if d.a_side == a_side and d.by_a and d.kind == "action" and len(d.legal) > 1
    ]
    return mine[index].turn, mine[index].view


def test_the_position_is_the_real_state_bot_a_decided_in() -> None:
    turn, view = _turn("hitter", "random", 30000, 1)

    state = rollouts.position("hitter", "random", seed=30000, a_side=1, turn=turn)

    assert state.turn == turn
    seen = rollouts._observe(state, 1)
    assert seen.active(1).species_id == view.active(1).species_id  # type: ignore[attr-defined]
    assert seen.active(1).current_hp == view.active(1).current_hp  # type: ignore[attr-defined]
    assert seen.active(0).current_hp == view.active(0).current_hp  # type: ignore[attr-defined]


def test_a_position_bot_a_never_decided_in_is_refused() -> None:
    with pytest.raises(ValueError, match="no decision of bot A at turn 999"):
        rollouts.position("hitter", "random", seed=30000, a_side=0, turn=999)


def test_a_rollout_plays_the_forced_option_then_both_bots_to_the_end() -> None:
    turn, _ = _turn("hitter", "random", 30000, 0)
    state = rollouts.position("hitter", "random", seed=30000, a_side=0, turn=turn)

    points = [rollouts.rollout("hitter", "random", state, 0, 0, seed=k) for k in range(3)]

    assert set(points) <= {0.0, 0.5, 1.0}
    assert rollouts.rollout("hitter", "random", state, 0, 0, seed=1) == points[1]  # same dice


def _row(chosen: int, best: int, loss: float = 0.3) -> rollouts.Blunder:
    turn, _ = _turn("random", "hitter", 30000, 0)
    return rollouts.Blunder(seed=30000, a_side=0, turn=turn, chosen=chosen, best=best, loss=loss)


def test_an_option_against_itself_shows_no_gap_with_the_same_dice() -> None:
    v = rollouts.verify("random", "hitter", _row(0, 0), games=6)

    assert v.gap == 0.0 and v.low == v.high == 0.0
    assert v.verdict == "unclear"
    assert v.games == 6


def test_a_verdict_follows_the_interval_of_the_gap() -> None:
    assert rollouts.verdict_of(0.05, 0.30) == "confirmed"  # the interval (low, high) above 0
    assert rollouts.verdict_of(-0.30, -0.05) == "refuted"
    assert rollouts.verdict_of(-0.05, 0.05) == "unclear"
    assert rollouts.interval([1.0, 1.0, 0.0, 1.0]) == pytest.approx(
        (0.75 - 1.96 * 0.5 / 2, 0.75 + 1.96 * 0.5 / 2)
    )


def test_verifying_on_several_cores_changes_nothing() -> None:
    rows = [_row(0, 1), _row(1, 0)]
    alone = rollouts.verify_all("random", "hitter", rows, games=4, jobs=1)
    assert rollouts.verify_all("random", "hitter", rows, games=4, jobs=2) == alone
    assert [v.chosen for v in alone] == [0, 1]


def test_the_worst_blunders_are_read_from_a_judge_s_file(tmp_path: Path) -> None:
    out = tmp_path / "j.npz"
    np.savez(
        out, seed=np.array([1, 2, 3]), a_side=np.array([0, 1, 0]), turn=np.array([4, 5, 6]),
        chosen=np.array([0, 1, 2]), best=np.array([1, 2, 3]), loss=np.array([0.1, 0.4, 0.2]),
    )  # fmt: skip

    worst = rollouts.worst_of(out, 2)

    assert [(b.seed, b.loss) for b in worst] == [(2, 0.4), (3, 0.2)]


def test_the_summary_compares_the_judge_s_losses_with_the_measured_gaps() -> None:
    v = [
        rollouts.Verification(1, 0, 3, 0, 1, 0.2, 0.15, 0.05, 0.25, 200, "confirmed"),
        rollouts.Verification(2, 0, 3, 0, 1, 0.1, -0.05, -0.15, 0.05, 200, "unclear"),
    ]

    s = rollouts.summarize(v)

    assert s.confirmed == 0.5 and s.refuted == 0.0
    assert s.mean_loss == pytest.approx(0.15) and s.mean_gap == pytest.approx(0.05)
    assert math.isnan(rollouts.summarize([]).mean_gap)


def test_the_command_verifies_the_worst_blunders_of_a_judge_s_file(tmp_path: Path) -> None:
    turn, _ = _turn("random", "hitter", 30000, 0)
    judged = tmp_path / "j.npz"
    np.savez(
        judged, seed=np.array([30000]), a_side=np.array([0]), turn=np.array([turn]),
        chosen=np.array([0]), best=np.array([1]), loss=np.array([0.3]),
    )  # fmt: skip
    shown: list[str] = []

    argv = ["random", "hitter", "--judged", str(judged), "--games", "4", "--jobs", "1"]
    assert command.main(argv, shown.append) == 0

    text = "".join(shown)
    assert text.startswith("the 1 worst blunders of random against hitter, 4 games per option")
    assert "confirmed " in text and "the judge's losses, on average " in text
    assert "trainerlab watch random hitter --seed 30000 --side 0" in text


def test_bot_a_on_side_1_plays_its_forced_option_too() -> None:
    turn, _ = _turn("hitter", "random", 30000, 1)
    state = rollouts.position("hitter", "random", seed=30000, a_side=1, turn=turn)
    assert rollouts.rollout("hitter", "random", state, 1, 0, seed=0) in {0.0, 0.5, 1.0}


@pytest.mark.parametrize("jobs", [1, 2])
def test_progress_counts_the_blunders_checked(jobs: int) -> None:
    shown: list[str] = []
    progress = parallel.Progress(2, shown.append, lambda: 0.0, unit="blunders")

    rollouts.verify_all("random", "hitter", [_row(0, 1), _row(1, 0)], 2, jobs, progress)

    assert [line.split()[1] for line in shown[:-1]] == ["0/2", "1/2", "2/2"]
