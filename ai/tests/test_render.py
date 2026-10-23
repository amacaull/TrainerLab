from dataclasses import dataclass

import pytest

from trainerlab import _engine
from trainerlab.environment import BattleEnvironment
from trainerlab.render import (
    Style,
    describe_board,
    describe_effects,
    describe_events,
    describe_field,
    describe_move,
    describe_moves,
    describe_pokemon,
    describe_switch,
    hp_bar,
    narrate,
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
    assert readable("UTurn") == "U-turn"
    assert readable("WillOWisp") == "Will-O-Wisp"


def test_the_hp_bar_fills_with_the_hp_left() -> None:
    assert hp_bar(100, 100, width=10) == "[##########]"
    assert hp_bar(50, 100, width=10) == "[#####.....]"
    assert hp_bar(0, 100, width=10) == "[..........]"


def test_a_pokemon_shows_its_types_status_stat_stages_and_hp() -> None:
    snorlax = _view(["Snorlax"], ["Blissey"]).active(0)
    snorlax.current_hp = 262
    snorlax.status = 4  # paralysis
    snorlax.stat_stages = (2, 0, 0, 0, -1, 0, 0)

    assert describe_pokemon(snorlax, PLAIN) == [
        "Snorlax  Normal  PAR  +2 Atk, -1 Spe",
        "[##########..........] 262/524 HP",
    ]


def test_a_dual_type_shows_both_types() -> None:
    (line, _) = describe_pokemon(_view(["TapuKoko"], ["Blissey"]).active(0), PLAIN)

    assert line == "Tapu Koko  Electric/Fairy"


@pytest.mark.parametrize(
    ("hp", "colour"), [(400, "32"), (263, "32"), (262, "33"), (105, "33"), (104, "31")]
)
def test_the_hp_bar_turns_yellow_then_red(hp: int, colour: str) -> None:
    # Snorlax has 524 HP: green above half (262), yellow above a fifth (104.8), red below.
    snorlax = _view(["Snorlax"], ["Blissey"]).active(0)
    snorlax.current_hp = hp

    (_, bar) = describe_pokemon(snorlax, Style(enabled=True))

    assert bar.startswith(f"\033[{colour}m[")


def test_the_board_shows_both_actives_the_bench_and_what_is_unseen() -> None:
    lines = describe_board(_view(["Snorlax", "Toxapex"], ["Blissey", "Gyarados"]), 0, PLAIN)

    assert lines == [
        "Foe  Blissey  Normal",
        "     [####################] 714/714 HP",
        "     1 seen standing, 1 not yet seen",
        "",
        "You  Snorlax  Normal",
        "     [####################] 524/524 HP",
        "     bench: Toxapex 304/304",
    ]


def test_a_lone_pokemon_has_no_bench() -> None:
    lines = describe_board(_view(["Snorlax"], ["Blissey"]), 0, PLAIN)

    assert lines[-1].strip() == "bench: none"


def test_the_field_comes_last_on_the_board() -> None:
    view = _view(["Snorlax"], ["Blissey"])
    view.weather = 1

    assert describe_board(view, 0, PLAIN)[-2:] == ["", "Weather: rain"]


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

    assert describe_move(_engine.Action.move(1), view, 0) == (
        "Waterfall (Water, 80 power, 15/15 PP)"
    )
    assert describe_move(_engine.Action.move(0), view, 0) == (
        "Dragon Dance (Dragon, status, 20/20 PP)"
    )


def test_a_fixed_damage_move_is_not_called_a_status_move() -> None:
    # Blissey's Seismic Toss has no power in the catalog, yet deals damage.
    assert describe_move(_engine.Action.move(3), _view(["Blissey"], ["Snorlax"]), 0) == (
        "Seismic Toss (Fighting, fixed damage, 20/20 PP)"
    )


@pytest.mark.parametrize(
    ("attacker", "slot", "defender", "tag"),
    [
        ("Excadrill", 0, "Gyarados", "  x0"),  # Earthquake on a Flying type
        ("Gyarados", 1, "Excadrill", "  x2"),  # Waterfall on Ground/Steel
        ("Gyarados", 1, "Snorlax", ""),  # neutral: no tag
        ("Gyarados", 0, "Snorlax", ""),  # Dragon Dance, a status move: no tag
    ],
)
def test_hints_show_the_type_effectiveness_of_damaging_moves(
    attacker: str, slot: int, defender: str, tag: str
) -> None:
    view = _view([attacker], [defender])
    plain = describe_move(_engine.Action.move(slot), view, 0)

    assert describe_move(_engine.Action.move(slot), view, 0, hints=True) == plain + tag


def test_the_help_details_every_move() -> None:
    lines = describe_moves(_view(["Infernape"], ["Snorlax"]), 0)

    assert lines[:2] == [
        "Close Combat: Fighting, physical, 120 power, 100% accuracy, 5/5 PP",
        "  Lower the user's Defense by 1 stage, lower the user's Sp. Def by 1 stage.",
    ]
    assert sum(not line.startswith("  ") for line in lines) == 4


def test_the_help_tells_priority_and_moves_that_never_miss() -> None:
    hoopa = describe_moves(_view(["HoopaUnbound"], ["Snorlax"]), 0)
    mawile = describe_moves(_view(["MegaMawile"], ["Snorlax"]), 0)

    assert "Aura Sphere: Fighting, special, 80 power, never misses, 20/20 PP" in hoopa
    assert "Sucker Punch: Dark, physical, 70 power, 100% accuracy, priority +1, 5/5 PP" in mawile


def test_a_move_whose_power_depends_on_weight_says_so() -> None:
    view = _view(["TapuKoko"], ["Snorlax"])

    assert describe_move(_engine.Action.move(3), view, 0) == (
        "Grass Knot (Grass, power from weight, 20/20 PP)"
    )


@pytest.mark.parametrize(
    ("attacker", "slot", "defender", "tag"),
    [
        ("Blissey", 3, "Snorlax", ""),  # Seismic Toss: fixed damage, Fighting x2 does not apply
        ("Blissey", 3, "Dragapult", "  x0"),  # but a Ghost is still immune
        ("TapuKoko", 3, "Starmie", "  x2"),  # Grass Knot: weight-based, types still count
    ],
)
def test_hints_respect_how_a_move_deals_its_damage(
    attacker: str, slot: int, defender: str, tag: str
) -> None:
    view = _view([attacker], [defender])
    plain = describe_move(_engine.Action.move(slot), view, 0)

    assert describe_move(_engine.Action.move(slot), view, 0, hints=True) == plain + tag


@pytest.mark.parametrize(
    ("move", "text"),
    [
        ("BodySlam", "30% chance to paralyze the target."),
        ("FlareBlitz", "Take 1/3 of the damage dealt as recoil, 10% chance to burn the target."),
        ("SwordsDance", "Raise the user's Attack by 2 stages."),
        ("Crunch", "20% chance to lower the target's Defense by 1 stage."),
        ("BanefulBunker", "Protect the user this turn and poison an attacker making contact."),
        ("SeismicToss", "Deal damage equal to the user's level."),
        ("Ruination", "Halve the target's HP."),
        ("Defog", "Lower the target's evasiveness by 1 stage, clear hazards on both sides."),
        ("RapidSpin", "Clear hazards, raise the user's Speed by 1 stage."),
        ("StealthRock", "Set Stealth Rock around the foe."),
        ("Earthquake", ""),
    ],
)
def test_move_effects_read_as_a_sentence(move: str, text: str) -> None:
    _view(["Snorlax"], ["Blissey"])  # initialises the engine

    assert describe_effects(_engine.move_entry(_engine.find_move_id(move))) == text


def test_every_move_of_the_catalog_can_be_described() -> None:
    _view(["Snorlax"], ["Blissey"])

    for move_id in range(_engine.move_count()):
        describe_effects(_engine.move_entry(move_id))  # a new effect kind would raise here


def test_a_switch_option_shows_who_comes_in_with_types_and_hp() -> None:
    view = _view(["Gyarados", "TapuKoko"], ["Blissey"])

    assert describe_switch(1, view, 0) == "Tapu Koko (Electric/Fairy, 281/281 HP)"


def test_a_switch_option_shows_a_status() -> None:
    view = _view(["Gyarados", "Snorlax"], ["Blissey"])
    snorlax = view.pokemon(0, 1)
    snorlax.status = 5  # sleep
    view.set_pokemon(0, 1, snorlax)

    assert describe_switch(1, view, 0) == "Snorlax (Normal, 524/524 HP, SLP)"


@pytest.mark.parametrize(
    ("candidate", "foe", "remark"),
    [
        ("Snorlax", "Infernape", "  weak to Fighting"),  # Fire/Fighting: Fire is neutral
        ("Toxapex", "Infernape", "  resists Fire, resists Fighting"),
        ("Dragapult", "Snorlax", "  immune to Normal"),
        ("Gyarados", "TapuKoko", "  weak to Electric"),
        ("Snorlax", "Blissey", ""),  # neutral: nothing to say
    ],
)
def test_hints_tell_how_a_switch_in_takes_the_foes_types(
    candidate: str, foe: str, remark: str
) -> None:
    view = _view(["Blissey", candidate], [foe])

    with_hints = describe_switch(1, view, 0, hints=True)

    assert with_hints == describe_switch(1, view, 0) + remark


def test_the_forced_options_say_what_will_really_happen() -> None:
    view = _view(["Gyarados"], ["Snorlax"])
    gyarados = view.active(0)
    gyarados.pp = (0, 0, 0, 0)
    view.set_pokemon(0, 0, gyarados)
    assert describe_move(_engine.Action.move(0), view, 0) == "Struggle (no PP left)"

    gyarados.charging_move_id = _engine.find_move_id("PhantomForce")
    view.set_pokemon(0, 0, gyarados)
    assert describe_move(_engine.Action.move(0), view, 0) == "Unleash Phantom Force"


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


def test_a_fainted_pokemon_leaving_the_field_is_not_told_again() -> None:
    view = _view(["Gyarados"], ["Snorlax"])
    events = [FakeEvent(1, i0=600, f0=1.0), FakeEvent(2), FakeEvent(11), FakeEvent(11, side=0)]

    assert _lines(events, view) == [
        "The foe's Snorlax lost 524 HP.",
        "The foe's Snorlax fainted!",
        "Your Gyarados withdrew.",
    ]


def test_knockouts_crits_and_super_effective_hits_are_worth_a_pause() -> None:
    view = _view(["Gyarados"], ["Snorlax"])
    events = [
        FakeEvent(1, i0=10, f0=1.0),
        FakeEvent(1, i0=10, f0=2.0),
        FakeEvent(1, i0=10, f0=1.0, flags=_engine.FLAG_CRIT),
        FakeEvent(2),
        FakeEvent(15, side=-1, slot=-1, i0=1),
    ]

    lines = narrate(list(events), view, view, 0, PLAIN)

    assert [line.emphasis for line in lines] == [False, True, True, True, False]
    assert [line.amount for line in lines] == [10, 10, 10, 0, 0]


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
