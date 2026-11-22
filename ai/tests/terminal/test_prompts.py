import termios
from typing import Any

import pytest
import questionary
from questionary.prompts.common import InquirerControl

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
    for kind in ("select", "text"):
        monkeypatch.setattr(questionary, kind, asked(kind))
    return calls


def test_a_choice_is_a_questionary_select_with_its_options_and_none_preselected(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    calls = _patch(monkeypatch, 2)
    options: list[Line] = [
        Option(1, "one", description="the first"),
        Separator("group"),
        Option(2, [("bold", "two")]),
        Option(3, "three", disabled="gone"),
    ]

    assert QuestionaryPrompter().select("Which?", options) == 2

    kind, (message, choices), kwargs = calls[0]
    assert (kind, message) == ("select", "Which?")
    assert [c.value for c in choices if not isinstance(c, questionary.Separator)] == [1, 2, 3]
    assert isinstance(choices[1], questionary.Separator) and choices[1].title == "── group ──"
    assert choices[0].description == "the first" and choices[3].disabled == "gone"
    assert "default" not in kwargs and not kwargs["use_search_filter"]


def test_a_long_list_can_be_filtered_by_typing_its_labels_as_plain_text(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    calls = _patch(monkeypatch, "b")
    options: list[Line] = [Option("a", [("bold", "a"), ("", " first")]), Option("b", "b")]

    QuestionaryPrompter().select("Which?", options, search=True)

    _, (_, choices), kwargs = calls[0]
    assert kwargs["use_search_filter"] and not kwargs["use_jk_keys"]
    assert [c.title for c in choices] == ["a first", "b"]  # questionary filters on str titles


def test_the_cursor_is_cyan_and_nothing_is_painted_as_chosen(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    calls = _patch(monkeypatch, 1)

    QuestionaryPrompter().select("Which?", [Option(1, "one")])
    QuestionaryPrompter().text("How many?")

    for _, _, kwargs in calls:
        rules = dict(kwargs["style"].style_rules)
        assert rules["selected"] == "noreverse" and "cyan" in rules["highlighted"]


def test_a_text_starts_empty_and_is_checked(monkeypatch: pytest.MonkeyPatch) -> None:
    calls = _patch(monkeypatch, "12")

    assert QuestionaryPrompter().text("How many?", lambda s: s.isdigit()) == "12"

    kwargs = calls[0][2]
    assert "default" not in kwargs
    assert kwargs["validate"]("7") is True and kwargs["validate"]("x") is False


def test_a_text_without_a_check_takes_anything(monkeypatch: pytest.MonkeyPatch) -> None:
    calls = _patch(monkeypatch, "")

    assert QuestionaryPrompter().text("Seed?") == ""
    assert calls[0][2]["validate"]("anything") is True


def test_ctrl_c_in_a_prompt_stops_like_ctrl_c(monkeypatch: pytest.MonkeyPatch) -> None:
    _patch(monkeypatch, None)

    with pytest.raises(KeyboardInterrupt):
        QuestionaryPrompter().select("Which?", [Option(1, "one")])


def test_a_pause_reads_one_line(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr("builtins.input", lambda hint: " A ")

    assert QuestionaryPrompter().pause("Enter") == "a"


def test_keys_pressed_before_a_question_are_forgotten_on_a_terminal(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    flushed: list[int] = []
    monkeypatch.setattr(termios, "tcflush", lambda stream, what: flushed.append(what))
    _patch(monkeypatch, 1)
    monkeypatch.setattr("builtins.input", lambda hint: "")

    monkeypatch.setattr("sys.stdin.isatty", lambda: False)
    QuestionaryPrompter().select("Which?", [Option(1, "one")])
    assert flushed == []

    monkeypatch.setattr("sys.stdin.isatty", lambda: True)
    QuestionaryPrompter().select("Which?", [Option(1, "one")])
    QuestionaryPrompter().text("Seed?")
    QuestionaryPrompter().pause("Enter")
    assert flushed == [termios.TCIFLUSH] * 3


def test_an_option_s_card_shows_without_questionary_s_label() -> None:
    control = InquirerControl([questionary.Choice("one", 1, description="╭─ card ─╮")])
    tokens = control._get_choice_tokens()  # type: ignore[no-untyped-call]
    assert tokens[-1][1] == "╭─ card ─╮"

    bare = InquirerControl([questionary.Choice("one", 1)])
    tokens = bare._get_choice_tokens()  # type: ignore[no-untyped-call]
    assert "Description" not in "".join(text for _, text in tokens)


def test_options_say_how_they_read_as_text() -> None:
    assert prompts.plain("x") == "x"
    assert prompts.plain([("bold", "a"), ("", "b")]) == "ab"
