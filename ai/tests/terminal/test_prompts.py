from typing import Any

import pytest
import questionary

from trainerlab.terminal import prompts
from trainerlab.terminal.prompts import Line, Option, QuestionaryPrompter, Separator


class _Asked:
    """What a questionary prompt was built with, and the answer it gives."""

    def __init__(self, answer: Any, calls: list[tuple[str, tuple[Any, ...], dict[str, Any]]]):
        self.answer, self.calls = answer, calls

    def __call__(self, kind: str) -> Any:
        def build(*args: Any, **kwargs: Any) -> "_Asked":
            self.calls.append((kind, args, kwargs))
            return self

        return build

    def ask(self) -> Any:
        return self.answer


def _patch(monkeypatch: pytest.MonkeyPatch, answer: Any) -> list[Any]:
    calls: list[Any] = []
    asked = _Asked(answer, calls)
    for kind in ("select", "text", "autocomplete"):
        monkeypatch.setattr(questionary, kind, asked(kind))
    return calls


def test_a_choice_is_a_questionary_select_with_its_options(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    calls = _patch(monkeypatch, 2)
    options: list[Line] = [
        Option(1, "one", description="the first"),
        Separator("group"),
        Option(2, [("bold", "two")]),
        Option(3, "three", disabled="gone"),
    ]

    assert QuestionaryPrompter().select("Which?", options, default=2) == 2

    kind, (message, choices), kwargs = calls[0]
    assert (kind, message) == ("select", "Which?")
    assert [c.value for c in choices if not isinstance(c, questionary.Separator)] == [1, 2, 3]
    assert isinstance(choices[1], questionary.Separator) and choices[1].title == "── group ──"
    assert choices[0].description == "the first" and choices[3].disabled == "gone"
    assert kwargs["default"] is choices[2]
    assert not kwargs["use_search_filter"]


def test_a_long_list_can_be_filtered_by_typing(monkeypatch: pytest.MonkeyPatch) -> None:
    calls = _patch(monkeypatch, "b")

    QuestionaryPrompter().select("Which?", [Option("a", "a"), Option("b", "b")], search=True)

    kwargs = calls[0][2]
    assert kwargs["use_search_filter"] and not kwargs["use_jk_keys"]
    assert kwargs["default"] is None


def test_a_text_is_checked_and_kept_by_default(monkeypatch: pytest.MonkeyPatch) -> None:
    calls = _patch(monkeypatch, "12")

    assert QuestionaryPrompter().text("How many?", "500", lambda s: s.isdigit()) == "12"

    kwargs = calls[0][2]
    assert kwargs["default"] == "500"
    assert kwargs["validate"]("7") is True and kwargs["validate"]("x") is False


def test_a_text_without_a_check_takes_anything(monkeypatch: pytest.MonkeyPatch) -> None:
    calls = _patch(monkeypatch, "")

    assert QuestionaryPrompter().text("Seed?") == ""
    assert calls[0][2]["validate"]("anything") is True


def test_a_name_is_typed_with_completion(monkeypatch: pytest.MonkeyPatch) -> None:
    calls = _patch(monkeypatch, "Léviator")

    assert QuestionaryPrompter().autocomplete("Pokémon?", ["Léviator"], str.istitle) == ("Léviator")
    assert calls[0][1] == ("Pokémon?", ["Léviator"])


def test_ctrl_c_in_a_prompt_stops_like_ctrl_c(monkeypatch: pytest.MonkeyPatch) -> None:
    _patch(monkeypatch, None)

    with pytest.raises(KeyboardInterrupt):
        QuestionaryPrompter().select("Which?", [Option(1, "one")])


def test_a_pause_reads_one_line(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("builtins.input", lambda hint: " A ")

    assert QuestionaryPrompter().pause("Enter") == "a"


def test_options_say_how_they_read_as_text() -> None:
    assert prompts.plain("x") == "x"
    assert prompts.plain([("bold", "a"), ("", "b")]) == "ab"
