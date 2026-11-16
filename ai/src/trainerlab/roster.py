"""Every bot, by the name the commands know it by: the composition root of the measurements."""

from trainerlab.bots import (
    HeuristicBot,
    MatchupBot,
    OutlookBot,
    RandomBot,
    SurvivalBot,
    SwitchingBot,
)
from trainerlab.harness import BotFactory

BOTS: dict[str, BotFactory] = {
    "random": lambda seed: RandomBot(seed=seed),
    "hitter": lambda seed: HeuristicBot(),  # rule 1
    "dodger": lambda seed: SwitchingBot(),  # rule 2
    "matcher": lambda seed: MatchupBot(),  # rule 3
    "survivor": lambda seed: SurvivalBot(),  # rule 4
    "calculator": lambda seed: OutlookBot(),  # rule 5
}

# One line each, shown by the menu and by `trainerlab bots`.
DESCRIPTIONS: dict[str, str] = {
    "random": "picks any legal option at random",
    "hitter": "rule 1: plays its highest power x STAB x type x accuracy",
    "dodger": "rule 2: hitter, but leaves a super effective threat",
    "matcher": "rule 3: hitter, but sends in the best matchup after a knock-out",
    "survivor": "rule 4: matcher, but saves a healthy Pokemon about to fall first",
    "calculator": "rule 5: survivor, but plays the damage the engine expects",
}

DEFAULT_OPPONENT = "survivor"
