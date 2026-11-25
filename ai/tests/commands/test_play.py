import io
import re
from collections.abc import Iterator
from pathlib import Path
from typing import Any

import pytest
from prompter import First, Scripted
from rich.console import Console

from trainerlab import _engine, i18n
from trainerlab.commands.play import main
from trainerlab.game.environment import DATA_DIR, BattleEnvironment
from trainerlab.game.events import EventKind as K
from trainerlab.i18n import names
from trainerlab.terminal.prompts import Option
from trainerlab.terminal.render import HP_LOSSES, Narration, species_card
from trainerlab.terminal.screen import CLEAR, Screen


@pytest.fixture(autouse=True)
def english() -> Iterator[None]:
    yield
    i18n.set_language("en")


@pytest.fixture(autouse=True)
def random_foe(monkeypatch: pytest.MonkeyPatch) -> None:
    """The bot's team drawn at random, as before palier 12: the battles below do not depend on
    the shipped species strengths."""
    from trainerlab.game.environment import random_team_pair

    def foe(seed: int, size: int) -> list[str]:
        return random_team_pair(seed, size)[1]

    monkeypatch.setattr("trainerlab.commands.play.bot_team", foe)


def _play(
    args: list[str], prompter: Scripted | None = None, *, terminal: bool = False, **kwargs: bool
) -> tuple[int, str]:
    """Plays with the first option every time (by default) and returns what the screen shows."""
    output = io.StringIO()
    console = Console(file=output, width=100, force_terminal=terminal, highlight=False)
    code = main(
        [*args, "--speed", "instant"], prompter=prompter or First(), console=console, **kwargs
    )
    return code, output.getvalue()


def test_a_whole_battle_can_be_played_to_its_end() -> None:
    code, shown = _play(["--seed", "3", "--against", "hitter"])

    assert code == 0
    assert "Turn 1 · against hitter" in shown
    assert f"{names.leader(3)} sends out" in shown
    assert "Seed 3: to replay this battle" in shown


def test_the_result_names_the_real_winner() -> None:
    # Seed 3 against the hitter bot, always taking the first option: the player wins this one.
    won = _play(["--seed", "3", "--against", "hitter"])[1]
    assert "Victory!" in won and f"You beat {names.leader(3)} (hitter) in" in won
    lost = _play(["--seed", "5", "--against", "hitter"])[1]
    assert "Defeat" in lost and f"{names.leader(5)} (hitter) beats you in" in lost


def test_the_battle_is_logged_without_colours(tmp_path: Path) -> None:
    log = tmp_path / "battle.txt"

    _play(["--seed", "3", "--against", "hitter", "--log", str(log)], terminal=True)

    text = log.read_text()
    assert "Turn 1 · against hitter" in text and "Victory!" in text
    assert "\x1b[" not in text


def test_hp_lost_over_a_battle_never_exceeds_a_pokemon_max_hp(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # The turns must follow the battle: told from the initial view, every hit would be capped
    # at full HP, and a Pokemon could lose more than it ever had.
    _engine.engine_init(str(DATA_DIR))
    told: list[Narration] = []
    narrate = Screen.narrate

    def telling(self: Screen, lines: list[Narration]) -> None:
        told.extend(lines)
        narrate(self, lines)

    monkeypatch.setattr(Screen, "narrate", telling)
    env_seen: list[BattleEnvironment] = []
    real = BattleEnvironment

    def keep(*args: Any, **kwargs: Any) -> BattleEnvironment:
        env_seen.append(real(*args, **kwargs))
        return env_seen[-1]

    monkeypatch.setattr("trainerlab.commands.play.BattleEnvironment", keep)
    _play(["--seed", "3", "--against", "hitter"])
    lost: dict[tuple[int, int], int] = {}
    for line in told:
        if line.kind in HP_LOSSES:
            lost[(line.side, line.slot)] = lost.get((line.side, line.slot), 0) + line.amount
        elif line.kind == K.HEALED:
            lost[(line.side, line.slot)] = lost.get((line.side, line.slot), 0) - line.amount

    final = env_seen[-1].observe(0)  # the battle's, after the check of the teams
    assert lost
    assert all(total <= final.pokemon(*key).stats.hp for key, total in lost.items())


def test_the_leads_entry_effects_are_told_before_the_first_turn() -> None:
    _, shown = _play(["--seed", "1", "--team", "TapuKoko"])

    assert shown.index("Electric Terrain covers the field.") < shown.index("Turn 1 ·")


def test_the_same_seed_and_answers_replay_the_same_battle() -> None:
    assert _play(["--seed", "8"])[1] == _play(["--seed", "8"])[1]


def test_without_a_seed_one_is_drawn_and_shown() -> None:
    assert re.search(r"Seed \d+: to replay this battle", _play([])[1])


def test_the_player_can_choose_their_team_in_either_language() -> None:
    _, shown = _play(["--seed", "1", "--team", "Ronflex,Toxapex"])

    assert re.search(r"You +Snorlax", shown)


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
    assert shown.startswith(reason)


def test_closing_the_input_abandons_the_battle() -> None:
    code, shown = _play(["--seed", "1"], Scripted([]))

    assert code == 130
    assert shown.rstrip().endswith("Battle abandoned.")


def test_a_rematch_plays_a_new_battle_and_the_same_one_replays_the_seed() -> None:
    prompter = First()
    answers = iter(["same", "rematch", "leave"])

    class Again(First):
        def select(self, message, options, *, search=False):  # type: ignore[no-untyped-def]
            if message == "Play again?":
                return next(answers)
            return super().select(message, options, search=search)

    _, shown = _play(["--seed", "3", "--against", "hitter"], Again())

    assert shown.count("Seed 3: to replay this battle") == 2
    assert len(re.findall(r"Seed \d+: to replay this battle", shown)) == 3
    del prompter


def test_from_the_menu_the_last_choice_goes_back_to_it() -> None:
    prompter = First()
    _play(["--seed", "3", "--against", "hitter"], prompter, in_menu=True)

    options = [o for kind, message, o in prompter.asked if message == "Play again?"][0]
    assert [getattr(o, "label", None) for o in options][-1] == "Back to the menu"


def test_in_french_the_battle_is_told_in_french() -> None:
    i18n.set_language("fr")

    _, shown = _play(["--seed", "3", "--against", "hitter"])

    assert "Tour 1 · contre hitter" in shown and "Victoire !" in shown


def test_a_terminal_gets_colours_and_cleared_screens(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("sys.stdout.isatty", lambda: True)

    coloured = _play(["--seed", "2"], terminal=True)[1]
    plain = _play(["--seed", "2", "--no-color"], terminal=True)[1]

    assert "\x1b[36m" in coloured and "\x1b[36m" not in plain
    assert CLEAR in coloured and CLEAR in plain


def test_outside_a_terminal_there_are_no_codes_at_all() -> None:
    assert "\x1b[" not in _play(["--seed", "2"])[1]


def test_by_default_the_output_goes_to_the_terminal(capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["--team", "Snorlx"]) == 2

    assert capsys.readouterr().out == "Invalid team: Unknown species: 'Snorlx'\n"


def test_a_draw_says_so(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("trainerlab.terminal.session.play_battle", lambda *args: None)

    shown = _play(["--seed", "3", "--against", "hitter"])[1]

    assert "Draw" in shown and f"A draw against {names.leader(3)} (hitter) after 0 turns." in shown


def test_running_away_ends_the_battle_as_lost_and_offers_to_play_again() -> None:
    from trainerlab.terminal.human import FLEE

    code, shown = _play(["--seed", "3", "--against", "hitter"], First([0, FLEE, True]))

    assert code == 0
    assert f"You ran away! {names.leader(3)} (hitter) wins." in shown


def test_the_teams_can_be_smaller_than_six() -> None:
    shown = _play(["--seed", "3", "--against", "hitter", "--size", "2"])[1]

    assert "●●" in shown and "●●●" not in shown


def test_the_player_chooses_who_goes_first() -> None:
    from trainerlab.game.environment import random_team_pair

    team = random_team_pair(3)[0]
    prompter = First([2])

    shown = _play(["--seed", "3", "--against", "hitter"], prompter)[1]

    assert prompter.asked[0][1] == "Who goes first?"
    lead = prompter.asked[0][2][2]
    assert isinstance(lead, Option) and lead.description == species_card(team[2])
    assert re.search(rf"You +{re.escape(names.species_name(team[2]))}", shown)


def test_a_battle_can_be_saved_once_played(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    from trainerlab.commands.play import slug

    monkeypatch.setenv("TRAINERLAB_GAMES", str(tmp_path / "games"))
    prompter = First(again="save")
    answers = iter(["save", "leave"])

    class Saving(First):
        def select(self, message, options, *, search=False):  # type: ignore[no-untyped-def]
            if message == "Play again?":
                return next(answers)
            return super().select(message, options, search=search)

    shown = _play(["--seed", "3", "--against", "hitter"], Saving())[1]

    (saved,) = (tmp_path / "games").iterdir()
    assert saved.name.endswith(f"_{slug(names.leader(3))}-hitter_3.txt")
    assert "Turn 1 · against hitter" in saved.read_text() and "Victory!" in saved.read_text()
    assert f"Battle saved: {saved}" in shown.replace("\n", "")
    assert slug("Lt. Surge") == "lt-surge" and slug("Érika") == "erika"
    del prompter


def test_the_bot_builds_its_own_team(monkeypatch: pytest.MonkeyPatch) -> None:
    asked = []

    def bot_team(seed: int, size: int) -> list[str]:
        asked.append((seed, size))
        return ["Snorlax", "Blissey"]

    monkeypatch.setattr("trainerlab.commands.play.bot_team", bot_team)

    shown = _play(["--seed", "3", "--against", "hitter", "--size", "2"])[1]

    assert asked == [(3, 2)]
    assert names.species_name("Snorlax") in shown
