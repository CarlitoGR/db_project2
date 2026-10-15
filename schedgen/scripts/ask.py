#!/usr/bin/env python3
"""Answer questions about the faculty schedule with Prolog (via pyswip).

    python3 scripts/ask.py "What classes does Dr Stone teach?"      one question (after 'make kb')
    python3 scripts/ask.py                                          interactive: type questions, 'quit' to leave
    python3 scripts/ask.py --file data/questions.txt                every question in a file (make demo)
    python3 scripts/ask.py "?- lecturer_of('CTEC 350', Name)."      a raw Prolog query

Division of labour:
    * Python (this file) is the front end. It recognises the kind of question, pulls out the lecturer
      name, course, day or time, builds a Prolog goal, and formats the answers as text.
    * Prolog is the interpreter. It consults the knowledge base that gen_prolog.py generated
      (output/knowledge_base.pl: rules + facts) and answers each goal by search. No answer is computed in Python.

Requires SWI-Prolog and the pyswip package (pip install pyswip); both must be 64-bit.
"""

from __future__ import annotations

import argparse
import difflib
import re
import sys
from collections import defaultdict
from collections.abc import Callable, Iterable
from dataclasses import dataclass
from pathlib import Path
from textwrap import wrap

from gen_prolog import DAY_NAMES, NAME_STOPWORDS, atom

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_KB = Path("output/knowledge_base.pl")

DAYS = list(DAY_NAMES.values())  # monday .. sunday
WEEKDAYS = DAYS[:5]
NOTE = "NOTE: Instructors are not to be interrupted during class times."

# Words that can sit next to a name in a question but are not part of it ("the", "professor", ...).
FILLER_WORDS = NAME_STOPWORDS | {"the", "a", "an", "lecturer", "instructor", "teacher", "doctor"}

COURSE_TEXT = r"[a-z]{2,5}\s*-?\s*\d{3}(?:\.\d{3})?"  # a course code inside a lower-cased question
COURSE_CODE_RE = re.compile(r"\b([A-Za-z]{2,5})\s*-?\s*(\d{3})(?:\.(\d{3}))?\b")
CLOCK_RE = re.compile(r"\b(\d{1,2})(?::(\d{2}))?\s*([ap])\.?\s*m\b\.?", re.IGNORECASE)
CLOCK_24H_RE = re.compile(r"\b([01]?\d|2[0-3]):([0-5]\d)\b")


class QuestionError(Exception):
    """The question could be parsed but refers to something that is not in the knowledge base."""


# --------------------------------------------------------------------------------------------- Prolog


class KnowledgeBase:
    """Thin wrapper around the pyswip engine: load the two .pl files, run goals, return solutions."""

    def __init__(self, path: Path) -> None:
        from pyswip import Prolog

        if not path.is_file():
            raise FileNotFoundError(f"missing knowledge base {path} (run 'make kb' first)")
        self.prolog = Prolog()
        # One file holds the rules and the facts. consult() replaces clauses loaded earlier from the same file.
        self.prolog.consult(path.resolve().as_posix())

    def all(self, goal: str) -> list[dict[str, object]]:
        """Every solution of the goal, as a list of {variable: value} dicts."""
        return list(self.prolog.query(goal))

    def holds(self, goal: str) -> bool:
        return bool(list(self.prolog.query(goal, maxresult=1)))

    def one(self, goal: str, var: str) -> object:
        """The value of var in the first solution, or None."""
        found = list(self.prolog.query(goal, maxresult=1))
        return found[0][var] if found else None


def plist(items: Iterable[str]) -> str:
    """Python strings -> Prolog list of quoted atoms."""
    return "[" + ", ".join(atom(item) for item in items) + "]"


# ----------------------------------------------------------------------------------- question parsing


def clean(question: str) -> str:
    """Lower-case, single-spaced, without trailing punctuation or a trailing 'please'."""
    text = re.sub(r"\s+", " ", question.strip().lower())
    text = re.sub(r"[?.!]+$", "", text).strip()
    return re.sub(r",?\s*please$", "", text)


def parse_day(text: str) -> str | None:
    """'wed', 'Wednesday', 'thurs' -> 'wednesday' / 'thursday'. None if no single day matches."""
    word = re.sub(r"[^a-z]", "", text.lower())
    if word.endswith("s") and word[:-1] in DAYS:  # "mondays"
        word = word[:-1]
    if len(word) < 2:
        return None
    matches = [day for day in DAYS if day.startswith(word)]
    return matches[0] if len(matches) == 1 else None


def find_day(text: str) -> str | None:
    """The first word in the text that names a day."""
    for word in re.findall(r"[a-z]+", text.lower()):
        if len(word) >= 3 and (day := parse_day(word)):
            return day
    return None


def parse_clock(text: str) -> int | None:
    """'3 pm', '3:30PM', '15:30' -> minutes after midnight."""
    if match := CLOCK_RE.search(text):
        hour, minute = int(match[1]), int(match[2] or 0)
        if 1 <= hour <= 12 and minute <= 59:
            return (hour % 12 + (12 if match[3].lower() == "p" else 0)) * 60 + minute
    if match := CLOCK_24H_RE.search(text):
        return int(match[1]) * 60 + int(match[2])
    return None


def name_words(text: str) -> list[str]:
    """'Dr. Tankeh's' -> ['tankeh']; 'professor Ruth Agada' -> ['ruth', 'agada']."""
    text = re.sub(r"'s\b", "", text.lower())
    words = [w.strip("'-") for w in re.findall(r"[a-z][a-z'\-]*", text)]
    return [w for w in words if len(w) >= 2 and w not in FILLER_WORDS]


def format_clock(minutes: int) -> str:
    hour, minute = divmod(minutes, 60)
    return f"{hour % 12 or 12}:{minute:02d} {'PM' if hour >= 12 else 'AM'}"


def format_range(start: int, end: int) -> str:
    """'10:45 AM – 1:00 PM'; the first AM/PM is dropped when both match: '3:00 – 5:00 PM' (as in the sample)."""
    first, last = format_clock(start), format_clock(end)
    if first[-2:] == last[-2:]:
        first = first[:-3]
    return f"{first} – {last}"


def table(rows: list[list[str]], indent: str = "  ") -> list[str]:
    """Left-aligned columns, two spaces apart."""
    if not rows:
        return []
    widths = [max(len(row[i]) for row in rows) for i in range(len(rows[0]))]
    return [indent + "  ".join(cell.ljust(w) for cell, w in zip(row, widths, strict=True)).rstrip() for row in rows]


# ------------------------------------------------------------------------------------------ answering


@dataclass
class Lecturer:
    id: str
    name: str


class Answerer:
    """Turns one English question into Prolog goals and the solutions into text."""

    def __init__(self, kb: KnowledgeBase) -> None:
        self.kb = kb
        # (pattern, handler) pairs, tried in order. More specific patterns come first.
        self.routes: list[tuple[re.Pattern[str], Callable[[re.Match[str]], str]]] = [
            (re.compile(r"^(?:help|\?|what can (?:i|you) ask.*)$"), lambda m: HELP),
            (re.compile(r"\b(?:conflicts?|double[- ]?booked|clash(?:es)?|overlapping classes)\b"), self.conflicts),
            (re.compile(r"\b(?:data issues|data problems|errors in the (?:data|schedule))\b"), self.data_issues),
            (
                re.compile(r"^(?:list|show|who are|name)\b.*\b(?:lecturers|faculty|professors|instructors)$"),
                self.list_lecturers,
            ),
            # ---- course syllabi
            (
                re.compile(
                    r"\b(?:add/drop|add-drop|add drop|important dates|key dates|last day|deadlines?|mid-semester)\b"
                ),
                self.important_dates,
            ),
            (re.compile(r"\b(?:prerequisites?|prereqs?|pre-requisites?)\b"), self.prerequisites),
            (
                re.compile(
                    r"^(?:can|may|could) (?:i|a student|someone|we) (?:take|enroll in|register for) (?P<what>.+?)"
                    r"(?: (?:if|after|when|since|with|having) (?P<done>.+))?$"
                ),
                self.can_take,
            ),
            (
                re.compile(
                    r"^(?:how much|what percent(?:age)?(?: of the grade)?) (?:is|are|does|do) (?:the )?(?P<comp>.+?)"
                    r"(?: worth| count| weigh)?(?: toward the grade)? (?:in|for) (?P<what>.+)$"
                ),
                self.component_weight,
            ),
            (
                re.compile(
                    r"^what (?:letter )?grade (?:is|would|does|do i get for) (?:an? |a score of )?(?P<score>\d+(?:\.\d+)?)%?"
                    r"(?: be| get| earn)? (?:in|for) (?P<what>.+)$"
                ),
                self.letter_grade,
            ),
            (re.compile(r"^how (?:is|are) (?P<what>.+?) graded$"), self.grading),
            (
                re.compile(
                    r"\bgrad(?:ing|e (?:breakdown|weights?|scale|distribution))\b.*\b(?:for|in|of) (?P<what>.+)$"
                ),
                self.grading,
            ),
            (
                re.compile(r"\b(?:textbooks?|text ?books?|required texts?|books?|reading list|references)\b"),
                self.textbook,
            ),
            (
                re.compile(
                    r"^(?:what (?:is|are|was|were) (?:covered|taught|due|happening|scheduled|planned|the topics?) "
                    r"(?:in|for|during|on) )?week (?P<week>\d+) (?:of|in|for) (?P<what>.+)$"
                ),
                self.week,
            ),
            (
                re.compile(
                    r"^(?:what is |what's |show |give me |list )?(?:the )?(?:course )?(?:outline|weekly schedule|topics|"
                    r"calendar|weekly topics|syllabus schedule|course schedule) (?:of|for|in) (?P<what>.+)$"
                ),
                self.outline,
            ),
            (re.compile(r"^(?:what is|what's) (?P<what>.+?) about$"), self.course_info),
            (re.compile(r"^(?:describe|description of|tell me about) (?P<what>.+)$"), self.course_info),
            (
                re.compile(
                    r"^(?:how (?:do|can) i (?:contact|reach|email|call|find)|contact (?:info(?:rmation)? )?(?:for|of)) (?P<who>.+)$"
                ),
                self.contact,
            ),
            (
                re.compile(
                    r"^(?:what is |what's |where is )?(?P<who>.+?)'s (?:email|e-mail|phone(?: number)?|office|contact(?: info(?:rmation)?)?)$"
                ),
                self.contact,
            ),
            (re.compile(r"\bsyllab(?:us|i)\b"), self.list_syllabi),
            (
                re.compile(
                    r"^when (?:is|are|will|do we have|do we|does|do) (?:the )?(?P<topic>.+?)"
                    r"(?: (?:be )?(?:covered|taught|due|given|held|discussed|scheduled|meet|meets))?"
                    rf"(?: (?:in|for) (?P<what>{COURSE_TEXT}))?$"
                ),
                self.when_topic,
            ),
            # ---- schedule
            (re.compile(r"\boffice hours?\b"), self.office_hours),
            (
                re.compile(r"^is (?P<who>.+?) (?:available|free|busy|in (?:his|her|their) office) (?P<when>.+)$"),
                self.availability,
            ),
            (
                re.compile(r"^how many (?:classes|courses|sections|credits?|credit hours) does (?P<who>.+?) teach"),
                self.load,
            ),
            (re.compile(r"^who (?:teaches|is teaching|has (?:a )?class(?:es)?) on (?P<day>\w+)$"), self.who_teaches_on),
            (
                re.compile(
                    r"^(?:what|which) (?:classes|courses|sections|subjects) (?:does|do|is|will) (?P<who>.+?) "
                    r"(?:teach|teaching|have|handle)\b(?: on (?P<day>\w+))?"
                ),
                self.lecturer_classes,
            ),
            (re.compile(r"^what does (?P<who>.+?) teach(?: on (?P<day>\w+))?$"), self.lecturer_classes),
            (
                re.compile(r"^(?:(?:show|list|give me|what is|what's) )?(?P<who>.+?)'s (?:full |teaching )?schedule$"),
                self.full_schedule,
            ),
            (
                re.compile(
                    r"^(?:(?:show|list|give me|what is|what's) )?(?:the )?(?:full )?schedule (?:of|for) (?P<who>.+)$"
                ),
                self.full_schedule,
            ),
            (
                re.compile(r"^(?:show |list |give me )?(?P<who>.+?)'s (?:classes|courses|sections)$"),
                self.lecturer_classes,
            ),
            (
                re.compile(r"^(?:show |list )?(?:the )?(?:classes|courses|sections) (?:taught by|of|for) (?P<who>.+)$"),
                self.lecturer_classes,
            ),
            (
                re.compile(
                    r"^(?:who (?:teaches|is teaching|instructs|will teach)|"
                    r"who is the (?:lecturer|instructor|professor|teacher) (?:for|of)) (?P<what>.+)$"
                ),
                self.who_teaches,
            ),
            (
                re.compile(
                    r"^(?:what|which) (?:classes|courses|sections) (?:meet|are|are held|run|are scheduled|"
                    r"take place|are taught) on (?P<day>\w+)$"
                ),
                self.classes_on_day,
            ),
            (re.compile(r"^(?:classes|courses|sections) on (?P<day>\w+)$"), self.classes_on_day),
            (
                re.compile(
                    r"^(?:what|which) (?:classes|courses|sections) (?:are|meet|are held) in (?:room )?(?P<room>.+)$"
                ),
                self.classes_in_room,
            ),
            (
                re.compile(
                    r"^(?:where|when|what time) (?:does|do|is) (?P<what>.+?)(?: meet| held| taught| scheduled)?$"
                ),
                self.where_when,
            ),
            (re.compile(r"^what room is (?P<what>.+?)(?: in)?$"), self.where_when),
        ]

    # ---- entry point

    def answer(self, question: str) -> str:
        raw = question.strip()
        if raw.startswith("?-"):
            return self.raw_query(raw[2:])
        text = clean(raw)
        if not text:
            return ""
        try:
            for pattern, handler in self.routes:
                if match := pattern.search(text):
                    return handler(match)
            return self.fallback(text)
        except QuestionError as exc:
            return str(exc)

    # ---- resolving names, courses and days

    def lecturers(self, who: str) -> list[Lecturer]:
        """Lecturers whose name (or listed alias) contains every name word of `who`."""
        words = name_words(who)
        if not words:
            raise QuestionError(f"I could not find a lecturer's name in '{who}'.")
        found = self.kb.all(f"lecturer_named({plist(words)}, Id), lecturer(Id, Name)")
        if not found:
            raise QuestionError(self.unknown_lecturer(who, words))
        return [Lecturer(str(r["Id"]), str(r["Name"])) for r in found]

    def unknown_lecturer(self, who: str, words: list[str]) -> str:
        tokens = sorted({str(r["T"]) for r in self.kb.all("name_token(_, T)")})
        close = {c for w in words for c in difflib.get_close_matches(w, tokens, n=3, cutoff=0.75)}
        message = (
            f"No lecturer named '{who.strip().title()}' is in the knowledge base (faculty file and class schedule)."
        )
        if close:
            names = sorted(
                {str(r["Name"]) for c in close for r in self.kb.all(f"name_token(Id, {atom(c)}), lecturer(Id, Name)")}
            )
            message += " Did you mean: " + ", ".join(names) + "?"
        return message

    def courses(self, what: str) -> tuple[list[str], str | None]:
        """Course codes matching a code ('ctec 350', 'CTEC350.170') or a title fragment ('python').

        Returns (codes, section) where section is set when a specific section was named.
        """
        if match := COURSE_CODE_RE.search(what):
            code = f"{match[1].upper()} {match[2]}"
            section = f"{code}.{match[3]}" if match[3] else None
            if section and not self.kb.holds(f"section({atom(section)}, _, _, _, _)"):
                raise QuestionError(f"There is no section {section} in the class schedule.")
            if not self.kb.holds(f"course({atom(code)}, _, _)"):
                raise QuestionError(f"There is no course {code} in the class schedule.")
            return [code], section
        fragment = re.sub(r"\b(?:the|class|course|section)\b", " ", what)
        fragment = re.sub(r"\s+", " ", fragment).strip(" '\"")
        if len(fragment) < 3:
            raise QuestionError(f"I could not find a course code or title in '{what}'.")
        codes = sorted({str(r["Code"]) for r in self.kb.all(f"course_title_contains({atom(fragment)}, Code)")})
        if not codes:
            raise QuestionError(f"No course code or title in the class schedule matches '{fragment}'.")
        return codes, None

    @staticmethod
    def day_or_error(text: str) -> str:
        day = parse_day(text)
        if day is None:
            raise QuestionError(f"'{text}' is not a day of the week.")
        return day

    def section_table(self, goal: str, with_instructor: bool = False) -> list[str]:
        """Course / Times / Location table (the sample's layout) for every Section bound by goal.

        Rows are sorted by session, first meeting day and start time. A course is shown as
        'COSC 631 (Database Management)'; the section number is added only when the same course appears more
        than once ('CTEC 350.170 (...)'), and the Session column only when the data has sessions.
        """
        found = self.kb.all(
            f"{goal}, section(Section, Code, Term, _, _), course(Code, Title, _), "
            "meeting_text(Section, When), room_text(Section, Room), section_sort_key(Section, Order, Start)"
        )
        unique = {str(r["Section"]): r for r in found}
        ordered = sorted(unique.values(), key=lambda r: (str(r["Term"]), int(r["Order"]), int(r["Start"])))
        if not ordered:
            return []
        per_code = defaultdict(int)
        for r in ordered:
            per_code[str(r["Code"])] += 1
        with_session = any(str(r["Term"]) for r in ordered)
        header = ["Course", *(["Session"] if with_session else []), "Times", "Location"]
        if with_instructor:
            header.append("Lecturer")
        rows = []
        for r in ordered:
            section, code = str(r["Section"]), str(r["Code"])
            label = code if per_code[code] == 1 else section
            row = [f"{label} ({r['Title']})", *([str(r["Term"]) or "-"] if with_session else [])]
            row += [str(r["When"]), str(r["Room"])]
            if with_instructor:
                teacher = self.kb.one(f"teaches(Id, {atom(section)}), lecturer(Id, Name)", "Name")
                row.append(str(teacher or self.kb.one(f"listed_instructor({atom(section)}, N)", "N")))
            rows.append(row)
        return table([header, *rows])

    def lecturer_header(self, lec: Lecturer) -> str:
        found = self.kb.all(f"faculty({atom(lec.id)}, _, Dept, Office, _, _)")
        dept, office = (str(found[0]["Dept"]), str(found[0]["Office"])) if found else ("", "")
        return f"{lec.name} ({lec.id}) | {dept} | Office: {office}"

    def office_hours_lines(self, lec: Lecturer, only_day: str | None = None) -> list[str]:
        """OFFICE HOURS block in the sample's layout: every weekday listed, each block labelled with its course."""
        by_day: dict[str, list[tuple[int, int]]] = defaultdict(list)
        for r in self.kb.all(f"office_hours({atom(lec.id)}, Day, Start, End, _, _)"):
            by_day[str(r["Day"])].append((int(r["Start"]), int(r["End"])))
        days = [only_day] if only_day else WEEKDAYS + [d for d in DAYS[5:] if d in by_day]
        lines = ["OFFICE HOURS"]
        for day in days:
            label = f"{day.title()}:".ljust(11)
            blocks = sorted(by_day.get(day, []))
            if not blocks:
                lines.append(f"  {label}{'none' if only_day else ''}".rstrip())
                continue
            for i, (start, end) in enumerate(blocks):
                goal = f"office_hours_course({atom(lec.id)}, {day}, {start}, Code)"
                codes = sorted({str(r["Code"]) for r in self.kb.all(goal)})
                tag = f" ({', '.join(codes)})" if codes else ""
                prefix = label if i == 0 else " " * 11 + "& "
                lines.append(f"  {prefix}{format_range(start, end)}{tag}")
        notes = [str(r["N"]) for r in self.kb.all(f"office_hours_note({atom(lec.id)}, N)")]
        lines += [f"  Also: {note}" for note in notes]
        has_policy = self.kb.holds(f"office_hours_required({atom(lec.id)}, _)")
        if has_policy and not self.kb.holds(f"meets_office_hours_policy({atom(lec.id)})"):
            have = self.kb.one(f"office_hours_minutes({atom(lec.id)}, M)", "M") or 0
            need = self.kb.one(f"office_hours_required({atom(lec.id)}, M)", "M") or 0
            lines.append(f"  (below requirement: {int(have) / 60:g} of {int(need) / 60:g} h/week)")
        lines.append(NOTE)
        return lines

    # ---- handlers

    def lecturer_classes(self, m: re.Match[str]) -> str:
        day = self.day_or_error(m["day"]) if m.groupdict().get("day") else None
        out = []
        for lec in self.lecturers(m["who"]):
            goal = f"teaches({atom(lec.id)}, Section)"
            if day:
                goal += f", meets(Section, {day}, _, _)"
            lines = self.section_table(goal)
            syllabi = [] if day else self.syllabus_table(lec.id)
            when = f" on {day.title()}" if day else ""
            if not lines and not syllabi:
                out += [f"{lec.name} ({lec.id}) teaches no classes{when}.", ""]
                continue
            if lines and day:
                out.append(f"{lec.name} ({lec.id}) teaches {len(lines) - 1} class(es){when}:")
            elif lines:
                sections, credits = self.load_of(lec.id)
                out.append(f"{lec.name} ({lec.id}) teaches {sections} section(s), {credits} credit hours:")
            else:
                out.append(f"{lec.name} ({lec.id}) is the instructor on {len(syllabi) - 1} course syllabus(es):")
            if lines:
                out += ["", "CLASS SCHEDULE", *lines]
            if syllabi:
                out += ["", "COURSE SYLLABI", *syllabi]
            out.append("")
        return "\n".join(out).rstrip()

    def full_schedule(self, m: re.Match[str]) -> str:
        """Teaching schedule and office hours together, laid out like the department's sample."""
        if COURSE_CODE_RE.search(m["who"]):  # "schedule for COSC 528" means the course's weekly outline
            return self.outline(re.match(r"(?P<what>.+)", m["who"]))
        out = []
        for lec in self.lecturers(m["who"]):
            lines = self.section_table(f"teaches({atom(lec.id)}, Section)")
            syllabi = self.syllabus_table(lec.id)
            out += [self.lecturer_header(lec), ""]
            if lines or not syllabi:
                out += ["CLASS SCHEDULE", *(lines or ["  (no classes)"]), ""]
            if syllabi:
                out += ["COURSE SYLLABI (meeting times are not given on the syllabus)", *syllabi, ""]
            out += [*self.office_hours_lines(lec), ""]
        return "\n".join(out).rstrip()

    def syllabus_table(self, fid: str) -> list[str]:
        """Course / Term / Prerequisite table of the syllabi that name this lecturer (cross-listed codes merged)."""
        found = self.kb.all(f"syllabus_teaches({atom(fid)}, Code, Term)")
        rows, seen = [], set()
        for r in sorted(found, key=lambda r: str(r["Code"])):
            group = self.course_group(str(r["Code"]))
            if tuple(group) in seen:
                continue
            seen.add(tuple(group))
            rows.append([self.course_label(str(r["Code"])), str(r["Term"]), self.prerequisite_text(group)])
        return table([["Course", "Term", "Prerequisite"], *rows]) if rows else []

    def load_of(self, fid: str) -> tuple[int, int]:
        found = self.kb.all(f"teaching_load({atom(fid)}, Sections, Credits)")
        return (int(found[0]["Sections"]), int(found[0]["Credits"])) if found else (0, 0)

    def load(self, m: re.Match[str]) -> str:
        lines = []
        for lec in self.lecturers(m["who"]):
            sections, credits = self.load_of(lec.id)
            lines.append(f"{lec.name} ({lec.id}) teaches {sections} section(s) totalling {credits} credit hours.")
        return "\n".join(lines)

    def who_teaches(self, m: re.Match[str]) -> str:
        codes, section = self.courses(m["what"])
        out = []
        for code in codes:
            title = self.kb.one(f"course({atom(code)}, Title, _)", "Title")
            target = atom(section) if section else "Section"
            found = self.kb.all(
                f"section({target}, {atom(code)}, Term, _, _), teaches(Id, {target}), lecturer(Id, Name)"
            )
            label = section or code
            by_lecturer: dict[tuple[str, str], list[str]] = defaultdict(list)
            for r in found:
                sec = section or str(r["Section"])
                term = f" [{r['Term']}]" if str(r["Term"]) else ""
                by_lecturer[(str(r["Name"]), str(r["Id"]))].append(f"{sec}{term}")
            if not section:
                for r in self.kb.all(f"syllabus_teaches(Id, {atom(code)}, Term), lecturer(Id, Name)"):
                    by_lecturer[(str(r["Name"]), str(r["Id"]))].append(f"course syllabus, {r['Term']}")
            if not by_lecturer:
                out.append(f"{label} {title}: no lecturer from the faculty file is assigned.")
                continue
            out.append(f"{label} {title} is taught by:")
            for (name, fid), items in sorted(by_lecturer.items()):
                out.append(f"  {name} ({fid}): {', '.join(sorted(items))}")
        return "\n".join(out)

    def office_hours(self, m: re.Match[str]) -> str:
        text = m.string
        if COURSE_CODE_RE.search(text):  # "office hours for COSC 631": the course's instructor(s)
            codes, _ = self.courses(text)
            ids = sorted({str(r["Id"]) for c in codes for r in self.kb.all(f"teaches_course(Id, {atom(c)})")})
            if not ids:
                raise QuestionError(f"No instructor is recorded for {', '.join(codes)}.")
            out = []
            for fid in ids:
                lec = Lecturer(fid, str(self.kb.one(f"lecturer({atom(fid)}, N)", "N")))
                out += [self.lecturer_header(lec), *self.office_hours_lines(lec), ""]
            return "\n".join(out).rstrip()
        day = None
        if day_match := re.search(r"\bon (\w+)", text):
            day = parse_day(day_match[1])
        who = None
        for pattern in (
            r"^(?:what|when|where) (?:are|is) (?P<who>.+?)(?:'s)? office hours?",
            r"office hours? (?:of|for|by) (?P<who>.+?)(?: on \w+)?$",
            r"^(?:when|what time|what days?) (?:does|do|is) (?P<who>.+?) (?:have|hold|has|keep) office hours?",
            r"^does (?P<who>.+?) (?:have|hold) office hours?",
            r"^(?:show |list )?(?P<who>.+?)'s office hours?",
        ):
            if found := re.search(pattern, text):
                candidate = found["who"]
                if name_words(candidate) and not (parse_day(candidate) or candidate in {"the", "all"}):
                    who = candidate
                break
        if who:
            return self.lecturer_office_hours(who, day)
        if day is None:
            day = find_day(text)
        if day:
            return self.office_hours_on_day(day)
        raise QuestionError(
            "Ask about a lecturer ('What are Dr Stone's office hours?') or a day ('Which office "
            "hours are on Wednesday?')."
        )

    def lecturer_office_hours(self, who: str, only_day: str | None) -> str:
        out = []
        for lec in self.lecturers(who):
            out += [self.lecturer_header(lec), *self.office_hours_lines(lec, only_day), ""]
        return "\n".join(out).rstrip()

    def office_hours_on_day(self, day: str) -> str:
        found = self.kb.all(f"office_hours_on({day}, Id, Start, End, Loc), lecturer(Id, Name)")
        if not found:
            return f"No office hours are scheduled on {day.title()}."
        found.sort(key=lambda r: (int(r["Start"]), str(r["Name"])))
        rows = [[format_range(int(r["Start"]), int(r["End"])), str(r["Name"]), str(r["Loc"])] for r in found]
        return "\n".join(
            [f"Office hours on {day.title()} ({len(rows)}):", *table([["Time", "Lecturer", "Location"], *rows])]
        )

    def who_teaches_on(self, m: re.Match[str]) -> str:
        day = self.day_or_error(m["day"])
        names = sorted({str(r["Name"]) for r in self.kb.all(f"teaches_on(Id, {day}), lecturer(Id, Name)")})
        if not names:
            return f"Nobody teaches on {day.title()}."
        return f"{len(names)} lecturer(s) teach on {day.title()}:\n" + "\n".join(f"  {n}" for n in names)

    def classes_on_day(self, m: re.Match[str]) -> str:
        day = self.day_or_error(m["day"])
        lines = self.section_table(f"class_on({day}, Section, _, _)", with_instructor=True)
        if not lines:
            return f"No classes meet on {day.title()}."
        return "\n".join([f"Classes on {day.title()} ({len(lines) - 1}):", *lines])

    def classes_in_room(self, m: re.Match[str]) -> str:
        room = m["room"].strip()
        lines = self.section_table(f"class_in_room({atom(room)}, Section)", with_instructor=True)
        if not lines:
            return f"No classes are scheduled in room {room.upper()}."
        return "\n".join([f"Classes in room {room.upper()} ({len(lines) - 1}):", *lines])

    def where_when(self, m: re.Match[str]) -> str:
        what = m["what"]
        if not COURSE_CODE_RE.search(what):
            # "Where does Dr Stone teach?" is about a lecturer, not a course.
            try:
                lecturers = self.lecturers(what)
            except QuestionError:
                lecturers = []
            if lecturers:
                return self.lecturer_classes(re.match(r"(?P<who>.+)", " ".join(name_words(what))))
        codes, section = self.courses(what)
        goal = (
            f"Section = {atom(section)}"
            if section
            else f"member(Code0, {plist(codes)}), section_of_course(Section, Code0)"
        )
        lines = self.section_table(goal, with_instructor=True)
        if lines:
            return "\n".join(lines)
        if any(self.has_syllabus(code) for code in codes):
            return self.course_info(re.match(r"(?P<what>.+)", " ".join(codes)))
        return f"No meeting time is recorded for {', '.join(codes)}."

    def availability(self, m: re.Match[str]) -> str:
        when = m["when"]
        day = find_day(when)
        minutes = parse_clock(when)
        if day is None or minutes is None:
            raise QuestionError("Give a day and a time, e.g. 'Is Dr Stone available on Monday at 3 PM?'")
        out = []
        for lec in self.lecturers(m["who"]):
            at = f"{day.title()} at {format_clock(minutes)}"
            section = self.kb.one(f"teaching_at({atom(lec.id)}, {day}, {minutes}, S)", "S")
            if section:
                out.append(f"No. {lec.name} is teaching {section} on {at}. {NOTE[6:]}")
            elif self.kb.holds(f"in_office({atom(lec.id)}, {day}, {minutes})"):
                out.append(f"Yes. {lec.name} is holding office hours on {at}.")
            else:
                out.append(f"{lec.name} has no class or office hours on {at} (not a scheduled office hour).")
        return "\n".join(out)

    def conflicts(self, m: re.Match[str]) -> str:
        out = []
        for r in self.kb.all("room_conflict(Room, A, B)"):
            out.append(
                f"  Room {r['Room']} is double-booked: {self.describe(str(r['A']))} vs {self.describe(str(r['B']))}"
            )
        for r in self.kb.all("instructor_conflict(Id, A, B), lecturer(Id, Name)"):
            out.append(f"  {r['Name']} is double-booked: {self.describe(str(r['A']))} vs {self.describe(str(r['B']))}")
        for r in self.kb.all("office_hours_clash(Id, Day, S), lecturer(Id, Name)"):
            out.append(f"  {r['Name']}'s office hours on {str(r['Day']).title()} overlap {r['S']}")
        if not out:
            return "No scheduling conflicts found."
        return f"{len(out)} scheduling conflict(s):\n" + "\n".join(out)

    def describe(self, section: str) -> str:
        r = self.kb.all(f"section({atom(section)}, _, Term, _, _), meeting_text({atom(section)}, When)")[0]
        term = f" [{r['Term']}]" if str(r["Term"]) else ""
        return f"{section}{term} {r['When']}"

    def data_issues(self, m: re.Match[str]) -> str:
        found = self.kb.all("data_issue(Level, Message)")
        if not found:
            return "The C program reported no data issues."
        return "\n".join(f"  {str(r['Level']).upper()}: {r['Message']}" for r in found)

    def list_lecturers(self, m: re.Match[str]) -> str:
        found = self.kb.all("lecturer(Id, Name), teaching_load(Id, Sections, Credits)")
        rows = [[str(r["Id"]), str(r["Name"]), str(r["Sections"]), str(r["Credits"])] for r in found]
        return f"{len(rows)} lecturers:\n" + "\n".join(table([["ID", "Name", "Sections", "Credits"], *rows]))

    def fallback(self, text: str) -> str:
        """No pattern matched: a bare course code or lecturer name still gets a useful answer."""
        if COURSE_CODE_RE.search(text):
            return self.where_when(re.match(r"(?P<what>.+)", text))
        if name_words(text):
            try:
                self.lecturers(text)
            except QuestionError:
                pass
            else:
                return self.lecturer_classes(re.match(r"(?P<who>.+)", text))
        return "Sorry, I don't understand that question.\n" + HELP

    # ---- course syllabi

    def has_syllabus(self, code: str) -> bool:
        return self.kb.holds(f"syllabus_course({atom(code)}, _, _, _, _)")

    def course_group(self, code: str) -> list[str]:
        """The course and the codes it is cross-listed with: 'COSC 528' -> ['COSC 428', 'COSC 528']."""
        others = {str(r["B"]) for r in self.kb.all(f"cross_listed({atom(code)}, B)")}
        return sorted({code} | others)

    def course_label(self, code: str) -> str:
        """'COSC 428 / COSC 528 (Design and Analysis of Algorithms)', using the syllabus title when there is one."""
        title = self.kb.one(f"syllabus_course({atom(code)}, T, _, _, _)", "T") or self.kb.one(
            f"course({atom(code)}, T, _)", "T"
        )
        return f"{' / '.join(self.course_group(code))} ({title})"

    def prerequisite_text(self, group: list[str]) -> str:
        """'COSC 531'; for cross-listed codes with different prerequisites: 'COSC 214 (COSC 428 only)'."""
        per_code = {c: sorted(str(r["P"]) for r in self.kb.all(f"prerequisite({atom(c)}, P)")) for c in group}
        parts = []
        for pre in sorted({p for pres in per_code.values() for p in pres}):
            having = [c for c, pres in per_code.items() if pre in pres]
            parts.append(pre if len(having) == len(group) else f"{pre} ({', '.join(having)} only)")
        return ", ".join(parts) or "none listed"

    def syllabus_codes(self, what: str) -> list[str]:
        """Courses named in `what` that have a syllabus, one per cross-listed group."""
        codes, _ = self.courses(what)
        with_syllabus = [code for code in codes if self.has_syllabus(code)]
        if not with_syllabus:
            available = sorted({self.course_label(str(r["C"])) for r in self.kb.all("syllabus_course(C, _, _, _, _)")})
            listed = "; ".join(available) or "none"
            raise QuestionError(f"There is no syllabus for {', '.join(codes)}. Syllabi available: {listed}.")
        groups: dict[tuple[str, ...], str] = {}
        for code in with_syllabus:
            groups.setdefault(tuple(self.course_group(code)), code)
        return list(groups.values())

    def course_in(self, text: str) -> str:
        """The course part of a question: a course code, or the words after 'for' / 'in' / 'of' / 'does'."""
        if match := COURSE_CODE_RE.search(text):
            return match[0]
        if match := re.search(
            r"\b(?:for|in|of|does|do|is|about) (?:the )?(?P<what>[a-z][a-z &-]+?)(?: use| require| need| have)?$", text
        ):
            return match["what"]
        raise QuestionError("Name the course, e.g. 'COSC 631' or 'Database Information Systems'.")

    def prerequisites(self, m: re.Match[str]) -> str:
        out = []
        for code in self.syllabus_codes(self.course_in(m.string)):
            for member in self.course_group(code):
                direct = sorted({str(r["P"]) for r in self.kb.all(f"prerequisite({atom(member)}, P)")})
                deeper = sorted({str(r["P"]) for r in self.kb.all(f"requires({atom(member)}, P)")} - set(direct))
                level = self.kb.one(f"course_level({atom(member)}, L)", "L")
                if direct:
                    line = f"{member} ({level}) requires {', '.join(direct)}."
                    if deeper:
                        line += f" Those in turn require {', '.join(deeper)}."
                else:
                    line = f"{member} ({level}): no prerequisite is listed on the syllabus."
                out.append(line)
        return "\n".join(out)

    def can_take(self, m: re.Match[str]) -> str:
        done = sorted({f"{x[1].upper()} {x[2]}" for x in COURSE_CODE_RE.finditer(m["done"] or "")})
        what = m["what"]
        if m["done"] is None and (split := re.split(r"\b(?:if|after|with)\b", what, maxsplit=1)) and len(split) == 2:
            what = split[0]
        out = []
        for code in self.syllabus_codes(what):
            for member in self.course_group(code) if not COURSE_CODE_RE.search(what) else [self.courses(what)[0][0]]:
                missing = sorted(
                    {str(r["P"]) for r in self.kb.all(f"missing_prerequisite({atom(member)}, {plist(done)}, P)")}
                )
                needed = sorted({str(r["P"]) for r in self.kb.all(f"prerequisite({atom(member)}, P)")})
                if self.kb.holds(f"can_take({atom(member)}, {plist(done)})"):
                    reason = (
                        f"it requires {', '.join(needed)}, which you have" if needed else "no prerequisite is listed"
                    )
                    out.append(f"Yes: {member} - {reason}.")
                else:
                    out.append(f"Not yet: {member} requires {', '.join(missing)}.")
        return "\n".join(out)

    def grading(self, m: re.Match[str]) -> str:
        out = []
        for code in self.syllabus_codes(m["what"]):
            term = self.kb.one(f"syllabus_course({atom(code)}, _, _, Term, _)", "Term")
            parts = self.kb.all(f"grade_component({atom(code)}, Name, Pct)")
            out.append(f"{self.course_label(code)}, {term} syllabus")
            if parts:
                rows = [[str(r["Name"]), f"{r['Pct']}%"] for r in parts]
                total = self.kb.one(f"grade_total({atom(code)}, T)", "T")
                out += table([["Component", "Weight"], *rows, ["Total", f"{total}%"]])
            else:
                out.append("  The syllabus lists no grade weights.")
            bands = sorted(self.kb.all(f"grade_scale({atom(code)}, L, Min, Max)"), key=lambda r: -int(r["Min"]))
            if bands:
                out.append("  Letter grades: " + " · ".join(f"{r['L']} {r['Min']}-{r['Max']}" for r in bands))
            out.append("")
        return "\n".join(out).rstrip()

    def component_weight(self, m: re.Match[str]) -> str:
        comp = re.sub(r"\b(?:exam|test)\b", "", m["comp"]).strip() or m["comp"]
        out = []
        for code in self.syllabus_codes(m["what"]):
            found = self.kb.all(f"component_weight({atom(code)}, {atom(comp)}, Name, Pct)")
            if found:
                out += [f"{r['Name']} is worth {r['Pct']}% of the grade in {self.course_label(code)}." for r in found]
            else:
                names = ", ".join(str(r["N"]) for r in self.kb.all(f"grade_component({atom(code)}, N, _)"))
                out.append(
                    f"{self.course_label(code)} has no grade component matching '{m['comp']}'. Components: {names}."
                )
        return "\n".join(out)

    def letter_grade(self, m: re.Match[str]) -> str:
        score = float(m["score"])
        out = []
        for code in self.syllabus_codes(m["what"]):
            letter = self.kb.one(f"letter_grade({atom(code)}, {score}, L)", "L")
            if letter is None:
                out.append(f"{self.course_label(code)}: the syllabus scale does not cover {score:g}.")
                continue
            band = self.kb.all(f"grade_scale({atom(code)}, {atom(str(letter))}, Min, Max)")[0]
            out.append(
                f"{score:g} is a {letter} in {self.course_label(code)} ({letter} = {band['Min']}-{band['Max']})."
            )
        return "\n".join(out)

    def textbook(self, m: re.Match[str]) -> str:
        out = []
        for code in self.syllabus_codes(self.course_in(m.string)):
            texts = [str(r["T"]) for r in self.kb.all(f"textbook({atom(code)}, T)")]
            refs = [str(r["T"]) for r in self.kb.all(f"reference({atom(code)}, T)")]
            out.append(self.course_label(code))
            out += [f"  Required text: {t}" for t in texts] or ["  No required text is listed."]
            out += [f"  Reference:     {t}" for t in refs]
            out.append("")
        return "\n".join(out).rstrip()

    def week_rows(self, code: str, goal: str) -> list[list[str]]:
        found = self.kb.all(goal)
        rows = []
        for r in sorted(found, key=lambda r: int(r["Week"])):
            pages = ", ".join(str(x["P"]) for x in self.kb.all(f"week_reading({atom(code)}, {r['Week']}, P)"))
            rows.append([str(r["Week"]), str(r["Date"]) or "-", str(r["Topic"]), pages])
        return rows

    def week(self, m: re.Match[str]) -> str:
        out = []
        for code in self.syllabus_codes(m["what"]):
            rows = self.week_rows(code, f"week_topic({atom(code)}, Week, Date, Topic), Week =:= {int(m['week'])}")
            if not rows:
                out.append(f"{self.course_label(code)}: the syllabus has no week {m['week']}.")
                continue
            for week, date, topic, pages in rows:
                when = f" ({date})" if date != "-" else ""
                reading = f" Reading: {pages}." if pages else ""
                out.append(f"{self.course_label(code)}, week {week}{when}: {topic}.{reading}")
        return "\n".join(out)

    def outline(self, m: re.Match[str]) -> str:
        out = []
        for code in self.syllabus_codes(m["what"]):
            term = self.kb.one(f"syllabus_course({atom(code)}, _, _, Term, _)", "Term")
            rows = self.week_rows(code, f"week_topic({atom(code)}, Week, Date, Topic)")
            out.append(f"{self.course_label(code)}, {term}: weekly outline")
            out += table([["Week", "Date", "Topics and due work", "Reading"], *rows]) if rows else ["  No outline."]
            out.append("")
        return "\n".join(out).rstrip()

    def when_topic(self, m: re.Match[str]) -> str:
        topic = m["topic"].strip()
        if COURSE_CODE_RE.search(topic) and not m["what"]:  # "When does CTEC 435 meet?" is about the course
            return self.where_when(re.match(r"(?P<what>.+)", topic))
        codes = self.syllabus_codes(m["what"]) if m["what"] else None
        needle = re.sub(r"\s+", " ", topic)
        found = self.kb.all(f"topic_week(Code, {atom(needle)}, Week, Date, Topic)")
        seen, out = set(), []
        for r in sorted(found, key=lambda r: (str(r["Code"]), int(r["Week"]))):
            code = str(r["Code"])
            group = tuple(self.course_group(code))
            if (codes is not None and code not in codes) or (group, r["Week"]) in seen:
                continue
            seen.add((group, r["Week"]))
            date = f" ({r['Date']})" if str(r["Date"]) else ""
            out.append(f"{self.course_label(code)}: week {r['Week']}{date}: {r['Topic']}")
        if out:
            return "\n".join(out)
        if not m["what"]:  # maybe a lecturer: "When does Dr Stone teach?"
            try:
                self.lecturers(topic)
            except QuestionError:
                pass
            else:
                return self.lecturer_classes(re.match(r"(?P<who>.+)", topic))
        return f"No week in the course syllabi mentions '{topic}'."

    def course_info(self, m: re.Match[str]) -> str:
        what = m["what"]
        if not COURSE_CODE_RE.search(what):
            try:
                lecturers = self.lecturers(what)
            except QuestionError:
                lecturers = []
            if lecturers:
                return self.full_schedule(re.match(r"(?P<who>.+)", what))
        out = []
        for code in self.syllabus_codes(what):
            info = self.kb.all(f"syllabus_course({atom(code)}, _, Credits, Term, File)")[0]
            teacher = self.kb.one(f"syllabus_instructor({atom(code)}, N)", "N") or "not named"
            text = self.kb.one(f"description({atom(code)}, D)", "D") or "The syllabus has no description."
            out += [
                f"{self.course_label(code)}, {info['Credits']} credits, {info['Term']}",
                f"  Instructor:   {teacher}",
                f"  Prerequisite: {self.prerequisite_text(self.course_group(code))}",
                f"  Source:       {info['File']}",
                *(f"  {line}" for line in wrap(str(text), 100)),
                "",
            ]
        return "\n".join(out).rstrip()

    def contact(self, m: re.Match[str]) -> str:
        out = []
        for lec in self.lecturers(m["who"]):
            found = self.kb.all(f"faculty({atom(lec.id)}, _, Dept, Office, Email, Phone)")[0]
            out += [
                f"{lec.name} ({lec.id}) | {found['Dept']}",
                f"  Office: {found['Office'] or 'not listed'}",
                f"  Email:  {found['Email'] or 'not listed'}",
                f"  Phone:  {found['Phone'] or 'not listed'}",
                "",
            ]
        return "\n".join(out).rstrip()

    def important_dates(self, m: re.Match[str]) -> str:
        found = self.kb.all("important_date(Code, Event, Date)")
        if not found:
            return "No important dates are listed in the course syllabi."
        words = [w for w in re.findall(r"[a-z/]+", m.string) if w not in {"when", "is", "the", "what", "are", "for"}]
        specific = [r for r in found if all(w in str(r["Event"]).lower() for w in words if len(w) > 3)]
        rows, seen = [], set()
        for r in specific or found:
            group = tuple(self.course_group(str(r["Code"])))
            if (group, r["Event"]) in seen:
                continue
            seen.add((group, r["Event"]))
            term = self.kb.one(f"syllabus_course({atom(str(r['Code']))}, _, _, T, _)", "T")
            rows.append([str(r["Event"]), str(r["Date"]), f"{' / '.join(group)}, {term}"])
        return "\n".join(table([["Event", "Date", "From the syllabus of"], *rows]))

    def list_syllabi(self, m: re.Match[str]) -> str:
        found = self.kb.all("syllabus_course(Code, _, _, Term, File)")
        if not found:
            return "No course syllabi are loaded."
        rows, seen = [], set()
        for r in sorted(found, key=lambda r: str(r["Code"])):
            group = tuple(self.course_group(str(r["Code"])))
            if group in seen:
                continue
            seen.add(group)
            teacher = self.kb.one(f"syllabus_instructor({atom(str(r['Code']))}, N)", "N") or "-"
            rows.append([self.course_label(str(r["Code"])), str(r["Term"]), str(teacher), str(r["File"])])
        return f"{len(rows)} course syllabi:\n" + "\n".join(table([["Course", "Term", "Instructor", "File"], *rows]))

    # ---- raw Prolog

    def raw_query(self, goal: str) -> str:
        from pyswip.prolog import PrologError

        goal = goal.strip().rstrip(".")
        try:
            found = self.kb.all(goal)
        except PrologError as exc:
            return f"Prolog error: {exc}"
        if not found:
            return "false."
        lines = []
        for solution in found:
            bindings = ", ".join(f"{k} = {v}" for k, v in solution.items())
            lines.append(bindings or "true.")
        return "\n".join(lines)


HELP = """Questions I understand (names, course codes and days come from the data, not from this list):
  What classes does Dr Stone teach?            Who teaches CTEC 350?   Who teaches Python?
  What is Dr Stone's schedule?                 (classes and office hours together)
  What are Ruth Agada's office hours?          Which office hours are on Wednesday?
  Which classes meet on Tuesday?               Which classes are in room 108?
  Where does CTEC 435 meet?                    Who teaches on Friday?
  Is Dr Stone available on Monday at 3 PM?     How many sections does Carl Dixon teach?
  Are there any conflicts?                     List all lecturers
From the course syllabi:
  What are the prerequisites for COSC 631?     Can I take COSC 631 if I completed COSC 531?
  How is COSC 528 graded?                      How much is the midterm worth in COSC 631?
  What grade is 85 in COSC 631?                What textbook does COSC 528 use?
  What is covered in week 6 of COSC 528?       When is quicksort covered?
  What is the outline for COSC 631?            What is COSC 631 about?
  How do I contact Dr Yang?                    When is the last day to add/drop?
  Which syllabi do you have?
  ?- lecturer_of('CTEC 350', Name).            (any Prolog query)"""


# ----------------------------------------------------------------------------------------------- main


def run_file(answerer: Answerer, path: Path) -> int:
    """Answer every question in a file (one per line; '#' comments and blank lines skipped)."""
    for line in path.read_text(encoding="utf-8").splitlines():
        question = line.strip()
        if not question or question.startswith("#"):
            continue
        print(f"Q: {question}")
        print(answerer.answer(question))
        print()
    return 0


def repl(answerer: Answerer) -> int:
    print("Faculty schedule assistant. Type a question, 'help', or 'quit'.")
    while True:
        try:
            question = input("ask> ")
        except EOFError:
            print()
            return 0
        if clean(question) in {"quit", "exit", "q"}:
            return 0
        if question.strip():
            print(answerer.answer(question))
            print()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Ask questions about the faculty schedule (Prolog via pyswip).")
    parser.add_argument("question", nargs="*", help="question to answer; omit for interactive mode")
    parser.add_argument("--kb", type=Path, default=DEFAULT_KB, help="knowledge base written by gen_prolog.py")
    parser.add_argument("--file", type=Path, help="answer every question in this file")
    args = parser.parse_args(argv)

    try:
        kb = KnowledgeBase(args.kb)
    except ModuleNotFoundError:
        print("ask: the pyswip package is missing. Run: python3 -m pip install -r requirements.txt", file=sys.stderr)
        return 1
    except FileNotFoundError as exc:
        print(f"ask: {exc}", file=sys.stderr)
        return 1
    answerer = Answerer(kb)
    if args.file:
        return run_file(answerer, args.file)
    if args.question:
        print(answerer.answer(" ".join(args.question)))
        return 0
    return repl(answerer)


if __name__ == "__main__":
    sys.exit(main())
