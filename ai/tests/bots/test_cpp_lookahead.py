"""The C++ rules 6 and 7 against the Python version they replaced (reference_lookahead.py)."""

import reference_lookahead as reference

from trainerlab import _engine
from trainerlab.bots.lookahead import Forecaster, Strategist
from trainerlab.bots.roster import BOTS
from trainerlab.bots.rules import calculator
from trainerlab.game.decisions import decisions
from trainerlab.game.environment import BattleEnvironment
from trainerlab.game.views import playable


def test_the_forecaster_makes_every_decision_the_python_one_made() -> None:
    bot = reference.Forecaster()
    played = [
        d
        for d in decisions(BOTS["forecaster"], BOTS["survivor"], seeds=range(60000, 60010))
        if d.by_a
    ]
    assert len(played) > 400
    assert {d.kind for d in played} == {"action", "replacement"}

    for d in played:
        if d.kind == "action":
            actions = [a for a in d.legal if isinstance(a, _engine.Action)]
            assert d.choice == bot.choose_action(d.side, d.view, actions), (d.seed, d.turn)
        else:
            options = [i for i in d.legal if isinstance(i, int)]
            assert d.choice == bot.choose_replacement(d.side, d.view, options), (d.seed, d.turn)


def test_the_strategist_makes_every_decision_the_python_one_made() -> None:
    bot = reference.Strategist()
    played = [
        d
        for d in decisions(BOTS["strategist"], BOTS["calculator"], seeds=range(60100, 60103))
        if d.by_a and d.kind == "action"
    ]
    assert len(played) > 100

    for d in played:
        actions = [a for a in d.legal if isinstance(a, _engine.Action)]
        assert d.choice == bot.choose_action(d.side, d.view, actions), (d.seed, d.turn)


def test_the_foe_it_assumes_plays_as_rule_5() -> None:
    forecaster = Forecaster()
    rule_5 = calculator()
    checked = 0
    for d in decisions(BOTS["calculator"], BOTS["survivor"], seeds=range(60200, 60206)):
        if d.kind != "action":
            continue
        state = playable(d.view, 1 - d.side)
        view = _engine.observe(state, d.side)
        legal = _engine.legal_actions(state, d.side)
        assert forecaster._search.calculator_action(view, d.side, legal) == rule_5.choose_action(
            d.side, view, legal
        )
        checked += 1
    assert checked > 200


def test_the_odds_differ_from_python_by_rounding_only() -> None:
    env = BattleEnvironment(["Blissey", "Toxapex"], ["Snorlax", "Gyarados"], seed=3)
    state = playable(env.observe(0), 1)

    assert abs(Forecaster().odds(state, 0) - reference.Forecaster().odds(state, 0)) < 1e-12
    assert isinstance(Strategist(), Forecaster)
