"""A human at the keyboard, as a bot: the game loop asks them exactly what it asks any bot."""

from collections.abc import Callable

from trainerlab import _engine
from trainerlab.render import Style, describe_action, describe_board

# Reads one line from the player after a prompt, and shows one line to the player.
Ask = Callable[[str], str]
Say = Callable[[str], None]


class HumanPlayer:
    """Shows the board and the options, and reads a number until it is a valid one.

    A wrong key is a typo, not a bug: it is caught here and asked again, so the referee only
    ever receives a legal choice.
    """

    def __init__(self, ask: Ask, say: Say, style: Style) -> None:
        self._ask = ask
        self._say = say
        self._style = style

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        for line in describe_board(view, side, self._style):
            self._say(line)
        labels = [describe_action(action, view, side) for action in legal]
        return legal[self._pick("What will you do?", labels)]

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        labels = [describe_action(_engine.Action.switch(index), view, side) for index in options]
        return options[self._pick("Who comes in?", labels)]

    def _pick(self, question: str, labels: list[str]) -> int:
        self._say(question)
        for number, label in enumerate(labels, start=1):
            self._say(f"  {number}. {label}")
        while True:
            answer = self._ask("> ").strip()
            if answer.isdigit() and 1 <= int(answer) <= len(labels):
                return int(answer) - 1
            self._say(f"Please type a number between 1 and {len(labels)}.")
