import argparse
import shutil
from pathlib import Path

import pytest

from trainerlab.bots.lookahead import Forecaster, Nash, Strategist
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
