from dataclasses import dataclass

import pytest

from trainerlab import _engine
from trainerlab.environment import BattleEnvironment
from trainerlab.render import (
    Style,
    describe_action,
    describe_board,
    describe_events,
    describe_field,
    describe_pokemon,
    hp_bar,
    readable,
)

PLAIN = Style(enabled=False)


@dataclass(frozen=True)
class FakeEvent:
    """An engine event built by hand, to reach every event kind."""

    kind: int
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
    return describe_events(list(events), view, view, 0, PLAIN)


def test_names_are_split_into_words() -> None:
    assert readable("DragonDarts") == "Dragon Darts"
    assert readable("Leftovers") == "Leftovers"


def test_the_hp_bar_fills_with_the_hp_left() -> None:
    assert hp_bar(100, 100, width=10) == "[##########]"
    assert hp_bar(50, 100, width=10) == "[#####.....]"
    assert hp_bar(0, 100, width=10) == "[..........]"


def test_a_pokemon_line_shows_hp_status_and_stat_stages() -> None:
    snorlax = _view(["Snorlax"], ["Blissey"]).active(0)
    snorlax.current_hp = 262
    snorlax.status = 4  # paralysis
    snorlax.stat_stages = (2, 0, 0, 0, -1, 0, 0)

    assert describe_pokemon(snorlax, PLAIN) == (
        "Snorlax  [##########..........] 262/524  PAR  +2 Atk, -1 Spe"
    )


def test_the_board_shows_both_actives_the_bench_and_what_is_unseen() -> None:
    lines = describe_board(_view(["Snorlax", "Toxapex"], ["Blissey", "Gyarados"]), 0, PLAIN)

    assert lines[0] == "--- Turn 1 ---"
    assert lines[1].startswith("Foe:  Blissey")
    assert "1 seen standing, 1 not yet seen" in lines[2]
    assert lines[3].startswith("You:  Snorlax")
    assert lines[4].strip() == "bench: Toxapex 304/304"


def test_a_lone_pokemon_has_no_bench() -> None:
    lines = describe_board(_view(["Snorlax"], ["Blissey"]), 0, PLAIN)

    assert lines[4].strip() == "bench: none"


def test_the_field_lists_only_what_is_in_play() -> None:
    view = _view(["Snorlax"], ["Blissey"])
    assert describe_field(view, 0) == []

    view.weather = 1
    view.terrain = 1
    foe_side = _engine.SideHazards()
    foe_side.stealth_rock = 1
    foe_side.spikes = 2
    view.set_hazards(1, foe_side)
    view.aurora_veil_turns = (5, 0)

    assert describe_field(view, 0) == [
        "Weather: rain",
        "Terrain: Electric Terrain",
        "On your team's side: Aurora Veil",
        "On the foe's team's side: Stealth Rock, Spikes x2",
    ]


def test_a_move_option_shows_its_type_power_and_pp() -> None:
    view = _view(["Gyarados"], ["Snorlax"])

    assert describe_action(_engine.Action.move(1), view, 0) == (
        "Waterfall (Water, 80 power, 15/15 PP)"
    )
    assert describe_action(_engine.Action.move(0), view, 0) == (
        "Dragon Dance (Dragon, status, 20/20 PP)"
    )


def test_a_switch_option_shows_who_enters_and_their_hp() -> None:
    view = _view(["Gyarados", "Snorlax"], ["Blissey"])

    assert describe_action(_engine.Action.switch(1), view, 0) == "Switch to Snorlax (524/524 HP)"


def test_the_forced_options_say_what_will_really_happen() -> None:
    view = _view(["Gyarados"], ["Snorlax"])
    gyarados = view.active(0)
    gyarados.pp = (0, 0, 0, 0)
    view.set_pokemon(0, 0, gyarados)
    assert describe_action(_engine.Action.move(0), view, 0) == "Struggle (no PP left)"

    gyarados.charging_move_id = _engine.find_move_id("PhantomForce")
    view.set_pokemon(0, 0, gyarados)
    assert describe_action(_engine.Action.move(0), view, 0) == "Unleash Phantom Force"


@pytest.mark.parametrize("kind", range(36))
def test_every_event_kind_reads_as_a_sentence(kind: int) -> None:
    view = _view(["Gyarados"], ["Snorlax"])
    names = {0: "Waterfall", 3: "Waterfall", 14: "Waterfall", 24: "Waterfall"}
    if kind in names:
        name_id = _engine.find_move_id(names[kind])
    elif kind in (13, 35):
        name_id = _engine.find_ability_id("Intimidate")
    elif kind in (26, 27, 28, 29):
        name_id = _engine.find_item_id("Leftovers")
    else:
        name_id = -1
    event = FakeEvent(kind, name_id=name_id, i0=1, i1=1, f0=1.0)

    (line,) = _lines([event], view)

    assert line[0].isupper()
    assert line.endswith((".", "!"))


def test_damage_never_exceeds_the_hp_left_and_is_followed_through_the_turn() -> None:
    view = _view(["Gyarados"], ["Snorlax"])  # Snorlax has 524 HP
    hits = [FakeEvent(1, i0=400, f0=1.0), FakeEvent(22, i0=50), FakeEvent(1, i0=400, f0=1.0)]

    assert _lines(hits, view) == [
        "The foe's Snorlax lost 400 HP.",
        "The foe's Snorlax restored 50 HP.",
        "The foe's Snorlax lost 174 HP.",
    ]


def test_a_foe_never_seen_before_enters_at_full_hp() -> None:
    before = _view(["Gyarados"], ["Snorlax", "Blissey"])  # Blissey, slot 1, is hidden
    # The foe's own view stands in for the view after the entry, where Blissey is visible.
    after = BattleEnvironment(["Gyarados"], ["Snorlax", "Blissey"], seed=1).observe(1)
    hit = FakeEvent(19, slot=1, i0=89, i1=0)  # Stealth Rock as Blissey enters

    assert describe_events([hit], before, after, 0, PLAIN) == [
        "The foe's Blissey is hurt by Stealth Rock (89 HP)."
    ]


def test_damage_tells_crits_and_type_effectiveness() -> None:
    view = _view(["Gyarados"], ["Snorlax"])
    events = [
        FakeEvent(1, i0=10, f0=2.0, flags=_engine.FLAG_CRIT),
        FakeEvent(1, i0=10, f0=0.5),
    ]

    assert _lines(events, view) == [
        "A critical hit! The foe's Snorlax lost 10 HP. It's super effective!",
        "The foe's Snorlax lost 10 HP. It's not very effective...",
    ]


@pytest.mark.parametrize(
    ("delta", "verb"),
    [(1, "rose"), (2, "rose sharply"), (3, "rose drastically"), (-1, "fell"), (-2, "harshly fell")]
    + [(-3, "severely fell")],
)
def test_stat_changes_say_how_much(delta: int, verb: str) -> None:
    (line,) = _lines([FakeEvent(9, i0=delta, i1=0)], _view(["Gyarados"], ["Snorlax"]))

    assert line == f"The foe's Snorlax's Attack {verb}!"


def test_struggle_and_its_recoil_have_names() -> None:
    view = _view(["Gyarados"], ["Snorlax"])
    events = [FakeEvent(0, name_id=-2), FakeEvent(35, name_id=-3, i0=5)]

    assert _lines(events, view) == [
        "The foe's Snorlax used Struggle!",
        "The foe's Snorlax is hurt by struggle recoil (5 HP).",
    ]


def test_events_say_whose_pokemon_it_is() -> None:
    view = _view(["Gyarados"], ["Snorlax"])
    events = [FakeEvent(2, side=0), FakeEvent(2, side=1), FakeEvent(18, side=1, slot=-1, i0=0)]

    assert _lines(events, view) == [
        "Your Gyarados fainted!",
        "The foe's Snorlax fainted!",
        "Stealth Rock now lies around the foe's team.",
    ]


def test_coloured_lines_keep_their_capital_letter() -> None:
    view = _view(["Gyarados"], ["Snorlax"])

    (line,) = describe_events([FakeEvent(2)], view, view, 0, Style(enabled=True))

    assert line == "\033[31mThe foe's Snorlax\033[0m fainted!"


def test_colours_are_ansi_codes_only_when_enabled() -> None:
    assert Style(enabled=True).mine("x") == "\033[32mx\033[0m"
    assert Style(enabled=True).theirs("x") == "\033[31mx\033[0m"
    assert Style(enabled=True).bold("x") == "\033[1mx\033[0m"
    assert Style(enabled=True).dim("x") == "\033[2mx\033[0m"
    assert Style(enabled=False).mine("x") == "x"
