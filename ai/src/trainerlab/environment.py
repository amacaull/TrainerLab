# ai/src/trainerlab/environment.py
"""The referee of a battle: owns the real state and enforces the rules of the game."""

from pathlib import Path

from trainerlab import _engine

# ai/src/trainerlab/environment.py -> repository root -> engine/data
DATA_DIR = Path(__file__).resolve().parents[3] / "engine" / "data"


class BattleEnvironment:
    def __init__(self, team0: list[str], team1: list[str], *, seed: int) -> None:
        _engine.engine_init(str(DATA_DIR))
        self._state = _engine.BattleState()
        for side, species in enumerate((team0, team1)):
            team = _build_team(species)
            _engine.validate_team(team)
            self._state.set_team(side, team)
        self._seed = seed
        _engine.start_battle(self._state, seed)

    @property
    def turn(self) -> int:
        return self._state.turn

    def is_over(self) -> bool:
        return False

    def legal_actions(self, side: int) -> list[_engine.Action]:
        return _engine.legal_actions(self._state, side)

    def observe(self, side: int) -> _engine.BattleState:
        return _engine.observe(self._state, side)

    def play_turn(self, action0: _engine.Action, action1: _engine.Action) -> list[_engine.Event]:
        return _engine.resolve_turn(self._state, action0, action1, self._turn_seed())

    def pending_replacements(self) -> list[int]:
        return [side for side in (0, 1) if _engine.legal_replacements(self._state, side)]

    def _turn_seed(self) -> int:
        # Same formula as the engine's golden test: each turn gets its own deterministic seed.
        return self._seed * 1_000_003 + self._state.turn


def _build_team(species: list[str]) -> list[_engine.BattlePokemon]:
    return [_engine.make_combatant(_engine.find_species_id(name)) for name in species]
