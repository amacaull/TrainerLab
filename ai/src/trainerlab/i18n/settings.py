"""Where the language chosen is kept between two launches: ~/.config/trainerlab/config.toml."""

import os
import tomllib
from pathlib import Path

from trainerlab.i18n import LANGUAGES


def config_path() -> Path:
    """The config file; TRAINERLAB_CONFIG moves it (the tests do)."""
    moved = os.environ.get("TRAINERLAB_CONFIG")
    return Path(moved) if moved else Path.home() / ".config" / "trainerlab" / "config.toml"


def saved_language() -> str | None:
    """The language chosen at an earlier launch, or None before the first."""
    try:
        chosen = tomllib.loads(config_path().read_text()).get("language")
    except (OSError, tomllib.TOMLDecodeError):
        return None
    return chosen if chosen in LANGUAGES else None


def save_language(chosen: str) -> None:
    path = config_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f'language = "{chosen}"\n')


def games_dir() -> Path:
    """Where the battles saved go; TRAINERLAB_GAMES moves it (the tests do)."""
    moved = os.environ.get("TRAINERLAB_GAMES")
    return Path(moved) if moved else Path.home() / "TrainerLab" / "parties"
