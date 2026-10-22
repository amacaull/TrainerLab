import re

import pytest

from trainerlab.bots import HeuristicBot, MatchupBot, RandomBot, SurvivalBot, SwitchingBot
from trainerlab.harness import run_tournament
from trainerlab.tournament import BOTS, main

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
    match = _run(["damage", "random", "--pairs", "3", "--first-seed", "5"], capsys)

    assert match.group(1, 2) == ("damage", "random")
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
    printed = _run(["random", "random", "--pairs", "4", "--first-seed", "7"], capsys)
    random_bot = BOTS["random"]

    expected = run_tournament(random_bot, random_bot, pairs=4, first_seed=7)

    assert int(printed[6]) == expected.wins
    assert int(printed[8]) == expected.losses


@pytest.mark.parametrize(
    ("name", "bot_class"),
    [
        ("random", RandomBot),
        ("damage", HeuristicBot),
        ("switching", SwitchingBot),
        ("matchup", MatchupBot),
        ("survival", SurvivalBot),
    ],
)
def test_each_name_builds_its_bot(name: str, bot_class: type) -> None:
    assert type(BOTS[name](0)) is bot_class


def test_an_unknown_bot_is_refused(capsys: pytest.CaptureFixture[str]) -> None:
    with pytest.raises(SystemExit):
        main(["genius", "random"])

    assert "invalid choice: 'genius'" in capsys.readouterr().err
