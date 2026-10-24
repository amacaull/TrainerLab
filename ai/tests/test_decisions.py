import itertools

from teams import TEAM0, TEAM1

from trainerlab import _engine
from trainerlab.bots import RandomBot
from trainerlab.decisions import Decision, decisions
from trainerlab.environment import BattleEnvironment, random_team_pair
from trainerlab.harness import BotFactory, play_pair


def _fixed(seed: int) -> tuple[list[str], list[str]]:
    return TEAM0, TEAM1


def _random(seed: int) -> RandomBot:
    return RandomBot(seed=seed)


def _battle(seed: int, a_side: int) -> list[Decision]:
    pair = decisions(_random, _random, seeds=[seed], teams=_fixed)
    return [d for d in pair if d.a_side == a_side]


class CountingBot(RandomBot):
    """A random bot that counts the choices it was asked for."""

    def __init__(self, *, seed: int) -> None:
        super().__init__(seed=seed)
        self.calls = 0

    def choose_action(
        self, side: int, view: _engine.BattleState, legal: list[_engine.Action]
    ) -> _engine.Action:
        self.calls += 1
        return super().choose_action(side, view, legal)

    def choose_replacement(self, side: int, view: _engine.BattleState, options: list[int]) -> int:
        self.calls += 1
        return super().choose_replacement(side, view, options)


def test_no_seed_gives_no_decision() -> None:
    assert list(decisions(_random, _random, seeds=[], teams=_fixed)) == []


def test_a_seed_gives_both_battles_of_its_pair() -> None:
    battles = {(d.seed, d.a_side) for d in decisions(_random, _random, seeds=[3], teams=_fixed)}
    assert battles == {(3, 0), (3, 1)}


def test_each_battle_ends_as_the_tournament_recorded_it() -> None:
    for seed in range(5):
        recorded = play_pair(_random, _random, seed=seed, team0=TEAM0, team1=TEAM1)
        replayed = tuple(
            {d.outcome for d in _battle(seed, a_side) if d.by_a}.pop() for a_side in (0, 1)
        )
        assert replayed == recorded


def test_replaying_the_choices_rebuilds_the_battle() -> None:
    battle = _battle(seed=1, a_side=0)
    assert any(d.kind == "replacement" for d in battle)  # the test covers both kinds
    env = BattleEnvironment(TEAM0, TEAM1, seed=1)
    chosen: dict[int, _engine.Action] = {}
    for d in battle:
        assert d.view == env.observe(d.side)  # what the bot saw is its side's view, at that time
        assert d.turn == env.turn
        if d.kind == "replacement":
            assert isinstance(d.choice, int)
            env.replace(d.side, team_index=d.choice)
            continue
        assert isinstance(d.choice, _engine.Action)
        chosen[d.side] = d.choice
        if len(chosen) == 2:
            env.play_turn(chosen[0], chosen[1])
            chosen.clear()
    assert env.is_over()
    assert {d.winner for d in battle} == {env.winner()}


def test_each_choice_is_one_of_the_options_offered() -> None:
    for d in _battle(seed=2, a_side=1):
        assert d.choice in d.legal
        expected = _engine.Action if d.kind == "action" else int
        assert all(isinstance(option, expected) for option in d.legal)


def test_each_decision_is_credited_to_the_bot_that_made_it() -> None:
    made: dict[str, list[CountingBot]] = {"a": [], "b": []}

    def factory(name: str) -> BotFactory:
        def make(seed: int) -> CountingBot:
            made[name].append(CountingBot(seed=seed))
            return made[name][-1]

        return make

    log = list(decisions(factory("a"), factory("b"), seeds=[4], teams=_fixed))
    assert sum(d.by_a for d in log) == sum(bot.calls for bot in made["a"])
    assert sum(not d.by_a for d in log) == sum(bot.calls for bot in made["b"])


def test_the_outcome_is_seen_from_the_side_that_decided() -> None:
    battle = _battle(seed=0, a_side=0)
    winner = battle[0].winner
    assert winner is not None
    assert {d.outcome for d in battle if d.side == winner} == {"win"}
    assert {d.outcome for d in battle if d.side != winner} == {"loss"}


def test_the_same_call_gives_the_same_decisions() -> None:
    def run() -> list[tuple[object, ...]]:
        return [
            (d.seed, d.a_side, d.side, d.turn, d.kind, d.choice, d.view)
            for d in decisions(_random, _random, seeds=[5, 6], teams=_fixed)
        ]

    assert run() == run()


def test_teams_default_to_the_tournament_random_pairs() -> None:
    first = next(decisions(_random, _random, seeds=[7]))
    team = (first.view.pokemon(first.side, i) for i in range(6))
    own = [_engine.species_id_string(pokemon.species_id) for pokemon in team]
    assert own == random_team_pair(7)[first.side]


def test_decisions_come_battle_by_battle() -> None:
    # Endless seeds: the first decision comes without playing them all, so a dataset streams.
    first = next(decisions(_random, _random, seeds=itertools.count(), teams=_fixed))
    assert first.seed == 0
