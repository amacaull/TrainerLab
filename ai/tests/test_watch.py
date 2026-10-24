import signal

import pytest

from trainerlab import _engine
from trainerlab.decisions import decisions
from trainerlab.environment import random_team_pair
from trainerlab.harness import play_pair
from trainerlab.render import describe_move, describe_switch
from trainerlab.tournament import BOTS
from trainerlab.watch import CHOSEN, main


def _run(argv: list[str]) -> list[str]:
    out: list[str] = []
    assert main(argv, write=out.append) == 0
    return "".join(out).splitlines()


def _losses(first_seed: int, pairs: int) -> list[tuple[int, int]]:
    """(seed, A's side) of every battle survival loses to matchup, measured by the harness."""
    lost = []
    for seed in range(first_seed, first_seed + pairs):
        team0, team1 = random_team_pair(seed)
        outcomes = play_pair(BOTS["survival"], BOTS["matchup"], seed=seed, team0=team0, team1=team1)
        lost += [(seed, a_side) for a_side in (0, 1) if outcomes[a_side] == "loss"]
    return lost


def test_losses_lists_a_command_to_watch_each_battle_bot_a_lost() -> None:
    lines = _run(["survival", "matchup", "--losses", "--pairs", "6", "--first-seed", "20000"])
    expected = [
        f"trainerlab-watch survival matchup --seed {seed} --a-side {a_side}"
        for seed, a_side in _losses(20000, 6)
    ]
    assert expected  # the test needs at least one loss to mean anything
    assert lines[: len(expected)] == expected
    assert lines[-1] == f"survival lost {len(expected)} of 12 battles against matchup."


def test_a_watched_loss_ends_on_bot_a_losing() -> None:
    # A loss on side 1 whose twin battle, on side 0, was not lost: watching the wrong battle
    # of the pair would not end the same way.
    lost = _losses(20000, 6)
    seed, a_side = next((s, a) for s, a in lost if a == 1 and (s, 0) not in lost)
    lines = _run(["survival", "matchup", "--seed", str(seed), "--a-side", str(a_side)])
    assert lines[-1].startswith("Bot A (survival) lost")


def test_every_decision_is_shown_once_with_the_option_chosen() -> None:
    seed, a_side = 20001, 1
    lines = _run(["survival", "matchup", "--seed", str(seed), "--a-side", str(a_side)])
    chosen = [line for line in lines if line.lstrip().startswith(CHOSEN)]
    played = [
        d for d in decisions(BOTS["survival"], BOTS["matchup"], seeds=[seed]) if d.a_side == a_side
    ]
    assert len(chosen) == len(played)
    for line, d in zip(chosen, played, strict=True):
        if isinstance(d.choice, _engine.Action) and d.choice.kind == _engine.ACTION_MOVE:
            label = describe_move(d.choice, d.view, d.side, hints=True)
        else:
            index = d.choice if isinstance(d.choice, int) else d.choice.index
            label = describe_switch(index, d.view, d.side, hints=True)
        assert label in line


def test_each_decision_shows_the_bot_its_estimates_and_the_threat() -> None:
    lines = _run(["damage", "random", "--seed", "3"])
    assert any(line.startswith("A, damage: ") for line in lines)
    assert any(line.startswith("B, random: ") for line in lines)
    assert any("HP of its" in line for line in lines)  # the damage estimate of a move
    assert any("takes ~" in line for line in lines)  # what a switch-in would take
    assert any("moves first" in line for line in lines)  # who is faster, and its best hit


def test_the_header_says_who_plays_which_side() -> None:
    lines = _run(["damage", "random", "--seed", "3", "--a-side", "1"])
    assert lines[0] == "Seed 3. Bot A = damage, side 1. Bot B = random, side 0."
    first_to_decide = next(line for line in lines if line.startswith(("A, ", "B, ")))
    assert first_to_decide.startswith("B, random: ")  # side 0 decides first
    team0, team1 = random_team_pair(3)
    assert "A's team: " + ", ".join(team1) in lines
    assert "B's team: " + ", ".join(team0) in lines


def test_each_bot_is_watched_on_its_own_side() -> None:
    # Rule 1 never switches while it can attack; the random bot often does. On the wrong side,
    # A's blocks would show the random bot's switches.
    lines = _run(["damage", "random", "--seed", "3", "--a-side", "1"])
    choices: dict[str, list[str]] = {"A": [], "B": []}
    block = ""
    for line in lines:
        if line.startswith(("A, ", "B, ")):
            block = line[0] if "sends a Pokemon in" not in line else ""
        elif block and line.lstrip().startswith(CHOSEN):
            choices[block].append(line)
    assert choices["A"]
    assert not any("takes ~" in line for line in choices["A"])
    assert any("takes ~" in line for line in choices["B"])


def test_what_happens_as_the_leads_enter_is_told() -> None:
    lines = _run(["damage", "random", "--seed", "7"])  # a lead with an entry ability
    assert "-- Battle start --" in lines


def test_the_same_battle_is_told_the_same_way() -> None:
    argv = ["survival", "matchup", "--seed", "20002"]
    assert _run(argv) == _run(argv)


@pytest.mark.parametrize(
    "argv",
    [
        ["survival", "matchup"],  # neither a seed nor --losses
        ["survival", "matchup", "--seed", "1", "--losses"],  # both
        ["survival", "nobody", "--seed", "1"],  # unknown bot
        ["survival", "matchup", "--seed", "1", "--a-side", "2"],
    ],
)
def test_bad_arguments_are_refused(argv: list[str]) -> None:
    with pytest.raises(SystemExit) as error:
        main(argv, write=lambda text: None)
    assert error.value.code == 2


def test_by_default_the_output_goes_to_the_terminal(
    capsys: pytest.CaptureFixture[str], monkeypatch: pytest.MonkeyPatch
) -> None:
    handlers: list[tuple[int, object]] = []
    monkeypatch.setattr(signal, "signal", lambda sig, handler: handlers.append((sig, handler)))
    assert main(["damage", "random", "--losses", "--pairs", "1"]) == 0
    assert capsys.readouterr().out.endswith("of 2 battles against random.\n")
    # Piped into head, the command stops quietly instead of raising BrokenPipeError.
    assert handlers == [(signal.SIGPIPE, signal.SIG_DFL)]
