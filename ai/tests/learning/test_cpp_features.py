"""The C++ features (search/src/features.cpp) against the Python ones, the reference."""

import numpy as np
import pytest

from trainerlab import _engine, _search
from trainerlab.bots.roster import BOTS
from trainerlab.game.decisions import decisions
from trainerlab.game.environment import DATA_DIR
from trainerlab.learning.features import feature_names, features


def setup_module() -> None:
    _search.init(str(DATA_DIR))


def test_the_columns_are_the_same_in_the_same_order() -> None:
    assert tuple(_search.feature_names()) == feature_names()


def test_every_value_is_the_same_bit_for_bit_on_thousands_of_positions() -> None:
    # Both sides, turns and replacements, against bots that play well and one that does not:
    # statuses, stages, weather, hazards and hidden Pokemon all come up.
    views = [
        (d.view, d.side)
        for a, b, seeds in (
            ("calculator", "survivor", range(40000, 40012)),
            ("random", "dodger", range(40100, 40112)),
        )
        for d in decisions(BOTS[a], BOTS[b], seeds=seeds)
    ]
    assert len(views) > 1000

    for view, side in views:
        expected = features(view, side)
        actual = _search.features(view, side)
        assert actual.dtype == np.float32
        assert np.array_equal(actual, expected), (view.turn, side)


def test_loading_again_from_the_same_place_changes_nothing() -> None:
    _search.init(str(DATA_DIR))


def test_another_catalog_is_refused() -> None:
    with pytest.raises(RuntimeError, match="already loaded"):
        _search.init("/elsewhere")


def test_the_engine_binding_and_the_search_share_their_states() -> None:
    (state,) = _search.openings(1)
    assert isinstance(state, _engine.BattleState)
    assert _search.openings(2, turns=0)[0].turn == 0
