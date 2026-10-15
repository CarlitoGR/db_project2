#!/usr/bin/env python3
"""Generate the Prolog knowledge base (facts) from the schedule data.

Called from the Makefile after the C program has run:

    python3 scripts/gen_prolog.py --input output --faculty data/faculty.csv -o output/facts.pl

Pipeline position:

    class_schedule.csv + faculty.csv --(C: schedgen)--> output/*.csv --(this script)--> facts.pl --(Prolog)--> answers

The C program has already parsed, validated and normalised the class schedule (instructor aliases resolved,
bad times repaired, office hours generated). This script translates those results into Prolog FACTS only;
every RULE lives in prolog/rules.pl, written by hand. Nothing here is specific to one professor or course:
the facts are derived entirely from the two input files.

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
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
from collections.abc import Iterable, Iterator
from pathlib import Path

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


def section(title: str, facts: Iterable[str]) -> list[str]:
    body = list(facts)
    return [f"% ---- {title} ({len(body)} facts) ".ljust(100, "-"), *body, ""]


def build(input_dir: Path, faculty_path: Path) -> str:
    summary = read_csv(input_dir / "faculty_summary.csv")
    classes = read_csv(input_dir / "classes.csv")
    hours = read_csv(input_dir / "office_hours.csv")
    faculty_file = read_csv(faculty_path)
    issues = read_diagnostics(input_dir / "diagnostics.txt")

    header = [
        "% " + "=" * 98,
        "% Prolog knowledge base: FACTS. GENERATED by scripts/gen_prolog.py; do not edit by hand.",
        f"% Source: {input_dir}/ (C program output) and {faculty_path}.",
        "% Times are minutes after midnight (960 = 4:00 PM). Rules that use these facts: prolog/rules.pl",
        "% " + "=" * 98,
        "",
        # Facts for one predicate are not always contiguous (faculty/alias/name_token interleave); say so.
        ":- discontiguous faculty/6, office_hours_required/2, alias/2, name_token/2, section/5, listed_instructor/2, teaches/2,",
        "   meets/4, room_listed/2, room/2.",
        "",
    ]
    lines = header
    lines += section("faculty, aliases and name tokens", faculty_facts(summary, faculty_file))
    lines += section("courses, sections, meetings and rooms", class_facts(classes))
    lines += section("office hours (generated by the C program)", office_hour_facts(hours))
    lines += section(
        "data issues reported by the C program",
        (f"data_issue({level}, {atom(message)})." for level, message in issues),
    )
    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Generate Prolog facts from schedgen output.")
    parser.add_argument("--input", type=Path, default=Path("output"), help="directory holding schedgen CSV output")
    parser.add_argument("--faculty", type=Path, default=Path("data/faculty.csv"), help="faculty information file")
    parser.add_argument("-o", "--output", type=Path, default=Path("output/facts.pl"), help="Prolog file to write")
    args = parser.parse_args(argv)

    try:
        text = build(args.input, args.faculty)
    except (KeyError, ValueError) as exc:
        print(f"gen_prolog: bad input data: {exc}", file=sys.stderr)
        return 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(text, encoding="utf-8")
    facts = sum(1 for line in text.splitlines() if line and not line.startswith(("%", ":-", " ")))
    print(f"gen_prolog: wrote {facts} facts to {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
