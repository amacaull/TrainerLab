from trainerlab.commands import bench as command
from trainerlab.experiments.bench import measure, positions, rate


def test_a_rate_is_the_units_done_over_the_time_taken() -> None:
    ticks = iter([0.0, 0.4, 0.8, 1.2])

    assert rate(lambda: 3, 1.0, clock=lambda: next(ticks)) == 9 / 1.2


def test_the_positions_are_battles_under_way_with_both_actives_standing() -> None:
    envs = positions(3)

    assert len(envs) == 3
    for env in envs:
        assert env.turn > 0
        assert env.legal_actions(0) and env.legal_actions(1)


def test_every_line_is_measured() -> None:
    lines = measure(0.001)

    assert [line.unit for line in lines] == ["turns", "decisions", "calls"]
    assert all(line.per_second > 0 for line in lines)


def test_the_command_prints_one_line_each() -> None:
    shown: list[str] = []

    assert command.main(["--seconds", "0.001"], write=shown.append) == 0
    assert len(shown) == 3
    assert shown[1].startswith("forecaster decision (rule 6)")
