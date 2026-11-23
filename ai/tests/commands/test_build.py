import re

import pytest

from trainerlab.commands import build
from trainerlab.commands.cli import SECTIONS


def test_the_command_scores_builder_a_with_its_interval() -> None:
    shown: list[str] = []
    argv = ["random", "random", "--bot", "hitter", "--size", "2", "--pairs", "3", "--jobs", "1"]

    assert build.main(argv, shown.append) == 0

    assert re.fullmatch(
        r"random vs random, teams of 2, from pools of 12, played by hitter: "
        r"\d\.\d{3} \[\d\.\d{3}, \d\.\d{3}\] "
        r"\(\d+ wins, \d+ draws, \d+ losses, seeds 0-2\)\n",
        shown[0],
    )


def test_a_core_is_given_by_its_names_in_either_language() -> None:
    shown: list[str] = []
    argv = ["random", "random", "--bot", "hitter", "--size", "2", "--pairs", "1", "--jobs", "1"]

    assert build.main([*argv, "--core", "Léviator"], shown.append) == 0
    assert "core Gyarados" in shown[0]


@pytest.mark.parametrize(
    ("argv", "error"),
    [
        (["best", "random"], "unknown builder 'best'"),
        (["random", "random", "--core", "Pikachu"], "unknown Pokémon: 'Pikachu'"),
        (["random", "random", "--size", "2", "--core", "Gyarados,Snorlax"], "fewer than 2"),
        (["random", "random", "--size", "6", "--pool", "5"], "a pool of 6 species at least"),
    ],
)
def test_wrong_options_are_refused(
    argv: list[str], error: str, capsys: pytest.CaptureFixture[str]
) -> None:
    with pytest.raises(SystemExit):
        build.main(argv)
    assert error in capsys.readouterr().err


def test_the_command_is_listed_with_the_analyses() -> None:
    assert "build" in SECTIONS["analyse"]


def test_a_pool_of_49_is_free_choice() -> None:
    shown: list[str] = []
    argv = ["random", "random", "--bot", "hitter", "--size", "2", "--pairs", "1", "--jobs", "1"]

    assert build.main([*argv, "--pool", "49"], shown.append) == 0
    assert "teams of 2, free choice, played by hitter" in shown[0]
