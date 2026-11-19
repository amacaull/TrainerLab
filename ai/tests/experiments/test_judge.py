"""The judge (palier 7.4): a far deeper search rates each decision of a bot."""

import math
import sys
from pathlib import Path

import numpy as np
import pytest

import trainerlab.experiments
from trainerlab import _engine
from trainerlab.bots.roster import SELFPLAY, factory
from trainerlab.commands import judge as command
from trainerlab.experiments import judge
from trainerlab.game.decisions import decisions

QUICK = {"SIMULATIONS": 60, "WORLDS": 2, "MIN_VISITS": 4}  # a small judge, for the tests


@pytest.fixture(autouse=True)
def quick_judge(monkeypatch: pytest.MonkeyPatch) -> None:
    for name, value in QUICK.items():
        monkeypatch.setattr(judge, name, value)
    monkeypatch.setattr(judge, "NETWORK", SELFPLAY)


def test_each_turn_with_a_choice_is_judged_and_nothing_else() -> None:
    seeds = range(30000, 30002)
    found = list(decisions(factory("hitter"), factory("random"), seeds=seeds))
    choices = [d for d in found if d.by_a and d.kind == "action" and len(d.legal) > 1]

    judged = judge.judge_block("hitter", "random", seeds)

    assert len(judged) == len(choices)
    assert [(j.seed, j.a_side, j.turn) for j in judged] == [
        (d.seed, d.a_side, d.turn) for d in choices
    ]


def test_a_loss_is_what_the_judge_s_choice_is_worth_over_the_bot_s() -> None:
    judged = judge.judge_block("random", "hitter", range(30000, 30001))

    for j in judged:
        assert j.loss >= 0
        assert (j.loss == 0) if j.chosen == j.best else True
        assert 0 <= j.chosen < 10 and 0 <= j.best < 10
    assert any(j.chosen != j.best for j in judged)  # random disagrees with any judge
    assert any(j.loss > 0 for j in judged)


def test_options_are_named_as_a_player_reads_them() -> None:
    judged = judge.judge_block("hitter", "random", range(30000, 30001))

    assert all(j.chosen_label and j.best_label for j in judged)
    switches = [j.best_label for j in judged if j.best >= 4]
    assert all(label.startswith("switch to ") for label in switches)


def test_the_judge_is_the_same_on_one_core_or_several() -> None:
    seeds = range(30000, 30003)
    assert judge.judge_on_cores("hitter", "random", seeds, 1) == judge.judge_block(
        "hitter", "random", seeds
    )


def _judged(loss: float, outcome: str = "win", agree: bool = False) -> judge.Judged:
    return judge.Judged(
        seed=1, a_side=0, turn=3, outcome=outcome, chosen=0, best=0 if agree else 1,
        loss=loss, active="A", foe="B", chosen_label="x", best_label="y",
    )  # fmt: skip


def test_the_summary_counts_inaccuracies_and_blunders() -> None:
    judged = [
        _judged(0.0, agree=True),
        _judged(0.02),
        _judged(0.06, "loss"),
        _judged(0.12, "loss"),
    ]

    s = judge.summarize(judged)

    assert s.decisions == 4
    assert s.agreement == 0.25
    assert s.mean_loss == pytest.approx(0.05)
    assert s.inaccuracies == 0.5  # 5 points or more, blunders included
    assert s.blunders == 0.25  # 10 points or more
    assert s.loss_in_won == pytest.approx(0.01) and s.loss_in_lost == pytest.approx(0.09)


def test_a_summary_of_nothing_is_empty() -> None:
    s = judge.summarize([])
    assert s.decisions == 0 and math.isnan(s.mean_loss) and math.isnan(s.loss_in_lost)


def test_the_command_reports_the_summary_and_the_worst_decisions(tmp_path: Path) -> None:
    shown: list[str] = []
    out = tmp_path / "j.npz"
    argv = ["random", "hitter", "--pairs", "1", "--from", "30000", "--jobs", "1", "--worst", "3"]

    assert command.main([*argv, "--out", str(out)], shown.append) == 0

    text = "".join(shown)
    assert text.startswith("random against hitter, seeds 30000-30000: ")
    assert "agrees with the judge on " in text
    assert "mean loss " in text and "blunders (10 points or more) " in text
    worst = [line for line in text.splitlines() if line.startswith("trainerlab watch ")]
    assert len(worst) == 3
    saved = np.load(out)
    assert len(saved["loss"]) == int(text.split(" decisions judged")[0].split()[-1])


def test_without_pytorch_the_command_says_how_to_get_it(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setitem(sys.modules, "trainerlab.experiments.judge", None)
    monkeypatch.delattr(trainerlab.experiments, "judge", raising=False)
    shown: list[str] = []

    assert command.main(["random", "hitter"], shown.append) == 2
    assert "pip install" in shown[0]


def test_a_judge_tries_every_option_and_rates_it_for_the_side() -> None:
    from trainerlab.game.environment import BattleEnvironment, random_team_pair
    from trainerlab.rl.planner import cpp_network

    env = BattleEnvironment(*random_team_pair(25), seed=25)
    view, legal = env.observe(0), env.legal_actions(0)
    visits, value = cpp_network(SELFPLAY).judge(view, 0, legal, 7, 120, 2, 8)

    mask = _search_mask(legal)
    assert ((visits >= 8) == (mask == 1)).all()
    assert (np.abs(value) <= 1).all()


def _search_mask(legal: list[_engine.Action]) -> np.ndarray:
    from trainerlab import _search

    return _search.action_mask(legal)


def test_by_default_the_report_goes_to_the_terminal(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    argv = ["hitter", "random", "--pairs", "1", "--from", "30000", "--jobs", "1"]
    assert command.main([*argv, "--out", str(tmp_path / "j.npz")]) == 0
    assert capsys.readouterr().out.startswith("hitter against random, seeds 30000-30000")
