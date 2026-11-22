"""`trainerlab watch`: replays a battle between two bots, turn by turn.

    trainerlab watch survivor matcher --losses --pairs 50 --from 20000
    trainerlab watch survivor matcher --seed 20017 --side 1

The first form looks for the battles bot A lost: on a terminal it lists them to choose one and
watch it; in a pipe it writes the command that watches each one. The second replays one battle
(terminal/replay.py), at one's pace on a terminal.
"""

import argparse
import sys

from rich.console import Console

from trainerlab.bots.roster import factory
from trainerlab.commands.console import quiet_on_closed_pipe
from trainerlab.commands.options import SCANNED_PAIRS, add_bots, add_pairs, seeds_of
from trainerlab.experiments.tournament import losses
from trainerlab.game.environment import random_team_pair
from trainerlab.game.pairs import pair_battles
from trainerlab.i18n import names, t
from trainerlab.terminal.progress import progress_bar
from trainerlab.terminal.prompts import Line, Option, Prompter, QuestionaryPrompter, Separator
from trainerlab.terminal.render import Style
from trainerlab.terminal.replay import ReplayStopped, replay
from trainerlab.terminal.screen import Screen

BACK = "back"
ENDINGS = {0: "watch.won", 1: "watch.lost_in", None: "watch.drew"}


def main(
    argv: list[str] | None = None,
    *,
    prompter: Prompter | None = None,
    console: Console | None = None,
    interactive: bool | None = None,
) -> int:
    parser = argparse.ArgumentParser(prog="trainerlab watch", description=t("command.watch"))
    add_bots(parser)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--seed", type=int, help=t("opt.watch_seed"))
    mode.add_argument("--losses", action="store_true", help=t("opt.losses"))
    parser.add_argument("--side", type=int, choices=(0, 1), default=0, help=t("opt.side"))
    add_pairs(parser, SCANNED_PAIRS)
    args = parser.parse_args(argv)
    if console is None:
        quiet_on_closed_pipe()
    console = console or Console(highlight=False)
    if interactive is None:
        interactive = console.is_terminal and sys.stdin.isatty()
    watcher = Watcher(
        args.bot_a, args.bot_b, console, prompter or QuestionaryPrompter(), interactive
    )
    if args.losses:
        watcher.losses(seeds_of(args))
    else:
        watcher.watch(args.seed, args.side)
    return 0


class Watcher:
    def __init__(
        self, bot_a: str, bot_b: str, console: Console, prompter: Prompter, interactive: bool
    ) -> None:
        self.bot_a, self.bot_b = bot_a, bot_b
        self.console, self.prompter, self.interactive = console, prompter, interactive
        self.style = Style(enabled=console.is_terminal)

    def losses(self, seeds: range) -> None:
        if not self.interactive:
            lost = losses(self.bot_a, self.bot_b, seeds)
            for loss in lost:
                command = t(
                    "watch.command", a=self.bot_a, b=self.bot_b, seed=loss.seed, side=loss.side
                )
                self.console.print(command, highlight=False)
            total = 2 * len(seeds)
            self.console.print(
                t("watch.lost_summary", bot=self.bot_a, lost=len(lost), total=total, b=self.bot_b)
            )
            return
        with progress_bar(self.console, t("duel.progress"), len(seeds)) as bar:
            lost = losses(self.bot_a, self.bot_b, seeds, bar.update)
        if not lost:
            self.console.print(t("watch.none_lost", bot=self.bot_a, total=2 * len(seeds)))
            return
        options: list[Line] = [
            Option(
                index,
                t(
                    "watch.loss",
                    seed=loss.seed,
                    side=loss.side,
                    turns=loss.turns,
                    mine=loss.mine,
                    theirs=loss.theirs,
                ),
            )
            for index, loss in enumerate(lost)
        ]
        options += [Separator(), Option(BACK, t("play.menu"))]
        question = t("watch.lost", bot=self.bot_a, lost=len(lost), total=2 * len(seeds))
        while (chosen := self.prompter.select(question, options)) != BACK:
            self.watch(lost[chosen].seed, lost[chosen].side)

    def watch(self, seed: int, a_side: int) -> None:
        team0, team1 = random_team_pair(seed)
        battles = pair_battles(
            factory(self.bot_a), factory(self.bot_b), seed=seed, team0=team0, team1=team1
        )
        _, env, bot0, bot1 = next(battle for battle in battles if battle[0] == a_side)
        bot_names = (self.bot_a, self.bot_b) if a_side == 0 else (self.bot_b, self.bot_a)
        screen = Screen(self.console, speed="instant", clear=self.interactive)
        screen.message(t("watch.header", seed=seed, a=self.bot_a, side=a_side, b=self.bot_b))
        for side in (a_side, 1 - a_side):
            team = ", ".join(
                names.species(env.observe(side).pokemon(side, i).species_id)
                for i in range(len((team0, team1)[side]))
            )
            screen.message(t("watch.teams", bot=bot_names[side], team=team))
        pause = self.prompter.pause if self.interactive else None
        try:
            winner = replay(env, (bot0, bot1), bot_names, a_side, screen, self.style, pause)
        except ReplayStopped:
            return
        result = None if winner is None else int(winner != a_side)
        screen.message(self.style.bold(t(ENDINGS[result], bot=self.bot_a, turns=env.turn)))
        if self.interactive:
            self.prompter.pause(t("menu.continue") + " ")
