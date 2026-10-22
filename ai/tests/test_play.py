import itertools
import re
from collections.abc import Iterator

import pytest

from trainerlab import _engine
from trainerlab.environment import DATA_DIR
from trainerlab.play import main

SEED_LINE = re.compile(r"Seed \d+ \(replay this battle with --seed \d+\)\. Opponent: survival\.")


def _play(args: list[str], answers: list[str] | None = None) -> tuple[int, list[str]]:
    """Plays with scripted answers (option 1 forever by default) and returns what was shown."""
    shown: list[str] = []
    replies: Iterator[str] = iter(answers) if answers is not None else itertools.repeat("1")

    def ask(prompt: str) -> str:
        try:
            return next(replies)
        except StopIteration:
            raise EOFError from None  # what input() raises when the keyboard input is closed

    code = main(args, ask=ask, say=shown.append)
    return code, shown


def test_a_whole_battle_can_be_played_to_its_end() -> None:
    code, shown = _play(["--seed", "3", "--opponent", "damage"])

    assert code == 0
    assert shown[0] == "Seed 3 (replay this battle with --seed 3). Opponent: damage."
    assert shown[-1] in ("You won!", "You lost.", "It's a draw.")
    assert any(line.startswith("--- Turn") for line in shown)


def test_the_result_line_names_the_real_winner() -> None:
    # Seed 3 against the damage bot, always taking option 1: the player wins this one.
    assert _play(["--seed", "3", "--opponent", "damage"])[1][-1] == "You won!"
    assert _play(["--seed", "5", "--opponent", "damage"])[1][-1] == "You lost."


def test_hp_lost_over_a_battle_never_exceeds_a_pokemon_max_hp() -> None:
    # The view the history is told from must follow the battle: from the initial view, every
    # hit would be capped at full HP, and a Pokemon could lose more than it ever had.
    _engine.engine_init(str(DATA_DIR))
    max_hp = {
        _engine.species_display_name(i): _engine.make_combatant(i).stats.hp
        for i in range(_engine.species_count())
    }
    hp_line = re.compile(
        r"^(Your|The foe's) (.+?) (?:lost (\d+) HP|restored (\d+) HP|is .*\((\d+) HP\))"
    )
    lost: dict[tuple[str, str], int] = {}
    for line in _play(["--seed", "3", "--opponent", "damage"])[1]:
        if match := hp_line.match(line):
            owner, species, hit, healed, other = match.groups()
            change = int(hit or other or 0) - int(healed or 0)
            lost[(owner, species)] = lost.get((owner, species), 0) + change

    assert lost
    assert all(total <= max_hp[species] for (_, species), total in lost.items())


def test_the_leads_entry_effects_are_shown_before_the_first_turn() -> None:
    _, shown = _play(["--seed", "1", "--team", "TapuKoko"])

    assert shown.index("Electric Terrain covers the field.") < shown.index("--- Turn 1 ---")


def test_the_same_seed_and_answers_replay_the_same_battle() -> None:
    assert _play(["--seed", "8"])[1] == _play(["--seed", "8"])[1]


def test_without_a_seed_one_is_drawn_and_shown() -> None:
    _, shown = _play([])

    assert re.fullmatch(
        r"Seed \d+ \(replay this battle with --seed \d+\)\. Opponent: survival\.", shown[0]
    )


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


def test_colours_are_on_in_a_terminal_unless_turned_off(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("sys.stdout.isatty", lambda: True)

    coloured = _play(["--seed", "2"])[1]
    plain = _play(["--seed", "2", "--no-color"])[1]

    assert any("\033[" in line for line in coloured)
    assert not any("\033[" in line for line in plain)
