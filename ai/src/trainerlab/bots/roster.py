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
# Rule 12's network: rule 10's, improved by its own search, trainerlab alphazero selfplay.pt
# --iterations 20 (docs/results.md). The judge of palier 7 searches with it.
RULE12 = SELFPLAY.with_name("rule12.pt")
# Rule 13's network: rule 12's, improved further by the cheaper loop of palier 8, trainerlab
# alphazero rule12.pt --iterations 140 (docs/results.md).
RULE13 = SELFPLAY.with_name("rule13.pt")
# Rule 14's network (palier 10): attention over the Pokemon, the matchups computed for it, taught
# rule 13's search (trainerlab distill), then 66 iterations of the loop (docs/results.md).
ALPHAZERO = SELFPLAY.with_name("alphazero.pt")

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
    "planner": lambda seed: _planner(str(SELFPLAY), seed),  # rule 11, needs [rl]
    "rule12": lambda seed: _planner(str(RULE12), seed),  # rule 12, needs [rl]
    "rule13": lambda seed: _planner(str(RULE13), seed),  # rule 13, needs [rl]
    "rule14": lambda seed: _planner(str(ALPHAZERO), seed),  # rule 14, needs [rl]
    "alphazero": lambda seed: _planner(str(ALPHAZERO), seed, replacements=True),  # rule 15
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
    "planner": "rule 11: a tree search over simultaneous moves guided by rule 10's network",
    "rule12": "rule 12: rule 11, its network trained on its own search (AlphaZero)",
    "rule13": "rule 13: rule 12, its network trained further on deeper searches",
    "rule14": "rule 14: a network with attention that sees the matchups, trained on its search",
    "alphazero": "rule 15: rule 14, its replacements after a knock-out searched",
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
# Rule 11's search guided by the network in a file (`trainerlab alphazero`; needs [rl]).
PLAN = "plan"
# The bots that search a tree with a network, by the network they search with. Any of them, and
# plan:FILE.pt, takes @N to search N simulations per decision instead of 200: alphazero@800.
SEARCHERS = {
    "planner": SELFPLAY,
    "rule12": RULE12,
    "rule13": RULE13,
    "rule14": ALPHAZERO,
    "alphazero": ALPHAZERO,
}
# Those that search their replacements after a knock-out (palier 10.4), and plan:FILE.pt; the
# bots measured before send in their network's favourite.
REPLACING = {"alphazero", PLAN}


def factory(spec: str) -> BotFactory:
    """The bot a name stands for: `forecaster:FILE.npz` plays with the model in that file."""
    base, simulations = _depth(spec)
    if simulations is not None:
        name, _, model = base.partition(":")
        path = str(Path(model).resolve()) if name == PLAN else str(SEARCHERS[name])
        replacing = name in REPLACING
        return lambda seed: _planner(path, seed, simulations, replacements=replacing)
    name, _, model = spec.partition(":")
    if name == NET:
        return _net_bot(str(Path(model).resolve()))
    if name == PLAN:
        path = str(Path(model).resolve())
        return lambda seed: _planner(path, seed, replacements=True)
    if not model:
        return BOTS[name]
    path, make = str(Path(model).resolve()), MODEL_BOTS[name]
    return lambda seed: make(_load(path), seed)


def _net_bot(path: str) -> BotFactory:
    from trainerlab.rl.bot import NetBot  # here: PyTorch is loaded only for this bot

    return lambda seed: NetBot(Path(path))


def _planner(
    path: str, seed: int, simulations: int | None = None, *, replacements: bool = False
) -> Bot:
    from trainerlab.rl.planner import SIMULATIONS, Planner  # PyTorch, only for this bot

    return Planner(
        Path(path), seed=seed, simulations=simulations or SIMULATIONS, replacements=replacements
    )


def _depth(spec: str) -> tuple[str, int | None]:
    """A search bot's name and its number of simulations: alphazero@800 -> (alphazero, 800)."""
    base, at, count = spec.rpartition("@")
    return (base, int(count)) if at else (spec, None)


@cache
def _load(path: str) -> WinModel:
    return WinModel.load(Path(path))


def bot_spec(text: str) -> str:
    """Checks a bot's name on the command line (argparse type)."""
    if "@" in text:
        base, _, count = text.rpartition("@")
        searcher = base in SEARCHERS or base.startswith(f"{PLAN}:")
        if not searcher or not count.isdigit() or int(count) < 2:
            raise argparse.ArgumentTypeError(
                f"{text!r}: @N, N simulations of 2 or more, goes after a search bot "
                f"({', '.join(SEARCHERS)} or {PLAN}:FILE.pt)"
            )
        bot_spec(base)
        return text
    name, _, model = text.partition(":")
    if name in (NET, PLAN):
        if not model.endswith(".pt"):
            raise argparse.ArgumentTypeError(f"{name} plays with a network: {name}:FILE.pt")
        if not Path(model).is_file():
            raise argparse.ArgumentTypeError(f"no such network: {model}")
        return text
    if name not in BOTS or (model and name not in MODEL_BOTS):
        names = ", ".join(BOTS)
        raise argparse.ArgumentTypeError(
            f"unknown bot {text!r}: one of {names}, {WITH_MODEL}:MODEL.npz, {NET}:FILE.pt or "
            f"{PLAN}:FILE.pt"
        )
    if model and not Path(model).is_file():
        raise argparse.ArgumentTypeError(f"no such model: {model}")
    return text


def slug(spec: str) -> str:
    """A bot's name fit for a file name: forecaster:models/gen2.npz -> forecaster-gen2."""
    base, simulations = _depth(spec)
    name, _, model = base.partition(":")
    name = f"{name}-{Path(model).stem}" if model else name
    return f"{name}-{simulations}" if simulations is not None else name
