"""The results of the measures, as rich renderables: a duel's verdict, how a bot plays, its
mistakes, the list of bots. Plain numbers in, so that the terminal knows no experiment."""

from collections.abc import Mapping, Sequence

from rich.console import Group, RenderableType
from rich.panel import Panel
from rich.table import Table
from rich.text import Text

from trainerlab.i18n import t

BAR = 10  # characters of the little bars of `usage`


def _panel(title: str, body: RenderableType, style: str = "none") -> Panel:
    return Panel(body, title=title, title_align="left", border_style=style, expand=False)


def duel_header(a: str, b: str, seeds: range) -> Panel:
    lines = [
        t("duel.against", a=f"[bold cyan]{a}[/]", b=f"[bold red]{b}[/]"),
        t("duel.pairs", pairs=len(seeds), first=seeds[0], last=seeds[-1]),
    ]
    return _panel(t("duel.title"), "\n".join(lines))


def duel_result(
    a: str,
    b: str,
    record: tuple[int, int, int],
    score: float,
    interval: tuple[float, float],
    elapsed: str,
) -> Panel:
    """The verdict follows the interval: above 0.5, A beats B; below, it loses; across, even."""
    wins, draws, losses = record
    low, high = interval
    if low > 0.5:
        verdict, colour = t("duel.beats", a=a, b=b), "green"
    elif high < 0.5:
        verdict, colour = t("duel.loses", a=a, b=b), "red"
    else:
        verdict, colour = t("duel.even", a=a, b=b), "yellow"
    grid = Table.grid(padding=(0, 4))
    grid.add_column()
    grid.add_column()
    grid.add_row(t("duel.score"), f"[bold]{score:.3f}[/]")
    grid.add_row(t("duel.interval"), t("duel.interval_value", low=low, high=high))
    body = Group(
        Text(verdict, style=f"bold {colour}"),
        "",
        grid,
        t("duel.record", wins=wins, losses=losses, draws=draws),
        "",
        Text(t("duel.time", battles=wins + draws + losses, time=elapsed), style="dim"),
    )
    return _panel(t("duel.result"), body, colour)


def _share(part: int, whole: int) -> str:
    return f"{100 * part / whole:.1f} %" if whole else "-"


def usage_report(
    a: str,
    b: str,
    pairs: int,
    turns: int,
    kinds: Sequence[str],
    available: Mapping[str, int],
    played: Mapping[str, int],
) -> Group:
    header = _panel(t("usage.title", bot=a), t("usage.subtitle", b=b, pairs=pairs, turns=turns))
    table = Table(box=None, padding=(0, 2), header_style="bold")
    table.add_column(t("usage.kind"))
    for title in ("usage.available", "usage.played", "usage.when"):
        table.add_column(t(title), justify="right")
    table.add_column("")
    for kind in kinds:
        when = played[kind] / available[kind] if available[kind] else 0.0
        table.add_row(
            t(f"kind.{kind}"),
            _share(available[kind], turns),
            _share(played[kind], turns),
            _share(played[kind], available[kind]),
            "[cyan]" + "█" * round(BAR * when) + "[/]",
        )
    return Group(header, table)


def audit_report(
    a: str,
    b: str,
    pairs: int,
    battles: Mapping[str, int],
    decisions: int,
    rows: Sequence[tuple[str, float, int, int, bool]],
    boosts: int,
    boost_shares: Mapping[str, int],
) -> Group:
    """`rows`: each check, its rate per 1000 decisions, the lost and won battles where it came,
    and whether it is a strategic one."""
    won, drawn, lost = battles["win"], battles["draw"], battles["loss"]
    header = _panel(
        t("audit.title", bot=a),
        t("audit.subtitle", b=b, pairs=pairs, battles=won + drawn + lost)
        + "\n"
        + t("audit.record", won=won, drawn=drawn, lost=lost, decisions=decisions),
    )
    table = Table(box=None, padding=(0, 2), header_style="bold")
    table.add_column(t("audit.mistake"))
    for title in ("audit.rate", "audit.in_lost", "audit.in_won"):
        table.add_column(t(title), justify="right")
    strategic_shown = False
    for name, rate, in_lost, in_won, strategic in rows:
        if strategic and not strategic_shown:
            table.add_row(f"[dim]── {t('audit.strategic')} ──[/]", "", "", "")
            strategic_shown = True
        lost_share, won_share = (in_lost / lost if lost else 0.0), (in_won / won if won else 0.0)
        style = "red" if lost_share > 1.5 * won_share and in_lost else ""
        table.add_row(
            t(f"check.{name}"),
            f"{rate:.1f}",
            Text(_share(in_lost, lost), style=style),
            _share(in_won, won),
        )
    shares = {name: _share(count, boosts) for name, count in boost_shares.items()}
    footer = t(
        "audit.boosts",
        boosts=boosts,
        wasted=shares["boost-wasted"],
        phazed=shares["boost-into-phazer"],
        ko=shares["boost-into-ko"],
    )
    return Group(header, table, "", footer)


def bots_table(rules: Mapping[str, int | None], best: str) -> Panel:
    table = Table(box=None, padding=(0, 2), header_style="bold")
    table.add_column(t("bots.rule"), justify="right")
    table.add_column(t("bots.bot"), style="cyan")
    table.add_column(t("bots.what"))
    sections = {1: "bots.hand", 6: "bots.search", 10: "bots.networks"}
    for name, rule in rules.items():
        if rule in sections:
            table.add_row("", "", f"[dim]── {t(sections[rule])} ──[/]")
        style = "bold" if name == best else ""
        table.add_row("" if rule is None else str(rule), name, Text(t(f"bot.{name}"), style=style))
    return _panel(t("bots.title"), table)
