"""Each check must find the mistakes found by hand in the error analysis (ai/analysis/)."""

import pytest

from trainerlab import _engine
from trainerlab._engine import Action
from trainerlab.bots.roster import BOTS
from trainerlab.experiments.audit import CHECKS, audit, main
from trainerlab.game.decisions import Decision, decisions
from trainerlab.game.environment import BattleEnvironment

SOFT_BOILED, SEISMIC_TOSS = Action.move(0), Action.move(3)  # Blissey's set


def _decision(
    foe: str, foe_hp: int, choice: Action, own: str = "Blissey", moves: list[str] | None = None
) -> Decision:
    """A decision of side 0, built to order: its active against a foe left with `foe_hp`."""
    env = BattleEnvironment([own, "Corviknight"], [foe], seed=1)
    view = env.observe(0)
    if moves:
        pokemon = view.pokemon(0, 0)
        pokemon.move_ids = [_engine.find_move_id(move) for move in moves]
        pokemon.pp = [10, 10, 10, 10]
        view.set_pokemon(0, 0, pokemon)
    target = view.pokemon(1, 0)
    target.current_hp = foe_hp
    view.set_pokemon(1, 0, target)
    legal = (Action.move(0), Action.move(1), Action.move(2), Action.move(3), Action.switch(1))
    return Decision(
        seed=1,
        a_side=0,
        side=0,
        turn=1,
        kind="action",
        view=view,
        legal=legal,
        choice=choice,
        events=(),
        winner=None,
    )


def _flagged(
    seed: int, a_side: int, bot_a: str = "survivor", bot_b: str = "matcher"
) -> dict[str, set[int]]:
    """For each check, the turns at which bot A made that mistake in this battle."""
    flagged: dict[str, set[int]] = {name: set() for name in CHECKS}
    for d in decisions(BOTS[bot_a], BOTS[bot_b], seeds=[seed]):
        if d.a_side == a_side and d.by_a:
            for name, check in CHECKS.items():
                if check(d):
                    flagged[name].add(d.turn)
    return flagged


def test_a_fixed_damage_knock_out_left_unplayed_is_found() -> None:
    # 20042-0: Drampa at 77 HP from turn 14 on; Blissey never plays Seismic Toss (100).
    flagged = _flagged(20042, 0)["fixed-damage-ko-missed"]
    assert 14 in flagged
    assert min(flagged) == 14  # before, Drampa has more HP than Seismic Toss takes


def test_seismic_toss_knocks_out_up_to_its_100_hp() -> None:
    missed = CHECKS["fixed-damage-ko-missed"]
    assert missed(_decision("Snorlax", 100, SOFT_BOILED))
    assert not missed(_decision("Snorlax", 101, SOFT_BOILED))


def test_playing_the_knock_out_or_a_move_strong_enough_is_no_mistake() -> None:
    missed = CHECKS["fixed-damage-ko-missed"]
    assert not missed(_decision("Snorlax", 50, SEISMIC_TOSS))
    moves = ["SeismicToss", "BodySlam", "Curse", "Rest"]
    assert not missed(_decision("Snorlax", 50, Action.move(1), own="Snorlax", moves=moves))
    assert missed(_decision("Snorlax", 50, Action.move(2), own="Snorlax", moves=moves))
    assert missed(_decision("Snorlax", 50, Action.switch(1)))


def test_a_ghost_takes_nothing_from_seismic_toss() -> None:
    assert not CHECKS["fixed-damage-ko-missed"](_decision("Dragapult", 50, SOFT_BOILED))


def test_a_choice_is_blind_only_if_no_move_is_estimated_to_deal_damage() -> None:
    blind = CHECKS["blind-choice"]
    assert blind(_decision("Snorlax", 300, SOFT_BOILED))  # Blissey: status moves, fixed damage
    moves = ["Curse", "BodySlam", "Rest", "SleepTalk"]
    assert not blind(_decision("Snorlax", 300, Action.move(0), own="Snorlax", moves=moves))
    assert not blind(_decision("Snorlax", 300, Action.switch(1)))  # a switch is not a move


def test_switching_out_instead_of_a_fixed_damage_knock_out_is_found() -> None:
    # Rule 2 sends Blissey away from a Luxray at 97 HP that Seismic Toss would finish.
    assert 14 in _flagged(27, 0, "dodger", "random")["fixed-damage-ko-missed"]


def test_a_move_that_fails_is_found() -> None:
    assert 13 in _flagged(20042, 0)["failed"]  # Fake Out a second turn in a row
    assert 12 not in _flagged(20042, 0)["failed"]  # the first Fake Out worked
    assert 18 in _flagged(20048, 0)["failed"]  # Stealth Rock laid a second time
    assert 24 in _flagged(20013, 1)["failed"]  # Poltergeist on a foe holding no item
    assert 18 in _flagged(20007, 0)["failed"]  # Will-O-Wisp on a Toxapex already burned
    assert 23 not in _flagged(20013, 1)["failed"]  # the foe's First Impression failed, not A's


def test_a_move_into_an_immunity_is_found() -> None:
    assert 15 in _flagged(20048, 0)["no-effect"]  # Superpower into a Ghost type
    assert 7 in _flagged(20049, 0)["no-effect"]  # Earthquake into Levitate
    assert 6 not in _flagged(20049, 0)["no-effect"]
    assert 4 not in _flagged(20002, 0)["no-effect"]  # Disguise takes a hit: no immunity


def test_a_choice_made_with_every_move_at_zero_is_found() -> None:
    assert 17 in _flagged(20048, 0)["blind-choice"]  # Ting-Lu against Mega Rayquaza


def test_no_seed_gives_an_empty_audit() -> None:
    result = audit(BOTS["survivor"], BOTS["matcher"], seeds=[])
    assert result.decisions == 0
    assert result.battles == {"win": 0, "draw": 0, "loss": 0}


def test_audit_counts_each_check_by_outcome() -> None:
    seeds = range(20042, 20044)
    result = audit(BOTS["survivor"], BOTS["matcher"], seeds=seeds)
    assert result.battles == {"win": 2, "draw": 0, "loss": 2}
    mine = [d for d in decisions(BOTS["survivor"], BOTS["matcher"], seeds=seeds) if d.by_a]
    assert result.decisions == len(mine)  # bot A's decisions only
    fixed = result.checks["fixed-damage-ko-missed"]
    assert fixed.occurrences >= 1
    assert fixed.battles["loss"] >= 1  # 20042-0 is a loss
    assert set(result.checks) == set(CHECKS)


def test_the_report_gives_a_rate_and_the_share_of_won_and_lost_battles() -> None:
    out: list[str] = []
    argv = ["survivor", "matcher", "--pairs", "2", "--from", "20042"]
    assert main(argv, write=out.append) == 0
    lines = "".join(out).splitlines()
    assert lines[0] == (
        "survivor against matcher, seeds 20042-20043: 4 battles (2 won, 0 drawn, 2 lost), "
        f"{audit(BOTS['survivor'], BOTS['matcher'], seeds=range(20042, 20044)).decisions} "
        "decisions by survivor."
    )
    assert lines[2].split() == ["check", "per", "1000", "decisions", "in", "lost", "in", "won"]
    assert {line.split()[0] for line in lines[3:]} == set(CHECKS)
    result = audit(BOTS["survivor"], BOTS["matcher"], seeds=range(20042, 20044))
    fixed = next(line for line in lines if line.startswith("fixed-damage-ko-missed"))
    rate = 1000 * result.checks["fixed-damage-ko-missed"].occurrences / result.decisions
    assert fixed.split()[1] == f"{rate:.1f}"


def test_a_share_is_left_blank_when_there_is_no_battle_to_divide_by() -> None:
    out: list[str] = []
    assert main(["hitter", "random", "--pairs", "1"], write=out.append) == 0
    # Rule 1 never loses these two battles: the "in lost" column has nothing to divide by.
    assert all(line.split()[-2] == "-" for line in "".join(out).splitlines()[3:])


def test_by_default_the_report_goes_to_the_terminal(capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["hitter", "random", "--pairs", "1"]) == 0
    assert capsys.readouterr().out.startswith("hitter against random, seeds 0-0")
