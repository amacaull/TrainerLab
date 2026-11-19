"""Every bot, by the name the commands know it by: the composition root of the measurements.

A bot is named alone (`forecaster`), or with the model it plays with (`forecaster:gen2.npz`), to
measure a newly trained model against the shipped one.
"""

import argparse
from collections.abc import Callable
from functools import cache
from pathlib import Path

from trainerlab.bots.baseline import RandomBot
from trainerlab.bots.lookahead import Forecaster, Mcts, Nash, Strategist
from trainerlab.bots.rules import calculator, dodger, hitter, matcher, survivor
from trainerlab.game.pairs import BotFactory
from trainerlab.game.player import Bot
from trainerlab.learning.model import WinModel

# Rule 10's network, shipped with the package: trainerlab selfplay net.pt --updates 3000
# (docs/results.md). Loaded with PyTorch only when the bot plays.
SELFPLAY = Path(__file__).resolve().parents[1] / "rl" / "models" / "selfplay.pt"

BOTS: dict[str, BotFactory] = {
    "random": lambda seed: RandomBot(seed=seed),
    "hitter": lambda seed: hitter(),  # rule 1
    "dodger": lambda seed: dodger(),  # rule 2
    "matcher": lambda seed: matcher(),  # rule 3
    "survivor": lambda seed: survivor(),  # rule 4
    "calculator": lambda seed: calculator(),  # rule 5
    "forecaster": lambda seed: Forecaster(),  # rule 6
    "strategist": lambda seed: Strategist(),  # rule 7
    "nash": lambda seed: Nash(seed=seed),  # rule 8
    "mcts": lambda seed: Mcts(seed=seed),  # rule 9
    "selfplay": lambda seed: _net_bot(str(SELFPLAY))(seed),  # rule 10, needs [rl]
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
    "nash": "rule 8: draws from the mixed strategy of the simultaneous game, a turn deep",
    "mcts": "rule 9: a tree search over simultaneous moves, three turns deep",
    "selfplay": "rule 10: a network trained by self-play, plays without search",
}

DEFAULT_OPPONENT = "forecaster"  # beats every bot but strategist, head to head
# The bots that play with a win probability model, and can be given another one.
MODEL_BOTS: dict[str, Callable[[WinModel, int], Bot]] = {
    "forecaster": lambda model, seed: Forecaster(model),
    "strategist": lambda model, seed: Strategist(model),
    "nash": lambda model, seed: Nash(model, seed=seed),
    "mcts": lambda model, seed: Mcts(model, seed=seed),
}
WITH_MODEL = "|".join(MODEL_BOTS)  # for help texts: forecaster|...|mcts:MODEL.npz
# A policy network alone, from a file made by `trainerlab pretrain` (needs the [rl] extras).
NET = "net"


def factory(spec: str) -> BotFactory:
    """The bot a name stands for: `forecaster:FILE.npz` plays with the model in that file."""
    name, _, model = spec.partition(":")
    if name == NET:
        return _net_bot(str(Path(model).resolve()))
    if not model:
        return BOTS[name]
    path, make = str(Path(model).resolve()), MODEL_BOTS[name]
    return lambda seed: make(_load(path), seed)


def _net_bot(path: str) -> BotFactory:
    from trainerlab.rl.bot import NetBot  # here: PyTorch is loaded only for this bot

    return lambda seed: NetBot(Path(path))


@cache
def _load(path: str) -> WinModel:
    return WinModel.load(Path(path))


def bot_spec(text: str) -> str:
    """Checks a bot's name on the command line (argparse type)."""
    name, _, model = text.partition(":")
    if name == NET:
        if not model.endswith(".pt"):
            raise argparse.ArgumentTypeError(f"{NET} plays with a network: {NET}:FILE.pt")
        if not Path(model).is_file():
            raise argparse.ArgumentTypeError(f"no such network: {model}")
        return text
    if name not in BOTS or (model and name not in MODEL_BOTS):
        names = ", ".join(BOTS)
        raise argparse.ArgumentTypeError(
            f"unknown bot {text!r}: one of {names}, {WITH_MODEL}:MODEL.npz or {NET}:FILE.pt"
        )
    if model and not Path(model).is_file():
        raise argparse.ArgumentTypeError(f"no such model: {model}")
    return text


def slug(spec: str) -> str:
    """A bot's name fit for a file name: forecaster:models/gen2.npz -> forecaster-gen2."""
    name, _, model = spec.partition(":")
    return f"{name}-{Path(model).stem}" if model else name
