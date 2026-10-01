#!/usr/bin/env python3
"""Export the department schedule workbook (.xlsx) to CSV for the C program.

Called from the Makefile:

    python3 scripts/xlsx_to_csv.py data/CTEC_Fall_2026_Schedule.xlsx data/class_schedule.csv

This is a faithful export, equivalent to Excel's "Save As > CSV": cell values are written as displayed
(times as "4:00 PM"), nothing is corrected or dropped. All validation and data repair happens in the
C program, so the same rules apply whether the CSV came from this script or from Excel directly.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import sys
from pathlib import Path


def format_cell(value: object) -> str:
    """Render one cell the way Excel displays it in this workbook."""
    if value is None:
        return ""
    if isinstance(value, dt.datetime):
        if value.time() == dt.time(0, 0):
            return value.date().isoformat()
        return value.strftime("%Y-%m-%d %I:%M %p")
    if isinstance(value, dt.time):
        return value.strftime("%I:%M %p").lstrip("0")
    if isinstance(value, float) and value.is_integer():
        return str(int(value))
    return str(value)


def export(workbook: Path, output: Path, sheet: str | None) -> int:
    """Write every non-empty row of the sheet to output; return the number of data rows."""
    from openpyxl import load_workbook

    wb = load_workbook(workbook, read_only=True, data_only=True)
    ws = wb[sheet] if sheet else wb.worksheets[0]
    rows = [[format_cell(v) for v in row] for row in ws.iter_rows(values_only=True)]
    wb.close()
    rows = [row for row in rows if any(cell.strip() for cell in row)]
    if not rows:
        sys.exit(f"xlsx_to_csv: {workbook} has no data")
    width = max(i + 1 for i, cell in enumerate(rows[0]) if cell.strip())  # trim empty trailing header columns
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle, lineterminator="\n")
        writer.writerows(row[:width] for row in rows)
    return len(rows) - 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Export an .xlsx class schedule to CSV.")
    parser.add_argument("workbook", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--sheet", help="worksheet name (default: first sheet)")
    args = parser.parse_args(argv)
    if not args.workbook.is_file():
        sys.exit(f"xlsx_to_csv: missing {args.workbook}")
    try:
        count = export(args.workbook, args.output, args.sheet)
    except ModuleNotFoundError as exc:
        print(f"xlsx_to_csv: missing Python package '{exc.name}'. Run: python3 -m pip install -r requirements.txt",
              file=sys.stderr)
        return 1
    print(f"xlsx_to_csv: wrote {count} sections to {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
