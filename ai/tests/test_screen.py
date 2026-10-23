import pytest

from trainerlab.render import Narration
from trainerlab.screen import CLEAR, Screen


class Terminal:
    """Collects what the screen writes, and the pauses it asks for, without waiting."""

    def __init__(self) -> None:
        self.output: list[str] = []
        self.pauses: list[float] = []
        self.log: list[str] = []

    def screen(self, speed: str = "instant", clear: bool = False, width: int = 24) -> Screen:
        return Screen(
            self.output.append,
            width=width,
            speed=speed,
            clear=clear,
            sleep=self.pauses.append,
            log=self.log.append,
        )

    @property
    def text(self) -> str:
        return "".join(self.output)


def _line(text: str, emphasis: bool = False) -> Narration:
    return Narration(text, emphasis, kind=0, side=0, slot=0, amount=0)


def test_a_panel_is_framed_at_the_screen_width() -> None:
    terminal = Terminal()

    terminal.screen().panel("Board", ["Snorlax", "\033[32mgreen\033[0m"])

    assert terminal.text.splitlines() == [
        "╭─ Board ──────────────╮",
        "│ Snorlax              │",
        "│ \033[32mgreen\033[0m                │",
        "╰──────────────────────╯",
    ]


def test_a_line_too_long_wraps_and_keeps_its_indentation() -> None:
    terminal = Terminal()

    terminal.screen().panel("Board", ["  bench: Blissey Luxray Inteleon"])

    assert terminal.text.splitlines()[1:3] == [
        "│   bench: Blissey     │",
        "│   Luxray Inteleon    │",
    ]


def test_the_history_is_typed_letter_by_letter_with_pauses() -> None:
    terminal = Terminal()

    terminal.screen(speed="normal").narrate("Turn 1", [_line("Hi"), _line("KO!", emphasis=True)])

    assert terminal.output[1:4] == ["│ ", "H", "i"]
    assert terminal.pauses == [0.015, 0.015, 0.25, 0.015, 0.015, 0.015, 1.0]


def test_colour_codes_are_written_at_once_not_typed() -> None:
    terminal = Terminal()

    terminal.screen(speed="normal").narrate("Turn 1", [_line("\033[31mA\033[0m")])

    assert terminal.output[1:5] == ["│ ", "\033[31m", "A", "\033[0m"]


def test_instant_speed_writes_whole_lines_and_never_waits() -> None:
    terminal = Terminal()

    terminal.screen(speed="instant").narrate("Turn 1", [_line("Hi", emphasis=True)])

    assert "│ Hi                   │\n" in terminal.text
    assert set(terminal.pauses) == {0.0}


def test_the_narrated_lines_are_kept_for_the_next_screen_once() -> None:
    screen = Terminal().screen()

    screen.narrate("Turn 1", [_line("A")])
    screen.narrate("Switch-in", [_line("B")])

    assert screen.take_history() == ["A", "B"]
    assert screen.take_history() == []


@pytest.mark.parametrize(("clear", "expected"), [(True, [CLEAR]), (False, [])])
def test_the_screen_is_cleared_only_in_a_terminal(clear: bool, expected: list[str]) -> None:
    terminal = Terminal()

    terminal.screen(clear=clear).clear()

    assert terminal.output == expected


def test_everything_shown_is_logged_without_colours() -> None:
    terminal = Terminal()
    screen = terminal.screen()

    screen.message("Seed 1")
    screen.panel("Board", ["\033[32mgreen\033[0m"])
    screen.narrate("Turn 1", [_line("Hit!")])
    screen.note("> Psyshock")

    assert terminal.log == ["Seed 1", "== Board ==", "green", "== Turn 1 ==", "Hit!", "> Psyshock"]


def test_without_a_log_nothing_is_written_aside() -> None:
    terminal = Terminal()
    screen = Screen(terminal.output.append, width=24, sleep=terminal.pauses.append)

    screen.note("only for the log")

    assert terminal.output == []
