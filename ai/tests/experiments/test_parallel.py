import pytest

from trainerlab.experiments import parallel, tournament


def test_the_seeds_are_cut_into_consecutive_blocks_in_order() -> None:
    assert parallel.blocks(range(10, 20), 3) == [range(10, 14), range(14, 17), range(17, 20)]


def test_never_more_blocks_than_seeds_nor_fewer_than_one() -> None:
    assert parallel.blocks(range(5, 7), 8) == [range(5, 6), range(6, 7)]
    assert parallel.blocks(range(5, 7), 0) == [range(5, 7)]


def test_spreading_the_pairs_over_processes_changes_no_result() -> None:
    seeds = range(30000, 30060)  # three blocks of 20

    alone = parallel.run(tournament.play_block, "hitter", "random", seeds, 1)
    spread = parallel.run(tournament.play_block, "hitter", "random", seeds, 2)

    assert len(alone) == len(spread) == 3
    assert [(r.wins, r.losses) for r in alone] == [(r.wins, r.losses) for r in spread]
    assert parallel.cores() >= 1


def test_progress_shows_the_pairs_done_and_the_time_left_at_this_pace() -> None:
    shown: list[str] = []
    now = iter([0.0, 30.0, 400.0, 404.0])
    progress = parallel.Progress(100, shown.append, lambda: next(now))

    progress.update(25)  # 30 s for 25 pairs: 90 s for the 75 left
    progress.update(80)  # 400 s for 80 pairs: 100 s for the 20 left
    progress.update(99)  # a shorter line covers the end of the longer one
    progress.finish()

    assert shown[0] == "\r25/100 pairs (25 %), about 1 min 30 s left"
    assert shown[1] == "\r80/100 pairs (80 %), about 1 min 40 s left"
    assert shown[2] == "\r99/100 pairs (99 %), about 4 s left".ljust(len(shown[1]))
    assert shown[3] == "\r" + " " * (len(shown[1]) - 1) + "\r"


def test_progress_is_updated_after_each_block() -> None:
    shown: list[str] = []
    clock = iter(float(t) for t in range(100))
    progress = parallel.Progress(30, shown.append, lambda: next(clock))

    parallel.run(tournament.play_block, "hitter", "random", range(30), 1, progress)

    assert [line.split()[0] for line in shown[:-1]] == ["15/30", "30/30"]
    assert "about 0 s left" in shown[-2]


def test_no_progress_line_outside_a_terminal(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("sys.stderr.isatty", lambda: False)
    assert parallel.progress_on_terminal(10) is None
    monkeypatch.setattr("sys.stderr.isatty", lambda: True)
    assert isinstance(parallel.progress_on_terminal(10), parallel.Progress)
