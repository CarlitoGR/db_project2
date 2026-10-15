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
import syllabus

SYLLABI = ROOT / "data" / "syllabi"
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


class SyllabusHelperTests(unittest.TestCase):
    def test_parse_days(self) -> None:
        self.assertEqual(syllabus.parse_days("MW"), ["monday", "wednesday"])
        self.assertEqual(syllabus.parse_days("TR"), ["tuesday", "thursday"])
        self.assertEqual(syllabus.parse_days("Thu"), ["thursday"])
        self.assertEqual(syllabus.parse_days("Mon/Wed"), ["monday", "wednesday"])
        self.assertEqual(syllabus.parse_days("Other"), [])

    def test_office_hours_lines(self) -> None:
        syl = syllabus.Syllabus(source=Path("x.pdf"))
        lines = ["MW 2:00-3:00p", "Thu 3:00PM-5:00PM", "F 11:00-1:00p", "Other time by appointment"]
        syllabus.read_office_hours(syl, {"office_hours": lines})
        got = [(b.days, b.start, b.end) for b in syl.office_hours]
        self.assertEqual(
            got,
            [(["monday", "wednesday"], 840, 900), (["thursday"], 900, 1020), (["friday"], 660, 780)],
        )
        self.assertEqual(syl.office_hours_notes, ["Other time by appointment"])

    def test_sections_inline_and_stacked_labels(self) -> None:
        header, sections = syllabus.split_sections(
            ["COSC 631 Databases (3 Credits)", "INSTRUCTOR:   Dr. A. B", "Office Hours  :", "   MW 1:00-2:00p"]
        )
        self.assertEqual(header, ["COSC 631 Databases (3 Credits)"])
        self.assertEqual(sections["instructor"], ["Dr. A. B"])
        self.assertEqual(sections["office_hours"], ["   MW 1:00-2:00p"])

    def test_tidy(self) -> None:
        self.assertEqual(syllabus.tidy("Systems ,  Elmasri;  7 th  ed."), "Systems, Elmasri; 7th ed.")


def pypdf_available() -> bool:
    try:
        import pypdf  # noqa: F401
    except ImportError:
        return False
    return True


@unittest.skipUnless(pypdf_available(), "needs pypdf")
class SyllabusPdfTests(unittest.TestCase):
    """The two syllabus PDFs in data/syllabi are read correctly despite very different layouts."""

    def test_cosc_528(self) -> None:
        syl = syllabus.read_syllabus(SYLLABI / "COSC_528_Syllabus_Fall_2026.pdf")
        self.assertEqual(syl.codes, ["COSC 428", "COSC 528"])
        self.assertEqual((syl.title, syl.credits, syl.term), ("Design and Analysis of Algorithms", 3, "Fall 2026"))
        self.assertEqual((syl.instructor, syl.office), ("Jim Gil de Lamadrid", "CSB 222"))
        self.assertEqual(syl.email, "jgildelamadrid@bowiestate.edu")
        self.assertEqual([(b.days, b.start, b.end) for b in syl.office_hours][1], (["tuesday", "thursday"], 870, 990))
        self.assertEqual(syl.prerequisites, {"COSC 428": ["COSC 214"]})
        self.assertEqual(sum(p for _, p in syl.weights), 100)
        self.assertEqual(len(syl.weeks), 15)
        self.assertEqual(syl.weeks[1][2], "Hmwk. #1 · Order of Growth, Recurrence Relations, Divide-and-Conquer")
        self.assertEqual(syl.weeks[10][3], ["pp.267-288", "pp.297-302"])
        self.assertEqual(syl.dates[0], ("Last Day to Add/Drop a Class", "09/04/26"))
        self.assertEqual(len(syl.references), 3)

    def test_cosc_631(self) -> None:
        syl = syllabus.read_syllabus(SYLLABI / "COSC_631_Syllabus_Spring_2022.pdf")
        self.assertEqual(syl.codes, ["COSC 631"])
        self.assertEqual(
            (syl.title, syl.term, syl.department),
            ("Database Information Systems II", "Spring 2022", "Computer Science"),
        )
        self.assertEqual(syl.instructor, "Dr. Bo Yang")
        self.assertEqual(syl.office, "Computer Science Building, Room 322")
        self.assertEqual(syl.phones, ["2-4459 (in campus)", "301-860-4459 (off campus)"])
        self.assertEqual(syl.office_hours_notes, ["Other time by appointment"])
        self.assertEqual(syl.prerequisites, {"COSC 631": ["COSC 531"]})
        self.assertEqual(dict(syl.weights)["Final"], 30)
        self.assertEqual(syl.scale[-1], ("F", 0, 59))
        self.assertEqual(syl.weeks[7][:3], (8, "3/16", "Tentative Midterm"))
        self.assertTrue(syl.texts[0].startswith("Fundamentals of Database Systems, Elmasri/Navathe; 7th"))


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

    def test_syllabus_questions(self) -> None:
        self.assertEqual(self.ask("What are the prerequisites for COSC 631?"), "COSC 631 (graduate) requires COSC 531.")
        self.assertTrue(self.ask("Can I take COSC 631 if I completed COSC 531?").startswith("Yes"))
        self.assertTrue(self.ask("Can I take COSC 428?").startswith("Not yet: COSC 428 requires COSC 214"))
        self.assertTrue(self.ask("What grade is 85 in COSC 631?").startswith("85 is a B in COSC 631"))
        self.assertIn("is worth 30%", self.ask("How much is the final worth in COSC 631?"))
        self.assertIn("Total                     100%", self.ask("How is COSC 528 graded?"))
        self.assertIn("week 6: Hmwk. #3 · Quicksort", self.ask("When is quicksort covered?"))
        self.assertIn("week 8 (3/16): Tentative Midterm", self.ask("When is the midterm in COSC 631?"))
        self.assertIn("McConnell", self.ask("What textbook does COSC 528 use?"))
        self.assertIn("09/04/26", self.ask("When is the last day to add/drop?"))
        self.assertIn("byang@bowiestate.edu", self.ask("How do I contact Dr Yang?"))

    def test_syllabus_instructor_in_schedule_questions(self) -> None:
        answer = self.ask("Who teaches COSC 631?")
        self.assertIn("Dr. Tankeh (CS01): COSC 631.001", answer)
        self.assertIn("Dr. Bo Yang", answer)
        self.assertIn("course syllabus, Spring 2022", answer)
        self.assertIn("Dr. Bo Yang", self.ask("Which office hours are on Thursday?"))
        lines = self.ask("What is Jim Gil de Lamadrid's schedule?").splitlines()
        self.assertIn("  Monday:    2:00 – 3:00 PM (COSC 428, COSC 528)", lines)

    def test_no_syllabus(self) -> None:
        self.assertIn("There is no syllabus for COSC 369", self.ask("What are the prerequisites for COSC 369?"))

    def test_raw_prolog_query(self) -> None:
        self.assertEqual(self.ask("?- lecturer_of('COSC 369', Name)."), "Name = Dr. Tankeh")
        self.assertEqual(self.ask("?- teaches_on('CS01', friday)."), "false.")


if __name__ == "__main__":
    unittest.main()
