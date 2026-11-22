"""The names of the engine's species, moves, items, abilities and types, in either language.

The tables (names.json) come from PokeAPI's official names; the descriptions of the moves
(descriptions.json) are written for this engine, from what each move does in it.
"""

import json
import random
from functools import cache
from pathlib import Path
from typing import Any

from trainerlab import _engine
from trainerlab.i18n import language, t

HERE = Path(__file__).parent
TYPES = [
    "Normal", "Fire", "Water", "Electric", "Grass", "Ice", "Fighting", "Poison", "Ground",
    "Flying", "Psychic", "Bug", "Rock", "Ghost", "Dragon", "Dark", "Steel", "Fairy",
]  # fmt: skip
# The opponents of `trainerlab play`: the gym leaders and the Elite Four of the first games.
LEADERS = {
    "en": ["Brock", "Misty", "Lt. Surge", "Erika", "Koga", "Sabrina", "Blaine", "Giovanni",
           "Lorelei", "Bruno", "Agatha", "Lance"],
    "fr": ["Pierre", "Ondine", "Major Bob", "Érika", "Koga", "Morgane", "Auguste", "Giovanni",
           "Olga", "Aldo", "Agatha", "Peter"],
}  # fmt: skip


@cache
def _table(name: str) -> dict[str, Any]:
    loaded: dict[str, Any] = json.loads((HERE / name).read_text())
    return loaded


def _name(kind: str, key: str) -> str:
    return str(_table("names.json")[kind][key][language()])


def species(species_id: int) -> str:
    return _name("species", _engine.species_id_string(species_id))


def move(move_id: int) -> str:
    if move_id == _engine.STRUGGLE:
        return t("move.struggle")
    return _name("moves", _engine.move_name(move_id))


def description(move_id: int) -> str:
    return str(_table("descriptions.json")[_engine.move_name(move_id)][language()])


def item(item_id: int) -> str:
    return _name("items", _engine.item_name(item_id))


def ability(ability_id: int) -> str:
    return _name("abilities", _engine.ability_name(ability_id))


def type_name(type_id: int) -> str:
    return _name("types", TYPES[type_id])


def all_species() -> list[str]:
    """Every species' name in the language chosen, sorted: what the player can type."""
    return sorted(entry[language()] for entry in _table("names.json")["species"].values())


def species_key(name: str) -> str | None:
    """The engine's id of a species, from its name in either language or its id; None if none."""
    wanted = name.strip().casefold()
    for key, entry in _table("names.json")["species"].items():
        if wanted in {key.casefold(), entry["en"].casefold(), entry["fr"].casefold()}:
            return str(key)
    return None


def leader(seed: int) -> str:
    """The trainer `trainerlab play` names the bot after: the same for the same battle."""
    return random.Random(seed).choice(LEADERS[language()])
