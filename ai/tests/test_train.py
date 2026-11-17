from pathlib import Path
from typing import Any

import numpy as np
import pytest

from trainerlab.dataset import build
from trainerlab.decisions import decisions
from trainerlab.model import WinModel
from trainerlab.roster import BOTS
from trainerlab.train import default_name, main


@pytest.fixture(scope="module")
def dataset(tmp_path_factory: pytest.TempPathFactory) -> Path:
    path = tmp_path_factory.mktemp("data") / "small.npz"
    arrays: dict[str, Any] = build(decisions(BOTS["hitter"], BOTS["random"], seeds=range(10)))
    np.savez(path, **arrays)
    return path


def test_the_command_compares_the_model_with_a_constant_on_unseen_seeds(dataset: Path) -> None:
    lines: list[str] = []

    assert main([str(dataset)], lines.append) == 0

    text = "".join(lines)
    assert text.startswith("small.npz: ")
    assert "to test (2 seeds, the multiples of 5)" in text
    assert "constant, test" in text
    assert "logistic, train" in text
    assert "logistic, test" in text
    model = WinModel.load(default_name([dataset], "logistic"))
    assert model.kind == "logistic"


def test_the_network_and_the_output_file_can_be_chosen(dataset: Path, tmp_path: Path) -> None:
    out = tmp_path / "net.npz"

    assert main([str(dataset), "--model", "network", "--out", str(out)], lambda text: None) == 0

    assert WinModel.load(out).kind == "network 64"


def test_a_missing_dataset_is_reported(capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["nowhere.npz"]) == 2
    assert capsys.readouterr().out == "No such dataset: nowhere.npz\n"


def test_the_model_file_is_named_after_the_datasets() -> None:
    assert default_name([Path("d/x.npz")], "network") == Path("d/x-network.npz")
    assert default_name([Path("d/x.npz"), Path("y.npz")], "logistic") == Path(
        "d/x-plus-1-logistic.npz"
    )


def test_several_datasets_are_put_together(dataset: Path, tmp_path: Path) -> None:
    other = tmp_path / "other.npz"
    arrays: dict[str, Any] = build(decisions(BOTS["matcher"], BOTS["random"], seeds=range(10, 15)))
    np.savez(other, **arrays)
    lines: list[str] = []

    assert main([str(dataset), str(other), "--out", str(tmp_path / "m.npz")], lines.append) == 0

    assert lines[0].startswith("small.npz, other.npz: ")
    assert "to test (3 seeds, the multiples of 5)" in lines[0]  # 0, 5 and 10


def test_datasets_with_other_features_are_refused(dataset: Path, tmp_path: Path) -> None:
    stray = tmp_path / "stray.npz"
    arrays = dict(np.load(dataset))
    arrays["feature_names"] = arrays["feature_names"][::-1]
    np.savez(stray, **arrays)
    lines: list[str] = []

    assert main([str(dataset), str(stray)], lines.append) == 2
    assert lines == ["These datasets do not have the same features: build them again\n"]
