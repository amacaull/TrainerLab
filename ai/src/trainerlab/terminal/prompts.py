"""The questions the interface asks: a choice in a list, a text, a name with completion.

Every question goes through a `Prompter`, so that the screens are tested with scripted answers;
the real one asks with questionary (arrow keys, Enter, filtering as one types).
"""

import sys
import termios
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from typing import Any, Protocol

import questionary
from prompt_toolkit.styles import Style
from questionary.prompts import common

from trainerlab.i18n import t

# The look of the questions: the line under the cursor in cyan, the rest plain.
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
# The lines of a list shown at once: a longer list scrolls with the cursor.
WINDOW = 10
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
    def select(self, message: str, options: Sequence[Line], *, search: bool = False) -> Any: ...

    def text(self, message: str, valid: Callable[[str], bool] | None = None) -> str: ...

    def pause(self, hint: str) -> str: ...


def plain(label: Label) -> str:
    return label if isinstance(label, str) else "".join(text for _, text in label)


def _asked(answer: Any) -> Any:
    if answer is None:  # questionary's answer to Ctrl-C
        raise KeyboardInterrupt
    return answer


def flush_keyboard() -> None:
    """Forgets the keys pressed while the screen was busy (Enter while a turn was typed out):
    they must not answer the next question."""
    if sys.stdin.isatty():
        termios.tcflush(sys.stdin, termios.TCIFLUSH)


_choice_tokens = common.InquirerControl._get_choice_tokens
DESCRIPTION = "  Description: "
NEWLINE = ("", "\n")
Tokens = list[tuple[str, str]]


def _windowed(control: common.InquirerControl) -> Tokens:
    """A list as questionary draws it, but WINDOW lines at most around the cursor, with how
    many are hidden above and below, then the highlighted option's card apart, without the
    "Description: " questionary writes before it. A filter keeps searching the whole list."""
    tokens: Tokens = _choice_tokens(control)  # type: ignore[no-untyped-call]
    card = None
    if tokens[-1][1].startswith(DESCRIPTION):
        card = tokens.pop()[1].removeprefix(DESCRIPTION)
    lines: list[Tokens] = [[]]
    for token in tokens:
        if token == NEWLINE:
            lines.append([])
        else:
            lines[-1].append(token)
    if not lines[-1]:
        lines.pop()
    start = min(max(control.pointed_at - WINDOW // 2, 0), max(len(lines) - WINDOW, 0))
    hidden = len(lines) - start - WINDOW
    shown = lines[start : start + WINDOW]
    if start > 0:
        shown.insert(0, [("class:instruction", "   " + t("more.above", count=start))])
    if hidden > 0:
        shown.append([("class:instruction", "   " + t("more.below", count=hidden))])
    if card is not None:
        shown += [[], [("class:text", card)]]
    drawn: Tokens = []
    for index, line in enumerate(shown):
        drawn += ([NEWLINE] if index else []) + line
    return drawn


common.InquirerControl._get_choice_tokens = _windowed  # type: ignore[method-assign]


class QuestionaryPrompter:
    """Asks with questionary: no answer preselected, the keys pressed before forgotten."""

    def select(self, message: str, options: Sequence[Line], *, search: bool = False) -> Any:
        choices: list[questionary.Choice | questionary.Separator] = []
        for line in options:
            if isinstance(line, Separator):
                choices.append(questionary.Separator(line.title and f"── {line.title} ──"))
                continue
            # questionary filters a list on its titles as text: no styled pieces then.
            label = plain(line.label) if search else line.label
            choices.append(
                questionary.Choice(
                    label, line.value, disabled=line.disabled, description=line.description
                )
            )
        flush_keyboard()
        question = questionary.select(
            message,
            choices,
            use_search_filter=search,
            use_jk_keys=not search,
            instruction=" ",
            style=STYLE,
        )
        return _asked(question.ask())

    def text(self, message: str, valid: Callable[[str], bool] | None = None) -> str:
        check = valid or (lambda answer: True)
        flush_keyboard()
        return str(_asked(questionary.text(message, validate=check, style=STYLE).ask()))

    def pause(self, hint: str) -> str:
        flush_keyboard()
        return input(hint).strip().lower()
