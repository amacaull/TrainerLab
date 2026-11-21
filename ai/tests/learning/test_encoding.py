"""The encoding read back against the view it came from, field by field."""

import numpy as np

from trainerlab import _engine
from trainerlab.bots.roster import BOTS
from trainerlab.game.decisions import Decision, decisions
from trainerlab.learning.encoding import (
    ACTIONS,
    SLOTS,
    action_index,
    action_mask,
    action_of,
    encode,
    field_names,
    pokemon_names,
)


def _decisions() -> list[Decision]:
    return [
        d
        for a, b, seeds in (
            ("calculator", "survivor", range(97000, 97006)),
            ("random", "dodger", range(97100, 97106)),
        )
        for d in decisions(BOTS[a], BOTS[b], seeds=seeds)
    ]


def test_the_columns_have_names_and_the_arrays_their_shapes() -> None:
    names, field = pokemon_names(), field_names()
    assert len(set(names)) == len(names) == 163  # 153, then the 10 matchup columns (10.2)
    assert len(set(field)) == len(field) == 17
    view = _decisions()[0].view
    encoded = encode(view, 0)
    assert encoded.pokemon.shape == (SLOTS, len(names)) and encoded.pokemon.dtype == np.float32
    assert encoded.ids.shape == (SLOTS, 7) and encoded.ids.dtype == np.int32
    assert encoded.field.shape == (len(field),)


def test_every_pokemon_reads_as_the_view_shows_it() -> None:
    col = {name: i for i, name in enumerate(pokemon_names())}
    for d in _decisions():
        encoded, view = encode(d.view, d.side), d.view
        for slot in range(SLOTS):
            side = d.side if slot < 6 else 1 - d.side
            index = slot % 6
            row, ids = encoded.pokemon[slot], encoded.ids[slot]
            if index >= view.team_size[side]:
                assert not row.any() and not ids.any()
                continue
            pokemon = view.pokemon(side, index)
            assert row[col["present"]] == 1
            if pokemon.is_empty():  # a foe not seen yet
                assert side != d.side
                assert row.sum() == 1 and not ids.any()
                continue
            assert row[col["active"]] == (index == view.active_index[side])
            assert row[col["fainted"]] == pokemon.is_fainted()
            assert row[col["hp"]] == np.float32(pokemon.current_hp / pokemon.stats.hp)
            assert row[col["stat_spe"]] == np.float32(pokemon.stats.speed / 500)
            assert row[col["stage_atk"]] == np.float32(pokemon.stat_stages[0] / 6)
            statuses = [c for c in col if c.startswith("status_")]
            assert sum(row[col[c]] for c in statuses) == (pokemon.status != 0)
            types = sum(row[col[c]] for c in col if c.startswith("type_"))
            entry = _engine.species_entry(pokemon.species_id)
            assert types == (1 if entry.type1 == entry.type2 else 2)
            assert ids[0] == pokemon.species_id + 1
            assert list(ids[3:]) == [m + 1 for m in pokemon.move_ids]
            for m, move_id in enumerate(pokemon.move_ids):
                move = _engine.move_entry(move_id)
                assert row[col[f"move{m + 1}_power"]] == np.float32(move.power / 150)
                assert row[col[f"move{m + 1}_pp"]] == np.float32(pokemon.pp[m] / move.pp)


def test_the_field_reads_as_the_view_shows_it() -> None:
    col = {name: i for i, name in enumerate(field_names())}
    weathers = [c for c in col if c.startswith("weather_")]
    for d in _decisions():
        field = encode(d.view, d.side).field
        assert sum(field[col[w]] for w in weathers) == (d.view.weather != 0)
        if d.view.weather:
            assert field[col[weathers[d.view.weather - 1]]] == 1
        for prefix, side in (("my_", d.side), ("foe_", 1 - d.side)):
            hazards = d.view.hazards(side)
            assert field[col[prefix + "stealth_rock"]] == hazards.stealth_rock
            assert field[col[prefix + "spikes"]] == np.float32(hazards.spikes / 3)


def test_the_mask_marks_exactly_the_legal_options() -> None:
    for d in _decisions():
        if d.kind != "action":
            continue
        legal = [a for a in d.legal if isinstance(a, _engine.Action)]
        mask = action_mask(legal)
        assert mask.shape == (ACTIONS,)
        assert sorted(np.flatnonzero(mask)) == sorted(action_index(a) for a in legal)


def test_an_option_and_its_place_go_both_ways() -> None:
    for index in range(ACTIONS):
        assert action_index(action_of(index)) == index
    assert action_of(5) == _engine.Action.switch(1)
