"""A human at the keyboard, as a bot: the game loop asks them exactly what it asks any bot."""

from collections.abc import Callable

from trainerlab import _engine
from trainerlab.i18n import names, t
from trainerlab.terminal.prompts import Line, Option, Prompter, Separator
from trainerlab.terminal.render import card, move_option, switch_options

SWITCH = "switch"  # the battle menu's entry that opens the team
BACK = "back"  # the team's entry that goes back to the moves


class HumanPlayer:
    """Draws the board, then asks for a move, or for a Pokemon to switch to.

    The moves come first, each with its card under the list; switching opens the team, from
    which Back returns. Only legal options can be chosen, so the referee always gets one.
    """

    def __init__(self, prompter: Prompter, draw: Callable[[_engine.BattleState], None]) -> None:
        self._prompter = prompter
        self._draw = draw

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        self._draw(view)
        moves = [action for action in legal if action.kind == _engine.ACTION_MOVE]
        switches = [action for action in legal if action.kind == _engine.ACTION_SWITCH]
        name = names.species(view.active(side).species_id)
        options: list[Line] = [move_option(action, view, side) for action in moves]
        if switches:
            entry = t("battle.switch")
            options += [Separator(), Option(SWITCH, entry, card(entry, [t("battle.switch_card")]))]
        team = [action.index for action in switches]
        while True:
            choice = self._prompter.select(t("battle.what", name=name), options)
            if choice != SWITCH:
                return choice  # type: ignore[no-any-return]
            lines: list[Line] = [
                *switch_options(view, side, team),
                Separator(),
                Option(BACK, t("back")),
            ]
            index = self._prompter.select(t("battle.instead", name=name), lines)
            if index != BACK:
                return switches[team.index(index)]

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        self._draw(view)
        return int(self._prompter.select(t("battle.replace"), switch_options(view, side, options)))
