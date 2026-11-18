"""Many battles at once, for self-play: the environment of palier 6, in C++.

Both sides of every battle are played by the caller. At each step every battle waits for a
decision: on a turn, both sides choose among their legal options; when a fainted Pokemon must be
replaced, only that side chooses (the other side's mask is all 0, its action ignored). A battle
that ends gives +1 to the winner, -1 to the loser (0 each on a draw) and starts again at once with
new teams. Options are numbered as in learning/encoding.py: moves 0-3, then switches 4-9.
"""

from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

from trainerlab import _engine, _search
from trainerlab.game.environment import DATA_DIR


@dataclass(frozen=True)
class Observation:
    pokemon: npt.NDArray[np.float32]  # battles x 2 sides x 12 slots x P
    ids: npt.NDArray[np.int32]  # battles x 2 x 12 x 7
    field: npt.NDArray[np.float32]  # battles x 2 x F
    mask: npt.NDArray[np.float32]  # battles x 2 x 10: 1 for each legal option


class VecEnv:
    def __init__(self, battles: int, seed: int, max_turns: int = 1000) -> None:
        _engine.engine_init(str(DATA_DIR))
        _search.init(str(DATA_DIR))
        self._env = _search.VecEnv(battles, seed, max_turns)

    @property
    def battles(self) -> int:
        return self._env.count

    @property
    def finished(self) -> int:
        """Battles finished since the start."""
        return self._env.finished

    def observe(self) -> Observation:
        return Observation(*self._env.observe())

    def step(
        self, actions: npt.NDArray[np.integer]
    ) -> tuple[npt.NDArray[np.float32], npt.NDArray[np.bool_]]:
        """Plays actions[battles, 2]; returns rewards[battles, 2] and done[battles]."""
        return self._env.step(actions)


def random_actions(
    mask: npt.NDArray[np.float32], rng: np.random.Generator
) -> npt.NDArray[np.int32]:
    """A legal option at random for every battle and side (0 where a side has none)."""
    scores = rng.random(mask.shape) * mask
    return np.argmax(scores, axis=-1).astype(np.int32)
