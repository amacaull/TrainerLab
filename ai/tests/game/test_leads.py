import math

import pytest

from trainerlab.game import leads
from trainerlab.game.building import species


def test_every_species_has_a_shipped_lead_score() -> None:
    assert set(leads.lead_scores()) == set(species())


def test_a_foe_leads_with_a_species_as_often_as_the_odds_of_its_lead_score(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    scores = {"Toxapex": 0.75, "Snorlax": 0.25, "Ditto": math.nan}
    monkeypatch.setattr(leads, "lead_scores", lambda: scores)

    chances = leads.foe_leads()

    # Odds 3, 1/3, and 1 for a species never seen to lead.
    total = 3 + 1 / 3 + 1
    assert chances == pytest.approx(
        {"Toxapex": 3 / total, "Snorlax": 1 / 3 / total, "Ditto": 1 / total}
    )
