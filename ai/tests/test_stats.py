from trainerlab.environment import BattleEnvironment
from trainerlab.render import Narration
from trainerlab.stats import BattleStats

MOVE_USED, DAMAGE, FAINTED, HEALED = 0, 1, 2, 22


def _event(kind: int, side: int, slot: int = 0, amount: int = 0) -> Narration:
    return Narration("", False, kind, side, slot, amount)


def test_damage_and_knockouts_go_to_the_pokemon_that_attacked() -> None:
    stats = BattleStats()

    stats.record(
        [
            _event(MOVE_USED, 1),
            _event(DAMAGE, 0, amount=50),
            _event(MOVE_USED, 0),
            _event(DAMAGE, 1, amount=200),
            _event(FAINTED, 1),
        ]
    )

    assert stats.damage == {(1, 0): 50, (0, 0): 200}
    assert stats.knockouts == {(0, 0): 1}
    assert stats.fainted == [0, 1]


def test_recoil_and_self_damage_are_not_credited_as_dealt() -> None:
    stats = BattleStats()

    stats.record([_event(MOVE_USED, 0), _event(DAMAGE, 0, amount=40), _event(FAINTED, 0)])

    assert stats.damage == {}
    assert stats.knockouts == {}
    assert stats.fainted == [1, 0]


def test_a_knockout_before_any_move_is_counted_but_credited_to_no_one() -> None:
    stats = BattleStats()

    stats.record([_event(DAMAGE, 1, amount=30), _event(FAINTED, 1)])

    assert stats.fainted == [0, 1]
    assert stats.knockouts == {}


def test_the_summary_names_the_most_decisive_pokemon() -> None:
    view = BattleEnvironment(["Snorlax", "Gyarados"], ["Blissey"], seed=1).observe(0)
    stats = BattleStats()
    stats.record(
        [
            _event(MOVE_USED, 0, slot=0),
            _event(DAMAGE, 1, amount=500),
            _event(MOVE_USED, 0, slot=1),
            _event(DAMAGE, 1, amount=100),
            _event(FAINTED, 1),
        ]
    )

    assert stats.summary(view, 0, turns=7) == [
        "Turns played: 7",
        "Pokemon knocked out: 1 of theirs, 0 of yours",
        "Your MVP: Gyarados (1 KO, 100 HP dealt)",
    ]


def test_without_damage_dealt_there_is_no_mvp() -> None:
    view = BattleEnvironment(["Snorlax"], ["Blissey"], seed=1).observe(0)

    assert len(BattleStats().summary(view, 0, turns=0)) == 2
