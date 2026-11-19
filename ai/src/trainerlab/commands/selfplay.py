"""`trainerlab selfplay`: improves a network by playing against itself, with PPO (needs [rl]).

trainerlab selfplay net.pt --updates 1000 --out selfplay.pt

Every `--every` updates, the network so far is saved to --out and measured against forecaster on
a few pairs of held-out seeds (150000 and up), to follow the training; the measurement that
counts comes after, with trainerlab duel.
"""

import argparse
import time
from pathlib import Path

from trainerlab.commands.console import stdout
from trainerlab.experiments.parallel import cores
from trainerlab.experiments.tournament import duel
from trainerlab.terminal.screen import Write

FOLLOW_SEEDS = 150000


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab selfplay", description="Improve a network by self-play (PPO)."
    )
    parser.add_argument("start", type=Path, help="the network to start from (trainerlab pretrain)")
    parser.add_argument("--updates", type=int, default=1000, help="rollouts (default: 1000)")
    parser.add_argument(
        "--out", type=Path, default=Path("selfplay.pt"), help="default: selfplay.pt"
    )
    parser.add_argument("--every", type=int, default=25, help="updates between measures (25)")
    parser.add_argument("--pairs", type=int, default=200, help="pairs per measure (200)")
    parser.add_argument("--battles", type=int, default=None, help="battles at once (256)")
    parser.add_argument("--steps", type=int, default=None, help="decisions per rollout (128)")
    args = parser.parse_args(argv)
    try:
        from trainerlab.rl import ppo, train
        from trainerlab.rl.net import PolicyValueNet
    except ImportError:
        write('trainerlab selfplay needs PyTorch: pip install -e "ai/.[rl]"\n')
        return 2

    on = train.device()
    write(f"from {args.start}, on {on}\n")
    start = time.monotonic()

    def report(progress: ppo.Progress, net: PolicyValueNet) -> None:
        elapsed = time.monotonic() - start
        write(
            f"update {progress.update}: {progress.decisions} decisions, {progress.battles} "
            f"battles, wins {progress.win_rate:.3f} against itself or its past, entropy "
            f"{progress.entropy:.3f}, {elapsed / 60:.1f} min\n"
        )
        if progress.update % args.every == 0 or progress.update == args.updates:
            net.save(args.out)
            seeds = range(FOLLOW_SEEDS, FOLLOW_SEEDS + args.pairs)
            result = duel(f"net:{args.out}", "forecaster", seeds, jobs=cores())
            write(
                f"  against forecaster: {result.score:.3f} ({result.games} battles) -> {args.out}\n"
            )

    kwargs = {k: v for k, v in (("battles", args.battles), ("steps", args.steps)) if v}
    ppo.selfplay(PolicyValueNet.load(args.start), args.updates, on=on, report=report, **kwargs)
    return 0
