"""Unit tests for scripts/gen_prolog.py and scripts/ask.py (stdlib unittest, no extra packages).

    make test-kb                                     runs these after generating the fixture knowledge base
    KB_TEST_KB=build/kb_test/knowledge_base.pl python3 -m unittest discover -s tests -p 'test_*.py'

The question-answering tests need SWI-Prolog, pyswip and the knowledge base generated from tests/data/cs_*.csv;
they are skipped when any of these is missing so the parser tests still run.
"""

from __future__ import annotations

import os
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import ask  # scripts/ is not a package, so it is put on sys.path above
import gen_prolog

KB = Path(os.environ.get("KB_TEST_KB", ROOT / "build" / "kb_test" / "knowledge_base.pl"))


class GenPrologTests(unittest.TestCase):
    def test_atom_escapes_quotes_and_backslashes(self) -> None:
        self.assertEqual(gen_prolog.atom("O'Brien"), r"'O\'Brien'")
        self.assertEqual(gen_prolog.atom("a\\b"), r"'a\\b'")

    def test_to_minutes(self) -> None:
        self.assertEqual(gen_prolog.to_minutes("4:00 PM"), 960)
        self.assertEqual(gen_prolog.to_minutes("12:00 PM"), 720)
        self.assertEqual(gen_prolog.to_minutes("12:15 AM"), 15)
        with self.assertRaises(ValueError):
            gen_prolog.to_minutes("25:00 PM")

    def test_expand_days(self) -> None:
        self.assertEqual(gen_prolog.expand_days("MWF"), ["monday", "wednesday", "friday"])
        self.assertEqual(gen_prolog.expand_days("TR"), ["tuesday", "thursday"])
        with self.assertRaises(ValueError):
            gen_prolog.expand_days("MX")

    def test_split_rooms_matches_c_program(self) -> None:
        self.assertEqual(gen_prolog.split_rooms("201/112"), ["201", "112"])
        for virtual in ("Online", "online", "Dept", "TBA", "Zoom", ""):
            self.assertEqual(gen_prolog.split_rooms(virtual), [], virtual)

    def test_name_tokens_drop_titles_and_initials(self) -> None:
        self.assertEqual(gen_prolog.name_tokens("Dr. Stone, D. B."), {"stone"})
        self.assertEqual(gen_prolog.name_tokens("Tankeh, Dr.", "Dr. Tankeh"), {"tankeh"})


class ParserTests(unittest.TestCase):
    def test_parse_day(self) -> None:
        self.assertEqual(ask.parse_day("Wed"), "wednesday")
        self.assertEqual(ask.parse_day("thurs"), "thursday")
        self.assertEqual(ask.parse_day("Mondays"), "monday")
        self.assertIsNone(ask.parse_day("t"))  # tuesday or thursday
        self.assertIsNone(ask.parse_day("room"))

    def test_parse_clock(self) -> None:
        self.assertEqual(ask.parse_clock("at 3 PM"), 900)
        self.assertEqual(ask.parse_clock("10:45am"), 645)
        self.assertEqual(ask.parse_clock("12 p.m."), 720)
        self.assertEqual(ask.parse_clock("15:30"), 930)
        self.assertIsNone(ask.parse_clock("sometime"))

    def test_name_words(self) -> None:
        self.assertEqual(ask.name_words("Dr Tankeh"), ["tankeh"])
        self.assertEqual(ask.name_words("Dr. Tankeh's"), ["tankeh"])
        self.assertEqual(ask.name_words("professor Jordan Ellis"), ["jordan", "ellis"])

    def test_clean(self) -> None:
        self.assertEqual(ask.clean("  What classes does Dr Tankeh   teach?? "), "what classes does dr tankeh teach")


def prolog_available() -> bool:
    try:
        import pyswip  # noqa: F401
    except (ImportError, OSError):
        return False
    return KB.is_file()


@unittest.skipUnless(prolog_available(), f"needs pyswip + SWI-Prolog and {KB} (run 'make test-kb')")
class AnswerTests(unittest.TestCase):
    answerer: ask.Answerer

    @classmethod
    def setUpClass(cls) -> None:
        cls.answerer = ask.Answerer(ask.KnowledgeBase(KB))

    def ask(self, question: str) -> str:
        return self.answerer.answer(question)

    def test_classes_of_lecturer_any_phrasing(self) -> None:
        for question in (
            "What classes does Dr Tankeh teach?",
            "what courses does Dr. Tankeh teach",
            "Which classes is Tankeh teaching?",
            "What does Dr Tankeh teach?",
            "classes taught by Tankeh",
        ):
            answer = self.ask(question)
            for course in (
                "COSC 330 (Systems Programming I)",
                "COSC 369 (Software Engineering I)",
                "COSC 631 (Database Management)",
            ):
                self.assertIn(course, answer, question)
            self.assertNotIn("COSC 112", answer, question)

    def test_classes_on_one_day(self) -> None:
        answer = self.ask("What does Dr Tankeh teach on Tuesday?")
        self.assertIn("COSC 369", answer)
        self.assertNotIn("COSC 330", answer)

    def test_who_teaches(self) -> None:
        self.assertIn("Dr. Tankeh", self.ask("Who teaches COSC 369?"))
        self.assertIn("Jordan Ellis", self.ask("who is the instructor for operating systems"))
        self.assertIn("COSC 631", self.ask("Who teaches database management?"))

    def test_office_hours_match_sample_layout(self) -> None:
        answer = self.ask("What are Dr Tankeh's office hours?")
        lines = answer.splitlines()
        self.assertIn("  Monday:    10:45 AM – 1:00 PM (COSC 330)", lines)
        self.assertIn("  Wednesday: 10:45 AM – 1:00 PM (COSC 330)", lines)
        self.assertIn("             & 3:00 – 5:00 PM (COSC 631)", lines)
        self.assertIn("  Friday:", lines)
        self.assertEqual(lines[-1], ask.NOTE)

    def test_full_schedule_matches_sample(self) -> None:
        """'What is Dr Tankeh's schedule?' gives the sample's CLASS SCHEDULE + OFFICE HOURS + NOTE layout."""
        for question in ("What is Dr Tankeh's schedule?", "Dr Tankeh's schedule", "schedule for Dr. Tankeh"):
            lines = self.ask(question).splitlines()
            self.assertIn("CLASS SCHEDULE", lines, question)
            self.assertEqual(lines[lines.index("CLASS SCHEDULE") + 1].split(), ["Course", "Times", "Location"])
            self.assertIn("  COSC 631 (Database Management)     Wednesday 5:00 PM to 7:30 PM            CS 313", lines)
            self.assertIn("OFFICE HOURS", lines, question)
            self.assertEqual(lines[-1], ask.NOTE, question)

    def test_format_range_like_sample(self) -> None:
        self.assertEqual(ask.format_range(645, 780), "10:45 AM – 1:00 PM")
        self.assertEqual(ask.format_range(900, 1020), "3:00 – 5:00 PM")

    def test_office_hours_on_a_day(self) -> None:
        answer = self.ask("Which office hours are on Wednesday?")
        self.assertIn("3:00 – 5:00 PM", answer)
        self.assertIn("Jordan Ellis", answer)
        self.assertNotIn("Monday", answer)

    def test_availability(self) -> None:
        self.assertTrue(self.ask("Is Dr Tankeh available on Wednesday at 4 PM?").startswith("Yes."))
        self.assertTrue(self.ask("Is Dr Tankeh free on Monday at 10 AM?").startswith("No."))

    def test_classes_in_room_with_space_and_any_case(self) -> None:
        answer = self.ask("Which classes are in room cs 311?")
        self.assertIn("COSC 330", answer)
        self.assertIn("COSC 450", answer)
        self.assertNotIn("COSC 369", answer)

    def test_classes_on_a_day(self) -> None:
        answer = self.ask("Which classes meet on Friday?")
        self.assertEqual(answer, "No classes meet on Friday.")

    def test_unknown_lecturer_suggests_close_name(self) -> None:
        answer = self.ask("What classes does Dr Tanke teach?")
        self.assertIn("No lecturer named", answer)
        self.assertIn("Dr. Tankeh", answer)

    def test_no_conflicts(self) -> None:
        self.assertEqual(self.ask("Are there any conflicts?"), "No scheduling conflicts found.")

    def test_raw_prolog_query(self) -> None:
        self.assertEqual(self.ask("?- lecturer_of('COSC 369', Name)."), "Name = Dr. Tankeh")
        self.assertEqual(self.ask("?- teaches_on('CS01', friday)."), "false.")


if __name__ == "__main__":
    unittest.main()
