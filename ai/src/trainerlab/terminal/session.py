"""A battle against a bot in the terminal: the screens, the narrated turns and the summary."""

from trainerlab import _engine
from trainerlab.game.battle import play_battle
from trainerlab.game.environment import BattleEnvironment
from trainerlab.game.player import Bot
from trainerlab.terminal.human import Ask, HumanPlayer
from trainerlab.terminal.render import Style, describe_board, narrate, title_for
from trainerlab.terminal.screen import Screen
from trainerlab.terminal.stats import BattleStats

ME = 0  # the human plays side 0
ABANDONED = 130  # the exit status of a program stopped by Ctrl-C
RESULTS = {ME: "You won!", 1 - ME: "You lost.", None: "It's a draw."}


def play_session(
    env: BattleEnvironment,
    opponent: Bot,
    opponent_name: str,
    seed: int,
    team: list[str],
    screen: Screen,
    style: Style,
    ask: Ask,
    hints: bool,
) -> int:
    """Plays the battle with the human as side 0; returns the exit status of the command."""
    screen.clear()
    screen.message(
        f"Seed {seed} (replay this battle with --seed {seed}). Opponent: {opponent_name}."
    )
    screen.message("Your team: " + ", ".join(team))
    stats = BattleStats()
    before = env.observe(ME)

    def tell(events: list[_engine.Event]) -> None:
        nonlocal before
        after = env.observe(ME)
        lines = narrate(list(events), before, after, ME, style)
        if lines:
            screen.narrate(title_for(before, after), lines)
        stats.record(lines)
        before = after

    tell(env.opening_events)
    player = HumanPlayer(ask, screen, style, hints=hints)
    try:
        winner = play_battle(env, player, opponent, tell)
    except (EOFError, KeyboardInterrupt):
        screen.message("Battle abandoned.")
        return ABANDONED

    final = env.observe(ME)
    screen.clear()
    screen.panel("Final board", describe_board(final, ME, style))
    screen.panel("Last turn", screen.take_history())
    screen.panel(RESULTS[winner], stats.summary(final, ME, env.turn))
    screen.message(RESULTS[winner])
    return 0
