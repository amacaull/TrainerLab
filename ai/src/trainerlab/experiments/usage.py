"""What a bot does with its turns: attacks, switches, and the moves that only pay later.

Hazards, boosts, statuses and recovery do no damage this turn: a bot that only weighs the turn
never plays them. This counts, over bot A's turns, how often each kind of move was among its
options, and how often it played one.
"""

from collections.abc import Iterable
from dataclasses import dataclass, field

from trainerlab import _engine
from trainerlab.game.decisions import decisions
from trainerlab.game.pairs import BotFactory

KINDS = ("attack", "hazard", "boost", "status", "recovery", "other", "switch")


def kind_of(move_id: int) -> str:
    """The kind of a move: damaging, or what a status move is for."""
    move = _engine.move_entry(move_id)
    if move.category != 2:  # physical or special: it hits
        return "attack"
    effects = move.effects
    if any(e.kind == "SetHazard" for e in effects):
        return "hazard"
    if any(
        e.kind == "BellyDrum" or (e.kind == "StatChange" and e.affects_user and e.delta > 0)
        for e in effects
    ):
        return "boost"
    if any(e.kind == "ApplyStatus" for e in effects):
        return "status"
    if any(e.kind in ("Recovery", "Rest", "Wish") for e in effects):
        return "recovery"
    return "other"  # Protect, screens, Defog, Whirlwind, Teleport, Destiny Bond, Sleep Talk


def _kind(action: _engine.Action, view: _engine.BattleState, side: int) -> str:
    if action.kind == _engine.ACTION_SWITCH:
        return "switch"
    active = view.active(side)
    if not any(active.pp):
        return "attack"  # Struggle, whatever slot it names
    return kind_of(active.move_ids[action.index])


@dataclass
class Usage:
    turns: int = 0  # bot A's turns, replacements left out
    available: dict[str, int] = field(default_factory=lambda: dict.fromkeys(KINDS, 0))
    played: dict[str, int] = field(default_factory=lambda: dict.fromkeys(KINDS, 0))


def usage(make_a: BotFactory, make_b: BotFactory, *, seeds: Iterable[int]) -> Usage:
    """How often each kind of move was among bot A's options, and how often it played one."""
    result = Usage()
    for d in decisions(make_a, make_b, seeds=seeds):
        if not d.by_a or d.kind != "action" or not isinstance(d.choice, _engine.Action):
            continue
        result.turns += 1
        options = {_kind(a, d.view, d.side) for a in d.legal if isinstance(a, _engine.Action)}
        for kind in options:
            result.available[kind] += 1
        result.played[_kind(d.choice, d.view, d.side)] += 1
    return result
