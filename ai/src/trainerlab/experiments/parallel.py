"""Spreading a series of pairs over the cores, with the time left shown as it goes.

The seeds are cut into small blocks of consecutive seeds, handed to the processes as each one
gets free. Every pair depends on its seed alone, so the split changes nothing to the battles: the
results are the same whatever the number of processes, only faster.
"""

import multiprocessing
import os
import signal
import sys
import threading
import time
from collections.abc import Callable
from concurrent.futures import ProcessPoolExecutor, as_completed
from typing import Protocol, TypeVar

T = TypeVar("T")
Work = Callable[[str, str, range], T]  # bot A's name, bot B's name, the seeds of the block
# Pairs per block: the progress line moves after each one, so small. A block loads its bots again,
# about a second for a network: little beside the minutes a block of slow bots takes.
BLOCK = 5


def cores() -> int:
    """The number of cores of this machine: the default number of processes."""
    return os.cpu_count() or 1


def blocks(seeds: range, count: int) -> list[range]:
    """`seeds` cut into at most `count` blocks of consecutive seeds, in order, none empty."""
    count = max(1, min(count, len(seeds)))
    size, extra = divmod(len(seeds), count)
    parts, start = [], seeds.start
    for i in range(count):
        length = size + (i < extra)
        parts.append(range(start, start + length))
        start += length
    return parts


class Meter(Protocol):
    """What follows a measure: told the pairs done after each block, then the end."""

    def update(self, done: int) -> None: ...

    def finish(self) -> None: ...


class Progress:
    """One line, rewritten in place: a bar, pairs played, share done, the time left at this pace.

    In colour (on a terminal), the bar is drawn in green blocks; elsewhere in plain characters.
    """

    WIDTH = 20  # characters of the bar

    def __init__(
        self,
        total: int,
        write: Callable[[str], object] = sys.stderr.write,
        clock: Callable[[], float] = time.monotonic,
        *,
        colour: bool = False,
        unit: str = "pairs",
    ) -> None:
        self._total, self._write, self._clock = total, write, clock
        self._unit = unit
        self.colour = colour
        self._start = clock()
        self._width = 0  # the longest line so far, as seen: a shorter one must cover all of it

    def update(self, done: int) -> None:
        share = 100 * done // self._total
        if done:
            left = (self._clock() - self._start) / done * (self._total - done)
            when = f"about {_duration(left)} left"
        else:
            when = "estimating the time left"
        text = f" {done}/{self._total} {self._unit} ({share} %), {when}"
        filled = self.WIDTH * done // self._total
        seen = 2 + self.WIDTH + len(text)
        self._width = max(self._width, seen)
        self._write("\r[" + self._bar(filled) + "]" + text + " " * (self._width - seen))

    def _bar(self, filled: int) -> str:
        empty = self.WIDTH - filled
        if not self.colour:
            return "#" * filled + "." * empty
        return f"{GREEN}{'█' * filled}{RESET}{DIM}{'░' * empty}{RESET}"

    def finish(self) -> None:
        self._write("\r" + " " * self._width + "\r")


GREEN, DIM, RESET = "\x1b[32m", "\x1b[2m", "\x1b[0m"


def _duration(seconds: float) -> str:
    minutes, secs = divmod(round(seconds), 60)
    return f"{minutes} min {secs:02d} s" if minutes else f"{secs} s"


def progress_on_terminal(total: int, unit: str = "pairs") -> Progress | None:
    """A progress line when someone watches the terminal, none in a pipe or a log file."""
    return Progress(total, colour=True, unit=unit) if sys.stderr.isatty() else None


def ignore_ctrl_c() -> None:
    """In a process of the pool: Ctrl-C is the parent's, which stops every process itself (a
    process left waiting on its parent, once the parent is gone, would never end)."""
    signal.signal(signal.SIGINT, signal.SIG_IGN)


def _stop(pool: ProcessPoolExecutor) -> None:
    # The executor keeps its processes to itself, and may still start one as it shuts down
    # (Python 3.12): its own table, read again once its manager is done, holds them all.
    processes = pool._processes
    manager: threading.Thread | None = getattr(pool, "_executor_manager_thread", None)
    pool.shutdown(wait=False, cancel_futures=True)
    for process in list(processes.values()):
        process.terminate()
    if manager is not None:
        manager.join(timeout=5)
    for process in list(processes.values()):
        process.terminate()
        process.join()


def run(
    work: "Work[T]",
    bot_a: str,
    bot_b: str,
    seeds: range,
    jobs: int,
    progress: Meter | None = None,
    on_result: Callable[[T], None] | None = None,
) -> list[T]:
    """The result of `work` on each block, in the order of the seeds."""
    parts = blocks(seeds, -(-len(seeds) // BLOCK))
    results: dict[int, T] = {}
    done = 0

    if progress is not None:
        progress.update(0)  # at once: the first block may take minutes

    def record(index: int, result: T) -> None:
        nonlocal done
        results[index] = result
        done += len(parts[index])
        if on_result is not None:
            on_result(result)
        if progress is not None:
            progress.update(done)

    if jobs == 1:
        for index, part in enumerate(parts):
            record(index, work(bot_a, bot_b, part))
    else:
        # spawn on every system, as macOS does by default: a fresh interpreter per process.
        context = multiprocessing.get_context("spawn")
        pool = ProcessPoolExecutor(
            min(jobs, len(parts)), mp_context=context, initializer=ignore_ctrl_c
        )
        try:
            futures = {pool.submit(work, bot_a, bot_b, part): i for i, part in enumerate(parts)}
            for future in as_completed(futures):
                record(futures[future], future.result())
        except BaseException:
            _stop(pool)  # Ctrl-C, or a block that failed: no process left behind
            raise
        pool.shutdown()
    if progress is not None:
        progress.finish()
    return [results[i] for i in range(len(parts))]
