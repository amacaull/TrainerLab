"""The questions the interface asks: a choice in a list, a text, a name with completion.

Every question goes through a `Prompter`, so that the screens are tested with scripted answers;
the real one asks with questionary (arrow keys, Enter, filtering as one types).
"""

from collections.abc import Callable, Sequence
from dataclasses import dataclass
from typing import Any, Protocol

import questionary
from prompt_toolkit.styles import Style

# The look of the questions: the line under the cursor in cyan; the default is preselected,
# not painted as already chosen (questionary's "selected" is reversed: a white block).
STYLE = Style(
    [
        ("selected", "noreverse"),
        ("highlighted", "fg:ansicyan bold"),
        ("pointer", "fg:ansicyan bold"),
        ("qmark", "fg:ansicyan"),
        ("answer", "fg:ansicyan"),
        ("separator", "fg:ansibrightblack"),
        ("instruction", "fg:ansibrightblack"),
    ]
)
# A label: plain text, or (style, text) pieces, prompt_toolkit's styles ("bold", "fg:ansired").
Label = str | list[tuple[str, str]]


@dataclass(frozen=True)
class Option:
    """One line of a list: what choosing it returns, how it reads, the text shown under the
    list while it is highlighted, and why it cannot be chosen, if it cannot."""

    value: Any
    label: Label
    description: str | None = None
    disabled: str | None = None


@dataclass(frozen=True)
class Separator:
    """A line between the groups of a list, with a title or none."""

    title: str = ""


Line = Option | Separator


class Prompter(Protocol):
    def select(
        self, message: str, options: Sequence[Line], default: Any = None, *, search: bool = False
    ) -> Any: ...

    def text(
        self, message: str, default: str = "", valid: Callable[[str], bool] | None = None
    ) -> str: ...

    def autocomplete(
        self, message: str, choices: list[str], valid: Callable[[str], bool]
    ) -> str: ...

    def pause(self, hint: str) -> str: ...


def plain(label: Label) -> str:
    return label if isinstance(label, str) else "".join(text for _, text in label)


def _asked(answer: Any) -> Any:
    if answer is None:  # questionary's answer to Ctrl-C
        raise KeyboardInterrupt
    return answer


class QuestionaryPrompter:
    def select(
        self, message: str, options: Sequence[Line], default: Any = None, *, search: bool = False
    ) -> Any:
        choices: list[questionary.Choice | questionary.Separator] = []
        chosen = None
        for line in options:
            if isinstance(line, Separator):
                choices.append(questionary.Separator(line.title and f"── {line.title} ──"))
                continue
            # questionary filters a list on its titles as text: no styled pieces then.
            label = plain(line.label) if search else line.label
            choice = questionary.Choice(
                label, line.value, disabled=line.disabled, description=line.description
            )
            chosen = choice if line.value == default and default is not None else chosen
            choices.append(choice)
        question = questionary.select(
            message,
            choices,
            default=chosen,
            use_search_filter=search,
            use_jk_keys=not search,
            instruction=" ",
            style=STYLE,
        )
        return _asked(question.ask())

    def text(
        self, message: str, default: str = "", valid: Callable[[str], bool] | None = None
    ) -> str:
        check = valid or (lambda answer: True)
        return str(
            _asked(questionary.text(message, default=default, validate=check, style=STYLE).ask())
        )

    def autocomplete(self, message: str, choices: list[str], valid: Callable[[str], bool]) -> str:
        question = questionary.autocomplete(
            message, choices, validate=valid, ignore_case=True, match_middle=True, style=STYLE
        )
        return str(_asked(question.ask()))

    def pause(self, hint: str) -> str:
        return input(hint).strip().lower()
