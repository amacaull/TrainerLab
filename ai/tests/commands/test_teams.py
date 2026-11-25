import json
from pathlib import Path

import numpy as np
import pytest

from trainerlab.commands import teams
from trainerlab.experiments.teams import analyze


def test_the_command_plays_keeps_and_reads_again(tmp_path: Path) -> None:
    out = tmp_path / "t.npz"
    shown: list[str] = []

    argv = ["random", "--pairs", "20", "--from", "5", "--jobs", "1", "--out", str(out)]
    assert teams.main(argv, shown.append) == 0

    assert shown[0] == "40 battles of teams of 6, seeds 5-24\n"
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


def test_the_size_of_the_teams_is_kept_with_the_battles(tmp_path: Path) -> None:
    out = tmp_path / "t.npz"
    shown: list[str] = []

    argv = ["random", "--size", "2", "--pairs", "10", "--jobs", "1", "--out", str(out)]
    assert teams.main(argv, shown.append) == 0

    assert shown[0] == "20 battles of teams of 2, seeds 0-9\n"
    again: list[str] = []
    assert teams.main(["--load", str(out)], again.append) == 0
    assert again[0] == shown[0]


def test_a_file_of_palier_7_holds_teams_of_six(tmp_path: Path) -> None:
    out = tmp_path / "old.npz"
    shown: list[str] = []
    teams.main(["random", "--pairs", "10", "--jobs", "1", "--out", str(out)], shown.append)
    saved = np.load(out)
    np.savez(out, seed=saved["seed"], points=saved["points"])  # as palier 7 kept them

    again: list[str] = []
    assert teams.main(["--load", str(out)], again.append) == 0
    assert again == shown[:-1]


def test_the_species_strengths_can_be_written_for_the_bot(tmp_path: Path) -> None:
    out, scores = tmp_path / "t.npz", tmp_path / "strengths.json"
    teams.main(["random", "--pairs", "20", "--from", "0", "--out", str(out)], lambda _: None)

    assert teams.main(["--load", str(out), "--strengths", str(scores)], lambda _: None) == 0

    saved = np.load(out)
    expected = {s.name: s.score for s in analyze(saved["seed"], saved["points"], 6).species}
    assert json.loads(scores.read_text()) == pytest.approx(expected, nan_ok=True)
