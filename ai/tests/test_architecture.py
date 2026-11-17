"""The dependencies between the domains go one way: a domain imports only those below it."""

import ast
from pathlib import Path

import pytest

PACKAGE = Path(__file__).resolve().parents[1] / "src" / "trainerlab"

# What each domain may import from the package, besides itself and the engine binding.
ALLOWED = {
    "game": set(),
    "learning": {"game"},
    "bots": {"game", "learning"},
    "experiments": {"game", "learning", "bots"},
    "terminal": {"game"},
    "commands": {"game", "learning", "bots", "experiments", "terminal"},
}


def _domains_imported(module: Path) -> set[str]:
    domains = set()
    for node in ast.walk(ast.parse(module.read_text())):
        if isinstance(node, ast.ImportFrom) and node.module:
            names = [node.module]
        elif isinstance(node, ast.Import):
            names = [alias.name for alias in node.names]
        else:
            continue
        for name in names:
            parts = name.split(".")
            if parts[0] == "trainerlab" and len(parts) > 1:
                domains.add(parts[1])
            elif name == "trainerlab" and isinstance(node, ast.ImportFrom):
                domains.update(alias.name for alias in node.names)
    return domains - {"_engine"}


@pytest.mark.parametrize("domain", sorted(ALLOWED))
def test_a_domain_imports_only_the_domains_below_it(domain: str) -> None:
    for module in sorted((PACKAGE / domain).rglob("*.py")):
        stray = _domains_imported(module) - ALLOWED[domain] - {domain}
        assert not stray, f"{module.relative_to(PACKAGE)} imports {sorted(stray)}"


def test_every_module_belongs_to_a_domain() -> None:
    loose = [p.name for p in PACKAGE.glob("*.py") if p.name != "__init__.py"]
    folders = {p.name for p in PACKAGE.iterdir() if p.is_dir() and not p.name.startswith("_")}

    assert loose == []
    assert folders - {"__pycache__"} == set(ALLOWED)
