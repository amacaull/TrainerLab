"""The mistakes of a bot that a program can spot on its own, counted over many battles.

The error analysis classified losses by hand (ai/analysis/). Some of its mistakes leave a trace
that needs no judgement: a move that fails, a move into an immunity, a fixed-damage knock-out
left unplayed, a choice made while every move is estimated at zero. This counts them over many
battles, to check that what the hand analysis found on a few losses holds at scale, and whether
each mistake comes with losing.
"""

from collections.abc import Callable, Iterable
from dataclasses import dataclass, field

from trainerlab import _engine
from trainerlab.game import estimate
from trainerlab.game.decisions import Decision, decisions
from trainerlab.game.pairs import BotFactory, Outcome, outcome_of

MOVE_USED, DAMAGE, ABILITY_TRIGGERED, STATUS_FAILED, MOVE_FAILED = 0, 1, 13, 5, 14
LEVEL = 100  # every Pokemon is level 100: Seismic Toss takes 100 HP
# The abilities that make a Pokemon immune to a move. Others also trigger as a move lands
# (Disguise takes the hit, Magic Bounce sends it back): those are not an immunity the bot missed.
IMMUNITIES = {"Levitate", "VoltAbsorb", "LightningRod", "FlashFire", "Bulletproof"}


def _own_move_followed_by(d: Decision) -> list[_engine.Event]:
    """The event right after each move the deciding side used this turn."""
    after = []
    for event, following in zip(d.events, d.events[1:], strict=False):
        if event.kind == MOVE_USED and event.side == d.side:
            after.append(following)
    return after


def _no_effect(d: Decision) -> bool:
    """A move into an immunity: of type (no damage at all) or of ability (Levitate...)."""
    return any(
        e.side != d.side
        and (
            (e.kind == DAMAGE and e.f0 == 0)
            or (e.kind == ABILITY_TRIGGERED and _engine.ability_name(e.name_id) in IMMUNITIES)
        )
        for e in _own_move_followed_by(d)
    )


def _failed(d: Decision) -> bool:
    """A move that failed outright: Fake Out after the first turn, hazards already laid..."""
    return any(
        e.kind == MOVE_FAILED or (e.kind == STATUS_FAILED and e.side != d.side)
        for e in _own_move_followed_by(d)
    )


def _fixed_damage_ko_missed(d: Decision) -> bool:
    """A level-damage move would have knocked the foe out, and the bot played something weaker."""
    if d.kind != "action":
        return False
    active, foe = d.view.active(d.side), d.view.active(1 - d.side)
    moves = [a for a in d.legal if isinstance(a, _engine.Action) and a.kind == _engine.ACTION_MOVE]
    knock_outs = [a for a in moves if _level_damage(active, foe, a.index) >= foe.current_hp]
    if not knock_outs or d.choice in knock_outs:
        return False
    choice = d.choice
    assert isinstance(choice, _engine.Action)
    if choice.kind != _engine.ACTION_MOVE:
        return True
    return estimate.estimate_damage(active, foe, choice.index) < foe.current_hp


def _level_damage(attacker: _engine.BattlePokemon, foe: _engine.BattlePokemon, slot: int) -> int:
    move = _engine.move_entry(attacker.move_ids[slot])
    level_based = any(effect.damage_equals_level for effect in move.effects)
    if not level_based or _engine.type_effectiveness(move.type, foe.species_id) == 0:
        return 0
    return LEVEL


def _blind_choice(d: Decision) -> bool:
    """The bot played a move while every one of its moves was estimated at zero damage."""
    if not isinstance(d.choice, _engine.Action) or d.choice.kind != _engine.ACTION_MOVE:
        return False
    active, foe = d.view.active(d.side), d.view.active(1 - d.side)
    moves = [a for a in d.legal if isinstance(a, _engine.Action) and a.kind == _engine.ACTION_MOVE]
    return all(estimate.estimate_damage(active, foe, a.index) == 0 for a in moves)


CHECKS: dict[str, Callable[[Decision], bool]] = {
    "fixed-damage-ko-missed": _fixed_damage_ko_missed,
    "failed": _failed,
    "no-effect": _no_effect,
    "blind-choice": _blind_choice,
}


def _no_battles() -> dict[Outcome, int]:
    return {"win": 0, "draw": 0, "loss": 0}


@dataclass
class CheckCount:
    occurrences: int = 0
    battles: dict[Outcome, int] = field(default_factory=_no_battles)  # with at least one


@dataclass
class AuditResult:
    decisions: int = 0  # made by bot A
    battles: dict[Outcome, int] = field(default_factory=_no_battles)
    checks: dict[str, CheckCount] = field(default_factory=dict)


def audit(make_a: BotFactory, make_b: BotFactory, *, seeds: Iterable[int]) -> AuditResult:
    """Runs every check on bot A's decisions over the pairs of these seeds."""
    result = AuditResult(checks={name: CheckCount() for name in CHECKS})
    battle: list[Decision] = []
    for d in decisions(make_a, make_b, seeds=seeds):
        if battle and (d.seed, d.a_side) != (battle[0].seed, battle[0].a_side):
            _tally(result, battle)
            battle = []
        battle.append(d)
    if battle:
        _tally(result, battle)
    return result


def _tally(result: AuditResult, battle: list[Decision]) -> None:
    mine = [d for d in battle if d.by_a]
    outcome = outcome_of(battle[0].winner, battle[0].a_side)
    result.decisions += len(mine)
    result.battles[outcome] += 1
    for name, check in CHECKS.items():
        found = sum(1 for d in mine if check(d))
        result.checks[name].occurrences += found
        if found:
            result.checks[name].battles[outcome] += 1
