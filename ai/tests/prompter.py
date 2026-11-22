"""A prompter answering from a script, for the tests of every screen that asks something."""

from collections.abc import Callable, Sequence
from typing import Any

from trainerlab.terminal.prompts import Line, Option


class Scripted:
    """Answers the questions in order; records each one (kind, message, options).

    A choice must be among the options that can be chosen; a text must pass the question's
    check; an exception in the script is raised, as Ctrl-C would be. With the script used up,
    the next question stops the program as Ctrl-D would.
    """

    def __init__(self, answers: Sequence[Any] = ()) -> None:
        self.answers = list(answers)
        self.asked: list[tuple[str, str, Sequence[Line]]] = []

    def _next(self) -> Any:
        if not self.answers:
            raise EOFError
        answer = self.answers.pop(0)
        if isinstance(answer, BaseException):
            raise answer
        return answer

    def select(self, message: str, options: Sequence[Line], *, search: bool = False) -> Any:
        self.asked.append(("select", message, options))
        answer = self._next()
        open_ = [o.value for o in options if isinstance(o, Option) and o.disabled is None]
        assert answer in open_, f"{answer!r} is not among {open_}"
        return answer

    def text(self, message: str, valid: Callable[[str], bool] | None = None) -> str:
        self.asked.append(("text", message, []))
        answer = self._next()
        assert valid is None or valid(answer), f"{answer!r} does not pass the check"
        return str(answer)

    def pause(self, hint: str) -> str:
        self.asked.append(("pause", hint, []))
        return str(self._next())

    def messages(self) -> list[str]:
        return [message for _, message, _ in self.asked]


class First(Scripted):
    """Takes the first option it can, once the script is used up; leaves at "Play again?",
    goes on at a pause."""

    def __init__(self, answers: Sequence[Any] = (), *, again: Any = "leave") -> None:
        super().__init__(answers)
        self.again = again

    def select(self, message: str, options: Sequence[Line], *, search: bool = False) -> Any:
        if self.answers:
            return super().select(message, options, search=search)
        self.asked.append(("select", message, options))
        if message in ("Play again?", "Rejouer ?"):
            return self.again
        return next(o.value for o in options if isinstance(o, Option) and o.disabled is None)

    def pause(self, hint: str) -> str:
        if self.answers:
            return super().pause(hint)
        self.asked.append(("pause", hint, []))
        return ""
