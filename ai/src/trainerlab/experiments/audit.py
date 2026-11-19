"""The mistakes of a bot that a program can spot on its own, counted over many battles.

The error analysis classified losses by hand (ai/analysis/). Some of its mistakes leave a trace
that needs no judgement: a move that fails, a move into an immunity, a fixed-damage knock-out
left unplayed, a choice made while every move is estimated at zero. This counts them over many
battles, to check that what the hand analysis found on a few losses holds at scale, and whether
each mistake comes with losing.

The strategic checks (palier 7.2) count what reading rule 12's battles found: boosts that come to
nothing, a Destiny Bond walked into, a sure knock-out left unplayed. They read a decision with the
rest of its battle, to see what came after.
"""

from collections.abc import Callable, Iterable
from dataclasses import dataclass, field

from trainerlab import _engine
from trainerlab.bots.roster import factory
from trainerlab.experiments import parallel
from trainerlab.game import estimate
from trainerlab.game.decisions import Decision, decisions
from trainerlab.game.events import EventKind
from trainerlab.game.pairs import BotFactory, Outcome, outcome_of

# The abilities that make a Pokemon immune to a move. Others also trigger as a move lands
# (Disguise takes the hit, Magic Bounce sends it back): those are not an immunity the bot missed.
IMMUNITIES = {"Levitate", "VoltAbsorb", "LightningRod", "FlashFire", "Bulletproof"}


def _own_move_followed_by(d: Decision) -> list[_engine.Event]:
    """The event right after each move the deciding side used this turn."""
    after = []
    for event, following in zip(d.events, d.events[1:], strict=False):
        if event.kind == EventKind.MOVE_USED and event.side == d.side:
            after.append(following)
    return after


def _no_effect(d: Decision) -> bool:
    """A move into an immunity: of type (no damage at all) or of ability (Levitate...)."""
    return any(
        e.side != d.side
        and (
            (e.kind == EventKind.DAMAGE_DEALT and e.f0 == 0)
            or (
                e.kind == EventKind.ABILITY_TRIGGERED
                and _engine.ability_name(e.name_id) in IMMUNITIES
            )
        )
        for e in _own_move_followed_by(d)
    )


def _failed(d: Decision) -> bool:
    """A move that failed outright: Fake Out after the first turn, hazards already laid..."""
    return any(
        e.kind == EventKind.MOVE_FAILED or (e.kind == EventKind.STATUS_FAILED and e.side != d.side)
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
    return estimate.LEVEL  # Seismic Toss takes 100 HP


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


def _moves_chosen(d: Decision) -> _engine.MoveEntry | None:
    """The move the deciding side chose, if it chose a move."""
    if d.kind != "action" or not isinstance(d.choice, _engine.Action):
        return None
    if d.choice.kind != _engine.ACTION_MOVE:
        return None
    return _engine.move_entry(d.view.active(d.side).move_ids[d.choice.index])


def _raises_own_stats(move: _engine.MoveEntry) -> bool:
    """A status move that raises its user's stats: Swords Dance, Nasty Plot, Belly Drum..."""
    return move.category == STATUS and any(
        (e.kind == "StatChange" and e.affects_user and e.delta > 0) or e.kind == "BellyDrum"
        for e in move.effects
    )


def _boost(battle: list[Decision], i: int) -> bool:
    move = _moves_chosen(battle[i])
    return move is not None and _raises_own_stats(move)


def _landed_hit(d: Decision) -> bool:
    return any(
        e.kind == EventKind.DAMAGE_DEALT and e.side != d.side and e.i0 > 0
        for e in _own_move_followed_by(d)
    )


def _boost_wasted(battle: list[Decision], i: int) -> bool:
    """A boost, then the Pokemon left the field (knocked out, switched, phazed) without a hit."""
    if not _boost(battle, i):
        return False
    d = battle[i]
    slot = d.view.active_index[d.side]
    for later in battle[i + 1 :]:
        if later.side != d.side:
            continue
        if later.kind == "replacement" or later.view.active_index[d.side] != slot:
            return True
        if _landed_hit(later):
            return False
    return d.winner != d.side  # the battle ended with it still in, unused


def _boost_into_phazer(battle: list[Decision], i: int) -> bool:
    """A boost in front of a foe that can force it out (Whirlwind, Dragon Tail): seen, revealed."""
    if not _boost(battle, i):
        return False
    d = battle[i]
    foe = d.view.active(1 - d.side)
    return any(
        e.kind == "ForceSwitch"
        for move_id in foe.move_ids
        if move_id != _engine.NO_MOVE
        for e in _engine.move_entry(move_id).effects
    )


def _boost_into_ko(battle: list[Decision], i: int) -> bool:
    """A boost while the foe's strongest hit, as estimated, knocks the booster out."""
    if not _boost(battle, i):
        return False
    d = battle[i]
    active, foe = d.view.active(d.side), d.view.active(1 - d.side)
    return estimate.strongest_hit(foe, active) >= active.current_hp


def _destiny_bond_trade(battle: list[Decision], i: int) -> bool:
    """The bot's Pokemon knocked the foe out and fell to its Destiny Bond.

    The event names the side of the Pokemon taken down, not of the one that bound it.
    """
    d = battle[i]
    return d.kind == "action" and any(
        e.kind == EventKind.DESTINY_BOND_TRIGGERED and e.side == d.side for e in d.events
    )


def _ko_missed(battle: list[Decision], i: int) -> bool:
    """The bot moves first, a move's smallest damage knocks the foe out, and it plays another."""
    d = battle[i]
    if d.kind != "action":
        return False
    active, foe = d.view.active(d.side), d.view.active(1 - d.side)
    if estimate.effective_speed(active) <= estimate.effective_speed(foe):
        return False
    moves = [a for a in d.legal if isinstance(a, _engine.Action) and a.kind == _engine.ACTION_MOVE]
    knock_outs = [
        a for a in moves if _engine.expected_damage(d.view, d.side, a.index).min >= foe.current_hp
    ]
    return bool(knock_outs) and d.choice not in knock_outs


STATUS = 2  # a move's category: physical 0, special 1, status 2
STRATEGIC: dict[str, Callable[[list[Decision], int], bool]] = {
    "boost": _boost,  # not a mistake: what the three next ones are a share of
    "boost-wasted": _boost_wasted,
    "boost-into-phazer": _boost_into_phazer,
    "boost-into-ko": _boost_into_ko,
    "destiny-bond-trade": _destiny_bond_trade,
    "ko-missed": _ko_missed,
}


def _no_battles() -> dict[Outcome, int]:
    return {"win": 0, "draw": 0, "loss": 0}


@dataclass
class CheckCount:
    """How often one check fired, and in how many battles of each outcome."""

    occurrences: int = 0
    battles: dict[Outcome, int] = field(default_factory=_no_battles)  # with at least one


@dataclass
class AuditResult:
    """Bot A's decisions and battles, and the count of every check."""

    decisions: int = 0  # made by bot A
    battles: dict[Outcome, int] = field(default_factory=_no_battles)
    checks: dict[str, CheckCount] = field(default_factory=dict)


def audit(make_a: BotFactory, make_b: BotFactory, *, seeds: Iterable[int]) -> AuditResult:
    """Runs every check on bot A's decisions over the pairs of these seeds."""
    result = AuditResult(checks={name: CheckCount() for name in [*CHECKS, *STRATEGIC]})
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
    mine = [i for i, d in enumerate(battle) if d.by_a]
    outcome = outcome_of(battle[0].winner, battle[0].a_side)
    result.decisions += len(mine)
    result.battles[outcome] += 1
    found = {name: sum(1 for i in mine if check(battle[i])) for name, check in CHECKS.items()}
    found |= {name: sum(1 for i in mine if check(battle, i)) for name, check in STRATEGIC.items()}
    for name, count in found.items():
        result.checks[name].occurrences += count
        if count:
            result.checks[name].battles[outcome] += 1


def audit_block(bot_a: str, bot_b: str, seeds: range) -> AuditResult:
    """One block of pairs, by the bots' names: a process of its own when spread over the cores."""
    return audit(factory(bot_a), factory(bot_b), seeds=seeds)


def audit_on_cores(
    bot_a: str, bot_b: str, seeds: range, jobs: int, progress: parallel.Progress | None = None
) -> AuditResult:
    """`audit` over these seeds, spread over `jobs` processes: the same counts, faster."""
    total = AuditResult(checks={name: CheckCount() for name in [*CHECKS, *STRATEGIC]})
    for part in parallel.run(audit_block, bot_a, bot_b, seeds, jobs, progress):
        total.decisions += part.decisions
        for outcome, n in part.battles.items():
            total.battles[outcome] += n
        for name, count in part.checks.items():
            total.checks[name].occurrences += count.occurrences
            for outcome, n in count.battles.items():
                total.checks[name].battles[outcome] += n
    return total
