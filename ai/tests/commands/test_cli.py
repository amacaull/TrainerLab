import argparse
from collections.abc import Iterator, Sequence
from pathlib import Path

import pytest
from consoles import recording, text
from prompter import First, Scripted

from trainerlab import i18n
from trainerlab.bots.roster import BOTS, FEATURED
from trainerlab.commands.cli import main, translate_argparse
from trainerlab.commands.menu import BACK, OTHER, menu
from trainerlab.i18n import settings
from trainerlab.terminal.prompts import Line, Option, Separator, plain
from trainerlab.terminal.render import species_card


@pytest.fixture(autouse=True)
def config(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Iterator[Path]:
    """A config file of the test's own, English kept in it: no first-launch question."""
    path = tmp_path / "config.toml"
    monkeypatch.setenv("TRAINERLAB_CONFIG", str(path))
    settings.save_language("en")
    yield path
    i18n.set_language("en")
    translate_argparse()


def _menu(answers: list[object]) -> tuple[list[str] | None, Scripted]:
    prompter = Scripted(answers)
    return menu(prompter, recording()), prompter


def _labels(options: Sequence[Line]) -> list[str]:
    return [plain(o.label).split()[0] for o in options if isinstance(o, Option)]


def _label(options: Sequence[Line], index: int) -> str:
    option = options[index]
    assert isinstance(option, Option)
    return plain(option.label)


def test_the_menu_offers_to_play_or_to_measure_then_the_language_and_quit() -> None:
    _, prompter = _menu(["bots"])

    _, message, options = prompter.asked[0]
    assert message == "What do you want to do?"
    sections = [o.title for o in options if isinstance(o, Separator)]
    assert sections == ["Play", "Measure", ""]
    assert [o.value for o in options if isinstance(o, Option)] == [
        "play",
        "watch",
        "duel",
        "usage",
        "audit",
        "bots",
        "language",
        "quit",
    ]


def test_a_duel_asks_for_two_bots_and_the_pairs_and_leaves_the_defaults_out() -> None:
    command, prompter = _menu(["duel", "alphazero", "forecaster", "5000", "30000"])

    assert command == ["duel", "alphazero", "forecaster", "--pairs", "5000", "--from", "30000"]
    assert _menu(["duel", "mcts", "hitter", "500", "0"])[0] == ["duel", "mcts", "hitter"]
    bots = prompter.asked[1][2]
    assert _labels(bots) == [*FEATURED, "Another", "←"]
    assert "search guided by a network, the strongest" in _label(bots, 0)
    assert prompter.asked[3][1] == "Pairs of battles (two each, sides swapped; 500 advised)"


def test_another_bot_opens_every_other_bot_filtered_as_one_types() -> None:
    command, prompter = _menu(["usage", OTHER, "focus", "hitter", "200", "0"])

    assert command == ["usage", "focus", "hitter"]
    _, message, options = prompter.asked[2]
    assert message == "Another bot (type to filter)"
    assert set(_labels(options)) == set(BOTS) - set(FEATURED) | {"←"}


def test_back_from_every_other_bot_returns_to_the_main_bots() -> None:
    command, prompter = _menu(["usage", OTHER, BACK, "mcts", "hitter", "200", "0"])

    assert command == ["usage", "mcts", "hitter"]
    assert prompter.messages()[1:4] == [
        "Bot A, the one measured",
        "Another bot (type to filter)",
        "Bot A, the one measured",
    ]


def test_back_asks_the_question_before_again_and_from_the_first_returns_to_the_menu() -> None:
    command, prompter = _menu(["duel", "alphazero", BACK, "mcts", "hitter", "10", "0"])

    assert command == ["duel", "mcts", "hitter", "--pairs", "10"]
    assert prompter.messages()[1:4] == [
        "Bot A, the one measured",
        "Bot B, its opponent",
        "Bot A, the one measured",
    ]
    assert _menu(["duel", BACK])[0] == []


def test_ctrl_c_in_a_text_goes_back_to_the_question_before() -> None:
    answers = ["duel", "mcts", "hitter", KeyboardInterrupt(), "random", "10", "0"]
    command, prompter = _menu(answers)

    assert command == ["duel", "mcts", "random", "--pairs", "10"]
    assert main([], Scripted([KeyboardInterrupt()]), recording()) == 130  # at the menu: quits


def test_play_asks_the_level_the_team_size_the_team_the_seed_and_the_speed() -> None:
    command, prompter = _menu(["play", "hard", 3, "random", "42", "fast"])

    assert command == ["play", "--level", "hard", "--size", "3", "--seed", "42", "--speed", "fast"]
    levels = [plain(o.label) for o in prompter.asked[1][2] if isinstance(o, Option)]
    assert levels == ["Easy", "Medium", "Hard", "Impossible", "Another bot…", "← Back"]
    assert _menu(["play", "medium", 6, "random", "", "normal"])[0] == ["play"]


def test_play_can_still_face_any_bot_by_its_name() -> None:
    command, prompter = _menu(["play", OTHER, BACK, OTHER, "nash", 3, "random", "42", "fast"])

    assert command == [
        "play",
        "--against",
        "nash",
        "--size",
        "3",
        "--seed",
        "42",
        "--speed",
        "fast",
    ]
    assert prompter.asked[3][1] == "Your opponent"  # Back from the whole list: the levels again
    assert "nash" in [o.value for o in prompter.asked[4][2] if isinstance(o, Option)]
    assert _labels(prompter.asked[5][2]) == ["6", "5", "4", "3", "2", "1", "←"]
    speeds = [o.value for o in prompter.asked[-1][2] if isinstance(o, Option)]
    assert speeds == ["slow", "normal", "fast", BACK]  # no instant text to play


def test_a_team_is_built_from_the_list_and_a_pokemon_can_be_taken_out() -> None:
    i18n.set_language("fr")
    answers = ["play", "medium", 2, "own", "add", "Gyarados", "add", "Blissey"]
    answers += ["remove", "Blissey", "add", "Snorlax", "done", "", "normal"]

    command, prompter = _menu(answers)

    assert command == ["play", "--team", "Gyarados,Snorlax"]
    _, message, options = prompter.asked[5]
    assert message == "Quel Pokémon ? (tape pour filtrer)"
    first = [o for o in options if isinstance(o, Option)][0]
    assert plain(first.label).split()[0] == "Arcanin" and "Feu / Roche" in plain(first.label)
    assert first.description == species_card(first.value)
    removed = [o for o in prompter.asked[9][2] if isinstance(o, Option)][0]
    assert removed.description == species_card("Gyarados")
    assert "Ton équipe (1/2) : Léviator" in prompter.messages()


def test_the_team_can_be_confirmed_only_full_and_back_leaves_the_team() -> None:
    answers = ["play", "medium", 1, "own", "add", BACK, "add", "Gyarados", "remove", BACK]
    answers += ["remove", "Gyarados", BACK, "random", "", "normal"]

    command, prompter = _menu(answers)

    assert command == ["play", "--size", "1"]
    actions = [o.value for o in prompter.asked[4][2] if isinstance(o, Option)]
    assert actions == ["add", BACK]


def test_watch_looks_for_losses_or_replays_one_battle_at_the_speed_chosen() -> None:
    survivor, matcher = [OTHER, "survivor"], [OTHER, "matcher"]
    assert _menu(["watch", *survivor, *matcher, "losses", "20", "500", "slow"])[0] == [
        "watch",
        "survivor",
        "matcher",
        "--losses",
        "--pairs",
        "20",
        "--from",
        "500",
        "--speed",
        "slow",
    ]
    assert _menu(["watch", *survivor, *matcher, "seed", "77", "1", "normal"])[0] == [
        "watch",
        "survivor",
        "matcher",
        "--seed",
        "77",
        "--side",
        "1",
    ]
    answers = ["watch", "mcts", "hitter", "seed", "77", BACK, KeyboardInterrupt(), "losses"]
    command, prompter = _menu([*answers, "50", "0", "normal"])
    assert command == ["watch", "mcts", "hitter", "--losses"]


def test_audit_and_bots_take_what_they_need() -> None:
    assert _menu(["audit", "hitter", "random", "12", "0"])[0] == [
        "audit",
        "hitter",
        "random",
        "--pairs",
        "12",
    ]
    assert _menu(["bots"])[0] == ["bots"]


def test_quit_leaves_and_the_language_switches_and_is_kept(config: Path) -> None:
    assert _menu(["quit"])[0] is None

    assert _menu(["language"])[0] == []
    assert i18n.language() == "fr" and settings.saved_language() == "fr"
    assert _menu(["language"])[0] == []
    assert i18n.language() == "en"


def test_at_the_first_launch_the_language_is_asked_once(config: Path) -> None:
    config.unlink()
    prompter = Scripted(["fr", "quit"])

    assert main([], prompter, recording()) == 0

    assert prompter.asked[0][1] == "Language / Langue"
    assert prompter.asked[1][1] == "Que veux-tu faire ?"
    assert settings.saved_language() == "fr"


def test_the_menu_runs_the_command_shows_it_in_one_line_and_comes_back() -> None:
    console = recording()

    assert main([], Scripted(["bots", "", "quit"]), console) == 0

    shown = text(console)
    assert "The bots of TrainerLab" in shown
    assert "Same command: trainerlab bots" in shown
    assert shown.count("Pokémon battles, and the AIs that learned to play them") == 2


def test_play_from_the_menu_ends_on_back_to_the_menu() -> None:
    prompter = First(["play", OTHER, "hitter", 6, "random", "3", "fast"])

    assert main([], prompter, recording()) == 130  # the script ends at the menu: Ctrl-D

    again = [options for _, message, options in prompter.asked if message == "Play again?"]
    assert _label(again[0], -1) == "Back to the menu"


def test_leaving_the_menu_with_ctrl_d_stops_quietly() -> None:
    assert main([], Scripted([]), recording()) == 130


def test_each_command_runs_directly(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["duel", "hitter", "random", "--pairs", "1"]) == 0
    assert main(["watch", "hitter", "random", "--losses", "--pairs", "1"]) == 0
    assert main(["audit", "hitter", "random", "--pairs", "1"]) == 0
    assert main(["usage", "hitter", "random", "--pairs", "1"]) == 0
    out = str(tmp_path / "d.npz")
    assert main(["dataset", "hitter", "random", "--pairs", "1", "--out", out]) == 0
    assert main(["train", "nowhere.npz"]) == 2
    printed = capsys.readouterr().out
    assert "hitter vs random: " in printed
    assert "hitter lost 0 of 2 battles against random." in printed
    assert (tmp_path / "d.npz").exists()

    play = ["play", "--seed", "3", "--against", "hitter", "--speed", "instant", "--no-color"]
    assert main(play, First(), recording()) == 0


def test_bots_lists_every_bot_with_its_rule_by_section() -> None:
    console = recording()

    assert main(["bots"], console=console) == 0

    shown = text(console)
    rows = [line.strip("│ ").split() for line in shown.splitlines()]
    rows = [row for row in rows if row and row[0] in ("1", "17")]
    assert rows[0][:2] == ["1", "hitter"] and rows[1][:2] == ["17", "alphazero"]
    assert shown.index("Hand-written rules") < shown.index("Search") < shown.index("Neural")
    assert all(name in shown for name in BOTS)


def test_help_lists_the_commands_by_section() -> None:
    console = recording()
    assert main(["--help"], console=console) == 0
    shown = text(console)
    assert "Usage: trainerlab [COMMAND [OPTIONS]]" in shown
    assert shown.index(" Play") < shown.index(" Measure") < shown.index(" Train")
    assert "   alphazero  improve the network with its own search" in shown
    assert "History (paliers 3 to 6)" in shown


def test_an_unknown_command_is_refused_with_the_help() -> None:
    console = recording()
    assert main(["fight"], console=console) == 2
    assert text(console).startswith("trainerlab: unknown command 'fight'")


def test_lang_chooses_the_language_of_one_launch(config: Path) -> None:
    console = recording()

    assert main(["--lang", "fr", "--help"], console=console) == 0

    assert "Usage : trainerlab [COMMANDE [OPTIONS]]" in text(console)
    assert settings.saved_language() == "en"  # one launch only
    with pytest.raises(SystemExit, match="--lang takes en or fr"):
        main(["--lang", "de", "bots"], console=recording())


def test_argparse_speaks_the_language_chosen(capsys: pytest.CaptureFixture[str]) -> None:
    i18n.set_language("fr")
    translate_argparse()
    with pytest.raises(SystemExit):
        argparse.ArgumentParser(prog="x").parse_args(["--help"])
    assert "usage : x" in capsys.readouterr().out
    assert argparse._("unrecognized arguments: %s") == "unrecognized arguments: %s"  # type: ignore[attr-defined]


def test_without_arguments_the_command_line_is_read(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr("sys.argv", ["trainerlab", "--help"])

    assert main() == 0
    assert "Usage: trainerlab" in capsys.readouterr().out


def test_a_number_of_pairs_or_a_seed_is_checked_before_it_is_taken() -> None:
    from trainerlab.commands.menu import _valid
    from trainerlab.commands.options import count

    assert _valid(count)("12") and not _valid(count)("0") and not _valid(count)("x")


def test_switching_the_language_from_the_menu_shows_the_menu_again_in_it() -> None:
    console = recording()

    assert main([], Scripted(["language", "quit"]), console) == 0

    assert "Des combats Pokémon, et les IA qui ont appris à y jouer" in text(console)
