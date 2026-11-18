"""A position as a network reads it: one vector per Pokemon, the field, and the legal options.

Computed in C++ (search/src/encoding.cpp). From a side's view, twelve slots: its team in team
order, then the foe's. Each Pokemon has numbers (HP, stats, stages, status, types, and for each of
its four moves power, accuracy, category, priority, PP and type) and seven catalog ids for
embeddings (species, ability, item, the four moves; 0 for none or unknown, else index + 1). A
foe not seen yet is present but unknown. The options are ten: moves 0-3, then switches to team
indices 0-5.
"""

from dataclasses import dataclass
from functools import cache

import numpy as np
import numpy.typing as npt

from trainerlab import _engine, _search
from trainerlab.game.environment import DATA_DIR

SLOTS = 12
ACTIONS = 10  # 4 moves, then 6 switches


@dataclass(frozen=True)
class Encoded:
    pokemon: npt.NDArray[np.float32]  # SLOTS x len(pokemon_names())
    ids: npt.NDArray[np.int32]  # SLOTS x 7: species, ability, item, four moves
    field: npt.NDArray[np.float32]  # len(field_names())


@cache
def _names() -> tuple[tuple[str, ...], tuple[str, ...]]:
    _engine.engine_init(str(DATA_DIR))
    _search.init(str(DATA_DIR))
    pokemon, field = _search.encoding_names()
    return tuple(pokemon), tuple(field)


def pokemon_names() -> tuple[str, ...]:
    """The name of each column of a Pokemon's vector."""
    return _names()[0]


def field_names() -> tuple[str, ...]:
    """The name of each column of the field's vector."""
    return _names()[1]


def encode(view: _engine.BattleState, side: int) -> Encoded:
    """What `side` sees in `view`, its own observe()."""
    _names()
    pokemon, ids, field = _search.encode(view, side)
    return Encoded(pokemon, ids, field)


def action_mask(legal: list[_engine.Action]) -> npt.NDArray[np.float32]:
    """1 for each legal option, among the ten."""
    _names()
    return _search.action_mask(legal)


def action_index(action: _engine.Action) -> int:
    """The option's place among the ten: a move by its slot, a switch by 4 + its team index."""
    return action.index if action.kind == _engine.ACTION_MOVE else 4 + action.index


def action_of(index: int) -> _engine.Action:
    """The option at this place among the ten."""
    return _engine.Action.move(index) if index < 4 else _engine.Action.switch(index - 4)
