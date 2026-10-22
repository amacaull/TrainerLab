"""The referee of a battle: owns the real state and enforces the rules of the game."""

import random
from pathlib import Path

from trainerlab import _engine

# ai/src/trainerlab/environment.py -> repository root -> engine/data
DATA_DIR = Path(__file__).resolve().parents[3] / "engine" / "data"


class BattleEnvironment:
    def __init__(
        self, team0: list[str], team1: list[str], *, seed: int, max_turns: int = 1000
    ) -> None:
        _engine.engine_init(str(DATA_DIR))
        self._state = _engine.BattleState()
        for side, species in enumerate((team0, team1)):
            team = _build_team(species)
            _engine.validate_team(team)
            self._state.set_team(side, team)
        self._seed = seed
        self._max_turns = max_turns
        self._opening_events = _engine.start_battle(self._state, seed)

    @property
    def opening_events(self) -> list[_engine.Event]:
        """What happened as the leads entered the field (Intimidate, Snow Warning...)."""
        return self._opening_events

    @property
    def turn(self) -> int:
        return self._state.turn

    def is_over(self) -> bool:
        return _engine.is_over(self._state) or self.turn >= self._max_turns

    def winner(self) -> int | None:
        side0_lost = _engine.side_has_lost(self._state, 0)
        side1_lost = _engine.side_has_lost(self._state, 1)
        if side0_lost and side1_lost:
            return None
        if side1_lost:
            return 0
        if side0_lost:
            return 1
        return None

    def legal_actions(self, side: int) -> list[_engine.Action]:
        return _engine.legal_actions(self._state, side)

    def legal_replacements(self, side: int) -> list[int]:
        return _engine.legal_replacements(self._state, side)

    def observe(self, side: int) -> _engine.BattleState:
        return _engine.observe(self._state, side)

    def play_turn(self, action0: _engine.Action, action1: _engine.Action) -> list[_engine.Event]:
        self._require_not_over()
        return _engine.resolve_turn(self._state, action0, action1, self._turn_seed())

    def replace(self, side: int, *, team_index: int) -> list[_engine.Event]:
        self._require_not_over()
        pending = self.pending_replacements()
        if side in pending and side != pending[0]:
            raise RuntimeError(f"Side {pending[0]} must replace first")
        return _engine.resolve_replacement(self._state, side, team_index)

    def _require_not_over(self) -> None:
        if self.is_over():
            raise RuntimeError("The battle is over")

    def pending_replacements(self) -> list[int]:
        first = _engine.faster_side(self._state, self._turn_seed())
        order = (first, 1 - first)
        return [side for side in order if self.legal_replacements(side)]

    def _turn_seed(self) -> int:
        # Same formula as the engine's golden test: each turn gets its own deterministic seed.
        return self._seed * 1_000_003 + self._state.turn


def _build_team(species: list[str]) -> list[_engine.BattlePokemon]:
    team: list[_engine.BattlePokemon] = []
    for name in species:
        species_id = _engine.find_species_id(name)
        if species_id == _engine.NO_SPECIES:
            raise ValueError(f"Unknown species: {name!r}")
        team.append(_engine.make_combatant(species_id))
    return team


def random_team_pair(seed: int, size: int = 6) -> tuple[list[str], list[str]]:
    """Two random teams that obey the team building rules, the same ones for the same seed."""
    _engine.engine_init(str(DATA_DIR))
    rng = random.Random(seed)
    species = [_engine.species_id_string(i) for i in range(_engine.species_count())]
    return _random_team(rng, species, size), _random_team(rng, species, size)


def _random_team(rng: random.Random, species: list[str], size: int) -> list[str]:
    # Draw until the engine accepts the team: about one draw in two breaks the Mega or the
    # legendary limit.
    while True:
        team = rng.sample(species, size)
        try:
            _engine.validate_team(_build_team(team))
        except _engine.TeamError:
            continue
        return team
