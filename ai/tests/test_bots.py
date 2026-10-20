from trainerlab._engine import Action
from trainerlab.bots import RandomBot
from trainerlab.environment import BattleEnvironment


def _new_battle() -> BattleEnvironment:
    return BattleEnvironment(["Dragapult", "Snorlax", "Toxapex"], ["Blissey"], seed=42)


def test_the_random_bot_chooses_a_legal_action() -> None:
    env = _new_battle()
    legal = env.legal_actions(0)
    bot = RandomBot(seed=1)

    action = bot.choose_action(0, env.observe(0), legal)

    assert action in legal


def test_the_random_bot_chooses_a_legal_replacement() -> None:
    env = BattleEnvironment(["Dragapult", "Snorlax"], ["KommoO", "Blissey"], seed=42)
    env.play_turn(env.legal_actions(0)[0], env.legal_actions(1)[0])
    options = env.legal_replacements(1)
    bot = RandomBot(seed=1)

    team_index = bot.choose_replacement(1, env.observe(1), options)

    assert team_index in options


def test_with_a_single_option_the_random_bot_takes_it() -> None:
    env = _new_battle()
    only_option = [Action.move(2)]
    bot = RandomBot(seed=1)

    action = bot.choose_action(0, env.observe(0), only_option)

    assert action == Action.move(2)


def test_the_same_seed_gives_the_same_choices() -> None:
    env = _new_battle()
    legal = env.legal_actions(0)
    first, second = RandomBot(seed=7), RandomBot(seed=7)

    first_choices = [first.choose_action(0, env.observe(0), legal) for _ in range(20)]
    second_choices = [second.choose_action(0, env.observe(0), legal) for _ in range(20)]

    assert first_choices == second_choices


def test_the_random_bot_does_not_always_choose_the_same() -> None:
    env = _new_battle()
    legal = env.legal_actions(0)
    bot = RandomBot(seed=7)

    choices = {bot.choose_action(0, env.observe(0), legal) for _ in range(50)}

    assert len(choices) > 1
