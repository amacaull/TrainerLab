import numpy as np
import pytest

from trainerlab import _engine
from trainerlab._engine import Action
from trainerlab.bots.lookahead import Forecaster, Mcts, Nash, Strategist
from trainerlab.game.environment import BattleEnvironment, random_team_pair
from trainerlab.game.views import playable
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
    bot = Forecaster(_model_valuing(foe_hp_total=-10.0))

    assert bot.choose_action(0, env.observe(0), env.legal_actions(0)) == Action.move(3)


def test_it_plays_from_either_side() -> None:
    env = BattleEnvironment(["Snorlax"], ["Blissey", "Toxapex"], seed=1)
    bot = Forecaster(_model_valuing(foe_hp_total=-10.0))

    assert bot.choose_action(1, env.observe(1), env.legal_actions(1)) == Action.move(3)


def test_a_replacement_is_the_one_that_leads_to_the_best_odds() -> None:
    env = BattleEnvironment(["Snorlax", "Blissey", "Conkeldurr"], ["Snorlax"], seed=1)
    view = env.observe(0)
    fainted = view.pokemon(0, 0)
    fainted.current_hp = 0
    view.set_pokemon(0, 0, fainted)
    bot = Forecaster(_model_valuing(my_best_damage=10.0))

    assert bot.choose_replacement(0, view, [1, 2]) == 2  # Conkeldurr hits Snorlax hardest


def test_a_finished_battle_is_won_lost_or_drawn_without_the_model() -> None:
    env = BattleEnvironment(["Blissey"], ["Snorlax"], seed=1)
    bot = Forecaster(_model_valuing())
    state = env.observe(0)

    def with_hp(state: _engine.BattleState, side: int) -> _engine.BattleState:
        state = playable(state, 1)
        pokemon = state.pokemon(side, 0)
        pokemon.current_hp = 0
        state.set_pokemon(side, 0, pokemon)
        return state

    assert bot.odds(with_hp(state, 1), 0) == 1.0
    assert bot.odds(with_hp(state, 0), 0) == 0.0
    assert bot.odds(with_hp(with_hp(state, 0), 1), 0) == 0.5
    assert bot.odds(playable(state, 1), 0) == 0.5  # every weight at 0: even odds


def test_the_shipped_model_loads_and_a_model_on_other_features_is_refused() -> None:
    assert Forecaster().model.kind == "logistic"
    names = ("a",)
    stray = WinModel((np.zeros((1, 1)),), (np.zeros(1),), np.zeros(1), np.ones(1), names)
    with pytest.raises(ValueError, match="other features"):
        Forecaster(stray)


def test_the_strategist_plays_like_the_forecaster_when_the_reply_changes_nothing() -> None:
    # Odds that only fall with the foe's HP: whatever Excadrill replies, Seismic Toss is best.
    # (Not Snorlax: its worst reply, Rest, heals it whatever Blissey did, and all options tie.)
    env = BattleEnvironment(["Blissey", "Toxapex"], ["Excadrill"], seed=1)
    bot = Strategist(_model_valuing(foe_hp_total=-10.0))

    assert bot.choose_action(0, env.observe(0), env.legal_actions(0)) == Action.move(3)


def test_the_strategist_fears_the_reply_that_hurts_most() -> None:
    # Odds that only rise with its own HP: Blissey heals rather than attack, as the worst reply
    # (Snorlax's strongest hit) leaves it the most HP after Soft-Boiled.
    env = BattleEnvironment(["Blissey"], ["Snorlax"], seed=1)
    view = env.observe(0)
    blissey = view.active(0)
    blissey.current_hp = blissey.stats.hp // 2
    view.set_pokemon(0, 0, blissey)
    bot = Strategist(_model_valuing(my_hp_total=10.0))

    assert bot.choose_action(0, view, env.legal_actions(0)) == Action.move(0)


def test_nash_plays_an_option_that_dominates_for_sure() -> None:
    # Odds that only fall with the foe's HP: Seismic Toss is best whatever Excadrill replies.
    env = BattleEnvironment(["Blissey", "Toxapex"], ["Excadrill"], seed=1)
    bot = Nash(_model_valuing(foe_hp_total=-10.0), seed=5)
    legal = env.legal_actions(0)

    strategy = bot._search.nash_strategy(env.observe(0), 0, legal)

    assert sum(strategy) == pytest.approx(1.0)
    assert strategy[legal.index(Action.move(3))] == pytest.approx(1.0)
    assert bot.choose_action(0, env.observe(0), legal) == Action.move(3)


def test_nash_draws_from_its_mixed_strategy_with_its_own_seed() -> None:
    # The opening of seed 25 is a real mixed game: two options, near half and half.
    env = BattleEnvironment(*random_team_pair(25), seed=25)
    view, legal = env.observe(0), env.legal_actions(0)
    strategy = Nash()._search.nash_strategy(view, 0, legal)
    mixed = {legal[i] for i, p in enumerate(strategy) if p > 0}
    assert len(mixed) == 2 and sum(strategy) == pytest.approx(1.0)

    def draws(seed: int) -> list[_engine.Action]:
        bot = Nash(seed=seed)
        return [bot.choose_action(0, view, legal) for _ in range(40)]

    assert draws(1) == draws(1)
    assert draws(1) != draws(2)
    assert set(draws(1)) == mixed


def test_mcts_plays_a_legal_option_and_its_seed_fixes_its_decisions() -> None:
    env = BattleEnvironment(*random_team_pair(25), seed=25)
    view, legal = env.observe(0), env.legal_actions(0)

    def decisions(seed: int) -> list[_engine.Action]:
        bot = Mcts(seed=seed)
        return [bot.choose_action(0, view, legal) for _ in range(3)]

    assert all(choice in legal for choice in decisions(1))
    assert decisions(1) == decisions(1)


def test_mcts_finds_the_only_damage_when_damage_is_all_that_counts() -> None:
    # Odds that only fall with the foe's HP: Blissey's Seismic Toss is its only damage.
    env = BattleEnvironment(["Blissey", "Toxapex"], ["Excadrill"], seed=1)
    bot = Mcts(_model_valuing(foe_hp_total=-10.0), seed=2)

    assert bot.choose_action(0, env.observe(0), env.legal_actions(0)) == Action.move(3)
