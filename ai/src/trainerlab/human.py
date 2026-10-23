"""A human at the keyboard, as a bot: the game loop asks them exactly what it asks any bot."""

from collections.abc import Callable

from trainerlab import _engine
from trainerlab.render import Style, describe_board, describe_move, describe_moves, describe_switch
from trainerlab.screen import Screen

# Reads one line from the player after a prompt.
Ask = Callable[[str], str]


class HumanPlayer:
    """Draws the board and the options, and reads a number until it is a valid one.

    The moves come first; switching opens a second menu, from which `0` comes back. A wrong key
    is a typo, not a bug: it is caught here and asked again, so the referee only ever receives
    a legal choice. `?` shows the details of the active Pokemon's moves.
    """

    def __init__(self, ask: Ask, screen: Screen, style: Style, *, hints: bool = False) -> None:
        self._ask = ask
        self._screen = screen
        self._style = style
        self._hints = hints

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        self._draw(side, view)
        moves = [action for action in legal if action.kind == _engine.ACTION_MOVE]
        switches = [action for action in legal if action.kind == _engine.ACTION_SWITCH]
        labels = [describe_move(action, view, side, self._hints) for action in moves]
        if switches:
            labels.append("Switch…")
        details = lambda: describe_moves(view, side)  # noqa: E731
        while True:
            choice = self._pick("What will you do?", labels, details=details)
            if choice is not None and choice < len(moves):
                return moves[choice]
            team = [action.index for action in switches]
            switch = self._pick("Switch to…", self._switch_labels(side, view, team), back=True)
            if switch is not None:
                return switches[switch]

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        self._draw(side, view)
        choice = self._pick("Who comes in?", self._switch_labels(side, view, options))
        assert choice is not None  # no way back from a replacement
        return options[choice]

    def _switch_labels(self, side: int, view: _engine.BattleState, team: list[int]) -> list[str]:
        return [describe_switch(index, view, side, self._hints) for index in team]

    def _draw(self, side: int, view: _engine.BattleState) -> None:
        self._screen.clear()
        self._screen.panel(f"Turn {view.turn + 1}", describe_board(view, side, self._style))
        history = self._screen.take_history()
        if history:
            self._screen.panel("Last turn", history)

    def _pick(
        self,
        question: str,
        labels: list[str],
        *,
        details: Callable[[], list[str]] | None = None,
        back: bool = False,
    ) -> int | None:
        """The index of the chosen label, or None for `0` when going back is offered."""
        lines = [f"{number}. {label}" for number, label in enumerate(labels, start=1)]
        if back:
            lines.append(self._style.dim("0. Back"))
        if details is not None:
            lines.append(self._style.dim("?. Details of your moves"))
        self._screen.panel(question, lines)
        lowest = 0 if back else 1
        while True:
            answer = self._ask("> ").strip()
            if details is not None and answer == "?":
                self._screen.panel("Your moves", details())
            elif back and answer == "0":
                return None
            elif answer.isdigit() and 1 <= int(answer) <= len(labels):
                choice = int(answer) - 1
                self._screen.note(f"> {labels[choice]}")
                return choice
            else:
                self._screen.message(f"Please type a number between {lowest} and {len(labels)}.")
