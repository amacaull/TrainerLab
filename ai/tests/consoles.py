"""A rich console writing into a string, for the tests of what the commands show."""

import io
import re

from rich.console import Console

CODES = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


def recording(*, terminal: bool = False, width: int = 120) -> Console:
    return Console(
        file=io.StringIO(),
        width=width,
        force_terminal=terminal,
        highlight=False,
        color_system="standard" if terminal else None,
    )


def shown(console: Console) -> str:
    """What the console showed, with its colour codes."""
    assert isinstance(console.file, io.StringIO)
    return console.file.getvalue()


def text(console: Console) -> str:
    """What the console showed, as read on the screen: without its codes."""
    return CODES.sub("", shown(console))
