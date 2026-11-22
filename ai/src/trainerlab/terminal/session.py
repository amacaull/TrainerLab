"""A battle against a bot in the terminal: the board before each choice, the turns as they
happen, and the result at the end."""

from trainerlab import _engine
from trainerlab.game.battle import play_battle
from trainerlab.game.environment import BattleEnvironment
from trainerlab.game.player import Bot
from trainerlab.i18n import t
from trainerlab.terminal.human import HumanPlayer
from trainerlab.terminal.prompts import Prompter
from trainerlab.terminal.render import Style, Teller, board
from trainerlab.terminal.screen import Screen
from trainerlab.terminal.stats import BattleStats

ME = 0  # the human plays side 0


def standing(view: _engine.BattleState, side: int) -> int:
    team = [view.pokemon(side, i) for i in range(view.team_size[side])]
    return sum(1 for pokemon in team if pokemon.is_empty() or not pokemon.is_fainted())


def play_session(
    env: BattleEnvironment,
    opponent: Bot,
    bot: str,
    trainer: str,
    seed: int,
    screen: Screen,
    style: Style,
    prompter: Prompter,
) -> int | None:
    """Plays the battle with the human as side 0, against `bot` named after `trainer`; returns
    the winning side, None for a draw. Ctrl-C or a closed input stops it (KeyboardInterrupt,
    EOFError)."""
    trainers = (None, trainer)
    teller = Teller(ME, style, trainers)
    stats = BattleStats()
    before = env.observe(ME)

    def draw(view: _engine.BattleState) -> None:
        screen.clear()
        title = t("board.turn", turn=view.turn + 1, bot=bot)
        screen.panel(title, board(view, ME, style, trainers))
        screen.lines(screen.take_history())

    def tell(events: list[_engine.Event]) -> None:
        nonlocal before
        after = env.observe(ME)
        lines = teller.narrate(list(events), before, after)
        screen.narrate(lines)
        stats.record(lines)
        before = after

    tell(env.opening_events)
    winner = play_battle(env, HumanPlayer(prompter, draw), opponent, tell)

    final = env.observe(ME)
    draw(final)
    if winner == ME:
        title, colour = t("play.victory"), "green"
        lines = [t("play.won", trainer=trainer, bot=bot, turns=env.turn)]
        lines.append(t("play.left_mine", count=standing(final, ME)))
    elif winner is None:
        title, colour = t("play.draw"), "yellow"
        lines = [t("play.drawn", trainer=trainer, bot=bot, turns=env.turn)]
    else:
        title, colour = t("play.defeat"), "red"
        lines = [t("play.lost", trainer=trainer, bot=bot, turns=env.turn)]
        lines.append(t("play.left_theirs", trainer=trainer, count=standing(final, 1 - ME)))
    mvp = stats.mvp(final, ME)
    lines += ([mvp] if mvp else []) + [t("play.seed", seed=seed)]
    screen.panel(style.bold(title), "\n".join(lines), colour if style.enabled else "")
    return winner
