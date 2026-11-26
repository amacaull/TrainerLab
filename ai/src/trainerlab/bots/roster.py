"""Every bot, by the name the commands know it by: the composition root of the measurements.

A bot is named alone (`forecaster`), or with the model it plays with (`forecaster:gen2.npz`), to
measure a newly trained model against the shipped one.
"""

import argparse
from collections.abc import Callable
from dataclasses import dataclass
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
# rule 13's search (trainerlab distill), then 66 iterations of the loop (docs/results.md). Rule 15
# plays it too, its replacements searched.
RULE14 = SELFPLAY.with_name("rule14.pt")
# Rule 16's network: rule 14's, the loop learning its searched replacements (docs/results.md).
RULE16 = SELFPLAY.with_name("rule16.pt")
# Rule 17's network: rule 16's, the loop also on teams of 2 and 4 (docs/results.md).
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
    "zero": lambda seed: _planner(str(RULE12), seed),  # rule 12, needs [rl]
    "deepzero": lambda seed: _planner(str(RULE13), seed),  # rule 13, needs [rl]
    "focus": lambda seed: _planner(str(RULE14), seed),  # rule 14, needs [rl]
    "scout": lambda seed: _planner(str(RULE14), seed, replacements=True),  # rule 15
    "scholar": lambda seed: _planner(str(RULE16), seed, replacements=True),  # rule 16
    "alphazero": lambda seed: _planner(str(ALPHAZERO), seed, replacements=True),  # rule 17
}

# The rule each bot stands for (docs/results.md); the words saying what each does are in
# trainerlab/i18n/messages.py (bot.NAME), in English and in French.
RULES: dict[str, int | None] = {
    "random": None, "hitter": 1, "dodger": 2, "matcher": 3, "survivor": 4, "calculator": 5,
    "forecaster": 6, "strategist": 7, "nash": 8, "mcts": 9, "selfplay": 10, "planner": 11,
    "zero": 12, "deepzero": 13, "focus": 14, "scout": 15, "scholar": 16, "alphazero": 17,
}  # fmt: skip
# The bots the menus offer first; every other one is a name to type.
FEATURED = ["alphazero", "mcts", "forecaster", "calculator", "hitter", "random"]

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
    "zero": RULE12,
    "deepzero": RULE13,
    "focus": RULE14,
    "scout": RULE14,
    "scholar": RULE16,
    "alphazero": ALPHAZERO,
}
# Those that search their replacements after a knock-out (palier 10.4), and plan:FILE.pt; the
# bots measured before send in their network's favourite.
REPLACING = {"scout", "scholar", "alphazero", PLAN}
# After a search bot (alphazero+lead, alphazero@800+lead): it chooses its lead (palier 13.1).
LEAD = "+lead"


@dataclass(frozen=True)
class Searcher:
    """A bot that searches with a network (rule 11 and after): what a measure needs to play it in
    C++, battles side by side (experiments/arena.py)."""

    network: Path
    simulations: int
    replacements: bool  # searched after a knock-out (palier 10.4)


def searcher(spec: str) -> Searcher | None:
    """The search bot a name stands for; None for any other bot, and for one that chooses its
    lead: the measures play it from Python."""
    if spec.endswith(LEAD):
        return None
    base, simulations = _depth(spec)
    name, _, model = base.partition(":")
    if name == PLAN:
        path = Path(model).resolve()
    elif name in SEARCHERS:
        path = SEARCHERS[name]
    else:
        return None
    from trainerlab.rl.planner import SIMULATIONS  # PyTorch, only for these bots

    return Searcher(path, simulations or SIMULATIONS, name in REPLACING)


def factory(spec: str) -> BotFactory:
    """The bot a name stands for: `forecaster:FILE.npz` plays with the model in that file."""
    if spec.endswith(LEAD):
        return _leading(factory(spec.removesuffix(LEAD)))
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


def _leading(make: BotFactory) -> BotFactory:
    from trainerlab.rl.planner import LeadChooser  # PyTorch, only for these bots

    return lambda seed: LeadChooser(make(seed))  # type: ignore[arg-type]


def _depth(spec: str) -> tuple[str, int | None]:
    """A search bot's name and its number of simulations: alphazero@800 -> (alphazero, 800)."""
    base, at, count = spec.rpartition("@")
    return (base, int(count)) if at else (spec, None)


@cache
def _load(path: str) -> WinModel:
    return WinModel.load(Path(path))


def bot_spec(text: str) -> str:
    """Checks a bot's name on the command line (argparse type)."""
    if text.endswith(LEAD):
        base = text.removesuffix(LEAD)
        if _depth(base)[0] not in SEARCHERS and not base.startswith(f"{PLAN}:"):
            raise argparse.ArgumentTypeError(
                f"{text!r}: {LEAD} goes after a search bot ({', '.join(SEARCHERS)} or "
                f"{PLAN}:FILE.pt)"
            )
        bot_spec(base)
        return text
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
    if spec.endswith(LEAD):
        return f"{slug(spec.removesuffix(LEAD))}-lead"
    base, simulations = _depth(spec)
    name, _, model = base.partition(":")
    name = f"{name}-{Path(model).stem}" if model else name
    return f"{name}-{simulations}" if simulations is not None else name
