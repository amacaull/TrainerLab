"""The game loop: asks each bot for its choices and hands them to the referee until the end."""

from trainerlab.bots import Bot
from trainerlab.environment import BattleEnvironment


def play_battle(env: BattleEnvironment, bot0: Bot, bot1: Bot) -> int | None:
    """Plays the battle to its end and returns the winning side, or None for a draw.

    A bot only ever sees its own side's view and the options the referee offers. An illegal
    choice is not caught here: the referee's error stops the battle, so a faulty bot is noticed.
    """
    bots = (bot0, bot1)
    while not env.is_over():
        pending = env.pending_replacements()
        if pending:
            # One replacement at a time, then ask again: an incoming Pokemon can faint on entry
            # (Stealth Rock...), which changes who must replace next, or even ends the battle.
            side = pending[0]
            team_index = bots[side].choose_replacement(
                side, env.observe(side), env.legal_replacements(side)
            )
            env.replace(side, team_index=team_index)
        else:
            action0, action1 = (
                bots[side].choose_action(side, env.observe(side), env.legal_actions(side))
                for side in (0, 1)
            )
            env.play_turn(action0, action1)
    return env.winner()
