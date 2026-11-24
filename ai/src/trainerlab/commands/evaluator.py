"""`trainerlab evaluator`: the strength of a team, learned from the measures' battles (12.4).

trainerlab evaluator runs/truth-2.npz runs/truth-6.npz runs/teams-2.npz runs/teams-4.npz \\
    runs/teams-6.npz --out runs/evaluator.pt

Two models learn from the same battles, those not held out (experiments/team_value.py): the
additive one, a weight per species and size, and the one with attention (rl/team_net.py). Each is
then judged on what was held out; both are kept (--out, and its -additive twin).
"""

import argparse
from pathlib import Path

import torch

from trainerlab.commands.console import Write, stdout
from trainerlab.commands.options import count
from trainerlab.experiments.team_value import describe, read, report
from trainerlab.rl.team_net import Additive, TeamNet, fit

EPOCHS = 10
BATCH = 2048
RATES = {"additive": 1e-2, "attention": 1e-3}  # fixed before measuring


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
    for model in (Additive(), TeamNet()):
        losses = fit(model, a, b, y, epochs=args.epochs, batch=BATCH, rate=RATES[model.kind])
        write(f"{model.kind}: loss {losses[0]:.4f} -> {losses[-1]:.4f} on what it learned\n")
        for line in describe(report(model, battles), model.kind):
            write(line)
        model.save(args.out if model.kind == "attention" else _twin(args.out))
    write(f"-> {args.out}\n")
    return 0


def _twin(path: Path) -> Path:
    return path.with_name(f"{path.stem}-additive{path.suffix}")
