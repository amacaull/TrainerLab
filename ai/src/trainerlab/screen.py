"""Draws the battle in a terminal: framed panels, and a turn history typed out letter by letter,
with a longer pause on the moments that matter. Only ANSI codes, no curses.

Every output goes through `write`, and every pause through `sleep`: both are injected, so the
screen can be tested without a terminal and without waiting.
"""

import re
import textwrap
import time
from collections.abc import Callable

from trainerlab.render import Narration

ANSI = re.compile(r"\x1b\[[0-9;]*m")
CLEAR = "\033[2J\033[H"

# Seconds: per character typed, after each line, after a line worth a pause.
SPEEDS = {
    "instant": (0.0, 0.0, 0.0),
    "fast": (0.004, 0.05, 0.4),
    "normal": (0.015, 0.25, 1.0),
    "slow": (0.035, 0.5, 1.8),
}

Write = Callable[[str], None]


def visible_length(text: str) -> int:
    return len(ANSI.sub("", text))


class Screen:
    def __init__(
        self,
        write: Write,
        *,
        width: int,
        speed: str = "normal",
        clear: bool = False,
        sleep: Callable[[float], None] = time.sleep,
        log: Write | None = None,
    ) -> None:
        self._write = write
        self._inner = width - 4  # "│ " and " │"
        self._char_delay, self._line_pause, self._emphasis_pause = SPEEDS[speed]
        self._clear = clear
        self._sleep = sleep
        self._log = log
        self._history: list[str] = []

    def clear(self) -> None:
        """Starts a fresh screen, in a real terminal only."""
        if self._clear:
            self._write(CLEAR)

    def panel(self, title: str, lines: list[str]) -> None:
        """A framed panel, drawn at once."""
        self._top(title)
        for line in lines:
            for row in self._rows(line):
                self._write(self._framed(row))
        self._bottom()
        self.note(f"== {title} ==", *lines)

    def narrate(self, title: str, lines: list[Narration]) -> None:
        """A framed panel whose lines are typed out, then kept for the next `take_history`."""
        self._top(title)
        for line in lines:
            for row in self._rows(line.text):
                self._write("│ ")
                for token in re.split(f"({ANSI.pattern})", row):
                    self._type(token)
                self._write(" " * (self._inner - visible_length(row)) + " │\n")
            self._sleep(self._emphasis_pause if line.emphasis else self._line_pause)
        self._bottom()
        self._history += [line.text for line in lines]
        self.note(f"== {title} ==", *(line.text for line in lines))

    def take_history(self) -> list[str]:
        """The lines narrated since the last call: the turn the player has just watched."""
        history, self._history = self._history, []
        return history

    def message(self, text: str) -> None:
        self._write(text + "\n")
        self.note(text)

    def note(self, *lines: str) -> None:
        """Writes to the log only, without colours."""
        if self._log is not None:
            for line in lines:
                self._log(ANSI.sub("", line))

    def _type(self, token: str) -> None:
        if ANSI.fullmatch(token) or not self._char_delay:
            self._write(token)
            return
        for char in token:
            self._write(char)
            self._sleep(self._char_delay)

    def _rows(self, line: str) -> list[str]:
        if visible_length(line) <= self._inner:
            return [line]
        # Too long: wrapped as plain text, since colours cannot be split safely, keeping the
        # line's indentation on every row.
        plain = ANSI.sub("", line)
        indent = plain[: len(plain) - len(plain.lstrip())]
        text = plain.lstrip()
        return textwrap.wrap(text, self._inner, initial_indent=indent, subsequent_indent=indent)

    def _framed(self, row: str) -> str:
        return "│ " + row + " " * (self._inner - visible_length(row)) + " │\n"

    def _top(self, title: str) -> None:
        label = f"─ {title} "
        self._write("╭" + label + "─" * (self._inner + 2 - len(label)) + "╮\n")

    def _bottom(self) -> None:
        self._write("╰" + "─" * (self._inner + 2) + "╯\n")
