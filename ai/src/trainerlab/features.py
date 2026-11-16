"""What a side sees, as numbers: the features of a position, for the dataset and for the models.

Every feature is seen from the side to play ("my" side against the "foe"), and only from what
that side can see: a foe Pokemon not yet seen is counted as alive at full HP, without its species.
"""

from functools import cache

import numpy as np
import numpy.typing as npt

from trainerlab import _engine, estimate
from trainerlab.environment import DATA_DIR

Features = npt.NDArray[np.float32]

STATUSES = ["burn", "poison", "toxic", "paralysis", "sleep", "freeze"]  # 1-6, 0 is none
STAGES = ["atk", "def", "spa", "spd", "spe", "accuracy", "evasion"]
WEATHERS = ["rain", "sun", "sand", "snow", "strong_winds"]  # 1-5, 0 is none


@cache
def feature_names() -> tuple[str, ...]:
    """The name of each column of `features`, in order."""
    _engine.engine_init(str(DATA_DIR))
    species = [_engine.species_id_string(i) for i in range(_engine.species_count())]
    names = ["replacement", "turn", "my_faster", "my_best_damage", "foe_best_damage"]
    names += [f"weather_{w}" for w in WEATHERS] + ["terrain_electric"]
    for p in ("my", "foe"):
        names += [f"{p}_active_hp"]
        names += [f"{p}_active_{s}" for s in STATUSES]
        names += [f"{p}_active_stage_{s}" for s in STAGES]
        names += [f"{p}_alive", f"{p}_hp_total", f"{p}_hidden"]
        names += [f"{p}_stealth_rock", f"{p}_spikes", f"{p}_toxic_spikes", f"{p}_aurora_veil"]
        names += [f"{p}_active_is_{s}" for s in species]
        names += [f"{p}_hp_of_{s}" for s in species]
    return tuple(names)


@cache
def _index() -> dict[str, int]:
    return {name: i for i, name in enumerate(feature_names())}


def features(view: _engine.BattleState, side: int) -> Features:
    """What `side` saw, as numbers between 0 and 1 (stat stages between -1 and 1).

    A foe Pokemon not yet seen is counted as alive at full HP, which it is, without its species.
    """
    index = _index()
    x = np.zeros(len(index), dtype=np.float32)
    me, foe = view.active(side), view.active(1 - side)

    x[index["replacement"]] = me.is_fainted()
    x[index["turn"]] = view.turn / 100
    lead = estimate.effective_speed(me) - estimate.effective_speed(foe)
    x[index["my_faster"]] = 1.0 if lead > 0 else 0.0 if lead < 0 else 0.5
    x[index["my_best_damage"]] = _best_damage(view, side)
    x[index["foe_best_damage"]] = _best_damage(view, 1 - side)
    if view.weather:
        x[index[f"weather_{WEATHERS[view.weather - 1]}"]] = 1
    x[index["terrain_electric"]] = view.terrain != 0

    for p, s in (("my", side), ("foe", 1 - side)):
        active = view.active(s)
        x[index[f"{p}_active_hp"]] = active.current_hp / active.stats.hp
        if active.status:
            x[index[f"{p}_active_{STATUSES[active.status - 1]}"]] = 1
        for stat, stage in zip(STAGES, active.stat_stages, strict=True):
            x[index[f"{p}_active_stage_{stat}"]] = stage / 6
        x[index[f"{p}_active_is_{_engine.species_id_string(active.species_id)}"]] = 1
        alive = hidden = 0
        hp_total = 0.0
        for i in range(view.team_size[s]):
            pokemon = view.pokemon(s, i)
            if pokemon.is_empty():
                hidden += 1
                continue
            share = pokemon.current_hp / pokemon.stats.hp
            alive += share > 0
            hp_total += share
            x[index[f"{p}_hp_of_{_engine.species_id_string(pokemon.species_id)}"]] = share
        x[index[f"{p}_alive"]] = (alive + hidden) / _engine.TEAM_SIZE
        x[index[f"{p}_hp_total"]] = (hp_total + hidden) / _engine.TEAM_SIZE
        x[index[f"{p}_hidden"]] = hidden / _engine.TEAM_SIZE
        hazards = view.hazards(s)
        x[index[f"{p}_stealth_rock"]] = hazards.stealth_rock
        x[index[f"{p}_spikes"]] = hazards.spikes / 3
        x[index[f"{p}_toxic_spikes"]] = hazards.toxic_spikes / 2
        x[index[f"{p}_aurora_veil"]] = view.aurora_veil_turns[s] / 8
    return x


def _best_damage(view: _engine.BattleState, side: int) -> float:
    """The highest damage the engine expects from this side's active, as a share of the foe's HP."""
    foe = view.active(1 - side)
    if foe.is_fainted():
        return 0.0
    attacker = view.active(side)
    slots = [i for i, move in enumerate(attacker.move_ids) if move != _engine.NO_MOVE]
    best = max(_engine.expected_damage(view, side, slot).mean for slot in slots)
    return best / foe.current_hp
