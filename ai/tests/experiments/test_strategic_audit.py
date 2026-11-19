"""The strategic checks (palier 7.2) find the mistakes seen by reading battles, in real battles."""

import pytest

from trainerlab import _engine
from trainerlab.bots.roster import factory
from trainerlab.experiments import audit as module
from trainerlab.experiments.audit import STRATEGIC, audit, audit_on_cores
from trainerlab.game.decisions import Decision, decisions


def _battle(seed: int, a_side: int) -> list[Decision]:
    """One battle of forecaster (bot A) against strategist, every decision in order."""
    found = decisions(factory("forecaster"), factory("strategist"), seeds=[seed])
    return [d for d in found if d.a_side == a_side]


def _turns(battle: list[Decision], check: str) -> set[int]:
    """The turns at which bot A made this mistake."""
    return {d.turn for i, d in enumerate(battle) if d.by_a and STRATEGIC[check](battle, i)}


@pytest.fixture(scope="module")
def koko_and_excadrill() -> list[Decision]:
    return _battle(20000, 0)


def test_a_sure_knock_out_left_unplayed_is_found(koko_and_excadrill: list[Decision]) -> None:
    # Turn 1: Tapu Koko, faster, plays Thunder (70 % accurate) where Volt Switch surely KOs.
    # Turn 14: Excadrill, locked into Iron Head, switches out instead of finishing Weezing.
    assert {1, 14} <= _turns(koko_and_excadrill, "ko-missed")


def test_a_boost_that_comes_to_nothing_is_found() -> None:
    # Turn 11: Mega Sceptile, at 90 HP, Nasty Plots in front of Marowak-Alola and falls.
    battle = _battle(20001, 0)
    assert 11 in _turns(battle, "boost")
    assert 11 in _turns(battle, "boost-wasted")
    assert 11 in _turns(battle, "boost-into-ko")


def test_a_boost_followed_by_a_hit_is_no_waste() -> None:
    # Turn 1: Zoroark-Hisui Nasty Plots, then hits Gyarados for 304 before it falls.
    battle = _battle(20003, 0)
    assert 0 in _turns(battle, "boost")
    assert 0 not in _turns(battle, "boost-wasted")


def test_boosting_again_and_again_without_a_hit_is_waste() -> None:
    # 20008-1: a boost at turn 22 pays off; the boosts of turns 24 to 26, one after the other,
    # never lead to a hit.
    battle = _battle(20008, 1)
    assert {22, 24, 25, 26} <= _turns(battle, "boost")
    wasted = _turns(battle, "boost-wasted")
    assert 22 not in wasted and {24, 25, 26} <= wasted


def test_a_boost_in_front_of_a_phazer_is_found() -> None:
    # Turns 15 and 18: Azelf Nasty Plots in front of Ting-Lu, who has Whirlwind.
    assert {15, 18} <= _turns(_battle(20017, 1), "boost-into-phazer")


def test_falling_to_destiny_bond_is_found_and_binding_the_foe_is_not() -> None:
    # Turn 22: forecaster's Conkeldurr knocks Mega Banette out and falls with it.
    assert 22 in _turns(_battle(20005, 0), "destiny-bond-trade")
    # 20001-0, turn 17: forecaster's own Mega Banette binds Zoroark: no mistake of its own.
    assert 17 not in _turns(_battle(20001, 0), "destiny-bond-trade")


def test_only_status_moves_that_raise_their_user_count_as_boosts() -> None:
    def move(name: str) -> _engine.MoveEntry:
        return _engine.move_entry(_engine.find_move_id(name))

    raises = module._raises_own_stats
    assert raises(move("SwordsDance")) and raises(move("BellyDrum")) and raises(move("TidyUp"))
    assert not raises(move("CloseCombat"))  # lowers its user, and attacks
    assert not raises(move("Defog")) and not raises(move("Whirlwind"))


def test_a_boost_still_in_play_when_its_side_wins_is_no_waste() -> None:
    battle = _battle(20001, 0)
    last = max(i for i, d in enumerate(battle) if d.by_a)
    boost = next(i for i, d in enumerate(battle) if d.by_a and STRATEGIC["boost"](battle, i))
    tail = [battle[boost]] + [d for d in battle[last + 1 :]]  # nothing after the boost
    won = [Decision(**{**vars(tail[0]), "winner": tail[0].side})]
    lost = [Decision(**{**vars(tail[0]), "winner": 1 - tail[0].side})]
    assert not STRATEGIC["boost-wasted"](won, 0)
    assert STRATEGIC["boost-wasted"](lost, 0)


def test_the_counts_are_the_same_on_one_core_or_several() -> None:
    seeds = range(20042, 20046)
    alone = audit(factory("survivor"), factory("matcher"), seeds=seeds)
    assert audit_on_cores("survivor", "matcher", seeds, jobs=2) == alone
    assert audit_on_cores("survivor", "matcher", seeds, jobs=1) == alone
