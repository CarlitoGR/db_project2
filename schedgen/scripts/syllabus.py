"""Read a course syllabus (PDF) into structured data for the Prolog knowledge base.

Used by gen_prolog.py:  python3 scripts/gen_prolog.py ... --syllabi data/syllabi/*.pdf

Syllabi have no fixed format, so the reader works from SECTION LABELS that syllabi commonly use ("Instructor:",
"Office Hours:", "Prerequisite:", "Outline:", "EVALUATION METHOD:", ...), in any case, order or layout, with
the label's text either on the same line or on the lines below. Each field has its own extractor. Anything not
found is left empty rather than guessed. Nothing here is specific to one course or instructor.

    python3 scripts/syllabus.py data/syllabi/COSC_631_Syllabus_Spring_2022.pdf      prints what was extracted
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

DAY_WORDS = {
    "mon": "monday",
    "monday": "monday",
    "tue": "tuesday",
    "tues": "tuesday",
    "tuesday": "tuesday",
    "wed": "wednesday",
    "wednesday": "wednesday",
    "thu": "thursday",
    "thur": "thursday",
    "thurs": "thursday",
    "thursday": "thursday",
    "fri": "friday",
    "friday": "friday",
    "sat": "saturday",
    "saturday": "saturday",
    "sun": "sunday",
    "sunday": "sunday",
}
DAY_LETTERS = {"M": "monday", "T": "tuesday", "W": "wednesday", "R": "thursday", "F": "friday", "S": "saturday"}

# Section label (normalised: lower case, single spaces, no trailing '!') -> field it introduces.
LABELS = {
    "ada statement": "other",
    "important dates": "dates",
    "instructor": "instructor",
    "professor": "instructor",
    "office location": "office",
    "office": "office",
    "email": "email",
    "e-mail": "email",
    "phone": "phone",
    "telephone": "phone",
    "office hours": "office_hours",
    "prerequisite": "prerequisites",
    "prerequisites": "prerequisites",
    "text": "text",
    "texts": "text",
    "textbook": "text",
    "required text": "text",
    "required textbook": "text",
    "references": "references",
    "recommended reading": "references",
    "recommended reading list": "references",
    "instruction modes": "other",
    "teaching mode": "other",
    "description": "description",
    "catalog description": "description",
    "course description": "description",
    "objectives": "other",
    "requirements": "weights",
    "evaluation method": "weights",
    "evaluation": "weights",
    "grading": "grading",
    "grading policy": "grading",
    "outline": "outline",
    "course outline": "outline",
    "tentative time schedule": "outline",
    "tentative schedule": "outline",
    "course schedule": "outline",
    "abet material": "other",
    "important reminders": "other",
    "important": "other",
    "policy on cell phones, etc.": "other",
}

COURSE_RE = re.compile(r"\b([A-Z]{3,4})\s?(\d{3})(?:\.(\d{3}))?\b")
LABEL_RE = re.compile(r"^(?P<indent>\s{0,3})(?P<label>[A-Za-z][A-Za-z &/.,!-]*?)\s*(?::\s*(?P<rest>.*))?$")
TERM_RE = re.compile(r"\b(Fall|Spring|Summer|Winter)\s+(\d{4})\b", re.IGNORECASE)
CREDITS_RE = re.compile(r"\b(\d)\s*Credits?\b", re.IGNORECASE)
EMAIL_RE = re.compile(r"[\w.+-]+@[\w-]+(?:\.[\w-]+)+")
DATE_RE = re.compile(r"\d{1,2}/\d{1,2}(?:-\d{1,2})?/\d{2,4}")
PAGES_RE = re.compile(r"^pp?\.\s*\d+(?:\s*-\s*\d+)?$")
WEEK_RE = re.compile(r"^\s*Week\s*#?\s*(\d+)\s*:?\s*(.*)$", re.IGNORECASE)
WEIGHT_RE = re.compile(r"^\W*(?P<name>[A-Za-z][^%]*?)\s{2,}(?P<pct>\d+(?:\.\d+)?)\s*%\s*$")
SCALE_LETTER_FIRST = re.compile(r"^\s*([A-F][+-]?)\s+(\d+)\s*%?\s*-\s*(\d+)\s*%?\s*$")
SCALE_RANGE_FIRST = re.compile(r"^\s*(\d+)\s*-\s*(\d+)\s*=\s*([A-F][+-]?)\s*$")
SCALE_BELOW = re.compile(r"^\s*below\s+(\d+)\s*=\s*([A-F])\s*$", re.IGNORECASE)
HOURS_RE = re.compile(
    r"^(?P<days>[A-Za-z]+(?:\s*[/,&]\s*[A-Za-z]+)*)\.?\s+"
    r"(?P<start>\d{1,2}(?::\d{2})?)\s*(?P<sm>[ap]\.?m?\.?)?\s*[-–]\s*"
    r"(?P<end>\d{1,2}(?::\d{2})?)\s*(?P<em>[ap]\.?m?\.?)?\s*$",
    re.IGNORECASE,
)


@dataclass
class OfficeHours:
    days: list[str]
    start: int  # minutes after midnight
    end: int


@dataclass
class Syllabus:
    source: Path
    codes: list[str] = field(default_factory=list)  # ['COSC 428', 'COSC 528'] when cross-listed
    title: str = ""
    credits: int = 0
    term: str = ""
    department: str = ""
    instructor: str = ""
    office: str = ""
    email: str = ""
    phones: list[str] = field(default_factory=list)
    office_hours: list[OfficeHours] = field(default_factory=list)
    office_hours_notes: list[str] = field(default_factory=list)
    prerequisites: dict[str, list[str]] = field(default_factory=dict)  # course code -> prerequisite codes
    texts: list[str] = field(default_factory=list)
    references: list[str] = field(default_factory=list)
    description: str = ""
    weights: list[tuple[str, float]] = field(default_factory=list)
    scale: list[tuple[str, int, int]] = field(default_factory=list)  # (letter, min, max)
    weeks: list[tuple[int, str, str, list[str]]] = field(default_factory=list)  # (week, date, topic, readings)
    dates: list[tuple[str, str]] = field(default_factory=list)  # (event, date)


# ------------------------------------------------------------------------------------------------ text


def pdf_lines(path: Path) -> list[str]:
    """Text of every page, layout preserved (columns stay apart), blank and page-number lines removed."""
    from pypdf import PdfReader

    text = "\n".join(page.extract_text(extraction_mode="layout") or "" for page in PdfReader(path).pages)
    lines = [line.rstrip() for line in text.splitlines()]
    return [line for line in lines if line.strip() and not re.fullmatch(r"\s*-\s*\d+\s*-\s*", line)]


def tidy(text: str) -> str:
    """Collapse layout spacing and undo PDF artefacts: 'Systems , Elmasri' -> 'Systems, Elmasri', '7 th' -> '7th'."""
    text = re.sub(r"[\uf000-\uf8ff\u2022\u25aa\u25cf]", " ", text)  # bullets (incl. Symbol-font private-use)
    text = re.sub(r"\s+", " ", text).strip()
    text = re.sub(r"\s+([,.;:)])", r"\1", text)
    text = re.sub(r"\(\s+", "(", text)
    text = re.sub(r"\b(\d+) (st|nd|rd|th)\b", r"\1\2", text)
    return re.sub(r",(?=[,.])", "", text)


def join_lines(lines: list[str]) -> str:
    """Join wrapped lines; a line ending in '-' continues the word ('Addison-' + 'Wesley')."""
    out = ""
    for line in (line.strip() for line in lines):
        if not line:
            continue
        out = out + line if out.endswith("-") else f"{out} {line}" if out else line
    return tidy(out)


def cells(line: str) -> list[str]:
    """Columns of a layout line: text separated by two or more spaces."""
    return [cell.strip() for cell in re.split(r"\s{2,}", line.strip()) if cell.strip()]


def course_code(match: re.Match[str]) -> str:
    return f"{match[1]} {match[2]}"


def to_minutes(clock: str, meridiem: str) -> int:
    hour, _, minute = clock.partition(":")
    hour_i, minute_i = int(hour), int(minute or 0)
    if not (1 <= hour_i <= 12 and 0 <= minute_i <= 59):
        raise ValueError(clock)
    return (hour_i % 12 + (12 if meridiem == "p" else 0)) * 60 + minute_i


# ------------------------------------------------------------------------------------------ sections


def split_sections(lines: list[str]) -> tuple[list[str], dict[str, list[str]]]:
    """(header lines before the first label, {field: body lines}). A label's inline text is its first line."""
    header: list[str] = []
    sections: dict[str, list[str]] = {}
    current: list[str] | None = None
    for line in lines:
        match = LABEL_RE.match(line)
        key = None
        if match:
            label = re.sub(r"\s+", " ", match["label"]).strip().lower().rstrip("!").strip()
            inline = match["rest"] is not None
            # A label without a colon only counts when it is the whole line ("Important Dates", "IMPORTANT!").
            if label in LABELS and (inline or line.strip().lower().rstrip("!").strip() == label):
                key = LABELS[label]
        if key is not None:
            current = sections.setdefault(key, [])
            if match["rest"]:
                current.append(match["rest"])
        elif current is None:
            header.append(line)
        else:
            current.append(line)
    return header, sections


def read_header(syl: Syllabus, header: list[str], all_lines: list[str]) -> None:
    text = "\n".join(header)
    codes = [course_code(m) for m in COURSE_RE.finditer(text)]
    syl.codes = list(dict.fromkeys(codes))
    if match := TERM_RE.search(text):
        syl.term = f"{match[1].title()} {match[2]}"
    if match := CREDITS_RE.search(text):
        syl.credits = int(match[1])
    if match := re.search(r"Department of ([A-Z][A-Za-z ]+)", "\n".join(all_lines)):
        syl.department = match[1].strip()
    # Title: the rest of the course-code line ("COSC 631 Database Information Systems II (3 Credits)"), or else
    # the first header line that is not the university, school, department, term, credits or the word Syllabus.
    skip = re.compile(
        r"universit|univ\.|school of|department of|^syllabus|credits?$|^(fall|spring|summer|winter)\b", re.IGNORECASE
    )
    for line in header:
        first = cells(line)[0] if cells(line) else ""
        candidate = COURSE_RE.sub("", first)
        candidate = re.sub(r"\(\s*\d\s*Credits?\s*\)", "", candidate, flags=re.IGNORECASE)
        candidate = tidy(candidate.strip(" ,;–-"))
        if len(candidate.split()) >= 2 and not skip.search(candidate):
            syl.title = candidate
            break


def read_people(syl: Syllabus, sections: dict[str, list[str]]) -> None:
    block = [line.strip() for line in sections.get("instructor", []) if line.strip()]
    if block:
        syl.instructor = tidy(block[0])
    for line in block[1:]:
        if email := EMAIL_RE.search(line):
            syl.email = syl.email or email[0]
        elif line.lower().startswith(("phone", "tel")):
            syl.phones.append(tidy(line.split(":", 1)[-1]))
        elif not syl.office:
            syl.office = tidy(line)
    if office := [line for line in sections.get("office", []) if line.strip()]:
        syl.office = join_lines(office)
    if email := EMAIL_RE.search(" ".join(sections.get("email", []))):
        syl.email = email[0]
    syl.phones += [tidy(line) for line in sections.get("phone", []) if line.strip()]


def read_office_hours(syl: Syllabus, sections: dict[str, list[str]]) -> None:
    for line in (tidy(line) for line in sections.get("office_hours", [])):
        if not line:
            continue
        match = HOURS_RE.match(line)
        days = parse_days(match["days"]) if match else []
        if not match or not days:
            syl.office_hours_notes.append(line)
            continue
        end_m = (match["em"] or match["sm"] or "p")[0].lower()
        start_m = (match["sm"] or match["em"] or "p")[0].lower()
        end = to_minutes(match["end"], end_m)
        start = to_minutes(match["start"], start_m)
        if start >= end and not match["sm"]:  # "11:00-1:00p": the start is in the morning
            start = to_minutes(match["start"], "a")
        if start < end:
            syl.office_hours.append(OfficeHours(days, start, end))
        else:
            syl.office_hours_notes.append(line)


def parse_days(text: str) -> list[str]:
    """'MW' / 'TR' / 'Thu' / 'Mon/Wed' -> day names; [] if the text is not a list of days."""
    days: list[str] = []
    for word in re.split(r"\s*[/,&]\s*", text.strip()):
        if word.lower().rstrip(".") in DAY_WORDS:
            days.append(DAY_WORDS[word.lower().rstrip(".")])
        elif word.isupper() and all(letter in DAY_LETTERS for letter in word):
            days += [DAY_LETTERS[letter] for letter in word]
        else:
            return []
    return list(dict.fromkeys(days))


def read_course_content(syl: Syllabus, sections: dict[str, list[str]]) -> None:
    for line in sections.get("prerequisites", []):
        found = [course_code(m) for m in COURSE_RE.finditer(line)]
        if not found:
            continue
        level = line.lower()
        targets = syl.codes
        if "undergrad" in level:
            targets = [c for c in syl.codes if int(c[-3:]) < 500] or syl.codes
        elif "grad" in level:
            targets = [c for c in syl.codes if int(c[-3:]) >= 500] or syl.codes
        for code in targets:
            syl.prerequisites.setdefault(code, []).extend(p for p in found if p not in syl.prerequisites.get(code, []))
    if text := join_lines(sections.get("text", [])):
        syl.texts.append(text)
    references = join_lines(sections.get("references", []))
    syl.references = [r for r in re.split(r"(?<=\d{4}\.)\s+(?=[A-Z])", references) if r]
    syl.description = join_lines(sections.get("description", []))


def read_grading(syl: Syllabus, sections: dict[str, list[str]], all_lines: list[str]) -> None:
    for line in sections.get("weights", []) + sections.get("grading", []):
        if match := WEIGHT_RE.match(line):
            syl.weights.append((tidy(match["name"]), float(match["pct"])))
    for line in all_lines:
        if match := SCALE_LETTER_FIRST.match(line):
            low, high = sorted((int(match[2]), int(match[3])))
            syl.scale.append((match[1], low, high))
        elif match := SCALE_RANGE_FIRST.match(line):
            low, high = sorted((int(match[1]), int(match[2])))
            syl.scale.append((match[3], low, high))
        elif match := SCALE_BELOW.match(line):
            syl.scale.append((match[2], 0, int(match[1]) - 1))


def read_outline(syl: Syllabus, sections: dict[str, list[str]], all_lines: list[str]) -> None:
    lines = sections.get("outline") or all_lines
    weeks: list[tuple[int, str, list[str], list[str]]] = []  # (week, date, topic cells, readings)
    for line in lines:
        if match := WEEK_RE.match(line):
            rest = match[2].strip()
            date = ""
            if dm := re.match(r"^(\d{1,2}/\d{1,2}(?:/\d{2,4})?)\s+(.*)$", rest):
                date, rest = dm[1], dm[2]
            parts = cells(rest)
            weeks.append(
                (
                    int(match[1]),
                    date,
                    [p for p in parts if not PAGES_RE.match(p)],
                    [p for p in parts if PAGES_RE.match(p)],
                )
            )
        elif weeks:  # a wrapped topic line or an extra reading under the last week
            parts = cells(line)
            _, _, topic, readings = weeks[-1]
            readings += [p for p in parts if PAGES_RE.match(p)]
            words = [p for p in parts if not PAGES_RE.match(p)]
            if words and topic:
                topic[-1] = f"{topic[-1]} {' '.join(words)}"
            elif words:
                topic += words
    for week, date, topic, readings in weeks:
        text = " · ".join(tidy(t).strip(" ,") for t in topic if tidy(t).strip(" ,"))
        syl.weeks.append((week, date, text, [tidy(r) for r in readings]))


def read_dates(syl: Syllabus, sections: dict[str, list[str]]) -> None:
    for line in sections.get("dates", []):
        parts = cells(line)
        if len(parts) >= 2 and DATE_RE.fullmatch(parts[-1]):
            syl.dates.append((tidy(" ".join(parts[:-1])), parts[-1]))


def read_syllabus(path: Path) -> Syllabus:
    lines = pdf_lines(path)
    header, sections = split_sections(lines)
    syl = Syllabus(source=path)
    read_header(syl, header, lines)
    if not syl.codes:
        raise ValueError(f"{path}: no course code (e.g. 'COSC 631') found at the top of the syllabus")
    read_people(syl, sections)
    read_office_hours(syl, sections)
    read_course_content(syl, sections)
    read_grading(syl, sections, lines)
    read_outline(syl, sections, lines)
    read_dates(syl, sections)
    return syl


def summary(syl: Syllabus) -> str:
    """One line describing what was extracted, printed by gen_prolog.py."""
    return (
        f"{syl.source.name}: {' / '.join(syl.codes)} {syl.title!r} {syl.term}, {syl.instructor or 'no instructor'}, "
        f"{len(syl.office_hours)} office-hour block(s), {sum(len(p) for p in syl.prerequisites.values())} "
        f"prerequisite(s), {len(syl.weights)} grade component(s), {len(syl.scale)} grade band(s), "
        f"{len(syl.weeks)} week(s), {len(syl.dates)} date(s)"
    )


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        s = read_syllabus(Path(arg))
        print(summary(s))
        for name, value in vars(s).items():
            print(f"  {name}: {value}")
