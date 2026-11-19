import pytest

from trainerlab.experiments import parallel, tournament


def test_the_seeds_are_cut_into_consecutive_blocks_in_order() -> None:
    assert parallel.blocks(range(10, 20), 3) == [range(10, 14), range(14, 17), range(17, 20)]


def test_never_more_blocks_than_seeds_nor_fewer_than_one() -> None:
    assert parallel.blocks(range(5, 7), 8) == [range(5, 6), range(6, 7)]
    assert parallel.blocks(range(5, 7), 0) == [range(5, 7)]


def test_spreading_the_pairs_over_processes_changes_no_result() -> None:
    seeds = range(30000, 30020)  # four blocks of 5

    alone = parallel.run(tournament.play_block, "hitter", "random", seeds, 1)
    spread = parallel.run(tournament.play_block, "hitter", "random", seeds, 2)

    assert len(alone) == len(spread) == 4
    assert [(r.wins, r.losses) for r in alone] == [(r.wins, r.losses) for r in spread]
    assert parallel.cores() >= 1


def test_progress_draws_a_bar_with_the_pairs_done_and_the_time_left() -> None:
    shown: list[str] = []
    now = iter([0.0, 30.0, 400.0, 404.0])
    progress = parallel.Progress(100, shown.append, lambda: next(now))

    progress.update(25)  # 30 s for 25 pairs: 90 s for the 75 left
    progress.update(80)  # 400 s for 80 pairs: 100 s for the 20 left
    progress.update(99)
    progress.finish()

    assert shown[0] == "\r[" + "#" * 5 + "." * 15 + "] 25/100 pairs (25 %), about 1 min 30 s left"
    assert shown[1] == "\r[" + "#" * 16 + "." * 4 + "] 80/100 pairs (80 %), about 1 min 40 s left"
    # A shorter line covers the end of the longer one.
    assert shown[2] == ("\r[" + "#" * 19 + "." + "] 99/100 pairs (99 %), about 4 s left").ljust(
        len(shown[1])
    )
    assert shown[3] == "\r" + " " * (len(shown[1]) - 1) + "\r"


def test_before_any_pair_the_bar_is_empty_and_the_time_left_unknown() -> None:
    shown: list[str] = []
    progress = parallel.Progress(40, shown.append, lambda: 0.0)

    progress.update(0)

    assert shown == ["\r[" + "." * 20 + "] 0/40 pairs (0 %), estimating the time left"]


def test_in_colour_the_bar_is_green_and_the_line_reads_the_same() -> None:
    shown: list[str] = []
    now = iter([0.0, 30.0])
    progress = parallel.Progress(100, shown.append, lambda: next(now), colour=True)

    progress.update(25)

    assert shown[0].startswith("\r[\x1b[32m" + "█" * 5 + "\x1b[0m\x1b[2m" + "░" * 15 + "\x1b[0m]")
    assert shown[0].endswith("] 25/100 pairs (25 %), about 1 min 30 s left")


def test_progress_shows_at_once_then_after_every_block_of_five_pairs() -> None:
    shown: list[str] = []
    clock = iter(float(t) for t in range(100))
    progress = parallel.Progress(15, shown.append, lambda: next(clock))

    parallel.run(tournament.play_block, "hitter", "random", range(15), 1, progress)

    assert [line.split()[1] for line in shown[:-1]] == ["0/15", "5/15", "10/15", "15/15"]
    assert "about 0 s left" in shown[-2]


def test_no_progress_line_outside_a_terminal(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("sys.stderr.isatty", lambda: False)
    assert parallel.progress_on_terminal(10) is None
    monkeypatch.setattr("sys.stderr.isatty", lambda: True)
    progress = parallel.progress_on_terminal(10)
    assert isinstance(progress, parallel.Progress) and progress.colour
