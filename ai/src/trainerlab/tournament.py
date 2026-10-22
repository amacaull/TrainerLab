"""Command line entry point: plays a tournament between two bots and prints bot A's score.

    trainerlab-tournament matchup damage --pairs 500 --first-seed 0

This is the composition root of the measurements: the only place that knows every bot by name.
"""

import argparse

from trainerlab.bots import HeuristicBot, MatchupBot, RandomBot, SurvivalBot, SwitchingBot
from trainerlab.harness import BotFactory, run_tournament

BOTS: dict[str, BotFactory] = {
    "random": lambda seed: RandomBot(seed=seed),
    "damage": lambda seed: HeuristicBot(),  # rule 1
    "switching": lambda seed: SwitchingBot(),  # rule 2
    "matchup": lambda seed: MatchupBot(),  # rule 3
    "survival": lambda seed: SurvivalBot(),  # rule 4
}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Score bot A against bot B over paired battles.")
    parser.add_argument("bot_a", choices=sorted(BOTS))
    parser.add_argument("bot_b", choices=sorted(BOTS))
    parser.add_argument("--pairs", type=int, default=500, help="pairs of battles (default: 500)")
    parser.add_argument("--first-seed", type=int, default=0, help="seed of the first pair")
    args = parser.parse_args(argv)

    result = run_tournament(
        BOTS[args.bot_a], BOTS[args.bot_b], pairs=args.pairs, first_seed=args.first_seed
    )
    low, high = result.interval
    print(
        f"{args.bot_a} vs {args.bot_b}: {result.score:.3f} [{low:.3f}, {high:.3f}] "
        f"({result.wins} wins, {result.draws} draws, {result.losses} losses, "
        f"seeds {args.first_seed}-{args.first_seed + args.pairs - 1})"
    )
    return 0
