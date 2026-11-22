from collections.abc import Iterator
from pathlib import Path
from string import Formatter

import pytest

from trainerlab import _engine, i18n
from trainerlab.game.environment import DATA_DIR
from trainerlab.i18n import names, settings
from trainerlab.i18n.messages import MESSAGES


@pytest.fixture(autouse=True)
def english() -> Iterator[None]:
    _engine.engine_init(str(DATA_DIR))
    yield
    i18n.set_language("en")


def test_a_message_comes_in_the_language_chosen_with_its_fields_filled() -> None:
    assert i18n.t("play.won", trainer="Misty", bot="nash", turns=23) == (
        "You beat Misty (nash) in 23 turns."
    )
    i18n.set_language("fr")
    assert i18n.language() == "fr"
    assert i18n.t("play.won", trainer="Ondine", bot="nash", turns=23) == (
        "Tu bats Ondine (nash) en 23 tours."
    )


def test_an_unknown_language_is_refused() -> None:
    with pytest.raises(ValueError, match="de"):
        i18n.set_language("de")


def test_both_languages_of_a_message_fill_the_same_fields() -> None:
    def fields(text: str) -> set[str]:
        return {name for _, name, _, _ in Formatter().parse(text) if name}

    for key, (english, french) in MESSAGES.items():
        assert fields(english) == fields(french), key
        assert english and french, key


def test_every_species_move_item_ability_and_type_of_the_engine_has_both_names() -> None:
    i18n.set_language("fr")
    assert names.species(_engine.find_species_id("Gyarados")) == "Léviator"
    assert names.species(_engine.find_species_id("MegaCharizardX")) == "Méga-Dracaufeu X"
    assert names.move(0) == "Aqua-Jet"
    assert names.move(_engine.STRUGGLE) == "Lutte"
    assert names.item(1) == "Restes"
    assert names.ability(0) == "Brasier"
    assert names.type_name(1) == "Feu"
    for language in i18n.LANGUAGES:
        i18n.set_language(language)
        for species_id in range(_engine.species_count()):
            assert names.species(species_id)
        for move_id in range(95):
            assert names.move(move_id) and names.description(move_id)
        for item_id in range(_engine.item_count()):
            assert names.item(item_id)
        for type_id in range(18):
            assert names.type_name(type_id)


def test_english_names_are_the_official_ones() -> None:
    assert names.species(_engine.find_species_id("KommoO")) == "Kommo-o"
    assert names.move(_engine.STRUGGLE) == "Struggle"
    assert "U-turn" in [names.move(i) for i in range(95)]
    assert names.item(9) == "Heavy-Duty Boots"


def test_a_pokemon_is_found_by_its_name_in_either_language() -> None:
    assert names.species_key("Léviator") == "Gyarados"
    assert names.species_key("gyarados") == "Gyarados"
    assert names.species_key("Méga-Dracaufeu X") == "MegaCharizardX"
    assert names.species_key("Pikachu") is None
    i18n.set_language("fr")
    assert "Léviator" in names.all_species()


def test_the_language_is_kept_in_the_config_file(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    config = tmp_path / "trainerlab" / "config.toml"
    monkeypatch.setenv("TRAINERLAB_CONFIG", str(config))
    assert settings.saved_language() is None

    settings.save_language("fr")

    assert config.read_text() == 'language = "fr"\n'
    assert settings.saved_language() == "fr"


def test_a_config_file_with_another_or_no_language_counts_as_none(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    config = tmp_path / "config.toml"
    monkeypatch.setenv("TRAINERLAB_CONFIG", str(config))
    config.write_text('language = "de"\n')
    assert settings.saved_language() is None
    config.write_text("not toml at all [")
    assert settings.saved_language() is None


def test_without_the_variable_the_config_lives_in_the_home_folder(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.delenv("TRAINERLAB_CONFIG", raising=False)
    assert settings.config_path() == Path.home() / ".config" / "trainerlab" / "config.toml"


def test_the_gym_leaders_are_drawn_from_the_seed() -> None:
    assert names.leader(3) == names.leader(3)
    assert {names.leader(seed) for seed in range(200)} == set(names.LEADERS["en"])
    i18n.set_language("fr")
    assert "Ondine" in {names.leader(seed) for seed in range(200)}
