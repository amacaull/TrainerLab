import argparse
import shutil
from pathlib import Path

import pytest

from trainerlab.bots.lookahead import Forecaster, Mcts, Nash, Strategist
from trainerlab.bots.roster import BOTS, bot_spec, factory, slug
from trainerlab.learning.model import SHIPPED


def test_a_bot_named_alone_is_the_one_listed() -> None:
    assert factory("matcher") is BOTS["matcher"]


def test_the_forecaster_can_play_with_another_model(tmp_path: Path) -> None:
    model = tmp_path / "gen2.npz"
    shutil.copy(SHIPPED, model)

    bot = factory(f"forecaster:{model}")(0)

    assert isinstance(bot, Forecaster)
    assert bot_spec(f"forecaster:{model}") == f"forecaster:{model}"


@pytest.mark.parametrize(
    ("text", "error"),
    [
        ("genius", "unknown bot 'genius'"),
        ("matcher:m.npz", "unknown bot 'matcher:m.npz'"),  # only the forecaster takes a model
        ("forecaster:nowhere.npz", "no such model: nowhere.npz"),
    ],
)
def test_a_wrong_name_is_refused_with_why(text: str, error: str) -> None:
    with pytest.raises(argparse.ArgumentTypeError, match=error):
        bot_spec(text)


def test_a_bot_with_a_model_gets_a_name_fit_for_a_file() -> None:
    assert slug("forecaster:models/gen2.npz") == "forecaster-gen2"
    assert slug("survivor") == "survivor"


def test_the_strategist_takes_a_model_too(tmp_path: Path) -> None:
    model = tmp_path / "gen2.npz"
    shutil.copy(SHIPPED, model)

    assert isinstance(factory(f"strategist:{model}")(0), Strategist)


def test_nash_takes_a_model_and_its_seed(tmp_path: Path) -> None:
    model = tmp_path / "gen2.npz"
    shutil.copy(SHIPPED, model)

    assert isinstance(factory(f"nash:{model}")(3), Nash)
    assert isinstance(factory("nash")(3), Nash)
    assert isinstance(factory(f"mcts:{model}")(3), Mcts)
    assert isinstance(factory("mcts")(3), Mcts)


def test_a_network_bot_needs_a_network_file(tmp_path: Path) -> None:
    with pytest.raises(argparse.ArgumentTypeError, match="net:FILE.pt"):
        bot_spec("net")
    with pytest.raises(argparse.ArgumentTypeError, match="no such network"):
        bot_spec("net:missing.pt")
    network = tmp_path / "n.pt"
    network.write_bytes(b"")
    assert bot_spec(f"net:{network}") == f"net:{network}"
    assert bot_spec(f"plan:{network}") == f"plan:{network}"
    with pytest.raises(argparse.ArgumentTypeError, match="plan:FILE.pt"):
        bot_spec("plan:model.npz")


def test_a_search_bot_takes_its_number_of_simulations(tmp_path: Path) -> None:
    network = tmp_path / "n.pt"
    network.write_bytes(b"")
    assert bot_spec("alphazero@800") == "alphazero@800"
    assert bot_spec("planner@50") == "planner@50"
    assert bot_spec(f"plan:{network}@800") == f"plan:{network}@800"
    for wrong in ("hitter@800", "alphazero@1", "alphazero@many", "net:n.pt@800"):
        with pytest.raises(argparse.ArgumentTypeError, match="@N, N simulations"):
            bot_spec(wrong)
    with pytest.raises(argparse.ArgumentTypeError, match="no such network"):
        bot_spec("plan:missing.pt@800")
    assert slug("alphazero@800") == "alphazero-800"
    assert slug("plan:runs/az.pt@800") == "plan-az-800"


def test_every_bot_has_its_rule_and_the_featured_ones_are_bots() -> None:
    from trainerlab.bots.roster import FEATURED, RULES

    assert set(RULES) == set(BOTS)
    assert RULES["random"] is None and RULES["hitter"] == 1 and RULES["alphazero"] == 17
    names = ("zero", "deepzero", "focus", "scout", "scholar")
    assert [RULES[name] for name in names] == [12, 13, 14, 15, 16]
    assert FEATURED == ["alphazero", "mcts", "forecaster", "calculator", "hitter", "random"]
