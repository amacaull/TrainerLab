"""`trainerlab distill`: a network with attention learns what a network's search chooses.

trainerlab distill ai/src/trainerlab/rl/models/alphazero.pt --out runs/attention.pt

The teacher's search plays until it has left --rows decisions, saved to --data; a second run with
the same --data learns from them again without playing. Then the student learns them, pass by
pass, and is saved to --out, ready for trainerlab alphazero runs/attention.pt (palier 10).
"""

import argparse
import time
from pathlib import Path
from typing import Any

import numpy as np

from trainerlab.commands.console import Write, stdout
from trainerlab.experiments.parallel import cores


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab distill",
        description="Teach a network with attention what a network's search chooses.",
    )
    parser.add_argument("teacher", type=Path, help="the network whose search is learned")
    parser.add_argument("--out", type=Path, default=Path("attention.pt"), help="the student")
    parser.add_argument(
        "--data", type=Path, default=Path("distill.npz"), help="the decisions, kept"
    )
    parser.add_argument("--rows", type=int, default=None, help="decisions to learn (500000)")
    parser.add_argument("--layers", type=int, default=None, help="of attention (2)")
    parser.add_argument("--epochs", type=int, default=None, help="passes (8)")
    parser.add_argument("--threads", type=int, default=cores(), help="default: every core")
    args = parser.parse_args(argv)
    try:
        from trainerlab.rl import distill, train
        from trainerlab.rl.net import PolicyValueNet
    except ImportError:
        write('trainerlab distill needs PyTorch: pip install -e "ai/.[rl]"\n')
        return 2

    start = time.monotonic()

    def minutes() -> float:
        return (time.monotonic() - start) / 60

    if args.data.is_file():
        with np.load(args.data) as saved:
            rows = {key: saved[key] for key in saved.files}
        write(f"{len(rows['result'])} decisions from {args.data}\n")
    else:
        wanted = args.rows or distill.ROWS

        def played(round_: int, total: int) -> None:
            write(f"round {round_}: {total} decisions of {wanted}, {minutes():.1f} min\n")

        rows = distill.generate(
            PolicyValueNet.load(args.teacher),
            wanted,
            threads=args.threads,
            report=played,
            **distill.GENERATION,
        )
        kept: dict[str, Any] = dict(rows)
        np.savez(args.data, **kept)
        write(f"saved {len(rows['result'])} decisions to {args.data}\n")

    student = distill.student(distill.LAYERS if args.layers is None else args.layers)
    size = sum(p.numel() for p in student.parameters())
    write(f"learning on {train.device()}: {student.shape.layers} layers, {size} parameters\n")

    def taught(e: "distill.Epoch") -> None:
        write(
            f"pass {e.epoch}: policy loss {e.policy:.3f}, value loss {e.value:.3f}; held out "
            f"{e.held_policy:.3f}, {e.held_value:.3f}; {minutes():.1f} min\n"
        )

    distill.teach(student, rows, epochs=args.epochs or distill.EPOCHS, report=taught).save(args.out)
    write(f"saved {args.out}\n")
    return 0
