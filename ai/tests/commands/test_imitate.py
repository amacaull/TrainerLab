from pathlib import Path

import numpy as np
import pytest

from trainerlab.commands import imitate as command


def test_the_command_writes_bot_a_s_decisions(tmp_path: Path) -> None:
    out = tmp_path / "rows.npz"
    shown: list[str] = []

    argv = ["calculator", "survivor", "--pairs", "2", "--from", "130100", "--out", str(out)]
    assert command.main([*argv, "--jobs", "1"], shown.append) == 0

    rows = np.load(out)
    assert shown == [
        f"calculator against survivor, seeds 130100-130101: {len(rows['action'])} decisions "
        f"by calculator -> {out}\n"
    ]
    assert {"pokemon", "ids", "field", "mask", "action", "result", "seed"} == set(rows.files)
    assert set(np.unique(rows["seed"])) == {130100, 130101}


def test_without_out_the_file_is_named_after_the_bots_and_seeds(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.chdir(tmp_path)
    shown: list[str] = []

    command.main(
        ["calculator", "survivor", "--pairs", "1", "--from", "130200", "--jobs", "1"], shown.append
    )

    assert (tmp_path / "imitate-calculator-vs-survivor-130200-130200.npz").is_file()
    assert shown[0].endswith("-> imitate-calculator-vs-survivor-130200-130200.npz\n")
