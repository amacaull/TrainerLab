"""Where the commands write: standard output, unless a test hands them another `write`."""

import signal
import sys
from collections.abc import Callable

Write = Callable[[str], None]


def stdout(text: str) -> None:
    sys.stdout.write(text)
    sys.stdout.flush()


def quiet_on_closed_pipe() -> None:
    """Stops without a traceback when the output is piped into a command that exits early
    (head, grep -q)."""
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)
