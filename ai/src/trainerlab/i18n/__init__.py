"""The words of the interface, in English or in French.

Every text a player reads goes through `t`: its key, in the language chosen, with its fields
filled. The training commands and their logs stay in English (docs/), as do the commands' names.
"""

from trainerlab.i18n.messages import MESSAGES

LANGUAGES = ("en", "fr")
_current = ["en"]


def language() -> str:
    return _current[0]


def set_language(chosen: str) -> None:
    if chosen not in LANGUAGES:
        raise ValueError(f"unknown language {chosen!r} (en or fr)")
    _current[0] = chosen


def t(key: str, **fields: object) -> str:
    """The message `key` in the language chosen, its {fields} filled."""
    return MESSAGES[key][LANGUAGES.index(_current[0])].format(**fields)
