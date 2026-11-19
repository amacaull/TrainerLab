import numpy as np
import pytest
import torch

from trainerlab import _engine, _search
from trainerlab.bots.roster import ALPHAZERO, SELFPLAY, factory
from trainerlab.game.environment import BattleEnvironment, random_team_pair
from trainerlab.rl.net import PolicyValueNet
from trainerlab.rl.planner import Planner, cpp_network


def test_the_cpp_network_gives_what_pytorch_gives() -> None:
    net = PolicyValueNet.load(SELFPLAY)
    cpp = cpp_network(SELFPLAY)
    for state in _search.openings(32):
        for side in (0, 1):
            view = _engine.observe(state, side)
            mask = _search.action_mask(_engine.legal_actions(state, side))
            logits, value = cpp.evaluate(view, side, mask)
            pokemon, ids, field = _search.encode(view, side)
            with torch.no_grad():
                expected, expected_value = net(
                    torch.from_numpy(pokemon)[None],
                    torch.from_numpy(ids).long()[None],
                    torch.from_numpy(field)[None],
                    torch.from_numpy(mask)[None],
                )
            legal = mask == 1
            assert np.allclose(logits[legal], expected[0].numpy()[legal], atol=1e-4)
            assert (logits[~legal] == expected[0].numpy()[~legal]).all()  # masked alike
            assert abs(value - float(expected_value)) < 1e-4
            assert int(np.argmax(logits)) == int(expected[0].argmax())


def test_the_planner_plays_a_legal_option_and_its_seed_fixes_its_decisions() -> None:
    env = BattleEnvironment(*random_team_pair(25), seed=25)
    view, legal = env.observe(0), env.legal_actions(0)

    def decisions(seed: int) -> list[_engine.Action]:
        bot = Planner(SELFPLAY, seed=seed)
        return [bot.choose_action(0, view, legal) for _ in range(2)]

    assert all(choice in legal for choice in decisions(1))
    assert decisions(1) == decisions(1)
    assert Planner(SELFPLAY).choose_action(0, view, legal[:1]) == legal[0]  # nothing to search


def test_the_planner_replaces_with_the_network_s_favourite_among_the_options() -> None:
    env = BattleEnvironment(["Snorlax", "Blissey", "Conkeldurr"], ["Excadrill"], seed=1)
    view = env.observe(0)
    snorlax = view.active(0)
    snorlax.current_hp = 0
    view.set_pokemon(0, 0, snorlax)
    bot = factory("planner")(0)

    assert isinstance(bot, Planner)
    assert bot.choose_replacement(0, view, [2]) == 2
    assert bot.choose_replacement(0, view, [1, 2]) in (1, 2)


def test_plan_searches_with_the_network_of_a_file() -> None:
    bot = factory(f"plan:{SELFPLAY}")(3)

    assert isinstance(bot, Planner)


def test_alphazero_is_the_planner_with_rule_12_s_network() -> None:
    assert ALPHAZERO.is_file()
    assert isinstance(factory("alphazero")(0), Planner)


def test_a_search_bot_searches_as_many_simulations_as_asked() -> None:
    deep, plan = factory("alphazero@8")(0), factory(f"plan:{SELFPLAY}@4")(0)
    assert isinstance(deep, Planner) and isinstance(plan, Planner)
    assert deep._simulations == 8 and plan._simulations == 4
    assert factory("alphazero")(0)._simulations == 200  # type: ignore[attr-defined]

    env = BattleEnvironment(*random_team_pair(25), seed=25)
    view, legal = env.observe(0), env.legal_actions(0)
    assert plan.choose_action(0, view, legal) in legal
    with pytest.raises(ValueError, match="at least one simulation"):
        plan.network.plan_action(view, 0, legal, 1, 0)
