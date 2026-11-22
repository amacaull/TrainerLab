"""`trainerlab pretrain`: trains the policy and value network by imitation (needs [rl]).

trainerlab pretrain imitate-forecaster-vs-calculator-110000-111999.npz --out net.pt
"""

import argparse
from pathlib import Path

from trainerlab.commands.console import Write, stdout


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab pretrain", description="Train the network by imitation."
    )
    parser.add_argument("datasets", type=Path, nargs="+", help=".npz files made by imitate")
    parser.add_argument("--out", type=Path, default=Path("net.pt"), help="default: net.pt")
    parser.add_argument(
        "--epochs", type=int, default=None, help="passes over the data (default: 8)"
    )
    args = parser.parse_args(argv)
    try:
        from trainerlab.rl import train
    except ImportError:
        write('trainerlab pretrain needs PyTorch: pip install -e "ai/.[rl]"\n')
        return 2

    rows = train.load(args.datasets)
    write(f"{len(rows['action'])} decisions, on {train.device()}\n")

    def report(epoch: int, test: train.Score) -> None:
        write(
            f"pass {epoch}: agreement {test.agreement:.3f}, value error {test.value_error:.3f}, "
            f"winner {test.value_accuracy:.3f} (test)\n"
        )

    epochs = args.epochs or train.EPOCHS
    net, fit, test = train.pretrain(rows, epochs=epochs, report=report)
    for label, s in (("train", fit), ("test", test)):
        write(
            f"{label:<6} agreement {s.agreement:.3f}, value error {s.value_error:.3f}, "
            f"winner {s.value_accuracy:.3f}\n"
        )
    net.save(args.out)
    write(f"-> {args.out}\n")
    return 0
