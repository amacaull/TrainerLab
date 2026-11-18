from trainerlab import _engine
from trainerlab.bots.roster import BOTS
from trainerlab.commands import usage as command
from trainerlab.experiments.usage import KINDS, kind_of, usage
from trainerlab.experiments.usage import _kind as _kind_of_action
from trainerlab.game.environment import DATA_DIR, BattleEnvironment


def _kind(name: str) -> str:
    _engine.engine_init(str(DATA_DIR))
    return kind_of(_engine.find_move_id(name))


def test_every_kind_of_move_is_told_apart() -> None:
    assert _kind("Earthquake") == "attack"
    assert _kind("SeismicToss") == "attack"  # fixed damage still hits
    assert _kind("StealthRock") == "hazard"
    assert _kind("ToxicSpikes") == "hazard"
    assert _kind("SwordsDance") == "boost"
    assert _kind("BellyDrum") == "boost"
    assert _kind("Curse") == "boost"  # raises more than it lowers
    assert _kind("TidyUp") == "boost"  # clears hazards, but is played for its boosts
    assert _kind("WillOWisp") == "status"
    assert _kind("Toxic") == "status"
    assert _kind("SoftBoiled") == "recovery"
    assert _kind("Rest") == "recovery"
    assert _kind("Wish") == "recovery"
    assert _kind("Defog") == "other"
    assert _kind("Whirlwind") == "other"


def test_a_bot_that_only_weighs_the_turn_switches_only_to_escape() -> None:
    result = usage(BOTS["hitter"], BOTS["random"], seeds=range(96000, 96003))

    assert result.turns > 0
    assert result.played["switch"] == 0  # rule 1 never switches while it can move
    assert result.available["attack"] == result.turns
    assert sum(result.played.values()) == result.turns
    assert all(result.played[k] <= result.available[k] for k in KINDS)


def test_the_random_bot_plays_every_kind_it_is_offered() -> None:
    result = usage(BOTS["random"], BOTS["hitter"], seeds=range(96000, 96010))

    assert all(result.played[k] > 0 for k in KINDS if result.available[k] > 20)


def test_the_command_prints_one_line_per_kind() -> None:
    shown: list[str] = []

    assert command.main(["random", "hitter", "--pairs", "2", "--from", "96000"], shown.append) == 0
    assert shown[0].startswith("random against hitter, seeds 96000-96001: ")
    assert [line.split()[0] for line in shown[2:]] == list(KINDS)


def test_struggle_is_an_attack_whatever_slot_it_names() -> None:
    env = BattleEnvironment(["Blissey"], ["Snorlax"], seed=1)
    view = env.observe(0)
    blissey = view.active(0)
    blissey.pp = [0, 0, 0, 0]
    view.set_pokemon(0, 0, blissey)

    assert _kind_of_action(_engine.Action.move(0), view, 0) == "attack"
