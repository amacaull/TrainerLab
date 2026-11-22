"""A prompter answering from a script, for the tests of every screen that asks something."""

from collections.abc import Callable, Sequence
from typing import Any

from trainerlab.terminal.prompts import Line, Option

DEFAULT = object()  # an answer that takes the question's default, as Enter does


class Scripted:
    """Answers the questions in order; records each one (kind, message, options, default).

    A choice must be among the options that can be chosen; a text must pass the question's check.
    With the script used up, the next question stops the program as Ctrl-D would.
    """

    def __init__(self, answers: Sequence[Any] = ()) -> None:
        self.answers = list(answers)
        self.asked: list[tuple[str, str, Sequence[Line], Any]] = []

    def _next(self) -> Any:
        if not self.answers:
            raise EOFError
        answer = self.answers.pop(0)
        if isinstance(answer, BaseException):
            raise answer  # Ctrl-C, as a question would raise it
        return answer

    def select(
        self, message: str, options: Sequence[Line], default: Any = None, *, search: bool = False
    ) -> Any:
        self.asked.append(("select", message, options, default))
        answer = self._next()
        if answer is DEFAULT:
            return default
        open_ = [o.value for o in options if isinstance(o, Option) and o.disabled is None]
        assert answer in open_, f"{answer!r} is not among {open_}"
        return answer

    def text(
        self, message: str, default: str = "", valid: Callable[[str], bool] | None = None
    ) -> str:
        self.asked.append(("text", message, [], default))
        answer = self._next()
        if answer is DEFAULT:
            return default
        assert valid is None or valid(answer), f"{answer!r} does not pass the check"
        return str(answer)

    def autocomplete(self, message: str, choices: list[str], valid: Callable[[str], bool]) -> str:
        self.asked.append(("autocomplete", message, [], None))
        answer = str(self._next())
        assert valid(answer), f"{answer!r} does not pass the check"
        return answer

    def pause(self, hint: str) -> str:
        self.asked.append(("pause", hint, [], None))
        return str(self._next())

    def messages(self) -> list[str]:
        return [message for _, message, _, _ in self.asked]


class First(Scripted):
    """Takes the first option it can, once the script is used up; leaves at "Play again?"."""

    def __init__(self, answers: Sequence[Any] = (), *, again: Any = "leave") -> None:
        super().__init__(answers)
        self.again = again

    def select(
        self, message: str, options: Sequence[Line], default: Any = None, *, search: bool = False
    ) -> Any:
        if self.answers:
            return super().select(message, options, default, search=search)
        self.asked.append(("select", message, options, default))
        if message in ("Play again?", "Rejouer ?"):
            return self.again
        return next(o.value for o in options if isinstance(o, Option) and o.disabled is None)
