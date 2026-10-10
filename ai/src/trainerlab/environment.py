"""The referee of a battle: owns the real state
and enforces the rules of the game."""


class BattleEnvironment:
    def __init__(self, team0: list[str], team1: list[str], *, seed: int) -> None:
        pass

    def is_over(self) -> bool:
        return False
