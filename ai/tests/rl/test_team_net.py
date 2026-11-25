from pathlib import Path

import numpy as np
import pytest
import torch

from trainerlab import _engine
from trainerlab.game.environment import DATA_DIR
from trainerlab.rl.team_net import (
    PAD,
    Additive,
    TeamNet,
    TraitNet,
    fit,
    load,
    log_loss,
    traits,
    win_probability,
)


def _teams(rows: list[list[int]]) -> torch.Tensor:
    """Teams as the models read them: each species' engine id, PAD after the last one."""
    species = torch.full((len(rows), 6), PAD, dtype=torch.long)
    for r, row in enumerate(rows):
        species[r, : len(row)] = torch.tensor(row)
    return species


@pytest.mark.parametrize("kind", [TeamNet, Additive, TraitNet])
def test_a_team_s_strength_ignores_the_order_of_its_pokemon(kind: type) -> None:
    torch.manual_seed(0)
    model = kind()
    first = model(_teams([[3, 7, 11, 2]]))
    again = model(_teams([[11, 2, 7, 3]]))

    assert first.shape == (1,)
    assert torch.allclose(first, again, atol=1e-5)


def test_the_chance_to_win_is_the_logistic_of_the_difference_of_strengths() -> None:
    a, b = torch.tensor([1.0, 0.0]), torch.tensor([0.0, 0.0])
    assert win_probability(a, b).tolist() == pytest.approx([1 / (1 + np.exp(-1)), 0.5])
    assert log_loss(torch.tensor([0.5]), torch.tensor([1.0])) == pytest.approx(np.log(2))


def _synthetic(count: int, seed: int) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
    """Battles of teams of two whose winner the strengths decide: species k is worth k / 10,
    and species 0 with species 1 are worth 3 more together."""
    rng = np.random.default_rng(seed)
    a = rng.choice(12, size=(count, 2), replace=True)
    b = rng.choice(12, size=(count, 2), replace=True)

    def worth(t: np.ndarray) -> np.ndarray:
        both = ((t == 0).any(axis=1) & (t == 1).any(axis=1)).astype(float)
        return np.asarray(t.sum(axis=1) / 10 + 3 * both)

    p = 1 / (1 + np.exp(-(worth(a) - worth(b))))
    y = torch.tensor((rng.random(count) < p).astype(np.float32))
    return _teams(a.tolist()), _teams(b.tolist()), y


def test_attention_learns_what_a_sum_of_species_cannot() -> None:
    torch.manual_seed(1)
    train, test = _synthetic(6000, 0), _synthetic(2000, 1)
    attention, additive = TeamNet(), Additive()

    losses = fit(attention, *train, epochs=8, batch=256, rate=3e-3)
    fit(additive, *train, epochs=8, batch=256, rate=3e-2)

    assert losses[-1] < losses[0]

    def held_out(model: torch.nn.Module) -> float:
        with torch.no_grad():
            p = win_probability(model(test[0]), model(test[1]))
        return log_loss(p, test[2])

    assert held_out(attention) < held_out(additive)


def test_a_model_is_kept_and_read_again(tmp_path: Path) -> None:
    torch.manual_seed(2)
    teams = _teams([[1, 2], [5, 9, 30, 4, 22, 17]])
    for model in (TeamNet(), Additive(), TraitNet()):
        path = tmp_path / "m.pt"
        model.save(path)
        assert torch.allclose(load(path)(teams), model(teams))


def test_each_species_is_read_from_what_it_is_not_from_its_name() -> None:
    _engine.engine_init(str(DATA_DIR))
    table = traits()
    toxapex = table[_engine.find_species_id("Toxapex")]
    zoroark = table[_engine.find_species_id("ZoroarkHisui")]

    assert table.shape == (PAD + 1, 201)
    assert not table[PAD].any()  # an empty place reads nothing
    assert len({tuple(row.tolist()) for row in table[:PAD]}) == PAD  # no two species alike
    assert toxapex[7] == 1 and toxapex[2] == 1  # Poison and Water
    assert toxapex[18:24].tolist() == pytest.approx(
        [50 / 255, 63 / 255, 152 / 255, 53 / 255, 142 / 255, 35 / 255]
    )
    assert toxapex[24:69].sum() == 1 and zoroark[24:69].sum() == 0  # its ability, or none
    assert toxapex[69:83].sum() == 1  # its item, or none
    assert toxapex[83:178].sum() == 4  # its four moves
    assert toxapex[178:196].sum() >= 1 and toxapex[196:199].sum() == 4  # their types, categories
    assert table[_engine.find_species_id("MegaRayquaza"), 199:201].tolist() == [1, 1]


def test_weight_decay_shrinks_what_is_learned() -> None:
    torch.manual_seed(3)
    a, b, y = _synthetic(2000, 2)
    free, held = Additive(), Additive()
    fit(free, a, b, y, epochs=3, batch=256, rate=3e-2)
    fit(held, a, b, y, epochs=3, batch=256, rate=3e-2, weight_decay=0.5)
    assert held.weights.abs().sum() < free.weights.abs().sum()
