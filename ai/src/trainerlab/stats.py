"""What a battle comes down to, for the summary at its end: who knocked out whom, who dealt
the most damage. Built from the narrated history, so damage is counted as really lost."""

from trainerlab import _engine
from trainerlab.render import Narration

MOVE_USED, DAMAGE, FAINTED = 0, 1, 2


class BattleStats:
    def __init__(self) -> None:
        self._actor: tuple[int, int] | None = None
        self.damage: dict[tuple[int, int], int] = {}
        self.knockouts: dict[tuple[int, int], int] = {}
        self.fainted = [0, 0]

    def record(self, lines: list[Narration]) -> None:
        for line in lines:
            actor = self._actor
            hit_by_foe = actor is not None and actor[0] != line.side
            if line.kind == MOVE_USED:
                self._actor = (line.side, line.slot)
            elif line.kind == DAMAGE and actor is not None and hit_by_foe:
                self.damage[actor] = self.damage.get(actor, 0) + line.amount
            elif line.kind == FAINTED:
                self.fainted[line.side] += 1
                if actor is not None and hit_by_foe:
                    self.knockouts[actor] = self.knockouts.get(actor, 0) + 1

    def summary(self, view: _engine.BattleState, me: int, turns: int) -> list[str]:
        lines = [
            f"Turns played: {turns}",
            f"Pokemon knocked out: {self.fainted[1 - me]} of theirs, {self.fainted[me]} of yours",
        ]
        mine = [key for key in self.damage if key[0] == me]
        if mine:
            best = max(mine, key=lambda key: (self.knockouts.get(key, 0), self.damage[key]))
            name = _engine.species_display_name(view.pokemon(*best).species_id)
            lines.append(
                f"Your MVP: {name} ({self.knockouts.get(best, 0)} KO, {self.damage[best]} HP dealt)"
            )
        return lines
