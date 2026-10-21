import pytest

from trainerlab.environment import BattleEnvironment, random_team_pair


@pytest.mark.parametrize("seed", range(50))
def test_random_teams_are_full_and_legal(seed: int) -> None:
    team0, team1 = random_team_pair(seed)

    assert len(team0) == len(team1) == 6
    assert len(set(team0)) == 6
    BattleEnvironment(team0, team1, seed=seed)  # raises TeamError on an illegal team


def test_the_same_seed_gives_the_same_teams() -> None:
    assert random_team_pair(3) == random_team_pair(3)


def test_different_seeds_give_different_teams() -> None:
    assert random_team_pair(3) != random_team_pair(4)


def test_the_two_teams_of_a_pair_differ() -> None:
    team0, team1 = random_team_pair(3)

    assert team0 != team1


def test_the_team_size_can_be_chosen() -> None:
    team0, team1 = random_team_pair(3, size=3)

    assert len(team0) == len(team1) == 3
