"""The four levels of `trainerlab play` (palier 14): a bot, and the kind of team it plays.

Measured, each level against the one below, 2000 pairs (docs/results.md): medium beats easy
0.874, hard beats medium 0.881; impossible against hard is about 0.80, from palier 12's species
teams and palier 13's deeper searches.
"""

import argparse

# The bot of each level, and whether it builds its team with the species strengths (else at
# random, as the player's).
LEVELS: dict[str, tuple[str, bool]] = {
    "easy": ("matcher", False),
    "medium": ("forecaster", False),
    "hard": ("alphazero", False),
    "impossible": ("alphazero@3200", True),
}
DEFAULT_LEVEL = "medium"
FRENCH = {"facile": "easy", "moyen": "medium", "difficile": "hard"}


def level_spec(text: str) -> str:
    """A level by its English or French name (argparse type): its English one."""
    level = FRENCH.get(text.lower(), text.lower())
    if level not in LEVELS:
        raise argparse.ArgumentTypeError(
            f"{text!r}: {', '.join(LEVELS)} ({', '.join(FRENCH)} in French)"
        )
    return level
