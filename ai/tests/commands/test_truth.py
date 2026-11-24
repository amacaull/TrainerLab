from pathlib import Path

import pytest

from trainerlab.commands import truth
from trainerlab.commands.cli import SECTIONS


def test_the_command_plays_keeps_and_reads_again(tmp_path: Path) -> None:
    out = tmp_path / "truth.npz"
    shown: list[str] = []

    argv = ["random", "--teams", "3", "--pairs", "2", "--from", "7", "--jobs", "1"]
    assert truth.main([*argv, "--out", str(out)], shown.append) == 0

    assert shown[0] == "12 battles: 3 teams of 2, 2 pairs each, seeds 7-12\n"
    assert shown[1].startswith("the teams' scores spread by ")
    assert "of it is the teams', the rest the dice" in shown[1]
    assert shown[2] == "the best teams:\n"
    assert len(shown) == 2 + 1 + 3 + 1 + 3 + 1  # 3 teams: the best and the worst are all of them
    assert shown[-1] == f"-> {out}\n" and out.is_file()

    again: list[str] = []
    assert truth.main(["--load", str(out)], again.append) == 0
    assert again == shown[:-1]


def test_the_command_needs_a_bot_or_a_file(capsys: pytest.CaptureFixture[str]) -> None:
    with pytest.raises(SystemExit):
        truth.main([])
    assert "give a bot to play, or --load FILE" in capsys.readouterr().err


def test_the_command_is_listed_with_the_analyses() -> None:
    assert "truth" in SECTIONS["analyse"]
