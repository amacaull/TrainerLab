"""What can be done with a player's view, which the engine does not accept as it is."""

import copy

from trainerlab import _engine


def playable(view: _engine.BattleState, foe: int) -> _engine.BattleState:
    """A state the engine accepts, built from a view: each foe Pokemon not yet seen becomes a copy
    of the foe's active, still unseen. It only fills the slot: observe hides it again."""
    state = copy.copy(view)
    stand_in = _engine.make_combatant(view.active(foe).species_id)
    for index in range(view.team_size[foe]):
        if view.pokemon(foe, index).is_empty():
            state.set_pokemon(foe, index, stand_in)
    return state
