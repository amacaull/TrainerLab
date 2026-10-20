import pytest
from teams import TEAM0, TEAM1

from trainerlab import _engine
from trainerlab.bots import RandomBot
from trainerlab.harness import TournamentResult, play_pair, run_tournament, wilson_interval


class FirstChoiceBot:
    """Always takes the first option: fully predictable."""

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        return legal[0]

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        return options[0]


def _random(seed: int) -> RandomBot:
    return RandomBot(seed=seed)


# --- Wilson interval ---------------------------------------------------------------------------


def test_sixty_wins_out_of_a_hundred_give_the_textbook_interval() -> None:
    low, high = wilson_interval(60, 100)

    assert low == pytest.approx(0.5020, abs=1e-4)
    assert high == pytest.approx(0.6906, abs=1e-4)


def test_the_interval_stays_within_zero_and_one_at_the_extremes() -> None:
    # 15 and 19 games are cases where rounding alone would push the bound outside [0, 1].
    low, _ = wilson_interval(0, 15)
    _, high = wilson_interval(19, 19)

    assert low == 0.0
    assert high == 1.0


@pytest.mark.parametrize(("score", "games"), [(1, 3), (7.5, 20), (480, 1000)])
def test_the_interval_contains_the_observed_rate(score: float, games: int) -> None:
    low, high = wilson_interval(score, games)

    assert low < score / games < high


def test_more_games_give_a_narrower_interval() -> None:
    small_low, small_high = wilson_interval(30, 50)
    large_low, large_high = wilson_interval(300, 500)

    assert large_high - large_low < small_high - small_low


def test_an_interval_without_games_is_refused() -> None:
    with pytest.raises(ValueError, match="at least one game"):
        wilson_interval(0, 0)


# --- A pair of battles -------------------------------------------------------------------------


class RecordingBot(FirstChoiceBot):
    """A predictable bot that records the side and the view of every decision it makes."""

    def __init__(self) -> None:
        self.sides: set[int] = set()
        self.views: list[_engine.BattleState] = []

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        self.sides.add(side)
        self.views.append(view)
        return super().choose_action(side, view, legal)


def _play_recorded_pair() -> list[RecordingBot]:
    """Plays one pair and returns the bots in creation order: A and B of battle 1, then battle 2."""
    created: list[RecordingBot] = []

    def make(seed: int) -> RecordingBot:
        created.append(RecordingBot())
        return created[-1]

    play_pair(make, make, seed=4, team0=TEAM0, team1=TEAM1)
    return created


def test_in_a_pair_each_bot_plays_both_sides() -> None:
    a_first, b_first, a_second, b_second = _play_recorded_pair()

    assert (a_first.sides, b_first.sides) == ({0}, {1})
    assert (a_second.sides, b_second.sides) == ({1}, {0})


def test_both_battles_of_a_pair_share_their_seed() -> None:
    # The same predictable bot holds side 0 in both battles: it sees the very same battle
    # only if the referee draws the same luck twice.
    a_first, _, _, b_second = _play_recorded_pair()

    assert a_first.views == b_second.views


def test_outcomes_are_counted_from_bot_a_point_of_view() -> None:
    # Whoever holds Dragapult knocks Kommo-o out on the first turn.
    def first_choice(seed: int) -> FirstChoiceBot:
        return FirstChoiceBot()

    outcomes = play_pair(first_choice, first_choice, seed=0, team0=["Dragapult"], team1=["KommoO"])

    assert outcomes == ("win", "loss")


# --- The tournament ----------------------------------------------------------------------------


def test_a_tournament_records_every_game() -> None:
    result = run_tournament(_random, _random, team0=TEAM0, team1=TEAM1, pairs=10)

    assert result.games == 20


def test_the_same_seeds_give_the_same_tournament() -> None:
    first = run_tournament(_random, _random, team0=TEAM0, team1=TEAM1, pairs=10, first_seed=3)
    second = run_tournament(_random, _random, team0=TEAM0, team1=TEAM1, pairs=10, first_seed=3)

    assert first == second


def test_a_bot_against_itself_is_compatible_with_an_even_match() -> None:
    result = run_tournament(_random, _random, team0=TEAM0, team1=TEAM1, pairs=100)
    low, high = result.interval

    assert low < 0.5 < high


def test_a_draw_counts_as_half_a_win() -> None:
    result = TournamentResult(wins=1, draws=2, losses=1)

    assert result.score == 0.5
