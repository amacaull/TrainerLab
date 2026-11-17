"""Every bot, by the name the commands know it by: the composition root of the measurements.

A bot is named alone (`forecaster`), or with the model it plays with (`forecaster:gen2.npz`), to
measure a newly trained model against the shipped one.
"""

import argparse
from collections.abc import Callable
from functools import cache
from pathlib import Path

from trainerlab.bots import (
    Bot,
    HeuristicBot,
    MatchupBot,
    OutlookBot,
    RandomBot,
    SurvivalBot,
    SwitchingBot,
)
from trainerlab.forecaster import ForecastBot, StrategistBot
from trainerlab.harness import BotFactory
from trainerlab.model import WinModel

BOTS: dict[str, BotFactory] = {
    "random": lambda seed: RandomBot(seed=seed),
    "hitter": lambda seed: HeuristicBot(),  # rule 1
    "dodger": lambda seed: SwitchingBot(),  # rule 2
    "matcher": lambda seed: MatchupBot(),  # rule 3
    "survivor": lambda seed: SurvivalBot(),  # rule 4
    "calculator": lambda seed: OutlookBot(),  # rule 5
    "forecaster": lambda seed: ForecastBot(),  # rule 6
    "strategist": lambda seed: StrategistBot(),  # rule 7
}

# One line each, shown by the menu and by `trainerlab bots`.
DESCRIPTIONS: dict[str, str] = {
    "random": "picks any legal option at random",
    "hitter": "rule 1: plays its highest power x STAB x type x accuracy",
    "dodger": "rule 2: hitter, but leaves a super effective threat",
    "matcher": "rule 3: hitter, but sends in the best matchup after a knock-out",
    "survivor": "rule 4: matcher, but saves a healthy Pokemon about to fall first",
    "calculator": "rule 5: survivor, but plays the damage the engine expects",
    "forecaster": "rule 6: plays each option a turn ahead, keeps what the win model rates best",
    "strategist": "rule 7: forecaster, but rates each option by the foe's best reply",
}

DEFAULT_OPPONENT = "survivor"
# The bots that play with a win probability model, and can be given another one.
MODEL_BOTS: dict[str, Callable[[WinModel], Bot]] = {
    "forecaster": ForecastBot,
    "strategist": StrategistBot,
}
WITH_MODEL = "|".join(MODEL_BOTS)  # for help texts: forecaster|strategist:MODEL.npz


def factory(spec: str) -> BotFactory:
    """The bot a name stands for: `forecaster:FILE.npz` plays with the model in that file."""
    name, _, model = spec.partition(":")
    if not model:
        return BOTS[name]
    path, make = str(Path(model).resolve()), MODEL_BOTS[name]
    return lambda seed: make(_load(path))


@cache
def _load(path: str) -> WinModel:
    return WinModel.load(Path(path))


def bot_spec(text: str) -> str:
    """Checks a bot's name on the command line (argparse type)."""
    name, _, model = text.partition(":")
    if name not in BOTS or (model and name not in MODEL_BOTS):
        names = ", ".join(BOTS)
        raise argparse.ArgumentTypeError(
            f"unknown bot {text!r}: one of {names}, or {WITH_MODEL}:MODEL.npz"
        )
    if model and not Path(model).is_file():
        raise argparse.ArgumentTypeError(f"no such model: {model}")
    return text


def slug(spec: str) -> str:
    """A bot's name fit for a file name: forecaster:models/gen2.npz -> forecaster-gen2."""
    name, _, model = spec.partition(":")
    return f"{name}-{Path(model).stem}" if model else name
