import io
import termios

import pytest
from rich.console import Console

from trainerlab.terminal.render import Narration
from trainerlab.terminal.screen import CLEAR, Screen


class Terminal:
    """Collects what the screen shows, and the pauses it asks for, without waiting."""

    def __init__(self) -> None:
        self.output = io.StringIO()
        self.log = io.StringIO()
        self.pauses: list[float] = []

    def screen(self, speed: str = "instant", clear: bool = False) -> Screen:
        console = Console(file=self.output, width=40, color_system=None)
        log = Console(file=self.log, width=40, color_system=None)
        return Screen(console, speed=speed, clear=clear, sleep=self.pauses.append, log=log)

    @property
    def text(self) -> str:
        return self.output.getvalue()


def _line(text: str, emphasis: bool = False) -> Narration:
    return Narration(text, emphasis, kind=0, side=0, slot=0, amount=0)


def test_a_panel_is_framed_with_its_title_and_copied_to_the_log() -> None:
    terminal = Terminal()

    terminal.screen().panel("Turn 7", "[red]Snorlax[/]")

    lines = terminal.text.splitlines()
    assert lines[0].startswith("╭─ Turn 7 ")
    assert lines[1].startswith("│ Snorlax")
    assert terminal.log.getvalue() == terminal.text


def test_the_turn_is_typed_letter_by_letter_with_pauses() -> None:
    terminal = Terminal()

    terminal.screen(speed="normal").narrate([_line("Hi!"), _line("KO!", emphasis=True)])

    assert terminal.text == "  Hi!\n  KO!\n"
    assert terminal.pauses == [0.015] * 3 + [0.25] + [0.015] * 3 + [1.0]


def test_the_turn_just_told_is_kept_for_the_next_screen_once() -> None:
    terminal = Terminal()
    screen = terminal.screen()

    screen.narrate([_line("[cyan]Gyarados[/] used Waterfall!")])

    assert screen.take_history() == ["[cyan]Gyarados[/] used Waterfall!"]
    assert screen.take_history() == []


def test_a_fresh_screen_is_cleared_only_in_a_real_terminal() -> None:
    terminal = Terminal()

    terminal.screen().clear()
    assert terminal.text == ""

    terminal.screen(clear=True).clear()
    assert terminal.text == CLEAR


def test_a_message_goes_to_the_log_and_a_reminder_only_to_the_screen() -> None:
    terminal = Terminal()
    screen = terminal.screen()

    screen.message("Seed 3")
    screen.lines(["one", "two"])

    assert terminal.text == "Seed 3\n  one\n  two\n"
    assert terminal.log.getvalue() == "Seed 3\n"


def test_without_a_log_only_the_screen_shows() -> None:
    output = io.StringIO()

    Screen(Console(file=output, color_system=None), sleep=lambda s: None).message("x")

    assert output.getvalue() == "x\n"


def test_keys_pressed_while_a_turn_is_typed_out_are_neither_shown_nor_read(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    settings: list[list[object]] = []
    monkeypatch.setattr("sys.stdin.isatty", lambda: True)
    monkeypatch.setattr("sys.stdin.fileno", lambda: 0)
    flags = termios.ECHO | termios.ICANON | 1
    monkeypatch.setattr(termios, "tcgetattr", lambda fd: [0, 0, 0, flags, 0, 0, []])
    monkeypatch.setattr(termios, "tcsetattr", lambda fd, when, attrs: settings.append(list(attrs)))

    Terminal().screen().narrate([_line("Hi!")])

    assert [attrs[3] for attrs in settings] == [1, flags]  # echo and lines off, then back


def test_a_transcript_keeps_what_was_shown_once_without_colours() -> None:
    terminal = Terminal()
    screen = terminal.screen()
    screen.message("before")
    screen.start_transcript()

    screen.message("[red]Seed[/] 3")
    screen.narrate([_line("[cyan]Hi![/]")])
    screen.lines(["a reminder"])

    assert screen.transcript() == "Seed 3\n  Hi!\n"
