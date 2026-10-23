import itertools
import re
from collections.abc import Iterator
from pathlib import Path

import pytest

from trainerlab import _engine
from trainerlab.environment import DATA_DIR
from trainerlab.play import main
from trainerlab.screen import CLEAR

SEED_LINE = re.compile(r"Seed \d+ \(replay this battle with --seed \d+\)\. Opponent: survival\.")
COLOUR = re.compile(r"\x1b\[3\dm")


def _play(args: list[str], answers: list[str] | None = None) -> tuple[int, list[str]]:
    """Plays with scripted answers (option 1 forever by default) and returns the screen lines."""
    replies: Iterator[str] = iter(answers) if answers is not None else itertools.repeat("1")

    def ask(prompt: str) -> str:
        try:
            return next(replies)
        except StopIteration:
            raise EOFError from None  # what input() raises when the keyboard input is closed

    output: list[str] = []
    code = main([*args, "--speed", "instant"], ask=ask, write=output.append)
    return code, "".join(output).splitlines()


def _contains(lines: list[str], text: str) -> int:
    return next(i for i, line in enumerate(lines) if text in line)


def test_a_whole_battle_can_be_played_to_its_end() -> None:
    code, shown = _play(["--seed", "3", "--opponent", "damage"])

    assert code == 0
    assert shown[0] == "Seed 3 (replay this battle with --seed 3). Opponent: damage."
    assert shown[-1] in ("You won!", "You lost.", "It's a draw.")
    assert _contains(shown, "─ Turn 1 ─") < _contains(shown, "─ What will you do? ─")
    assert _contains(shown, "Pokemon knocked out:") > _contains(shown, "─ Final board ─")


def test_the_result_line_names_the_real_winner() -> None:
    # Seed 3 against the damage bot, always taking option 1: the player wins this one.
    assert _play(["--seed", "3", "--opponent", "damage"])[1][-1] == "You won!"
    assert _play(["--seed", "5", "--opponent", "damage"])[1][-1] == "You lost."


def test_the_battle_is_logged_without_colours(tmp_path: Path) -> None:
    log = tmp_path / "battle.txt"

    _play(["--seed", "3", "--opponent", "damage", "--log", str(log)])

    lines = log.read_text().splitlines()
    assert lines[0] == "Seed 3 (replay this battle with --seed 3). Opponent: damage."
    assert "== Turn 1 ==" in lines
    assert "> Psyshock (Psychic, 80 power, 10/10 PP)" in lines
    assert lines[-1] == "You won!"
    assert not any(COLOUR.search(line) for line in lines)


def test_hp_lost_over_a_battle_never_exceeds_a_pokemon_max_hp(tmp_path: Path) -> None:
    # The history must follow the battle: told from the initial view, every hit would be capped
    # at full HP, and a Pokemon could lose more than it ever had. Counted from the log, where
    # each turn appears once outside the "Last turn" reminders.
    _engine.engine_init(str(DATA_DIR))
    max_hp = {
        _engine.species_display_name(i): _engine.make_combatant(i).stats.hp
        for i in range(_engine.species_count())
    }
    log = tmp_path / "battle.txt"
    _play(["--seed", "3", "--opponent", "damage", "--log", str(log)])
    hp_line = re.compile(
        r"^(Your|The foe's) (.+?) (?:lost (\d+) HP|restored (\d+) HP|is .*\((\d+) HP\))"
    )
    lost: dict[tuple[str, str], int] = {}
    section = ""
    for line in log.read_text().splitlines():
        if line.startswith("== "):
            section = line
        elif section != "== Last turn ==" and (match := hp_line.match(line)):
            owner, species, hit, healed, other = match.groups()
            change = int(hit or other or 0) - int(healed or 0)
            lost[(owner, species)] = lost.get((owner, species), 0) + change

    assert lost
    assert all(total <= max_hp[species] for (_, species), total in lost.items())


def test_the_leads_entry_effects_are_shown_before_the_first_turn() -> None:
    _, shown = _play(["--seed", "1", "--team", "TapuKoko"])

    assert _contains(shown, "─ Battle start ─") < _contains(shown, "─ Turn 1 ─")
    assert _contains(shown, "Electric Terrain covers the field.") < _contains(shown, "─ Turn 1 ─")


def test_leads_without_an_entry_effect_draw_no_empty_panel() -> None:
    # Seed 3: Hoopa Unbound against Mega Sceptile, neither has an entry ability.
    _, shown = _play(["--seed", "3", "--opponent", "damage"])

    assert not any("─ Battle start ─" in line for line in shown)


def test_hints_are_shown_unless_turned_off() -> None:
    tagged = re.compile(r"PP\)  x")

    assert any(tagged.search(line) for line in _play(["--seed", "4"])[1])
    assert not any(tagged.search(line) for line in _play(["--seed", "4", "--no-hints"])[1])


def test_the_same_seed_and_answers_replay_the_same_battle() -> None:
    assert _play(["--seed", "8"])[1] == _play(["--seed", "8"])[1]


def test_without_a_seed_one_is_drawn_and_shown() -> None:
    _, shown = _play([])

    assert SEED_LINE.fullmatch(shown[0])


def test_the_player_can_choose_their_team() -> None:
    _, shown = _play(["--seed", "1", "--team", "Snorlax,Toxapex"])

    assert shown[1] == "Your team: Snorlax, Toxapex"


@pytest.mark.parametrize(
    ("team", "reason"),
    [
        ("Snorlax,Snorlax", "Invalid team: E_TEAM"),
        ("Snorlx", "Invalid team: Unknown species: 'Snorlx'"),
    ],
)
def test_an_invalid_team_is_refused_before_the_battle(team: str, reason: str) -> None:
    code, shown = _play(["--team", team])

    assert code == 2
    assert shown[0].startswith(reason)


def test_closing_the_input_abandons_the_battle() -> None:
    code, shown = _play(["--seed", "1"], answers=[])

    assert code == 130
    assert shown[-1] == "Battle abandoned."


def test_a_terminal_gets_colours_and_cleared_screens(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("sys.stdout.isatty", lambda: True)

    coloured = "\n".join(_play(["--seed", "2"])[1])
    plain = "\n".join(_play(["--seed", "2", "--no-color"])[1])

    assert COLOUR.search(coloured)
    assert not COLOUR.search(plain)
    assert CLEAR in coloured and CLEAR in plain


def test_outside_a_terminal_there_are_no_codes_at_all() -> None:
    assert "\x1b[" not in "\n".join(_play(["--seed", "2"])[1])


def test_by_default_the_output_goes_to_the_terminal(capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["--team", "Snorlx"]) == 2

    assert capsys.readouterr().out == "Invalid team: Unknown species: 'Snorlx'\n"
