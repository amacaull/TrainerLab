import itertools
from collections.abc import Iterator
from pathlib import Path

import pytest

from trainerlab.bots.roster import BOTS
from trainerlab.commands.cli import main, menu
from trainerlab.terminal.human import Ask


def _scripted(answers: list[str], prompts: list[str] | None = None) -> Ask:
    replies: Iterator[str] = iter(answers)

    def ask(prompt: str) -> str:
        if prompts is not None:
            prompts.append(prompt)
        try:
            return next(replies)
        except StopIteration:
            raise EOFError from None

    return ask


def _menu(answers: list[str]) -> tuple[list[str], str]:
    """The command the menu builds, and everything it showed: lists, messages and prompts."""
    shown: list[str] = []
    return menu(_scripted(answers, shown), shown.append), "".join(shown)


# The modes, in menu order: 1 play, 2 duel, 3 watch, 4 audit, 5 dataset.
# The bots, in menu order: 1 random, 2 hitter, 3 dodger, 4 matcher, 5 survivor, 6 calculator.


def test_the_menu_lists_every_mode_and_every_bot_with_what_it_does() -> None:
    _, shown = _menu(["2", "6", "5", "", ""])

    assert "1. play" in shown
    assert "5. dataset" in shown
    assert "6. calculator  rule 5: survivor, but plays the damage the engine expects" in shown


def test_a_duel_asks_how_many_pairs_and_from_which_seed() -> None:
    command, shown = _menu(["2", "6", "5", "5000", "30000"])

    assert command == ["duel", "calculator", "survivor", "--pairs", "5000", "--from", "30000"]
    assert "How many pairs of battles (two battles each, sides swapped) [500] > " in shown
    assert "Seed of the first pair [0] > " in shown


def test_enter_keeps_a_default_and_a_default_is_left_out_of_the_command() -> None:
    assert _menu(["2", "2", "1", "", ""])[0] == ["duel", "hitter", "random"]


def test_a_name_is_as_good_as_its_number() -> None:
    assert _menu(["duel", "hitter", "random", "12", ""])[0] == [
        "duel",
        *("hitter", "random", "--pairs", "12"),
    ]


def test_a_wrong_answer_is_asked_again() -> None:
    answers = ["0", "9", "duel", "", "genius", "2", "1", "0", "x", "2", "-1", "7"]
    command, shown = _menu(answers)

    assert command == ["duel", "hitter", "random", "--pairs", "2", "--from", "7"]
    assert shown.count("Type a number from 1 to 6.") == 2  # the mode: 0, 9
    assert shown.count("Type a number from 1 to 8.") == 2  # bot A: empty (no default), genius
    assert shown.count("Invalid answer, try again.") == 3


def test_play_asks_for_the_opponent_the_seed_the_team_and_the_speed() -> None:
    assert _menu(["1", "", "", "", "", ""])[0] == ["play"]  # Enter: forecaster, shipped model
    team = "Dragapult,Snorlax"
    assert _menu(["1", "2", "x", "42", team, "instant"])[0] == [
        "play",
        *("--against", "hitter", "--seed", "42", "--team", team, "--speed", "instant"),
    ]


def test_watch_lists_losses_or_replays_one_battle() -> None:
    assert _menu(["3", "5", "4", "", "", "20000"])[0] == [
        "watch",
        *("survivor", "matcher", "--losses", "--from", "20000"),
    ]
    assert _menu(["3", "5", "4", "2", "", "20004", "2", "1"])[0] == [
        "watch",
        *("survivor", "matcher", "--seed", "20004", "--side", "1"),
    ]
    assert _menu(["3", "5", "4", "seed", "7", ""])[0] == [
        "watch",
        "survivor",
        "matcher",
        "--seed",
        "7",
    ]


def test_audit_asks_for_two_bots_and_how_many_pairs() -> None:
    assert _menu(["4", "5", "4", "1000", "20000"])[0] == [
        "audit",
        *("survivor", "matcher", "--pairs", "1000", "--from", "20000"),
    ]


def test_the_dataset_file_is_named_after_its_bots_and_seeds_unless_told_otherwise() -> None:
    command, shown = _menu(["5", "6", "5", "2000", "40000", ""])
    assert command == ["dataset", "calculator", "survivor", "--pairs", "2000", "--from", "40000"]
    assert "[calculator-vs-survivor-40000-41999.npz]" in shown
    assert _menu(["5", "6", "5", "", "", "mine.npz"])[0] == [
        "dataset",
        *("calculator", "survivor", "--out", "mine.npz"),
    ]


def test_train_asks_for_a_dataset_that_exists_and_a_model(tmp_path: Path) -> None:
    data = tmp_path / "d.npz"
    data.write_bytes(b"")

    command, shown = _menu(["6", "missing.npz", f"{data} {data}", "2", "mine.npz"])

    assert command == ["train", str(data), str(data), "--model", "network", "--out", "mine.npz"]
    assert "Invalid answer, try again." in shown
    assert _menu(["6", str(data), "", ""])[0] == ["train", str(data)]


def test_the_forecaster_asks_for_its_model(tmp_path: Path) -> None:
    model = tmp_path / "gen2.npz"
    model.write_bytes(b"")

    command, shown = _menu(["2", "7", "nowhere.npz", str(model), "7", "", "", ""])

    assert command == ["duel", f"forecaster:{model}", "forecaster"]
    assert "Its model (empty: the one shipped) > " in shown
    assert _menu(["1", "7", str(model), "", "", ""])[0] == [
        "play",
        "--against",
        f"forecaster:{model}",
    ]


def test_the_menu_prints_the_command_then_runs_it(capsys: pytest.CaptureFixture[str]) -> None:
    shown: list[str] = []

    assert main([], _scripted(["2", "1", "1", "1", "3"]), shown.append) == 0

    assert shown[-1] == "\ntrainerlab duel random random --pairs 1 --from 3\n\n"
    assert capsys.readouterr().out.startswith("random vs random: ")


def test_leaving_the_menu_with_ctrl_d_stops_quietly() -> None:
    shown: list[str] = []

    assert main([], _scripted(["2"]), shown.append) == 130


def test_each_command_runs_directly(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["duel", "hitter", "random", "--pairs", "1"]) == 0
    assert main(["watch", "hitter", "random", "--losses", "--pairs", "1"]) == 0
    assert main(["audit", "hitter", "random", "--pairs", "1"]) == 0
    out = str(tmp_path / "d.npz")
    assert main(["dataset", "hitter", "random", "--pairs", "1", "--out", out]) == 0
    assert main(["train", "nowhere.npz"]) == 2
    printed = capsys.readouterr().out
    assert "hitter vs random: " in printed
    assert "hitter lost 0 of 2 battles against random." in printed
    assert (tmp_path / "d.npz").exists()

    always_one = itertools.repeat("1")
    play = ["play", "--seed", "3", "--against", "hitter", "--speed", "instant", "--no-color"]
    assert main(play, lambda prompt: next(always_one)) == 0


def test_bots_lists_every_bot(capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["bots"]) == 0

    lines = capsys.readouterr().out.splitlines()
    assert [line.split()[0] for line in lines] == list(BOTS)


def test_help_and_an_unknown_command() -> None:
    shown: list[str] = []
    assert main(["--help"], write=shown.append) == 0
    assert shown[0].startswith("usage: trainerlab [COMMAND [OPTIONS]]")
    assert "  dataset  turn battles into a dataset to learn from" in shown[0]

    shown.clear()
    assert main(["fight"], write=shown.append) == 2
    assert shown[0].startswith("trainerlab: unknown command 'fight'")


def test_without_arguments_the_command_line_is_read(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr("sys.argv", ["trainerlab", "--help"])

    assert main() == 0
    assert capsys.readouterr().out.startswith("usage:")
