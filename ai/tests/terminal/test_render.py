import io
from collections.abc import Iterator
from dataclasses import dataclass

import pytest
from rich.console import Console

from trainerlab import _engine, i18n
from trainerlab.game.environment import BattleEnvironment
from trainerlab.game.events import EventKind as K
from trainerlab.terminal.prompts import plain as plain_label
from trainerlab.terminal.render import (
    Style,
    Teller,
    board,
    card,
    describe_field,
    hp_bar,
    move_option,
    plain,
    species_card,
    switch_options,
    team_dots,
)

PLAIN = Style(enabled=False)
BOT = (None, "Misty")  # the player at the keyboard, and the trainer of the foe


@pytest.fixture(autouse=True)
def english() -> Iterator[None]:
    yield
    i18n.set_language("en")


@dataclass(frozen=True)
class FakeEvent:
    """An engine event built by hand, to reach every event kind."""

    kind: K
    side: int = 1
    slot: int = 0
    name_id: int = -1
    i0: int = 0
    i1: int = 0
    f0: float = 0.0
    flags: int = 0


def _view(team: list[str], foe: list[str]) -> _engine.BattleState:
    return BattleEnvironment(team, foe, seed=1).observe(0)


def _lines(events: list[FakeEvent], view: _engine.BattleState) -> list[str]:
    return [line.text for line in Teller(0, PLAIN, BOT).narrate(list(events), view, view)]


def _shown(renderable: object) -> str:
    console = Console(file=io.StringIO(), width=100, color_system=None)
    console.print(renderable)
    assert isinstance(console.file, io.StringIO)
    return console.file.getvalue()


def test_the_hp_bar_fills_with_the_hp_left() -> None:
    assert hp_bar(100, 100, width=10) == "██████████"
    assert hp_bar(50, 100, width=10) == "█████░░░░░"
    assert hp_bar(0, 100, width=10) == "░░░░░░░░░░"


def test_the_board_shows_both_actives_with_their_item_level_exact_hp_and_team() -> None:
    view = _view(["Snorlax", "Gyarados"], ["Blissey", "Toxapex"])
    snorlax = view.active(0)
    snorlax.current_hp = 262
    snorlax.status = 4  # paralysis
    snorlax.stat_stages = (2, 0, 0, 0, -1, 0, 0)
    view.set_pokemon(0, 0, snorlax)

    shown = _shown(board(view, 0, PLAIN, BOT))

    misty, *rest = [line.strip() for line in shown.splitlines() if line.strip()]
    assert misty.split() == ["Misty", "Blissey", "◆", "L100", "█" * 24, "714/714", "HP", "●●"]
    assert rest[0].split()[:4] == ["You", "Snorlax", "◆", "L100"]
    assert "262/524 HP" in rest[0] and rest[0].endswith("●●")
    assert rest[1] == "PAR · Atk +2 · Spe -1"


def test_a_pokemon_without_an_item_has_no_mark_and_a_fainted_one_is_a_hollow_dot() -> None:
    view = _view(["Snorlax", "Gyarados"], ["Blissey"])
    snorlax = view.active(0)
    snorlax.item_consumed = True
    view.set_pokemon(0, 0, snorlax)
    gyarados = view.pokemon(0, 1)
    gyarados.current_hp = 0
    view.set_pokemon(0, 1, gyarados)

    you = [line for line in _shown(board(view, 0, PLAIN, BOT)).splitlines() if "You" in line][0]

    assert "◆" not in you
    assert team_dots(view, 0) == "●○"


def test_in_french_the_board_says_pv_and_n() -> None:
    i18n.set_language("fr")

    shown = _shown(board(_view(["Gyarados"], ["Snorlax"]), 0, PLAIN, (None, "Ondine")))

    assert "Ronflex" in shown and "Léviator" in shown and "N.100" in shown and "PV" in shown


def test_the_field_lists_only_what_is_in_play() -> None:
    view = _view(["Gyarados"], ["Snorlax"])
    assert describe_field(view, 0, BOT) == []

    view.weather = 1
    view.terrain = 1
    foe_side = _engine.SideHazards()
    foe_side.stealth_rock = 1
    foe_side.spikes = 2
    view.set_hazards(1, foe_side)
    view.aurora_veil_turns = (3, 0)

    assert describe_field(view, 0, BOT) == [
        "Weather: rain",
        "Terrain: Electric Terrain",
        "Your side: Aurora Veil",
        "Misty's side: Stealth Rock, Spikes ×2",
    ]


def test_a_move_option_reads_its_name_type_and_category_and_its_card_tells_the_rest() -> None:
    view = _view(["Gyarados"], ["Snorlax"])
    slot = list(view.active(0).move_ids).index(_engine.find_move_id("Waterfall"))

    option = move_option(_engine.Action.move(slot), view, 0)

    assert plain_label(option.label).split() == ["Waterfall", "Water", "Physical"]
    assert option.description is not None
    assert "Power 80 · Accuracy 100 % · PP 15/15" in option.description
    assert "20% chance to make it flinch." in " ".join(
        word for word in option.description.split() if word not in "│╭╮╰╯─"
    )


def test_a_status_move_never_misses_and_has_no_power() -> None:
    view = _view(["Gyarados"], ["Snorlax"])
    slot = list(view.active(0).move_ids).index(_engine.find_move_id("DragonDance"))

    option = move_option(_engine.Action.move(slot), view, 0)

    assert option.description is not None
    assert "Power — · Accuracy never misses" in option.description


def test_the_forced_options_say_what_will_really_happen() -> None:
    view = _view(["Gyarados"], ["Snorlax"])
    gyarados = view.active(0)
    gyarados.pp = (0, 0, 0, 0)
    view.set_pokemon(0, 0, gyarados)
    assert plain_label(move_option(_engine.Action.move(0), view, 0).label) == "Struggle"

    gyarados.charging_move_id = _engine.find_move_id("PhantomForce")
    view.set_pokemon(0, 0, gyarados)
    assert plain_label(move_option(_engine.Action.move(0), view, 0).label).startswith(
        "Phantom Force"
    )


def test_the_switch_menu_lists_the_whole_team_and_greys_out_who_cannot_come_in() -> None:
    view = _view(["Gyarados", "Snorlax", "Blissey"], ["Toxapex"])
    blissey = view.pokemon(0, 2)
    blissey.current_hp = 0
    view.set_pokemon(0, 2, blissey)
    snorlax = view.pokemon(0, 1)
    snorlax.status = 4
    view.set_pokemon(0, 1, snorlax)

    active, coming, fainted = switch_options(view, 0, [1])

    assert active.disabled == "in battle" and fainted.disabled == "fainted"
    assert coming.disabled is None and coming.value == 1
    assert plain_label(coming.label).split() == [
        "Snorlax",
        "◆",
        "Normal",
        "█" * 24,
        "524/524",
        "HP",
        "PAR",
    ]
    assert coming.description == species_card("Snorlax")


def test_a_species_card_shows_its_stats_ability_item_and_moves() -> None:
    words = " ".join(
        word for word in species_card("Snorlax").split() if not set(word) <= set("│╭╮╰╯─")
    )

    assert words.startswith("Snorlax Normal HP 160 · Atk 110 · Def 65 · SpA 65 · SpD 110")
    assert "Thick Fat: Halves the damage of Fire and Ice moves." in words
    assert "◆ Leftovers: Restores 1/16 of max HP each turn." in words
    assert "Curse · Ghost · Status Body Slam · Normal · Physical" in words


def test_a_used_up_item_leaves_the_card() -> None:
    assert "Leftovers" not in species_card("Snorlax", holding=False)


def test_a_species_without_ability_or_item_shows_neither() -> None:
    assert species_card("ZoroarkHisui").count(":") == 1  # the item alone
    assert "◆" not in species_card("MegaMawile")


def test_a_card_wraps_its_lines_in_a_frame() -> None:
    assert card("Title", ["one two three"], width=12).splitlines() == [
        "╭─ Title ──╮",
        "│ one two  │",
        "│ three    │",
        "╰──────────╯",
    ]


@pytest.mark.parametrize("kind", list(K))
@pytest.mark.parametrize("language", ["en", "fr"])
def test_every_event_kind_reads_as_a_sentence(kind: K, language: str) -> None:
    i18n.set_language(language)
    view = _view(["Gyarados"], ["Snorlax"])
    if kind in (K.MOVE_USED, K.MISSED, K.MOVE_FAILED, K.CHARGING):
        name_id = _engine.find_move_id("Waterfall")
    elif kind in (K.ABILITY_TRIGGERED, K.ABILITY_DAMAGE):
        name_id = _engine.find_ability_id("Intimidate")
    elif kind in (K.ITEM_TRIGGERED, K.ITEM_CONSUMED, K.ITEM_DAMAGE, K.ITEM_KNOCKED_OFF):
        name_id = _engine.find_item_id("Leftovers")
    else:
        name_id = -1
    event = FakeEvent(kind, name_id=name_id, i0=1, i1=1, f0=1.0)

    (line,) = _lines([event], view)

    assert line[0].isupper()
    assert line.endswith((".", "!"))


def test_damage_never_exceeds_the_hp_left_and_is_followed_through_the_turn() -> None:
    view = _view(["Gyarados"], ["Snorlax"])  # Snorlax has 524 HP
    hits = [
        FakeEvent(K.DAMAGE_DEALT, i0=400, f0=1.0),
        FakeEvent(K.HEALED, i0=50),
        FakeEvent(K.DAMAGE_DEALT, i0=400, f0=1.0),
    ]

    assert _lines(hits, view) == [
        "Snorlax lost 400 HP.",
        "Snorlax restored 50 HP.",
        "Snorlax lost 174 HP.",
    ]


def test_a_foe_never_seen_before_enters_at_full_hp() -> None:
    before = _view(["Gyarados"], ["Snorlax", "Blissey"])  # Blissey, slot 1, is hidden
    # The foe's own view stands in for the view after the entry, where Blissey is visible.
    after = BattleEnvironment(["Gyarados"], ["Snorlax", "Blissey"], seed=1).observe(1)
    hit = FakeEvent(K.HAZARD_DAMAGE, slot=1, i0=89, i1=0)  # Stealth Rock as Blissey enters

    (line,) = Teller(0, PLAIN, BOT).narrate([hit], before, after)

    assert line.text == "Blissey is hurt by Stealth Rock (89 HP)."


def test_damage_tells_crits_and_type_effectiveness() -> None:
    view = _view(["Gyarados"], ["Snorlax"])
    events = [
        FakeEvent(K.DAMAGE_DEALT, i0=10, f0=2.0, flags=_engine.FLAG_CRIT),
        FakeEvent(K.DAMAGE_DEALT, i0=10, f0=0.5),
    ]

    assert _lines(events, view) == [
        "A critical hit! Snorlax lost 10 HP. It's super effective!",
        "Snorlax lost 10 HP. It's not very effective…",
    ]


@pytest.mark.parametrize(
    ("delta", "verb"),
    [(1, "rose"), (2, "rose sharply"), (4, "rose drastically"), (-1, "fell")]
    + [(-2, "harshly fell"), (-3, "severely fell")],
)
def test_stat_changes_say_how_much(delta: int, verb: str) -> None:
    (line,) = _lines(
        [FakeEvent(K.STAT_STAGE_CHANGED, i0=delta, i1=0)], _view(["Gyarados"], ["Snorlax"])
    )

    assert line == f"Snorlax's Attack {verb}!"


def test_in_french_a_turn_reads_in_french() -> None:
    i18n.set_language("fr")
    view = _view(["Gyarados"], ["Snorlax"])
    events = [
        FakeEvent(K.MOVE_USED, side=0, name_id=_engine.find_move_id("Waterfall")),
        FakeEvent(K.DAMAGE_DEALT, i0=58, f0=1.0),
        FakeEvent(K.STAT_STAGE_CHANGED, side=0, i0=1, i1=0),
    ]

    assert _lines(events, view) == [
        "Léviator utilise Cascade !",
        "Ronflex perd 58 PV.",
        "L'Attaque de Léviator augmente !",
    ]


def test_struggle_and_its_recoil_have_names() -> None:
    view = _view(["Gyarados"], ["Snorlax"])
    events = [
        FakeEvent(K.MOVE_USED, name_id=_engine.STRUGGLE),
        FakeEvent(K.ABILITY_DAMAGE, name_id=_engine.STRUGGLE_RECOIL, i0=5),
    ]

    assert _lines(events, view) == [
        "Snorlax used Struggle!",
        "Snorlax is hurt by Struggle's recoil (5 HP).",
    ]


def test_switches_name_the_trainer_or_speak_to_the_player() -> None:
    view = _view(["Gyarados"], ["Snorlax"])
    events = [
        FakeEvent(K.SWITCHED_IN, side=0),
        FakeEvent(K.SWITCHED_IN, side=1),
        FakeEvent(K.SWITCHED_OUT, side=0),
        FakeEvent(K.SWITCHED_OUT, side=1),
        FakeEvent(K.HAZARD_SET, side=1, slot=-1, i0=0),
        FakeEvent(K.HAZARD_SET, side=0, slot=-1, i0=1),
    ]

    assert _lines(events, view) == [
        "Go, Gyarados!",
        "Misty sends out Snorlax!",
        "Gyarados, come back!",
        "Misty withdraws Snorlax!",
        "Stealth Rock now lies around Misty's team.",
        "Spikes now lies around your team.",
    ]


def test_a_fainted_pokemon_leaving_the_field_is_not_told_again() -> None:
    view = _view(["Gyarados"], ["Snorlax"])
    events = [
        FakeEvent(K.DAMAGE_DEALT, i0=600, f0=1.0),
        FakeEvent(K.FAINTED),
        FakeEvent(K.SWITCHED_OUT),
    ]

    assert _lines(events, view) == ["Snorlax lost 524 HP.", "Snorlax fainted!"]


def test_knockouts_crits_and_super_effective_hits_are_worth_a_pause() -> None:
    view = _view(["Gyarados"], ["Snorlax"])
    events = [
        FakeEvent(K.DAMAGE_DEALT, i0=10, f0=1.0),
        FakeEvent(K.DAMAGE_DEALT, i0=10, f0=2.0),
        FakeEvent(K.DAMAGE_DEALT, i0=10, f0=1.0, flags=_engine.FLAG_CRIT),
        FakeEvent(K.FAINTED),
        FakeEvent(K.WEATHER_STARTED, side=-1, slot=-1, i0=1),
    ]

    lines = Teller(0, PLAIN, BOT).narrate(list(events), view, view)

    assert [line.emphasis for line in lines] == [False, True, True, True, False]
    assert [line.amount for line in lines] == [10, 10, 10, 0, 0]


def test_colours_are_markup_only_when_enabled() -> None:
    style = Style(enabled=True)
    assert style.mine("x") == "[cyan]x[/]"
    assert style.theirs("x") == "[red]x[/]"
    assert style.bold("x") == "[bold]x[/]"
    assert style.dim("x") == "[dim]x[/]"
    assert style.hp("x", 0.6) == "[green]x[/]"
    assert style.hp("x", 0.3) == "[yellow]x[/]"
    assert style.hp("x", 0.1) == "[red]x[/]"
    assert style.type_name(1) == "[#F08030]Fire[/]"
    assert PLAIN.mine("x") == "x"
    assert plain("[red]Snorlax[/] fainted!") == "Snorlax fainted!"
