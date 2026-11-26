import re
from collections.abc import Callable

import pytest
from consoles import recording, shown
from consoles import text as as_read

from trainerlab.bots.baseline import RandomBot
from trainerlab.bots.lookahead import Forecaster, Strategist
from trainerlab.bots.roster import BOTS
from trainerlab.bots.rules import RuleBot, calculator, dodger, hitter, matcher, survivor
from trainerlab.commands.duel import main
from trainerlab.experiments.tournament import run_tournament
from trainerlab.game.building import species_team_pair

LINE = re.compile(
    r"^(\w+) vs (\w+): (\d\.\d{3}) \[(\d\.\d{3}), (\d\.\d{3})\] "
    r"\((\d+) wins, (\d+) draws, (\d+) losses, seeds (\d+)-(\d+)\)$"
)


def _run(args: list[str], capsys: pytest.CaptureFixture[str]) -> re.Match[str]:
    assert main(args) == 0
    match = LINE.match(capsys.readouterr().out.strip())
    assert match is not None
    return match


def test_the_command_prints_the_score_its_interval_and_the_record(
    capsys: pytest.CaptureFixture[str],
) -> None:
    match = _run(["hitter", "random", "--pairs", "3", "--from", "5"], capsys)

    assert match.group(1, 2) == ("hitter", "random")
    assert float(match[3]) > 0.5  # the score is bot A's: rule 1 beats the random bot
    low, score, high = float(match[4]), float(match[3]), float(match[5])
    assert low <= score <= high
    assert int(match[6]) + int(match[7]) + int(match[8]) == 6
    assert match.group(9, 10) == ("5", "7")


def test_the_same_arguments_reproduce_the_same_line(capsys: pytest.CaptureFixture[str]) -> None:
    first = _run(["random", "random", "--pairs", "3"], capsys)[0]
    second = _run(["random", "random", "--pairs", "3"], capsys)[0]

    assert first == second


def test_the_command_plays_the_requested_seeds(capsys: pytest.CaptureFixture[str]) -> None:
    printed = _run(["random", "random", "--pairs", "4", "--from", "7"], capsys)
    random_bot = BOTS["random"]

    expected = run_tournament(random_bot, random_bot, pairs=4, first_seed=7)

    assert int(printed[6]) == expected.wins
    assert int(printed[8]) == expected.losses


@pytest.mark.parametrize(
    ("name", "rule"),
    [
        ("hitter", hitter),
        ("dodger", dodger),
        ("matcher", matcher),
        ("survivor", survivor),
        ("calculator", calculator),
    ],
)
def test_each_rule_is_built_from_its_choices(name: str, rule: Callable[[], RuleBot]) -> None:
    assert BOTS[name](0) == rule()


@pytest.mark.parametrize(
    ("name", "bot_class"),
    [("random", RandomBot), ("forecaster", Forecaster), ("strategist", Strategist)],
)
def test_each_name_builds_its_bot(name: str, bot_class: type) -> None:
    assert type(BOTS[name](0)) is bot_class


def test_an_unknown_bot_is_refused(capsys: pytest.CaptureFixture[str]) -> None:
    with pytest.raises(SystemExit):
        main(["genius", "random"])

    assert "unknown bot 'genius'" in capsys.readouterr().err


def test_a_single_seed_is_one_pair(capsys: pytest.CaptureFixture[str]) -> None:
    assert _run(["random", "random", "--pairs", "1", "--from", "12"], capsys).group(9, 10) == (
        "12",
        "12",
    )


@pytest.mark.parametrize(
    ("option", "value", "error"),
    [
        ("--pairs", "0", "number of pairs"),
        ("--pairs", "x", "number of pairs"),
        ("--from", "-3", "not a seed"),
        ("--jobs", "0", "number of pairs"),
    ],
)
def test_a_bad_number_of_pairs_or_seed_is_refused(
    option: str, value: str, error: str, capsys: pytest.CaptureFixture[str]
) -> None:
    with pytest.raises(SystemExit):
        main(["random", "random", option, value])

    assert error in capsys.readouterr().err


def test_the_number_of_processes_changes_no_result(capsys: pytest.CaptureFixture[str]) -> None:
    alone = _run(["hitter", "random", "--pairs", "4", "--jobs", "1"], capsys)[0]
    spread = _run(["hitter", "random", "--pairs", "4", "--jobs", "2"], capsys)[0]

    assert alone == spread


def test_on_a_terminal_the_duel_is_framed_with_a_bar_then_its_verdict() -> None:
    console = recording(terminal=True)
    clock = iter([0.0, 263.0])

    assert (
        main(
            ["hitter", "random", "--pairs", "3", "--from", "5", "--jobs", "1"],
            console=console,
            clock=lambda: next(clock),
        )
        == 0
    )

    text = as_read(console)
    assert "hitter  against  random" in text and "3 pairs · seeds 5 to 7" in text
    assert "hitter's score so far:" in text
    assert "hitter beats random" in text or "No clear difference" in text
    assert "6 battles in 4:23" in text


@pytest.mark.parametrize(
    ("interval", "verdict"),
    [((0.6, 0.7), "a beats b"), ((0.3, 0.4), "a loses to b"), ((0.45, 0.55), "No clear")],
)
def test_the_verdict_follows_the_interval(interval: tuple[float, float], verdict: str) -> None:
    from trainerlab.terminal.reports import duel_result

    console = recording()
    console.print(duel_result("a", "b", (1, 0, 1), 0.5, interval, "0:01"))

    assert verdict in shown(console)


def test_in_french_the_verdict_is_in_french() -> None:
    from trainerlab import i18n
    from trainerlab.terminal.reports import duel_result

    i18n.set_language("fr")
    console = recording()
    console.print(duel_result("a", "b", (906, 12, 82), 0.912, (0.894, 0.929), "4:23"))
    i18n.set_language("en")

    text = shown(console)
    assert "a bat b" in text and "[0.894, 0.929] à 95 %" in text and "Victoires 906" in text


def test_a_duel_on_species_teams_says_so(capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["hitter", "random", "--pairs", "2", "--from", "5", "--teams", "species"]) == 0
    line = capsys.readouterr().out.strip()

    result = run_tournament(
        BOTS["hitter"], BOTS["random"], pairs=2, first_seed=5, teams=species_team_pair
    )
    assert line.startswith(f"hitter vs random on species teams: {result.score:.3f} ")


def test_in_a_pipe_the_progress_goes_to_the_terminal_watching(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    seen: list[int] = []

    class Meter:
        def update(self, done: int) -> None:
            seen.append(done)

        def finish(self) -> None:
            seen.append(-1)

    monkeypatch.setattr("trainerlab.commands.duel.parallel.progress_on_terminal", lambda n: Meter())

    _run(["hitter", "random", "--pairs", "6", "--from", "5", "--jobs", "1"], capsys)

    assert seen[0] == 0 and 6 in seen  # at once, then as the blocks end
