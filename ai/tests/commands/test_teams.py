from pathlib import Path

import pytest

from trainerlab.commands import teams


def test_the_command_plays_keeps_and_reads_again(tmp_path: Path) -> None:
    out = tmp_path / "t.npz"
    shown: list[str] = []

    argv = ["random", "--pairs", "20", "--from", "5", "--jobs", "1", "--out", str(out)]
    assert teams.main(argv, shown.append) == 0

    assert shown[0] == "40 battles, seeds 5-24\n"
    assert shown[1].startswith("the same team won both battles of a pair: ")
    assert shown[2].startswith("the composition alone calls the winner of ")
    assert "outside 0.5 at 95 %, about 2 by chance alone" in shown[3]
    assert shown[-1] == f"-> {out}\n" and out.is_file()

    again: list[str] = []
    assert teams.main(["--load", str(out)], again.append) == 0
    assert again == shown[:-1]


def test_the_command_needs_a_bot_or_a_file(capsys: pytest.CaptureFixture[str]) -> None:
    with pytest.raises(SystemExit):
        teams.main([])
    with pytest.raises(SystemExit):
        teams.main(["random", "--load", "x.npz"])
    assert "give a bot to play, or --load FILE" in capsys.readouterr().err
