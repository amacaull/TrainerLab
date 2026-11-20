"""The situations of palier 7.5: what a rule can spot, for the judge to say if it is a mistake."""

import pytest

from trainerlab import _engine
from trainerlab._engine import Action
from trainerlab.experiments import situations
from trainerlab.experiments.situations import SITUATIONS, flagged
from trainerlab.game.decisions import Decision
from trainerlab.game.environment import BattleEnvironment


def _decision(
    team: list[str],
    foe: str,
    choice: Action,
    *,
    hp: float = 1.0,
    foe_hp: int | None = None,
    side: int = 0,
    exact_hp: int | None = None,
) -> Decision:
    """A turn built to order: `side`'s first Pokemon at `hp` of its HP (or `exact_hp`) against
    `foe`, alone on the other side."""
    teams = (team, [foe]) if side == 0 else ([foe], team)
    env = BattleEnvironment(*teams, seed=1)
    view = env.observe(side)
    mine = view.pokemon(side, 0)
    mine.current_hp = exact_hp if exact_hp is not None else max(1, int(mine.stats.hp * hp))
    view.set_pokemon(side, 0, mine)
    if foe_hp is not None:
        theirs = view.pokemon(1 - side, 0)
        theirs.current_hp = foe_hp
        view.set_pokemon(1 - side, 0, theirs)
    legal = tuple(env.legal_actions(side))
    return Decision(
        seed=1, a_side=side, side=side, turn=1, kind="action", view=view, legal=legal,
        choice=choice, events=(), winner=None,
    )  # fmt: skip


def _flags(d: Decision) -> set[str]:
    return set(flagged([d], 0))


DRAIN_PUNCH, MACH_PUNCH = Action.move(0), Action.move(1)  # Conkeldurr's set
BODY_SLAM = Action.move(1)  # Snorlax's
SOFT_BOILED, SEISMIC_TOSS = Action.move(0), Action.move(3)  # Blissey's


def test_a_priority_knock_out_left_unplayed_against_a_faster_foe() -> None:
    assert "priority-ko-missed" in _flags(
        _decision(["Conkeldurr"], "Weavile", DRAIN_PUNCH, foe_hp=10)
    )
    assert "priority-ko-missed" not in _flags(
        _decision(["Conkeldurr"], "Weavile", MACH_PUNCH, foe_hp=10)
    )
    # A slower foe: the bot moves first anyway, which ko-missed already covers.
    assert "priority-ko-missed" not in _flags(
        _decision(["Conkeldurr"], "Snorlax", DRAIN_PUNCH, foe_hp=10)
    )


def test_staying_in_against_a_faster_knock_out_and_the_sacrifice_are_told_apart() -> None:
    team = ["Snorlax", "Corviknight"]
    healthy = _flags(_decision(team, "Conkeldurr", BODY_SLAM, hp=0.4))
    weak = _flags(_decision(team, "Conkeldurr", BODY_SLAM, hp=0.2))
    switched = _flags(_decision(team, "Conkeldurr", Action.switch(1), hp=0.4))

    assert "stayed-facing-ko" in healthy and "sacrifice" not in healthy
    assert "sacrifice" in weak and "stayed-facing-ko" not in weak
    assert not {"stayed-facing-ko", "sacrifice"} & switched
    # At full HP, Conkeldurr's best hit does not knock Snorlax out: nothing to spot.
    assert not {"stayed-facing-ko", "sacrifice"} & _flags(_decision(team, "Conkeldurr", BODY_SLAM))


def test_a_heal_at_high_hp_is_spotted_with_who_moves_first() -> None:
    assert "heal-high-hp-slower" in _flags(_decision(["Blissey"], "Weavile", SOFT_BOILED, hp=0.9))
    assert "heal-high-hp-faster" in _flags(_decision(["Blissey"], "Snorlax", SOFT_BOILED, hp=0.9))
    assert not {"heal-high-hp-slower", "heal-high-hp-faster"} & _flags(
        _decision(["Blissey"], "Weavile", SOFT_BOILED, hp=0.5)
    )
    assert not {"heal-high-hp-slower", "heal-high-hp-faster"} & _flags(
        _decision(["Blissey"], "Weavile", SEISMIC_TOSS, hp=0.9)
    )


def test_a_switch_into_a_weakness_to_the_foe_s_best_hit() -> None:
    team = ["Blissey", "Corviknight"]
    assert "switch-into-weakness" in _flags(_decision(team, "Luxray", Action.switch(1)))
    # Conkeldurr's best hit on Corviknight is not super effective.
    assert "switch-into-weakness" not in _flags(_decision(team, "Conkeldurr", Action.switch(1)))
    assert "switch-into-weakness" not in _flags(_decision(team, "Luxray", SEISMIC_TOSS))


def test_the_situations_of_palier_7_2_are_kept_as_they_were() -> None:
    from trainerlab.experiments.audit import STRATEGIC

    for name, check in STRATEGIC.items():
        assert SITUATIONS[name] is check
    assert len(SITUATIONS) == len(STRATEGIC) + 6


def test_only_status_free_healing_moves_count_as_heals() -> None:
    def move(name: str) -> _engine.MoveEntry:
        return _engine.move_entry(_engine.find_move_id(name))

    heals = situations._heals
    assert heals(move("SoftBoiled")) and heals(move("Roost")) and heals(move("Rest"))
    assert heals(move("Wish")) and heals(move("Recover"))
    assert not heals(move("DrainPunch")) and not heals(move("Scald"))
    assert not heals(move("SwordsDance")) and not heals(move("Toxic"))  # status, but no heal


def test_a_replacement_after_a_knock_out_is_no_switch_into_anything() -> None:
    turn = _decision(["Blissey", "Corviknight"], "Luxray", Action.switch(1))
    replacement = Decision(**{**vars(turn), "kind": "replacement", "legal": (1,), "choice": 1})
    assert flagged([replacement], 0) == []


REST = Action.move(2)  # Snorlax's


@pytest.mark.parametrize("side", [0, 1])
def test_every_situation_reads_the_foe_on_the_other_side(side: int) -> None:
    def flags(*args: object, **kwargs: object) -> set[str]:
        return _flags(_decision(*args, side=side, **kwargs))  # type: ignore[arg-type]

    assert "priority-ko-missed" in flags(["Conkeldurr"], "Weavile", DRAIN_PUNCH, foe_hp=10)
    assert "stayed-facing-ko" in flags(["Snorlax", "Corviknight"], "Conkeldurr", BODY_SLAM, hp=0.4)
    assert "heal-high-hp-slower" in flags(["Blissey"], "Weavile", SOFT_BOILED, hp=0.9)
    assert "switch-into-weakness" in flags(["Blissey", "Corviknight"], "Luxray", Action.switch(1))


def test_a_foe_as_fast_does_not_move_first() -> None:
    # Two Blissey: the same speed. A tie is decided by the dice, not "the foe moves first".
    tie = _decision(["Blissey"], "Blissey", SOFT_BOILED, hp=0.9)
    assert "heal-high-hp-faster" in _flags(tie) and "heal-high-hp-slower" not in _flags(tie)


def test_a_quarter_of_the_hp_is_a_sacrifice_and_three_quarters_no_heal_at_high_hp() -> None:
    team = ["Snorlax", "Corviknight"]  # Snorlax has 524 HP: a quarter is 131, three quarters 393
    at_a_quarter = _flags(_decision(team, "Conkeldurr", BODY_SLAM, exact_hp=131))
    assert "sacrifice" in at_a_quarter and "stayed-facing-ko" not in at_a_quarter
    above = _flags(_decision(team, "Conkeldurr", BODY_SLAM, exact_hp=132))
    assert "stayed-facing-ko" in above and "sacrifice" not in above
    assert "heal-high-hp-slower" not in _flags(_decision(team, "Conkeldurr", REST, exact_hp=393))
    assert "heal-high-hp-slower" in _flags(_decision(team, "Conkeldurr", REST, exact_hp=394))


def test_a_switch_in_front_of_a_foe_that_cannot_hurt_it_is_no_weakness() -> None:
    # Blissey's moves do no damage the estimate can count (Seismic Toss is fixed damage).
    d = _decision(["Blissey", "Corviknight"], "Blissey", Action.switch(1))
    assert "switch-into-weakness" not in _flags(d)
