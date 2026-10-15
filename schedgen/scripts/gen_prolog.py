#!/usr/bin/env python3
"""Generate the Prolog knowledge base (rules + facts) from the schedule data.

Called from the Makefile after the C program has run:

    python3 scripts/gen_prolog.py --input output --faculty data/faculty.csv -o output/knowledge_base.pl

Pipeline position:

    class_schedule.csv + faculty.csv --(C: schedgen)--> output/*.csv --(this script)--> knowledge_base.pl
                                                                            --(Prolog)--> answers

The C program has already parsed, validated and normalised the class schedule (instructor aliases resolved,
bad times repaired, office hours generated). This script writes ONE knowledge base file for Prolog:
    part 1, RULES: the rule library prolog/rules.pl (hand-written; independent of any department), and
    part 2, FACTS: generated here from the data.
Nothing here is specific to one professor or course: the facts are derived entirely from the two input files.

Fact schema (times are minutes after midnight, so rules can compare them with < and >):

    faculty(Id, Name, Department, Office, Email, Phone).
    name_token(Id, Token).                     lower-case word from the faculty name or any alias
    alias(Id, ListedName).                     spelling used in the class schedule
    course(Code, Title, Credits).
    section(SectionId, Code, Session, Meeting, Enrollment).
    listed_instructor(SectionId, ListedName).
    teaches(FacultyId, SectionId).
    meets(SectionId, Day, Start, End).         one fact per meeting day
    room_listed(SectionId, Text).              room exactly as the schedule lists it ("201/112", "Online", "Dept")
    room(SectionId, Room).                     one fact per PHYSICAL room ("201/112" -> 201 and 112)
    office_hours(FacultyId, Day, Start, End, Location, Source).
    office_hours_required(FacultyId, Minutes). per-week requirement the C program applied
    data_issue(Severity, Message).             diagnostics reported by the C program

From each course syllabus (PDF, read by syllabus.py):

    syllabus_course(Code, Title, Credits, Term, SourceFile).
    syllabus_teaches(FacultyId, Code, Term).   the syllabus's instructor (added to faculty/6 if not already there)
    cross_listed(Code, OtherCode).             "COSC 428.100, COSC 528.100" on one syllabus
    prerequisite(Code, PrerequisiteCode).
    textbook(Code, Text).   reference(Code, Text).   description(Code, Text).
    grade_component(Code, Name, Percent).      grade_scale(Code, Letter, Min, Max).
    week_topic(Code, Week, Date, Topic).       week_reading(Code, Week, Pages).
    important_date(Code, Event, Date).
    office_hours(...) with Source = syllabus,  syllabus_office_hours_course(FacultyId, Day, Start, Code),
    office_hours_note(FacultyId, Text).        "Other time by appointment"
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from collections.abc import Iterable, Iterator
from pathlib import Path

from syllabus import Syllabus, read_syllabus, summary

DAY_NAMES = {
    "M": "monday",
    "T": "tuesday",
    "W": "wednesday",
    "R": "thursday",
    "F": "friday",
    "S": "saturday",
    "U": "sunday",
}

# Words that are not part of a person's name. Dropped from name_token/2 so "Dr. D. B. Stone" matches "stone".
NAME_STOPWORDS = {"dr", "prof", "professor", "mr", "mrs", "ms", "jr", "sr", "phd"}

# Room values that are not a physical room, so they can never be double-booked. Same list as
# room_is_physical() in src/lib/loader.c, so Prolog and the C program agree on what a conflict is.
VIRTUAL_ROOMS = {"", "online", "dept", "tba", "tbd", "virtual", "zoom", "na", "none"}

TIME_RE = re.compile(r"^\s*(\d{1,2}):(\d{2})\s*([AP]M)\s*$", re.IGNORECASE)


def atom(value: object) -> str:
    """Render a Python value as a quoted Prolog atom: it is always a valid term, whatever the text."""
    text = str(value).replace("\\", "\\\\").replace("'", "\\'").replace("\n", " ")
    return f"'{text}'"


def to_minutes(clock: str) -> int:
    """'4:00 PM' -> 960. Raises ValueError for anything that is not an h:mm AM/PM time."""
    match = TIME_RE.match(clock)
    if match is None:
        raise ValueError(f"not a time: {clock!r}")
    hour, minute, meridiem = int(match[1]), int(match[2]), match[3].upper()
    if not (1 <= hour <= 12 and 0 <= minute <= 59):
        raise ValueError(f"not a time: {clock!r}")
    return (hour % 12 + (12 if meridiem == "PM" else 0)) * 60 + minute


def expand_days(codes: str) -> list[str]:
    """'MW' -> ['monday', 'wednesday']. Unknown letters are a data error, not something to skip silently."""
    days = []
    for letter in codes.strip().upper():
        if letter not in DAY_NAMES:
            raise ValueError(f"unknown day code {letter!r} in {codes!r}")
        days.append(DAY_NAMES[letter])
    return days


def name_tokens(*names: str) -> set[str]:
    """Lower-case name words of length >= 2 (initials and titles dropped): 'Stone, D. B.' -> {'stone'}."""
    tokens = set()
    for name in names:
        for word in re.findall(r"[A-Za-z][A-Za-z'\-]*", name):
            word = word.lower().strip("'-")
            if len(word) >= 2 and word not in NAME_STOPWORDS:
                tokens.add(word)
    return tokens


def split_rooms(room: str) -> list[str]:
    """'201/112' -> ['201', '112']; 'Online', 'Dept' -> []."""
    rooms = []
    for part in room.split("/"):
        part = part.strip()
        if part.lower() not in VIRTUAL_ROOMS and "online" not in part.lower():
            rooms.append(part)
    return rooms


def read_csv(path: Path) -> list[dict[str, str]]:
    """Read a CSV with a header row, skipping '#' comment lines (used by the faculty file)."""
    if not path.is_file():
        sys.exit(f"gen_prolog: missing input {path} (run 'make run' first)")
    with path.open(newline="", encoding="utf-8-sig") as handle:
        lines = (line for line in handle if not line.lstrip().startswith("#"))
        return [{k.strip(): (v or "").strip() for k, v in row.items() if k} for row in csv.DictReader(lines)]


def read_diagnostics(path: Path) -> list[tuple[str, str]]:
    """'ERROR: message' lines from the C program's diagnostics.txt."""
    if not path.is_file():
        return []
    issues = []
    for line in path.read_text(encoding="utf-8").splitlines():
        level, sep, message = line.partition(": ")
        if sep and level in {"ERROR", "WARNING"}:
            issues.append((level.lower(), message.strip()))
    return issues


def faculty_facts(summary: list[dict[str, str]], faculty_file: list[dict[str, str]]) -> Iterator[str]:
    aliases = {
        row["faculty_id"]: [a.strip() for a in row.get("aliases", "").split(";") if a.strip()] for row in faculty_file
    }
    for row in summary:
        fid = row["faculty_id"]
        yield "faculty({}, {}, {}, {}, {}, {}).".format(
            atom(fid),
            atom(row["name"]),
            atom(row["department"]),
            atom(row["office_hours_location"]),
            atom(row["email"]),
            atom(row["phone"]),
        )
        required = round(float(row["oh_required_hours"] or 0) * 60)
        yield f"office_hours_required({atom(fid)}, {required})."
        for listed in aliases.get(fid, []):
            yield f"alias({atom(fid)}, {atom(listed)})."
        for token in sorted(name_tokens(row["name"], *aliases.get(fid, []))):
            yield f"name_token({atom(fid)}, {atom(token)})."


def class_facts(classes: list[dict[str, str]]) -> Iterator[str]:
    courses: dict[str, tuple[str, str]] = {}
    for row in classes:
        courses.setdefault(row["course"], (row["title"], row["credits"] or "0"))
    for code, (title, credits) in sorted(courses.items()):
        yield f"course({atom(code)}, {atom(title)}, {int(credits)})."

    for row in classes:
        sid = row["section_id"]
        enrollment = int(row["enrollment"]) if row["enrollment"].isdigit() else 0
        meeting = row["meeting"].lower() or "scheduled"
        yield f"section({atom(sid)}, {atom(row['course'])}, {atom(row['session'])}, {meeting}, {enrollment})."
        yield f"listed_instructor({atom(sid)}, {atom(row['instructor_as_listed'])})."
        if row["faculty_id"]:
            yield f"teaches({atom(row['faculty_id'])}, {atom(sid)})."
        if row["days"] and row["start_time"] and row["end_time"]:
            start, end = to_minutes(row["start_time"]), to_minutes(row["end_time"])
            for day in expand_days(row["days"]):
                yield f"meets({atom(sid)}, {day}, {start}, {end})."
        yield f"room_listed({atom(sid)}, {atom(row['room'] or 'TBA')})."
        for room in split_rooms(row["room"]):
            yield f"room({atom(sid)}, {atom(room)})."


def office_hour_facts(rows: list[dict[str, str]]) -> Iterator[str]:
    for row in rows:
        start, end = to_minutes(row["start_time"]), to_minutes(row["end_time"])
        source = row["source"].lower() or "generated"
        for day in expand_days(row["days"]):
            yield "office_hours({}, {}, {}, {}, {}, {}).".format(
                atom(row["faculty_id"]), day, start, end, atom(row["location"]), atom(source)
            )


def faculty_name_tokens(summary_rows: list[dict[str, str]], faculty_file: list[dict[str, str]]) -> dict[str, set[str]]:
    """faculty id -> every name word of the lecturer (name and aliases)."""
    aliases = {row["faculty_id"]: row.get("aliases", "").split(";") for row in faculty_file}
    return {row["faculty_id"]: name_tokens(row["name"], *aliases.get(row["faculty_id"], [])) for row in summary_rows}


def syllabus_facts(syllabi: list[Syllabus], known: dict[str, set[str]], scheduled: set[str]) -> Iterator[str]:
    """Facts from the syllabi. An instructor already in the faculty file is matched by name; anyone else is
    added as a lecturer with id SY01, SY02, ... A department named on one syllabus is used for every syllabus
    with the same course prefix."""
    known = dict(known)
    departments = {
        code.split()[0]: f"{syl.department} ({code.split()[0]})"
        for syl in syllabi
        if syl.department
        for code in syl.codes
    }
    added: dict[str, str] = {}
    courses = set(scheduled)
    for syl in syllabi:
        fid = ""
        if syl.instructor:
            tokens = name_tokens(syl.instructor)
            fid = next((i for i, t in known.items() if tokens and tokens <= t), "") or added.get(syl.instructor, "")
            if not fid:
                fid = added[syl.instructor] = f"SY{len(added) + 1:02d}"
                known[fid] = tokens
                department = departments.get(syl.codes[0].split()[0], syl.codes[0].split()[0])
                yield "faculty({}, {}, {}, {}, {}, {}).".format(
                    atom(fid),
                    atom(syl.instructor),
                    atom(department),
                    atom(syl.office or "Office TBA"),
                    atom(syl.email),
                    atom("; ".join(syl.phones)),
                )
                yield from (f"name_token({atom(fid)}, {atom(t)})." for t in sorted(tokens))
        for code in syl.codes:
            c = atom(code)
            yield f"syllabus_course({c}, {atom(syl.title)}, {syl.credits}, {atom(syl.term)}, {atom(syl.source.name)})."
            if code not in courses:
                courses.add(code)
                yield f"course({c}, {atom(syl.title)}, {syl.credits})."
            if fid:
                yield f"syllabus_teaches({atom(fid)}, {c}, {atom(syl.term)})."
            yield from (f"cross_listed({c}, {atom(other)})." for other in syl.codes if other != code)
            yield from (f"prerequisite({c}, {atom(pre)})." for pre in syl.prerequisites.get(code, []))
            yield from (f"textbook({c}, {atom(text)})." for text in syl.texts)
            yield from (f"reference({c}, {atom(text)})." for text in syl.references)
            if syl.description:
                yield f"description({c}, {atom(syl.description)})."
            yield from (f"grade_component({c}, {atom(name)}, {pct:g})." for name, pct in syl.weights)
            yield from (f"grade_scale({c}, {atom(letter)}, {low}, {high})." for letter, low, high in syl.scale)
            for week, date, topic, readings in syl.weeks:
                yield f"week_topic({c}, {week}, {atom(date)}, {atom(topic)})."
                yield from (f"week_reading({c}, {week}, {atom(pages)})." for pages in readings)
            yield from (f"important_date({c}, {atom(event)}, {atom(date)})." for event, date in syl.dates)
        if fid:
            for block in syl.office_hours:
                for day in block.days:
                    yield (
                        f"office_hours({atom(fid)}, {day}, {block.start}, {block.end}, "
                        f"{atom(syl.office or 'Office TBA')}, syllabus)."
                    )
                    yield from (
                        f"syllabus_office_hours_course({atom(fid)}, {day}, {block.start}, {atom(code)})."
                        for code in syl.codes
                    )
            yield from (f"office_hours_note({atom(fid)}, {atom(note)})." for note in syl.office_hours_notes)


def section(title: str, facts: Iterable[str]) -> list[str]:
    body = list(facts)
    return [f"% ---- {title} ({len(body)} facts) ".ljust(100, "-"), *body, ""]


def build(input_dir: Path, faculty_path: Path, rules_path: Path, syllabus_paths: list[Path]) -> tuple[str, int]:
    """The complete knowledge base: RULES (from the rule library) followed by FACTS (from the data).

    Returns (text, number of facts).
    """
    summary_rows = read_csv(input_dir / "faculty_summary.csv")
    classes = read_csv(input_dir / "classes.csv")
    hours = read_csv(input_dir / "office_hours.csv")
    faculty_file = read_csv(faculty_path)
    issues = read_diagnostics(input_dir / "diagnostics.txt")
    if not rules_path.is_file():
        sys.exit(f"gen_prolog: missing rule library {rules_path}")
    rules = rules_path.read_text(encoding="utf-8").rstrip()

    facts: list[str] = []
    facts += section("faculty, aliases and name tokens", faculty_facts(summary_rows, faculty_file))
    facts += section("courses, sections, meetings and rooms", class_facts(classes))
    facts += section("office hours (generated by the C program)", office_hour_facts(hours))
    facts += section(
        "data issues reported by the C program",
        (f"data_issue({level}, {atom(message)})." for level, message in issues),
    )
    syllabi = [read_syllabus(path) for path in syllabus_paths]
    for syl in syllabi:
        print(f"gen_prolog: syllabus {summary(syl)}")
    if syllabi:
        known = faculty_name_tokens(summary_rows, faculty_file)
        scheduled = {row["course"] for row in classes}
        facts += section("course syllabi (read from the syllabus PDFs)", syllabus_facts(syllabi, known, scheduled))
    count = sum(1 for line in facts if line and not line.startswith("%"))

    banner = "% " + "=" * 98
    lines = [
        banner,
        "% Prolog KNOWLEDGE BASE. GENERATED by scripts/gen_prolog.py; do not edit by hand.",
        f"%   Part 1, RULES: copied from {rules_path} (hand-written; how the facts relate).",
        f"%   Part 2, FACTS: generated from {input_dir}/ (C program output), {faculty_path}"
        + (f" and {len(syllabus_paths)} syllabus PDF(s)." if syllabus_paths else "."),
        "% Load it with:  swipl knowledge_base.pl     or, in Python:  Prolog().consult('knowledge_base.pl')",
        "% Times are minutes after midnight (960 = 4:00 PM).",
        banner,
        "",
        # Facts for one predicate are not always contiguous (faculty/alias/name_token interleave); say so.
        # Course titles, names and syllabus text are UTF-8; say so, or Prolog reads them in the locale's encoding.
        ":- encoding(utf8).",
        # Facts for one predicate are not always contiguous (faculty/alias/name_token, and the per-course
        # syllabus facts, interleave); say so.
        ":- discontiguous faculty/6, office_hours_required/2, alias/2, name_token/2, course/3, section/5,",
        "   listed_instructor/2, teaches/2, meets/4, room_listed/2, room/2, office_hours/6,",
        "   syllabus_course/5, syllabus_teaches/3, cross_listed/2, prerequisite/2, textbook/2, reference/2,",
        "   description/2, grade_component/3, grade_scale/4, week_topic/4, week_reading/3, important_date/3,",
        "   syllabus_office_hours_course/4.",
        "",
        "% " + "#" * 98,
        f"% PART 1: RULES ({rules_path})",
        "% " + "#" * 98,
        "",
        rules,
        "",
        "% " + "#" * 98,
        f"% PART 2: FACTS ({count} facts)",
        "% " + "#" * 98,
        "",
        *facts,
    ]
    return "\n".join(lines), count


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Generate the Prolog knowledge base (rules + facts).")
    parser.add_argument("--input", type=Path, default=Path("output"), help="directory holding schedgen CSV output")
    parser.add_argument("--faculty", type=Path, default=Path("data/faculty.csv"), help="faculty information file")
    parser.add_argument("--rules", type=Path, default=Path("prolog/rules.pl"), help="rule library to include")
    parser.add_argument("--syllabi", type=Path, nargs="*", default=[], help="course syllabus PDFs to read")
    parser.add_argument(
        "-o", "--output", type=Path, default=Path("output/knowledge_base.pl"), help="knowledge base to write"
    )
    args = parser.parse_args(argv)

    try:
        text, count = build(args.input, args.faculty, args.rules, sorted(args.syllabi))
    except (KeyError, ValueError, OSError) as exc:
        print(f"gen_prolog: bad input data: {exc}", file=sys.stderr)
        return 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text + "\n", encoding="utf-8")
    print(f"gen_prolog: wrote {count} facts + the rules from {args.rules} to {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
