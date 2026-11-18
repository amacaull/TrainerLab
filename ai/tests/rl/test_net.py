from pathlib import Path

import numpy as np
import pytest
import torch

from trainerlab.learning.encoding import ACTIONS, SLOTS
from trainerlab.learning.env import VecEnv
from trainerlab.rl.net import MASKED, PolicyValueNet, Shape


def _batch(n: int = 6) -> tuple[torch.Tensor, ...]:
    obs = VecEnv(n, seed=1).observe()
    return (
        torch.from_numpy(obs.pokemon[:, 0]),
        torch.from_numpy(obs.ids[:, 0]).long(),
        torch.from_numpy(obs.field[:, 0]),
        torch.from_numpy(obs.mask[:, 0]),
    )


def test_one_logit_per_option_and_a_value_between_minus_one_and_one() -> None:
    torch.manual_seed(0)
    net = PolicyValueNet(Shape.for_catalog())
    pokemon, ids, field, mask = _batch()

    logits, value = net(pokemon, ids, field, mask)

    assert logits.shape == (6, ACTIONS) and value.shape == (6,)
    assert ((value > -1) & (value < 1)).all()
    assert (logits[mask == 0] == MASKED).all()
    assert (logits[mask == 1] > MASKED).all()
    assert pokemon.shape[1] == SLOTS


def test_a_saved_network_loads_and_plays_the_same(tmp_path: Path) -> None:
    torch.manual_seed(1)
    net = PolicyValueNet(Shape.for_catalog())
    net.eval()
    batch = _batch()
    net.save(tmp_path / "net.pt")

    loaded = PolicyValueNet.load(tmp_path / "net.pt")

    with torch.no_grad():
        for a, b in zip(net(*batch), loaded(*batch), strict=True):
            assert torch.equal(a, b)
    assert loaded.shape == net.shape


def test_the_switches_read_the_pokemon_they_would_send_in() -> None:
    # Two benches swapped: the logits of the two switches swap with them.
    torch.manual_seed(2)
    net = PolicyValueNet(Shape.for_catalog())
    pokemon, ids, field, mask = (t[:1].clone() for t in _batch())
    mask[:] = 1
    swapped = [t.clone() for t in (pokemon, ids)]
    for t in swapped:
        t[0, [4, 5]] = t[0, [5, 4]]

    with torch.no_grad():
        before, _ = net(pokemon, ids, field, mask)
        after, _ = net(swapped[0], swapped[1], field, mask)

    assert after[0, 8] == pytest.approx(float(before[0, 9]), abs=1e-5)
    assert after[0, 9] == pytest.approx(float(before[0, 8]), abs=1e-5)
    assert np.allclose(after[0, :4].numpy(), before[0, :4].numpy(), atol=1e-5)
