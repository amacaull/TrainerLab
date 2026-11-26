"""What a bot expects of the foe's lead, before the battle shows it (palier 13).

The foe's team is unknown. A species leads it as often as the odds of its lead score: the share
of points of the random teams it led in `teams-6b` (experiments/teams.py), shipped in
lead-strength.json, written by `trainerlab teams --load runs/teams-6b.npz --leads ...`. A sensible
foe leads more often with a lead that wins.
"""

import json
import math
from functools import cache
from pathlib import Path

LEADS = Path(__file__).parent / "lead-strength.json"


@cache
def lead_scores() -> dict[str, float]:
    """Each species' shipped lead score."""
    scores: dict[str, float] = json.loads(LEADS.read_text())
    return scores


def foe_leads() -> dict[str, float]:
    """The chance that the foe leads with each species: proportional to the odds of its lead
    score, 1 for a species never seen to lead."""
    odds = {
        name: 1.0 if math.isnan(score) else score / (1 - score)
        for name, score in lead_scores().items()
    }
    total = sum(odds.values())
    return {name: value / total for name, value in odds.items()}
