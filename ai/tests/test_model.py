from pathlib import Path

import numpy as np
import pytest

from trainerlab.model import WinModel, accuracy, brier, is_test, log_loss, train

XOR = np.array([[0.0, 0.0], [0.0, 1.0], [1.0, 0.0], [1.0, 1.0]] * 64)
XOR_WINS = np.array([0.0, 1.0, 1.0, 0.0] * 64)
NAMES = ("a", "b")


def _linear_data(seed: int = 0) -> tuple[np.ndarray, np.ndarray]:
    rng = np.random.default_rng(seed)
    x = rng.normal(size=(2000, 2))
    wins = rng.random(2000) < 1 / (1 + np.exp(-3 * x[:, 0]))
    return x, wins.astype(float)


def test_a_logistic_regression_finds_the_feature_that_decides() -> None:
    x, y = _linear_data()
    model = train(x, y, NAMES, epochs=100, rate=1e-2)

    assert model.kind == "logistic"
    assert model.weights[0].shape == (2, 1)
    assert abs(model.weights[0][0, 0]) > 5 * abs(model.weights[0][1, 0])
    assert accuracy(model.predict(x), y) > 0.8


def test_a_hidden_layer_learns_what_a_logistic_regression_cannot() -> None:
    logistic = train(XOR, XOR_WINS, NAMES, epochs=200, batch=64)
    network = train(XOR, XOR_WINS, NAMES, hidden=(8,), epochs=200, batch=64, rate=1e-2)

    assert network.kind == "network 8"
    assert accuracy(logistic.predict(XOR), XOR_WINS) <= 0.75
    assert accuracy(network.predict(XOR), XOR_WINS) == 1.0


def test_the_same_data_and_seed_give_the_same_model() -> None:
    x, y = _linear_data()
    first = train(x, y, NAMES, hidden=(4,), epochs=2).predict(x)

    assert np.array_equal(first, train(x, y, NAMES, hidden=(4,), epochs=2).predict(x))
    assert not np.array_equal(first, train(x, y, NAMES, hidden=(4,), epochs=2, seed=1).predict(x))


def test_a_constant_feature_does_no_harm() -> None:
    x, y = _linear_data()
    x[:, 1] = 7.0

    p = train(x, y, NAMES, epochs=2).predict(x)

    assert np.all(np.isfinite(p))


def test_a_saved_model_predicts_the_same_once_loaded(tmp_path: Path) -> None:
    x, y = _linear_data()
    model = train(x, y, NAMES, hidden=(4, 3), epochs=1)
    model.save(tmp_path / "m.npz")

    loaded = WinModel.load(tmp_path / "m.npz")

    assert loaded.kind == "network 4-3"
    assert loaded.feature_names == NAMES
    assert np.array_equal(loaded.predict(x), model.predict(x))


def test_the_test_set_is_every_fifth_seed() -> None:
    assert list(is_test(np.arange(11))) == [i % 5 == 0 for i in range(11)]


def test_the_scores_of_a_coin_toss_and_of_a_perfect_guess() -> None:
    y = np.array([1.0, 0.0, 0.5, 1.0])
    coin = np.full(4, 0.5)

    assert log_loss(coin, y) == pytest.approx(np.log(2))
    assert brier(coin, y) == pytest.approx(0.25 * 3 / 4)
    assert log_loss(np.array([1.0, 0.0]), np.array([1.0, 0.0])) < 1e-9
    assert accuracy(np.array([0.9, 0.2, 0.9, 0.4]), y) == pytest.approx(2 / 3)  # draw left out
