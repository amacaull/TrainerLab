"""Spreading a series of pairs over the cores, with the time left shown as it goes.

The seeds are cut into small blocks of consecutive seeds, handed to the processes as each one
gets free. Every pair depends on its seed alone, so the split changes nothing to the battles: the
results are the same whatever the number of processes, only faster.
"""

import multiprocessing
import os
import sys
import time
from collections.abc import Callable
from concurrent.futures import ProcessPoolExecutor, as_completed
from typing import TypeVar

T = TypeVar("T")
Work = Callable[[str, str, range], T]  # bot A's name, bot B's name, the seeds of the block
BLOCK = 25  # pairs per block: small enough for a steady progress line and a fair share of work


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


class Progress:
    """One line, rewritten in place: pairs played, share done, and the time left at this pace."""

    def __init__(
        self,
        total: int,
        write: Callable[[str], object] = sys.stderr.write,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        self._total, self._write, self._clock = total, write, clock
        self._start = clock()
        self._width = 0  # the longest line so far: a shorter one must cover all of it

    def update(self, done: int) -> None:
        left = (self._clock() - self._start) / done * (self._total - done)
        share = 100 * done // self._total
        line = f"{done}/{self._total} pairs ({share} %), about {_duration(left)} left"
        self._width = max(self._width, len(line))
        self._write("\r" + line.ljust(self._width))

    def finish(self) -> None:
        self._write("\r" + " " * self._width + "\r")


def _duration(seconds: float) -> str:
    minutes, secs = divmod(round(seconds), 60)
    return f"{minutes} min {secs:02d} s" if minutes else f"{secs} s"


def progress_on_terminal(total: int) -> Progress | None:
    """A progress line when someone watches the terminal, none in a pipe or a log file."""
    return Progress(total) if sys.stderr.isatty() else None


def run(
    work: "Work[T]",
    bot_a: str,
    bot_b: str,
    seeds: range,
    jobs: int,
    progress: Progress | None = None,
) -> list[T]:
    """The result of `work` on each block, in the order of the seeds."""
    parts = blocks(seeds, -(-len(seeds) // BLOCK))
    results: dict[int, T] = {}
    done = 0

    def record(index: int, result: T) -> None:
        nonlocal done
        results[index] = result
        done += len(parts[index])
        if progress is not None:
            progress.update(done)

    if jobs == 1:
        for index, part in enumerate(parts):
            record(index, work(bot_a, bot_b, part))
    else:
        # spawn on every system, as macOS does by default: a fresh interpreter per process.
        context = multiprocessing.get_context("spawn")
        with ProcessPoolExecutor(min(jobs, len(parts)), mp_context=context) as pool:
            futures = {pool.submit(work, bot_a, bot_b, part): i for i, part in enumerate(parts)}
            for future in as_completed(futures):
                record(futures[future], future.result())
    if progress is not None:
        progress.finish()
    return [results[i] for i in range(len(parts))]
