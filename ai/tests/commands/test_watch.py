import signal
from collections.abc import Iterator, Sequence
from typing import Any

import pytest
from consoles import recording, shown
from prompter import Scripted

from trainerlab import i18n
from trainerlab.bots.roster import BOTS
from trainerlab.commands.watch import BACK, main
from trainerlab.game.decisions import decisions
from trainerlab.game.environment import random_team_pair
from trainerlab.game.pairs import play_pair


@pytest.fixture(autouse=True)
def english() -> Iterator[None]:
    yield
    i18n.set_language("en")


def _run(argv: list[str], prompter: Scripted | None = None, interactive: bool = False) -> str:
    console = recording()
    assert (
        main(argv, prompter=prompter or Scripted(), console=console, interactive=interactive) == 0
    )
    return shown(console)


def _losses(first_seed: int, pairs: int) -> list[tuple[int, int]]:
    """(seed, A's side) of every battle survivor loses to matcher, measured by a tournament."""
    lost = []
    for seed in range(first_seed, first_seed + pairs):
        team0, team1 = random_team_pair(seed)
        outcomes = play_pair(BOTS["survivor"], BOTS["matcher"], seed=seed, team0=team0, team1=team1)
        lost += [(seed, a_side) for a_side in (0, 1) if outcomes[a_side] == "loss"]
    return lost


class Viewer(Scripted):
    """Chooses from a script, and presses `key` at every pause."""

    def __init__(self, answers: Sequence[Any], key: str = "") -> None:
        super().__init__(answers)
        self.key, self.pauses = key, 0

    def pause(self, hint: str) -> str:
        self.pauses += 1
        return self.key


def test_in_a_pipe_losses_lists_a_command_to_watch_each_battle_bot_a_lost() -> None:
    lines = _run(
        ["survivor", "matcher", "--losses", "--pairs", "6", "--from", "20000"]
    ).splitlines()
    expected = [
        f"trainerlab watch survivor matcher --seed {seed} --side {a_side}"
        for seed, a_side in _losses(20000, 6)
    ]
    assert expected  # the test needs at least one loss to mean anything
    assert lines[: len(expected)] == expected
    assert lines[-1] == f"survivor lost {len(expected)} of 12 battles against matcher."


def test_a_watched_loss_ends_on_bot_a_losing() -> None:
    # A loss on side 1 whose twin battle, on side 0, was not lost: watching the wrong battle
    # of the pair would not end the same way.
    lost = _losses(20000, 6)
    seed, a_side = next((s, a) for s, a in lost if a == 1 and (s, 0) not in lost)
    text = _run(["survivor", "matcher", "--seed", str(seed), "--side", str(a_side)])
    assert text.splitlines()[-1].startswith("survivor lost in ")


def test_every_decision_is_told_once_with_the_bot_that_made_it() -> None:
    seed, a_side = 20001, 1
    text = _run(["survivor", "matcher", "--seed", str(seed), "--side", str(a_side)])
    played = list(decisions(BOTS["survivor"], BOTS["matcher"], seeds=[seed]))
    mine = [d for d in played if d.a_side == a_side and d.by_a]
    theirs = [d for d in played if d.a_side == a_side and not d.by_a]
    assert text.count("survivor chose: ") == len(mine)
    assert text.count("matcher chose: ") == len(theirs)


def test_the_header_says_who_plays_which_side_and_the_board_names_both_bots() -> None:
    text = _run(["hitter", "random", "--seed", "3", "--side", "1"])
    lines = text.splitlines()
    assert lines[0] == "Seed 3: hitter (side 1) against random."
    assert lines[1].startswith("hitter's team: ") and lines[2].startswith("random's team: ")
    assert "Turn 1 · hitter (side 1) against random" in text


def test_the_same_battle_is_told_the_same_way() -> None:
    argv = ["survivor", "matcher", "--seed", "20002"]
    assert _run(argv) == _run(argv)


def test_on_a_terminal_a_loss_is_chosen_from_a_list_and_watched_turn_by_turn() -> None:
    viewer = Viewer([0, BACK])

    text = _run(
        ["survivor", "matcher", "--losses", "--pairs", "6", "--from", "20000"],
        viewer,
        interactive=True,
    )

    question, options = viewer.asked[0][1], viewer.asked[0][2]
    lost = _losses(20000, 6)
    assert question == f"survivor lost {len(lost)} of 12 battles. Which one?"
    first = [o for o in options if getattr(o, "value", None) == 0][0]
    assert getattr(first, "label", "").startswith(f"Seed {lost[0][0]}   side {lost[0][1]}   ")
    assert "beaten 0 to " in getattr(first, "label", "")
    assert viewer.pauses > 2  # each turn, then once at the end
    assert "survivor lost in" in text


def test_q_stops_the_replay_and_a_plays_the_rest_without_waiting() -> None:
    quitter = Viewer([0, BACK], key="q")
    _run(
        ["survivor", "matcher", "--losses", "--pairs", "6", "--from", "20000"],
        quitter,
        interactive=True,
    )
    assert quitter.pauses == 1

    auto = Viewer([], key="a")
    text = _run(["survivor", "matcher", "--seed", "20001", "--side", "1"], auto, interactive=True)
    assert auto.pauses == 2  # after the first turn, then at the end
    assert "survivor" in text


def test_without_a_loss_there_is_nothing_to_choose() -> None:
    viewer = Viewer([])

    text = _run(["hitter", "random", "--losses", "--pairs", "1"], viewer, interactive=True)

    assert "hitter lost none of the 2 battles." in text
    assert viewer.asked == []


def test_in_french_the_replay_is_told_in_french() -> None:
    i18n.set_language("fr")
    text = _run(["hitter", "random", "--seed", "3"])
    assert "Seed 3 : hitter (camp 0) contre random." in text and "hitter a choisi : " in text


@pytest.mark.parametrize(
    "argv",
    [
        ["survivor", "matcher"],  # neither a seed nor --losses
        ["survivor", "matcher", "--seed", "1", "--losses"],  # both
        ["survivor", "nobody", "--seed", "1"],  # unknown bot
        ["survivor", "matcher", "--seed", "1", "--side", "2"],
    ],
)
def test_bad_arguments_are_refused(argv: list[str]) -> None:
    with pytest.raises(SystemExit) as error:
        main(argv, console=recording())
    assert error.value.code == 2


def test_by_default_the_output_goes_to_the_terminal(
    capsys: pytest.CaptureFixture[str], monkeypatch: pytest.MonkeyPatch
) -> None:
    handlers: list[tuple[int, object]] = []
    monkeypatch.setattr(signal, "signal", lambda sig, handler: handlers.append((sig, handler)))
    assert main(["hitter", "random", "--losses", "--pairs", "1"]) == 0
    assert capsys.readouterr().out.endswith("of 2 battles against random.\n")
    # Piped into head, the command stops quietly instead of raising BrokenPipeError.
    assert handlers == [(signal.SIGPIPE, signal.SIG_DFL)]
