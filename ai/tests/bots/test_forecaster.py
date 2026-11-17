import numpy as np
import pytest

from trainerlab import _engine
from trainerlab._engine import Action
from trainerlab.bots.lookahead import ForecastBot, StrategistBot, _replies, playable
from trainerlab.game.environment import BattleEnvironment
from trainerlab.learning.features import feature_names
from trainerlab.learning.model import WinModel


def _model_valuing(**weights: float) -> WinModel:
    """A model whose odds rise with the features named, nothing else: what the bot then wants."""
    names = feature_names()
    w = np.zeros((len(names), 1))
    for name, weight in weights.items():
        w[names.index(name), 0] = weight
    return WinModel((w,), (np.zeros(1),), np.zeros(len(names)), np.ones(len(names)), names)


def test_a_view_becomes_a_state_the_engine_accepts_and_observe_hides_again() -> None:
    env = BattleEnvironment(["Blissey"], ["Snorlax", "Gyarados", "KommoO"], seed=1)
    view = env.observe(0)

    state = playable(view, 1)

    _engine.validate_state(state)
    assert state.pokemon(1, 1).species_id == view.active(1).species_id
    assert not state.pokemon(1, 1).revealed
    assert _engine.observe(state, 0) == view


def test_it_plays_the_option_that_leads_to_the_best_odds() -> None:
    # Odds that only fall with the foe's HP: Blissey must play Seismic Toss, its only damage.
    env = BattleEnvironment(["Blissey", "Toxapex"], ["Snorlax"], seed=1)
    bot = ForecastBot(_model_valuing(foe_hp_total=-10.0))

    assert bot.choose_action(0, env.observe(0), env.legal_actions(0)) == Action.move(3)


def test_it_plays_from_either_side() -> None:
    env = BattleEnvironment(["Snorlax"], ["Blissey", "Toxapex"], seed=1)
    bot = ForecastBot(_model_valuing(foe_hp_total=-10.0))

    assert bot.choose_action(1, env.observe(1), env.legal_actions(1)) == Action.move(3)


def test_a_replacement_is_the_one_that_leads_to_the_best_odds() -> None:
    env = BattleEnvironment(["Snorlax", "Blissey", "Conkeldurr"], ["Snorlax"], seed=1)
    view = env.observe(0)
    fainted = view.pokemon(0, 0)
    fainted.current_hp = 0
    view.set_pokemon(0, 0, fainted)
    bot = ForecastBot(_model_valuing(my_best_damage=10.0))

    assert bot.choose_replacement(0, view, [1, 2]) == 2  # Conkeldurr hits Snorlax hardest


def test_a_finished_battle_is_won_lost_or_drawn_without_the_model() -> None:
    env = BattleEnvironment(["Blissey"], ["Snorlax"], seed=1)
    bot = ForecastBot(_model_valuing())
    state = env.observe(0)

    def with_hp(state: _engine.BattleState, side: int) -> _engine.BattleState:
        state = playable(state, 1)
        pokemon = state.pokemon(side, 0)
        pokemon.current_hp = 0
        state.set_pokemon(side, 0, pokemon)
        return state

    assert bot._value(with_hp(state, 1), 0) == 1.0
    assert bot._value(with_hp(state, 0), 0) == 0.0
    assert bot._value(with_hp(with_hp(state, 0), 1), 0) == 0.5
    assert bot._value(playable(state, 1), 0) == 0.5  # every weight at 0: even odds


def test_the_shipped_model_loads_and_a_model_on_other_features_is_refused() -> None:
    assert ForecastBot()._model.kind == "logistic"
    names = ("a",)
    stray = WinModel((np.zeros((1, 1)),), (np.zeros(1),), np.zeros(1), np.ones(1), names)
    with pytest.raises(ValueError, match="other features"):
        ForecastBot(stray)


def test_the_strategist_plays_like_the_forecaster_when_the_reply_changes_nothing() -> None:
    # Odds that only fall with the foe's HP: whatever Excadrill replies, Seismic Toss is best.
    # (Not Snorlax: its worst reply, Rest, heals it whatever Blissey did, and all options tie.)
    env = BattleEnvironment(["Blissey", "Toxapex"], ["Excadrill"], seed=1)
    bot = StrategistBot(_model_valuing(foe_hp_total=-10.0))

    assert bot.choose_action(0, env.observe(0), env.legal_actions(0)) == Action.move(3)


def test_the_strategist_fears_the_reply_that_hurts_most() -> None:
    # Odds that only rise with its own HP: Blissey heals rather than attack, as the worst reply
    # (Snorlax's strongest hit) leaves it the most HP after Soft-Boiled.
    env = BattleEnvironment(["Blissey"], ["Snorlax"], seed=1)
    view = env.observe(0)
    blissey = view.active(0)
    blissey.current_hp = blissey.stats.hp // 2
    view.set_pokemon(0, 0, blissey)
    bot = StrategistBot(_model_valuing(my_hp_total=10.0))

    assert bot.choose_action(0, view, env.legal_actions(0)) == Action.move(0)


def test_the_foe_replies_are_its_moves_and_its_switches_to_pokemon_seen() -> None:
    env = BattleEnvironment(["Blissey"], ["Snorlax", "Gyarados", "KommoO"], seed=1)
    view = env.observe(0)
    assert all(a.kind == _engine.ACTION_MOVE for a in _replies(view, playable(view, 1), 1))

    gyarados = _engine.make_combatant(_engine.find_species_id("Gyarados"))
    gyarados.revealed = 1
    view.set_pokemon(1, 1, gyarados)
    switches = [a for a in _replies(view, playable(view, 1), 1) if a.kind == _engine.ACTION_SWITCH]

    assert switches == [Action.switch(1)]
