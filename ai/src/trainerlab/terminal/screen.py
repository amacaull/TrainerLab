"""Draws the battle in a terminal with rich: framed panels, and a turn typed out letter by
letter, with a longer pause on the moments that matter.

Every output goes to a rich `Console` and every pause through `sleep`: both are injected, so
the screen can be tested without a terminal and without waiting. A second console, without
colours, keeps a copy of everything shown (play --log).
"""

import io
import sys
import termios
import time
from collections.abc import Callable, Iterator
from contextlib import contextmanager

from rich.console import Console, RenderableType
from rich.panel import Panel
from rich.text import Text

from trainerlab.terminal.render import Narration

CLEAR = "\033[2J\033[H"
# Seconds: per character typed, after each line, after a line worth a pause.
SPEEDS = {
    "slow": (0.035, 0.5, 1.8),
    "normal": (0.015, 0.25, 1.0),
    "fast": (0.004, 0.05, 0.4),
    "instant": (0.0, 0.0, 0.0),
}


@contextmanager
def keys_silenced() -> Iterator[None]:
    """While the screen writes: the keys pressed are not echoed, and wait unread (the next
    question forgets them, prompts.flush_keyboard)."""
    if not sys.stdin.isatty():
        yield
        return
    fd = sys.stdin.fileno()
    before = termios.tcgetattr(fd)
    quiet = list(before)
    quiet[3] = before[3] & ~(termios.ECHO | termios.ICANON)
    termios.tcsetattr(fd, termios.TCSANOW, quiet)
    try:
        yield
    finally:
        termios.tcsetattr(fd, termios.TCSANOW, before)


class Screen:
    def __init__(
        self,
        console: Console,
        *,
        speed: str = "normal",
        clear: bool = False,
        sleep: Callable[[float], None] = time.sleep,
        log: Console | None = None,
    ) -> None:
        self.console = console
        self._char_delay, self._line_pause, self._emphasis_pause = SPEEDS[speed]
        self._clear = clear
        self._sleep = sleep
        self._logs = [log] if log is not None else []
        self._history: list[str] = []
        self.start_transcript()

    def clear(self) -> None:
        """Starts a fresh screen, in a real terminal only."""
        if self._clear:
            self.console.file.write(CLEAR)

    def start_transcript(self) -> None:
        """From now on, keeps a copy of what is shown, without colours, for `transcript`."""
        self._kept = io.StringIO()
        self._transcript = Console(
            file=self._kept, color_system=None, width=self.console.width, highlight=False
        )

    def transcript(self) -> str:
        """What was shown since `start_transcript`, as the log has it: the battle, once."""
        return self._kept.getvalue()

    def show(self, renderable: RenderableType) -> None:
        self.console.print(renderable)
        for log in (*self._logs, self._transcript):
            log.print(renderable)

    def panel(self, title: str, body: RenderableType, style: str = "") -> None:
        panel = Panel(body, title=title, title_align="left", border_style=style or "none")
        panel.expand = False
        self.show(panel)

    def message(self, text: str) -> None:
        self.show(text)

    def lines(self, texts: list[str]) -> None:
        """A reminder of lines already told: on the screen only, the log has them once."""
        for text in texts:
            self.console.print("  " + text)

    def narrate(self, lines: list[Narration]) -> None:
        """The lines of a turn typed out, then kept for the next `take_history`."""
        with keys_silenced():
            self._type_out(lines)
        self._history += [line.text for line in lines]

    def _type_out(self, lines: list[Narration]) -> None:
        for line in lines:
            text = Text.from_markup("  " + line.text)
            if self._char_delay:
                for index in range(len(text)):
                    self.console.print(text[index : index + 1], end="")
                    if index >= 2:
                        self._sleep(self._char_delay)
                self.console.print()
            else:
                self.console.print(text)
            for log in (*self._logs, self._transcript):
                log.print(text)
            self._sleep(self._emphasis_pause if line.emphasis else self._line_pause)

    def take_history(self) -> list[str]:
        """The lines told since the last call: the turn the player has just watched."""
        history, self._history = self._history, []
        return history
