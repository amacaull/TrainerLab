"""Situations a rule can spot, for the judge to say whether they are mistakes (palier 7.5).

A rule sees the shape of a decision, not its context: a Pokemon left to fall can be a sacrifice
that brings the next one in for free, a heal at high HP is worth it when the foe hits first, a
switch into a weakness pays when the newcomer survives and strikes back first. So a situation is
only spotted here; the judge (experiments/judge.py) rates each decision it is spotted in, and a
situation counts as a mistake only where the judge confirms it most of the time.

Those of palier 7.2 are kept as the audit defines them, so that its counts stay comparable.
"""

from collections.abc import Callable

from trainerlab import _engine
from trainerlab.experiments.audit import STATUS, STRATEGIC, _moves_chosen
from trainerlab.game import estimate
from trainerlab.game.decisions import Decision

LOW_HP = 0.25  # at or below, a Pokemon left to fall is a sacrifice rather than a mistake
HIGH_HP = 0.75  # above, a heal is a heal at high HP
HEALS = {"Recovery", "Roost", "Rest", "Wish"}


def _heals(move: _engine.MoveEntry) -> bool:
    return move.category == STATUS and any(e.kind in HEALS for e in move.effects)


def _foe_first(d: Decision) -> bool:
    active, foe = d.view.active(d.side), d.view.active(1 - d.side)
    return estimate.effective_speed(foe) > estimate.effective_speed(active)


def _knocks_out(d: Decision, action: _engine.Action) -> bool:
    foe = d.view.active(1 - d.side)
    return _engine.expected_damage(d.view, d.side, action.index).min >= foe.current_hp


def _moves(d: Decision) -> list[_engine.Action]:
    return [a for a in d.legal if isinstance(a, _engine.Action) and a.kind == _engine.ACTION_MOVE]


def _priority_ko_missed(battle: list[Decision], i: int) -> bool:
    """The foe moves first, a priority move surely knocks it out, and the bot plays another."""
    d = battle[i]
    if d.kind != "action" or not _foe_first(d):
        return False
    active = d.view.active(d.side)
    first = [
        a
        for a in _moves(d)
        if _engine.move_entry(active.move_ids[a.index]).priority > 0 and _knocks_out(d, a)
    ]
    return bool(first) and d.choice not in first


def _facing_ko(d: Decision) -> bool:
    """The bot attacks without knocking out a faster foe whose best hit knocks it out."""
    move = _moves_chosen(d)
    if move is None or not _foe_first(d):
        return False
    active, foe = d.view.active(d.side), d.view.active(1 - d.side)
    assert isinstance(d.choice, _engine.Action)
    return estimate.strongest_hit(foe, active) >= active.current_hp and not _knocks_out(d, d.choice)


def _hp_share(d: Decision) -> float:
    active = d.view.active(d.side)
    return active.current_hp / active.stats.hp


def _stayed_facing_ko(battle: list[Decision], i: int) -> bool:
    d = battle[i]
    return _facing_ko(d) and _hp_share(d) > LOW_HP


def _sacrifice(battle: list[Decision], i: int) -> bool:
    d = battle[i]
    return _facing_ko(d) and _hp_share(d) <= LOW_HP


def _heal_high_hp(battle: list[Decision], i: int, *, slower: bool) -> bool:
    d = battle[i]
    move = _moves_chosen(d)
    return move is not None and _heals(move) and _hp_share(d) > HIGH_HP and _foe_first(d) == slower


def _switch_into_weakness(battle: list[Decision], i: int) -> bool:
    """The Pokemon sent in is weak to the foe's best hit on it."""
    d = battle[i]
    if d.kind != "action" or not isinstance(d.choice, _engine.Action):
        return False
    if d.choice.kind == _engine.ACTION_MOVE:
        return False
    target, foe = d.view.pokemon(d.side, d.choice.index), d.view.active(1 - d.side)
    slots = range(len(foe.move_ids))
    best = max(slots, key=lambda slot: estimate.estimate_damage(foe, target, slot))
    if estimate.estimate_damage(foe, target, best) <= 0:
        return False
    move = _engine.move_entry(foe.move_ids[best])
    return _engine.type_effectiveness(move.type, target.species_id) >= 2


SITUATIONS: dict[str, Callable[[list[Decision], int], bool]] = {
    **STRATEGIC,
    "priority-ko-missed": _priority_ko_missed,
    "stayed-facing-ko": _stayed_facing_ko,
    "sacrifice": _sacrifice,
    "heal-high-hp-faster": lambda battle, i: _heal_high_hp(battle, i, slower=False),
    "heal-high-hp-slower": lambda battle, i: _heal_high_hp(battle, i, slower=True),
    "switch-into-weakness": _switch_into_weakness,
}


def flagged(battle: list[Decision], i: int) -> list[str]:
    """The situations spotted in decision i of this battle."""
    return [name for name, spot in SITUATIONS.items() if spot(battle, i)]
