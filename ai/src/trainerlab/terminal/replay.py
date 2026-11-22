"""A battle between two bots, replayed turn by turn: the board, what each bot chose, then what
happened. Watched at one's pace (Enter, autoplay, quit), or told at once into a pipe."""

import time
from collections.abc import Callable

from trainerlab import _engine
from trainerlab.game.battle import play_battle
from trainerlab.game.environment import BattleEnvironment
from trainerlab.game.player import Bot
from trainerlab.i18n import names, t
from trainerlab.terminal.render import Style, Teller, board
from trainerlab.terminal.screen import Screen


class ReplayStopped(Exception):  # noqa: N818 (a stop, not an error)
    """The viewer quit the replay."""


class Chooser:
    """Plays as the bot it wraps, and notes what it chose, to tell it before the turn."""

    def __init__(self, bot: Bot, name: str, chosen: list[str]) -> None:
        self._bot, self._name, self._chosen = bot, name, chosen

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        action = self._bot.choose_action(side, view, legal)
        active = view.active(side)
        if action.kind == _engine.ACTION_MOVE:
            move_id = active.move_ids[action.index]
            text = names.move(_engine.STRUGGLE if active.pp[action.index] == 0 else move_id)
            if active.charging_move_id != _engine.NO_MOVE:
                text = names.move(active.charging_move_id)
        else:
            text = names.species(view.pokemon(side, action.index).species_id)
        self._chosen.append(t("watch.chose", bot=self._name, choice=text))
        return action

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        index = self._bot.choose_replacement(side, view, options)
        text = names.species(view.pokemon(side, index).species_id)
        self._chosen.append(t("watch.chose", bot=self._name, choice=text))
        return index


def replay(
    env: BattleEnvironment,
    bots: tuple[Bot, Bot],
    bot_names: tuple[str, str],
    a_side: int,
    screen: Screen,
    style: Style,
    pause: Callable[[str], str] | None = None,
    *,
    autoplay: float = 0.0,
    sleep: Callable[[float], None] = time.sleep,
) -> int | None:
    """Plays the battle, told from bot A's side; returns the winner. With `pause`, waits after
    each turn for Enter (next), a (autoplay, `autoplay` seconds between two turns) or q (quit:
    ReplayStopped)."""
    trainers = (bot_names[0], bot_names[1])
    teller = Teller(a_side, style, trainers)
    chosen: list[str] = []
    before = env.observe(a_side)
    auto = [False]
    a_name, b_name = bot_names[a_side], bot_names[1 - a_side]

    def tell(events: list[_engine.Event]) -> None:
        nonlocal before
        after = env.observe(a_side)
        if pause is not None:
            screen.clear()
        title = t("board.watch", turn=before.turn + 1, a=a_name, side=a_side, b=b_name)
        screen.panel(title, board(before, a_side, style, trainers))
        screen.lines([style.bold(line) for line in chosen])
        chosen.clear()
        screen.narrate(teller.narrate(list(events), before, after))
        before = after
        if pause is None or env.is_over():
            return
        if auto[0]:
            sleep(autoplay)
            return
        key = pause(t("watch.keys") + " ")
        if key == "q":
            raise ReplayStopped
        auto[0] = key == "a"

    after = env.observe(a_side)
    screen.narrate(teller.narrate(list(env.opening_events), before, after))
    before = after
    choosers = (Chooser(bots[0], bot_names[0], chosen), Chooser(bots[1], bot_names[1], chosen))
    return play_battle(env, *choosers, tell)
