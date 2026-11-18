import sys
from pathlib import Path

import numpy as np
import pytest
import torch

import trainerlab.rl
from trainerlab.bots.roster import factory
from trainerlab.commands import pretrain as command
from trainerlab.experiments.imitation import generate, save
from trainerlab.game.environment import BattleEnvironment
from trainerlab.rl import train
from trainerlab.rl.bot import NetBot


@pytest.fixture(scope="module")
def dataset(tmp_path_factory: pytest.TempPathFactory) -> Path:
    path = tmp_path_factory.mktemp("imitation") / "data.npz"
    save(path, generate("forecaster", "calculator", range(130000, 130006), jobs=1))
    return path


def test_pretraining_learns_and_scores_on_held_out_battles(dataset: Path) -> None:
    rows = train.load([dataset])
    passes: list[int] = []

    net, fit, test = train.pretrain(
        rows, epochs=2, on=torch.device("cpu"), report=lambda epoch, _: passes.append(epoch)
    )

    assert passes == [1, 2]
    for s in (fit, test):
        assert 0 <= s.agreement <= 1 and 0 <= s.value_accuracy <= 1 and s.value_error >= 0
    assert fit.agreement > 0.2  # far above choosing among ~7 options at random
    assert next(net.parameters()).device.type == "cpu"
    assert train.device().type in ("cpu", "mps")


def test_the_network_bot_plays_only_legal_options(dataset: Path, tmp_path: Path) -> None:
    net, _, _ = train.pretrain(train.load([dataset]), epochs=1, on=torch.device("cpu"))
    net.save(tmp_path / "net.pt")
    bot = factory(f"net:{tmp_path / 'net.pt'}")(0)
    assert isinstance(bot, NetBot)

    env = BattleEnvironment(["Blissey", "Toxapex", "Snorlax"], ["Excadrill"], seed=1)
    legal = env.legal_actions(0)
    assert bot.choose_action(0, env.observe(0), legal) in legal
    view = env.observe(0)
    blissey = view.active(0)
    blissey.current_hp = 0
    view.set_pokemon(0, 0, blissey)
    assert bot.choose_replacement(0, view, [2]) == 2
    assert bot.choose_replacement(0, view, [1, 2]) in (1, 2)


def test_the_command_trains_and_saves(dataset: Path, tmp_path: Path) -> None:
    shown: list[str] = []
    out = tmp_path / "net.pt"

    assert command.main([str(dataset), "--epochs", "1", "--out", str(out)], shown.append) == 0
    assert out.is_file()
    assert shown[0].endswith(" decisions, on " + str(train.device()) + "\n")
    assert shown[1].startswith("pass 1: agreement ")
    assert shown[-1] == f"-> {out}\n"


def test_without_pytorch_the_command_says_how_to_get_it(
    dataset: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setitem(sys.modules, "trainerlab.rl.train", None)
    monkeypatch.delattr(trainerlab.rl, "train")
    shown: list[str] = []

    assert command.main([str(dataset)], shown.append) == 2
    assert "pip install" in shown[0]


def test_the_rows_hold_what_the_bot_saw_chose_and_got(dataset: Path) -> None:
    rows = np.load(dataset)
    n = len(rows["action"])
    assert n > 100
    assert rows["mask"][np.arange(n), rows["action"]].all()  # every choice was legal
    assert set(np.unique(rows["result"])) <= {-1.0, 0.0, 1.0}
    assert rows["pokemon"].dtype == np.float16
