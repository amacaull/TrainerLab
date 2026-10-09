"""
TrainerLab battle engine: the flat C++ API, unchanged.
"""
from __future__ import annotations
import collections.abc
import typing
__all__: list[str] = ['ACTION_MOVE', 'ACTION_SWITCH', 'Action', 'ActionError', 'ArgError', 'BattlePokemon', 'BattleState', 'DataError', 'EngineError', 'Event', 'FLAG_CRIT', 'FLAG_STAB', 'InitError', 'MAX_MOVES', 'MoveEntry', 'NO_ITEM', 'NO_MOVE', 'NO_NAME', 'NO_SPECIES', 'STRUGGLE', 'STRUGGLE_RECOIL', 'SideHazards', 'SpeciesEntry', 'StateError', 'Stats', 'TEAM_SIZE', 'TeamError', 'ability_count', 'ability_name', 'catalog_fingerprint', 'engine_data_dir', 'engine_init', 'engine_is_initialised', 'faster_side', 'find_ability_id', 'find_item_id', 'find_move_id', 'find_species_id', 'is_over', 'item_count', 'item_name', 'legal_actions', 'legal_replacements', 'make_combatant', 'move_count', 'move_entry', 'move_name', 'observe', 'resolve_replacement', 'resolve_turn', 'side_has_lost', 'species_count', 'species_display_name', 'species_entry', 'species_id_string', 'start_battle', 'struggle_move_id', 'struggle_recoil_ability_id', 'validate_state', 'validate_team']
class EngineError(Exception):
    """
    A contract violation reported by the engine. str(e) keeps the full message.
    """
class InitError(EngineError):
    pass
class DataError(EngineError):
    pass
class ArgError(EngineError):
    pass
class StateError(EngineError):
    pass
class TeamError(EngineError):
    pass
class ActionError(EngineError):
    subcode: typing.ClassVar[str] = ''
class Stats:
    """
    Computed battle stats (level 100).
    """
    __hash__: typing.ClassVar[None] = None
    def __eq__(self, arg0: typing.Any) -> typing.Any:
        ...
    def __init__(self) -> None:
        ...
    def __repr__(self) -> str:
        ...
    @property
    def attack(self) -> int:
        ...
    @attack.setter
    def attack(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def defense(self) -> int:
        ...
    @defense.setter
    def defense(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def hp(self) -> int:
        ...
    @hp.setter
    def hp(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def special_attack(self) -> int:
        ...
    @special_attack.setter
    def special_attack(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def special_defense(self) -> int:
        ...
    @special_defense.setter
    def special_defense(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def speed(self) -> int:
        ...
    @speed.setter
    def speed(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class BattlePokemon:
    """
    One team slot. Build it with make_combatant, never field by field.
    """
    __hash__: typing.ClassVar[None] = None
    stats: Stats
    def __copy__(self) -> BattlePokemon:
        ...
    def __deepcopy__(self, arg0: dict) -> BattlePokemon:
        ...
    def __eq__(self, arg0: typing.Any) -> typing.Any:
        ...
    def __getstate__(self) -> bytes:
        ...
    def __init__(self) -> None:
        ...
    def __repr__(self) -> str:
        ...
    def __setstate__(self, arg0: bytes) -> None:
        ...
    def is_empty(self) -> bool:
        """
        True for a slot hidden by observe or unused.
        """
    def is_fainted(self) -> bool:
        ...
    @property
    def charging_move_id(self) -> int:
        ...
    @charging_move_id.setter
    def charging_move_id(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def current_hp(self) -> int:
        ...
    @current_hp.setter
    def current_hp(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def destiny_bond_active(self) -> int:
        ...
    @destiny_bond_active.setter
    def destiny_bond_active(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def disguise_broken(self) -> int:
        ...
    @disguise_broken.setter
    def disguise_broken(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def flash_fire_active(self) -> int:
        ...
    @flash_fire_active.setter
    def flash_fire_active(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def flinched(self) -> int:
        ...
    @flinched.setter
    def flinched(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def invulnerable_state(self) -> int:
        ...
    @invulnerable_state.setter
    def invulnerable_state(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def item_consumed(self) -> int:
        ...
    @item_consumed.setter
    def item_consumed(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def item_id(self) -> int:
        ...
    @item_id.setter
    def item_id(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def last_move_id(self) -> int:
        ...
    @last_move_id.setter
    def last_move_id(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def level(self) -> int:
        ...
    @level.setter
    def level(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def locked_move_id(self) -> int:
        ...
    @locked_move_id.setter
    def locked_move_id(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def move_ids(self) -> tuple[int, ...]:
        ...
    @move_ids.setter
    def move_ids(self, arg1: typing.Annotated[collections.abc.Sequence[typing.SupportsInt | typing.SupportsIndex], "FixedSize(4)"]) -> None:
        ...
    @property
    def pp(self) -> tuple[int, ...]:
        ...
    @pp.setter
    def pp(self, arg1: typing.Annotated[collections.abc.Sequence[typing.SupportsInt | typing.SupportsIndex], "FixedSize(4)"]) -> None:
        ...
    @property
    def protect_chain(self) -> int:
        ...
    @protect_chain.setter
    def protect_chain(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def protect_contact_status(self) -> int:
        ...
    @protect_contact_status.setter
    def protect_contact_status(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def protected_now(self) -> int:
        ...
    @protected_now.setter
    def protected_now(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def revealed(self) -> int:
        ...
    @revealed.setter
    def revealed(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def roosted(self) -> int:
        ...
    @roosted.setter
    def roosted(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def sleep_self_inflicted(self) -> int:
        ...
    @sleep_self_inflicted.setter
    def sleep_self_inflicted(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def species_id(self) -> int:
        ...
    @species_id.setter
    def species_id(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def stat_stages(self) -> tuple[int, ...]:
        ...
    @stat_stages.setter
    def stat_stages(self, arg1: typing.Annotated[collections.abc.Sequence[typing.SupportsInt | typing.SupportsIndex], "FixedSize(7)"]) -> None:
        ...
    @property
    def status(self) -> int:
        ...
    @status.setter
    def status(self, arg1: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def status_turns(self) -> int:
        ...
    @status_turns.setter
    def status_turns(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def turns_on_field(self) -> int:
        ...
    @turns_on_field.setter
    def turns_on_field(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class SideHazards:
    """
    Hazard layers on one side.
    """
    __hash__: typing.ClassVar[None] = None
    def __eq__(self, arg0: typing.Any) -> typing.Any:
        ...
    def __init__(self) -> None:
        ...
    @property
    def spikes(self) -> int:
        ...
    @spikes.setter
    def spikes(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def stealth_rock(self) -> int:
        ...
    @stealth_rock.setter
    def stealth_rock(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def toxic_spikes(self) -> int:
        ...
    @toxic_spikes.setter
    def toxic_spikes(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
class BattleState:
    """
    The whole battle, owned by the caller. Accessors return copies: write back with the matching set_* method.
    """
    __hash__: typing.ClassVar[None] = None
    def __copy__(self) -> BattleState:
        ...
    def __deepcopy__(self, arg0: dict) -> BattleState:
        ...
    def __eq__(self, arg0: typing.Any) -> typing.Any:
        ...
    def __getstate__(self) -> bytes:
        ...
    def __init__(self) -> None:
        ...
    def __setstate__(self, arg0: bytes) -> None:
        ...
    def active(self, side: typing.SupportsInt | typing.SupportsIndex) -> BattlePokemon:
        """
        A copy of this side's active Pokemon.
        """
    def hazards(self, side: typing.SupportsInt | typing.SupportsIndex) -> SideHazards:
        ...
    def pokemon(self, side: typing.SupportsInt | typing.SupportsIndex, index: typing.SupportsInt | typing.SupportsIndex) -> BattlePokemon:
        """
        A copy of one team slot.
        """
    def set_hazards(self, side: typing.SupportsInt | typing.SupportsIndex, hazards: SideHazards) -> None:
        ...
    def set_pokemon(self, side: typing.SupportsInt | typing.SupportsIndex, index: typing.SupportsInt | typing.SupportsIndex, pokemon: BattlePokemon) -> None:
        ...
    def set_team(self, side: typing.SupportsInt | typing.SupportsIndex, team: collections.abc.Sequence[BattlePokemon]) -> None:
        """
        Fills a side from slot 0, sets team_size, and makes slot 0 the lead.
        """
    @property
    def active_index(self) -> tuple[int, ...]:
        ...
    @active_index.setter
    def active_index(self, arg1: typing.Annotated[collections.abc.Sequence[typing.SupportsInt | typing.SupportsIndex], "FixedSize(2)"]) -> None:
        ...
    @property
    def aurora_veil_turns(self) -> tuple[int, ...]:
        ...
    @aurora_veil_turns.setter
    def aurora_veil_turns(self, arg1: typing.Annotated[collections.abc.Sequence[typing.SupportsInt | typing.SupportsIndex], "FixedSize(2)"]) -> None:
        ...
    @property
    def team_size(self) -> tuple[int, ...]:
        ...
    @team_size.setter
    def team_size(self, arg1: typing.Annotated[collections.abc.Sequence[typing.SupportsInt | typing.SupportsIndex], "FixedSize(2)"]) -> None:
        ...
    @property
    def terrain(self) -> int:
        ...
    @terrain.setter
    def terrain(self, arg1: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def terrain_turns_left(self) -> int:
        ...
    @terrain_turns_left.setter
    def terrain_turns_left(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def turn(self) -> int:
        ...
    @turn.setter
    def turn(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def weather(self) -> int:
        ...
    @weather.setter
    def weather(self, arg1: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def weather_turns_left(self) -> int:
        ...
    @weather_turns_left.setter
    def weather_turns_left(self, arg0: typing.SupportsInt | typing.SupportsIndex) -> None:
        ...
    @property
    def wish_heal(self) -> tuple[int, ...]:
        ...
    @wish_heal.setter
    def wish_heal(self, arg1: typing.Annotated[collections.abc.Sequence[typing.SupportsInt | typing.SupportsIndex], "FixedSize(2)"]) -> None:
        ...
    @property
    def wish_turns(self) -> tuple[int, ...]:
        ...
    @wish_turns.setter
    def wish_turns(self, arg1: typing.Annotated[collections.abc.Sequence[typing.SupportsInt | typing.SupportsIndex], "FixedSize(2)"]) -> None:
        ...
class Action:
    """
    A turn decision. Build with Action.move or Action.switch.
    """
    @staticmethod
    def move(slot: typing.SupportsInt | typing.SupportsIndex, pivot_target: typing.SupportsInt | typing.SupportsIndex = -1) -> Action:
        ...
    @staticmethod
    def switch(team_index: typing.SupportsInt | typing.SupportsIndex) -> Action:
        ...
    def __eq__(self, arg0: typing.Any) -> typing.Any:
        ...
    def __hash__(self) -> int:
        ...
    def __init__(self, kind: typing.SupportsInt | typing.SupportsIndex, index: typing.SupportsInt | typing.SupportsIndex, pivot_target: typing.SupportsInt | typing.SupportsIndex = -1) -> None:
        ...
    def __repr__(self) -> str:
        ...
    @property
    def index(self) -> int:
        ...
    @property
    def kind(self) -> int:
        ...
    @property
    def pivot_target(self) -> int:
        ...
class Event:
    """
    One engine event. The meaning of i0, i1, f0 and flags depends on kind: see the event table in engine/README.md, section 6.
    """
    def __repr__(self) -> str:
        ...
    @property
    def f0(self) -> float:
        ...
    @property
    def flags(self) -> int:
        ...
    @property
    def i0(self) -> int:
        ...
    @property
    def i1(self) -> int:
        ...
    @property
    def kind(self) -> int:
        ...
    @property
    def name_id(self) -> int:
        ...
    @property
    def side(self) -> int:
        ...
    @property
    def slot(self) -> int:
        ...
class SpeciesEntry:
    @property
    def display_name(self) -> str:
        ...
    @property
    def id_string(self) -> str:
        ...
    @property
    def legendary(self) -> bool:
        ...
    @property
    def mega(self) -> bool:
        ...
    @property
    def type1(self) -> int:
        ...
    @property
    def type2(self) -> int:
        ...
    @property
    def weight_kg(self) -> float:
        ...
class MoveEntry:
    @property
    def accuracy(self) -> int:
        ...
    @property
    def category(self) -> int:
        ...
    @property
    def name(self) -> str:
        ...
    @property
    def power(self) -> int:
        ...
    @property
    def pp(self) -> int:
        ...
    @property
    def priority(self) -> int:
        ...
    @property
    def type(self) -> int:
        ...
def ability_count() -> int:
    ...
def ability_name(id: typing.SupportsInt | typing.SupportsIndex) -> str:
    ...
def catalog_fingerprint() -> int:
    ...
def engine_data_dir() -> str:
    ...
def engine_init(data_dir: str) -> None:
    """
    Loads the catalog. Idempotent for the same path; another path raises InitError.
    """
def engine_is_initialised() -> bool:
    ...
def faster_side(state: BattleState, seed: typing.SupportsInt | typing.SupportsIndex) -> int:
    ...
def find_ability_id(name: str) -> int:
    """
    -1 when unknown.
    """
def find_item_id(name: str) -> int:
    """
    -1 when unknown.
    """
def find_move_id(name: str) -> int:
    """
    -1 when unknown.
    """
def find_species_id(id_string: str) -> int:
    """
    -1 when unknown.
    """
def is_over(state: BattleState) -> bool:
    ...
def item_count() -> int:
    ...
def item_name(id: typing.SupportsInt | typing.SupportsIndex) -> str:
    ...
def legal_actions(state: BattleState, side: typing.SupportsInt | typing.SupportsIndex) -> list[Action]:
    """
    What resolve_turn accepts for this side, one entry per distinct outcome. Empty when the active is fainted.
    """
def legal_replacements(state: BattleState, side: typing.SupportsInt | typing.SupportsIndex) -> list[int]:
    """
    Team indices resolve_replacement accepts. Empty unless the active is fainted.
    """
def make_combatant(species_id: typing.SupportsInt | typing.SupportsIndex) -> BattlePokemon:
    ...
def move_count() -> int:
    ...
def move_entry(id: typing.SupportsInt | typing.SupportsIndex) -> MoveEntry:
    ...
def move_name(id: typing.SupportsInt | typing.SupportsIndex) -> str:
    ...
def observe(state: BattleState, side: typing.SupportsInt | typing.SupportsIndex) -> BattleState:
    """
    What the player on this side can see. A view: never pass it back to the engine, legal_actions included. Compute legal actions from the real state.
    """
def resolve_replacement(state: BattleState, side: typing.SupportsInt | typing.SupportsIndex, team_index: typing.SupportsInt | typing.SupportsIndex) -> list[Event]:
    ...
def resolve_turn(state: BattleState, action0: Action, action1: Action, seed: typing.SupportsInt | typing.SupportsIndex) -> list[Event]:
    """
    Updates state in place, returns the events. On error, state is left untouched.
    """
def side_has_lost(state: BattleState, side: typing.SupportsInt | typing.SupportsIndex) -> bool:
    ...
def species_count() -> int:
    ...
def species_display_name(id: typing.SupportsInt | typing.SupportsIndex) -> str:
    ...
def species_entry(id: typing.SupportsInt | typing.SupportsIndex) -> SpeciesEntry:
    ...
def species_id_string(id: typing.SupportsInt | typing.SupportsIndex) -> str:
    ...
def start_battle(state: BattleState, seed: typing.SupportsInt | typing.SupportsIndex) -> list[Event]:
    """
    Mandatory before the first turn. Updates state in place, returns the events.
    """
def struggle_move_id() -> int:
    ...
def struggle_recoil_ability_id() -> int:
    ...
def validate_state(state: BattleState) -> None:
    ...
def validate_team(team: collections.abc.Sequence[BattlePokemon]) -> None:
    """
    Raises TeamError when the team breaks a team building rule.
    """
ACTION_MOVE: int = 0
ACTION_SWITCH: int = 1
FLAG_CRIT: int = 2
FLAG_STAB: int = 1
MAX_MOVES: int = 4
NO_ITEM: int = -1
NO_MOVE: int = -1
NO_NAME: int = -1
NO_SPECIES: int = -1
STRUGGLE: int = -2
STRUGGLE_RECOIL: int = -3
TEAM_SIZE: int = 6
