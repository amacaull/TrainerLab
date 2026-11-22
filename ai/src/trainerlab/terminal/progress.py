"""A progress bar for the long measures, drawn by rich: pairs done, time spent, time left, and
a note that the measure updates as it goes (a duel's score so far)."""

from collections.abc import Iterator
from contextlib import contextmanager

from rich.console import Console
from rich.progress import (
    BarColumn,
    MofNCompleteColumn,
    Progress,
    TaskID,
    TextColumn,
    TimeElapsedColumn,
    TimeRemainingColumn,
)

from trainerlab.i18n import t


class Bar:
    def __init__(self, progress: Progress, task: TaskID) -> None:
        self._progress, self._task = progress, task

    def update(self, done: int) -> None:
        self._progress.update(self._task, completed=done)

    def note(self, text: str) -> None:
        self._progress.update(self._task, note=text)

    def finish(self) -> None:
        """Nothing to do: the bar stays, full, when the measure ends."""


@contextmanager
def progress_bar(console: Console, label: str, total: int) -> Iterator[Bar]:
    columns = (
        TextColumn(f"  {label}"),
        BarColumn(bar_width=36),
        MofNCompleteColumn(),
        TimeElapsedColumn(),
        TextColumn(t("progress.left")),
        TimeRemainingColumn(),
        TextColumn("{task.fields[note]}"),
    )
    with Progress(*columns, console=console) as progress:
        yield Bar(progress, progress.add_task(label, total=total, note=""))
