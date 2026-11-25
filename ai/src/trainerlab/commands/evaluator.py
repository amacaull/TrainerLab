"""`trainerlab evaluator`: the strength of a team, learned from the measures' battles (12.4).

trainerlab evaluator runs/truth-2.npz runs/truth-6.npz runs/teams-2.npz runs/teams-4.npz \\
    runs/teams-6.npz runs/teams-6b.npz --out runs/evaluator.pt

Three models learn from the same battles, those not held out (experiments/team_value.py): the
additive one, a weight per species and size; the one with attention over the species' names; and
the one with attention over what each species is (rl/team_net.py, the second attempt). Each is
judged on what was held out, file by file; then the last against the additive one, battle by
battle, on each file's held-out battles. The last is kept at --out, the others beside it.
"""

import argparse
from pathlib import Path

import torch
from torch import nn

from trainerlab.commands.console import Write, stdout
from trainerlab.commands.options import count
from trainerlab.experiments.team_value import describe, gap, read, report
from trainerlab.rl.team_net import Additive, TeamNet, TraitNet, fit

EPOCHS = 10
BATCH = 2048
# Fixed before measuring: (learning rate, weight decay).
SETTINGS = {"additive": (1e-2, 0.0), "attention": (1e-3, 0.0), "traits": (1e-3, 1e-4)}


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab evaluator", description="Learn the strength of a team from battles."
    )
    parser.add_argument("files", type=Path, nargs="+", help="files of truth and teams")
    parser.add_argument("--out", type=Path, default=Path("evaluator.pt"))
    parser.add_argument("--epochs", type=count, default=EPOCHS, help=f"({EPOCHS})")
    args = parser.parse_args(argv)

    battles = read(args.files)
    learn = ~battles.test
    write(
        f"{len(battles.y)} battles, {int(learn.sum())} to learn from, "
        f"{int(battles.test.sum())} held out\n"
    )
    a, b = torch.from_numpy(battles.a[learn]), torch.from_numpy(battles.b[learn])
    y = torch.from_numpy(battles.y[learn])
    torch.manual_seed(0)
    models: dict[str, nn.Module] = {}
    for model in (Additive(), TeamNet(), TraitNet()):
        rate, decay = SETTINGS[model.kind]
        losses = fit(model, a, b, y, epochs=args.epochs, batch=BATCH, rate=rate, weight_decay=decay)
        write(f"{model.kind}: loss {losses[0]:.4f} -> {losses[-1]:.4f} on what it learned\n")
        for line in describe(report(model, battles), model.kind):
            write(line)
        model.save(args.out if model.kind == TraitNet.kind else _beside(args.out, model.kind))
        models[model.kind] = model
    for file in battles.files:
        found = gap(models[Additive.kind], models[TraitNet.kind], battles, file)
        write(
            f"traits against additive, held-out battles of {file}: log loss lower by "
            f"{found.mean:+.4f} [{found.low:+.4f}, {found.high:+.4f}] ({found.count} battles)\n"
        )
    write(f"-> {args.out}\n")
    return 0


def _beside(path: Path, kind: str) -> Path:
    return path.with_name(f"{path.stem}-{kind}{path.suffix}")
