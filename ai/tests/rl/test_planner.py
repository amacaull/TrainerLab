import numpy as np
import pytest
import torch

from trainerlab import _engine, _search
from trainerlab.bots.roster import ALPHAZERO, RULE12, RULE13, SELFPLAY, factory
from trainerlab.experiments import judge
from trainerlab.game.environment import BattleEnvironment, random_team_pair
from trainerlab.rl.net import PolicyValueNet, Shape
from trainerlab.rl.planner import Planner, cpp_network, to_cpp


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


def test_the_cpp_attention_network_gives_what_pytorch_gives() -> None:
    torch.manual_seed(5)
    net = PolicyValueNet(Shape(**{**Shape.for_catalog().__dict__, "layers": 2})).eval()
    cpp = to_cpp(net)
    for state in _search.openings(16):
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
            assert abs(value - float(expected_value)) < 1e-4


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


def test_alphazero_is_the_planner_with_rule_14_s_network_and_rule12_rule13_with_theirs() -> None:
    assert ALPHAZERO.is_file() and RULE12.is_file() and RULE13.is_file()
    assert isinstance(factory("alphazero")(0), Planner)
    assert isinstance(factory("rule12@8")(0), Planner)
    assert isinstance(factory("rule13@8")(0), Planner)
    assert judge.NETWORK == RULE12  # the judge of palier 7 stays the same


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


def test_alphazero_searches_its_replacements_and_the_bots_measured_do_not() -> None:
    # Palier 10.4: a replacement after a knock-out was the network's favourite, never searched.
    for name in ("planner", "rule12", "rule13", f"plan:{SELFPLAY}", "rule13@8"):
        assert factory(name)(0)._replacements is False  # type: ignore[attr-defined]
    for name in ("alphazero", "alphazero@8"):
        assert factory(name)(0)._replacements is True  # type: ignore[attr-defined]

    env = BattleEnvironment(["Snorlax", "Blissey", "Conkeldurr"], ["Excadrill"], seed=1)
    view = env.observe(0)
    snorlax = view.active(0)
    snorlax.current_hp = 0
    view.set_pokemon(0, 0, snorlax)
    searching = Planner(SELFPLAY, seed=3, simulations=8, replacements=True)
    choice = searching.choose_replacement(0, view, [1, 2])
    assert choice in (1, 2)
    again = Planner(SELFPLAY, seed=3, simulations=8, replacements=True)
    assert again.choose_replacement(0, view, [1, 2]) == choice
