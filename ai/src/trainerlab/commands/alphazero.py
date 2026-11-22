"""`trainerlab alphazero`: improves a network with its own search, as AlphaZero (needs [rl]).

trainerlab alphazero selfplay.pt --iterations 20 --out runs/alphazero.pt

After each iteration, the network is saved to --out and to a copy per iteration
(runs/alphazero-3.pt...). Every --follow iterations and after the last, the search it guides is
measured against the network it started from (or --against) on a few pairs of held-out seeds
(160000 and up), to follow the training; the measurement that counts comes after, with
trainerlab duel plan:runs/alphazero.pt alphazero --pairs 2000.
"""

import argparse
import shutil
import time
from pathlib import Path

from trainerlab.bots.roster import bot_spec
from trainerlab.commands.console import stdout
from trainerlab.experiments.parallel import cores
from trainerlab.experiments.tournament import duel
from trainerlab.terminal.screen import Write

FOLLOW_SEEDS = 160000


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab alphazero",
        description="Improve a network with its own search: the AlphaZero loop.",
    )
    parser.add_argument("start", type=Path, help="the network to start from (rule 10's)")
    parser.add_argument("--iterations", type=int, default=20, help="default: 20")
    parser.add_argument("--games", type=int, default=None, help="battles per iteration (960)")
    parser.add_argument(
        "--out", type=Path, default=Path("alphazero.pt"), help="default: alphazero.pt"
    )
    parser.add_argument("--pairs", type=int, default=100, help="pairs per measure (100)")
    parser.add_argument("--follow", type=int, default=5, help="measure every N iterations (5)")
    parser.add_argument(
        "--against", type=bot_spec, default=None, help="measured against (the start network)"
    )
    parser.add_argument("--threads", type=int, default=cores(), help="default: every core")
    parser.add_argument("--simulations", type=int, default=None, help="per full search (800)")
    parser.add_argument("--fast", type=int, default=None, help="per fast search (100)")
    args = parser.parse_args(argv)
    try:
        from trainerlab.rl import alphazero, train
        from trainerlab.rl.net import PolicyValueNet
    except ImportError:
        write('trainerlab alphazero needs PyTorch: pip install -e "ai/.[rl]"\n')
        return 2

    against = args.against or f"plan:{args.start}"
    on = train.device()
    write(f"from {args.start}, learning on {on}, playing on {args.threads} threads\n")
    start = time.monotonic()

    def report(progress: alphazero.Progress, net: PolicyValueNet) -> None:
        elapsed = time.monotonic() - start
        write(
            f"iteration {progress.iteration}: {progress.games} battles, {progress.from_endgames} "
            f"from endgames, {progress.small_teams} with small teams, {progress.decisions} "
            f"decisions, {progress.turns:.1f} turns, draws {progress.draws:.3f}, entropy "
            f"{progress.entropy:.3f}, fresh {progress.fresh_policy:.3f}, "
            f"{progress.fresh_value:.3f}, policy loss "
            f"{progress.policy_loss:.3f}, value loss {progress.value_loss:.3f}, "
            f"{elapsed / 60:.1f} min\n"
        )
        net.save(args.out)
        copy = args.out.with_stem(f"{args.out.stem}-{progress.iteration}")
        shutil.copyfile(args.out, copy)
        if progress.iteration % args.follow and progress.iteration != args.iterations:
            return
        seeds = range(FOLLOW_SEEDS, FOLLOW_SEEDS + args.pairs)
        result = duel(f"plan:{copy}", against, seeds, jobs=cores())
        write(f"  against {against}: {result.score:.3f} ({result.games} battles) -> {copy}\n")

    given = (("games", args.games), ("simulations", args.simulations), ("fast", args.fast))
    kwargs = {k: v for k, v in given if v is not None}
    alphazero.loop(
        PolicyValueNet.load(args.start),
        args.iterations,
        threads=args.threads,
        on=on,
        report=report,
        **kwargs,
    )
    return 0
