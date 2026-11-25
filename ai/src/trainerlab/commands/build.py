"""`trainerlab build`: one team builder against another, the same bot playing both sides.

trainerlab build species:runs/teams-6.npz random --size 6 --pairs 2000 --from 430000
trainerlab build random random --size 2 --core Gyarados --pool 49
trainerlab build net:runs/evaluator.pt species:runs/teams-6b.npz --size 6 --from 525000

Each pair draws from its seed a core of one or two Pokemon for each side (or takes --core for
both), then a pool of --pool species outside them, the same for both sides (12; 49, every
species: free choice); each builder completes both cores from the pool, once each
(game/building.py, experiments/building.py).
"""

import argparse

from trainerlab.bots.roster import bot_spec
from trainerlab.commands.console import Write, stdout
from trainerlab.commands.options import add_jobs, add_pairs, seeds_of
from trainerlab.experiments import parallel
from trainerlab.experiments.building import builder_of, duel
from trainerlab.game.building import POOL
from trainerlab.game.pairs import span
from trainerlab.i18n import names

ALL_SPECIES = 49


def builder(spec: str) -> str:
    try:
        builder_of(spec)
    except (ValueError, OSError) as error:
        raise argparse.ArgumentTypeError(str(error)) from error
    return spec


def species_list(text: str) -> list[str]:
    keys = []
    for name in text.split(","):
        key = names.species_key(name)
        if key is None:
            raise argparse.ArgumentTypeError(f"unknown Pokémon: {name.strip()!r}")
        keys.append(key)
    return keys


def main(argv: list[str] | None = None, write: Write = stdout) -> int:
    parser = argparse.ArgumentParser(
        prog="trainerlab build", description="Measure one team builder against another."
    )
    parser.add_argument(
        "builder_a",
        type=builder,
        metavar="BUILDER_A",
        help="random, or species:FILE.npz (the scores of trainerlab teams)",
    )
    parser.add_argument("builder_b", type=builder, metavar="BUILDER_B", help="the same")
    parser.add_argument(
        "--bot", type=bot_spec, default="alphazero", help="the bot playing both sides (alphazero)"
    )
    parser.add_argument(
        "--size", type=int, choices=range(2, 7), default=6, help="Pokemon per team (6)"
    )
    parser.add_argument(
        "--pool",
        type=int,
        default=POOL,
        help="species to pick from, the same for both sides (12; 49: free choice)",
    )
    parser.add_argument(
        "--core", type=species_list, help="both sides' core, by names (else drawn from each seed)"
    )
    add_pairs(parser)
    add_jobs(parser)
    args = parser.parse_args(argv)
    if args.core is not None and len(args.core) >= args.size:
        parser.error(f"a core holds fewer than {args.size} Pokemon")
    if args.pool < args.size:
        parser.error(f"teams of {args.size} need a pool of {args.size} species at least")

    seeds = seeds_of(args)
    result = duel(
        args.builder_a,
        args.builder_b,
        seeds,
        args.jobs,
        bot=args.bot,
        size=args.size,
        core=args.core,
        pool=args.pool,
        progress=parallel.progress_on_terminal(len(seeds)),
    )
    low, high = result.interval
    core = f", core {','.join(args.core)}" if args.core else ""
    pool = "free choice" if args.pool >= ALL_SPECIES else f"from pools of {args.pool}"
    write(
        f"{args.builder_a} vs {args.builder_b}, teams of {args.size}, {pool}, played by "
        f"{args.bot}{core}: "
        f"{result.score:.3f} [{low:.3f}, {high:.3f}] ({result.wins} wins, {result.draws} draws, "
        f"{result.losses} losses, seeds {span(seeds)})\n"
    )
    return 0
