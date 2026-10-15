# =================================================================================================
# Faculty Schedule Generator - Makefile
#
#   make            build the static library and the program            (default target: all)
#   make run        run the program on the two input files in data/
#   make docs       convert the program's CSV output to Word + Excel via Python
#   make test       build and run unit tests + an end-to-end conflict test
#   make inspect    show what is inside the static library (ar -t, nm)
#   make csv        export the department workbook (.xlsx) to the CSV the C program reads
#   make kb         Python writes the Prolog knowledge base, rules + facts (output/knowledge_base.pl)
#   make ask        ask a question:  make ask Q="What classes does Dr Stone teach?"  (no Q = interactive)
#   make demo       answer every question in data/questions.txt -> output/qa_demo.txt
#   make test-kb    Prolog rule tests (plunit) + Python tests + end-to-end question test
#   make fetch      OPTIONAL: download the input files from URLs instead of using data/
#   make dist       clean build + all tests, then zip the submission: dist/Carlson_Cox_COSC631_Midterm.zip
#   make clean      remove build products     make distclean   also remove output/, .venv/ and dist/
#
# Dependency chain (make rebuilds only what is out of date):
#   src/lib/*.c --> build/obj/lib/*.o --> build/libschedule.a --+
#   src/main.c  --> build/obj/main.o  -------------------------+--> build/schedgen
#   data/CTEC_Fall_2026_Schedule.xlsx --(scripts/xlsx_to_csv.py)--> data/class_schedule.csv
#   build/schedgen + data/class_schedule.csv + data/faculty.csv --> output/*.csv, output/schedule.txt
#   output/*.csv + scripts/export_docs.py --> output/Faculty_Schedules.docx / .xlsx
#   output/*.csv + data/faculty.csv + data/syllabi/*.pdf + prolog/rules.pl
#                                            --(scripts/gen_prolog.py)--> output/knowledge_base.pl
#   output/knowledge_base.pl --(scripts/ask.py: pyswip -> SWI-Prolog)--> answers
# =================================================================================================

# ---- Toolchain -----------------------------------------------------------------------------------
CC       := gcc
AR       := ar
ARFLAGS  := rcs
PYTHON   ?= python3
SWIPL    ?= swipl
CPPFLAGS := -Iinclude -D_POSIX_C_SOURCE=200809L
CFLAGS   ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wformat=2
DEPFLAGS  = -MMD -MP
LDFLAGS   = -L$(BUILD)
LDLIBS   := -lschedule

# ---- Layout --------------------------------------------------------------------------------------
BUILD    := build
OBJDIR   := $(BUILD)/obj
LIB      := $(BUILD)/libschedule.a
BIN      := $(BUILD)/schedgen
OUT      := output

# ---- The two input files (local by default; override on the command line) -------------------------
# SCHEDULE_XLSX is the department's spreadsheet; SYLLABUS is its CSV export (input file 1).
SCHEDULE_XLSX := data/CTEC_Fall_2026_Schedule.xlsx
SYLLABUS      := data/class_schedule.csv
FACULTY       := data/faculty.csv
BUFFER        := 15
# STRICT=1 makes 'make run' fail when the schedule contains conflicts (program exit status 3).
STRICT        ?= 0

# Optional remote sources used only by 'make fetch'
SCHEDULE_URL ?=
FACULTY_URL  ?=

# ---- Sources -------------------------------------------------------------------------------------
LIB_SRCS := $(wildcard src/lib/*.c)
LIB_OBJS := $(patsubst src/lib/%.c,$(OBJDIR)/lib/%.o,$(LIB_SRCS))
MAIN_OBJ := $(OBJDIR)/main.o
TEST_SRC := tests/test_schedule.c
TEST_BIN := $(BUILD)/test_schedule
DEPS     := $(LIB_OBJS:.o=.d) $(MAIN_OBJ:.o=.d) $(OBJDIR)/test_schedule.d

CSV_OUT  := $(OUT)/faculty_summary.csv $(OUT)/classes.csv $(OUT)/office_hours.csv
TXT_OUT  := $(OUT)/schedule.txt
DOCX_OUT := $(OUT)/Faculty_Schedules.docx
XLSX_OUT := $(OUT)/Faculty_Schedules.xlsx

.PHONY: all lib csv run docs test inspect fetch clean distclean help venv kb ask demo test-kb dist

# ---- Default target ------------------------------------------------------------------------------
all: $(BIN)

lib: $(LIB)

# ---- Compile: one object per source; -MMD -MP writes .d files so header edits trigger rebuilds -----
$(OBJDIR)/lib/%.o: src/lib/%.c | $(OBJDIR)/lib
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJDIR)/test_schedule.o: $(TEST_SRC) | $(OBJDIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# ---- Archive: bundle library objects into a static library ----------------------------------------
# ar r = insert/replace members, c = create silently, s = write the symbol index (same as ranlib)
$(LIB): $(LIB_OBJS)
	$(AR) $(ARFLAGS) $@ $^
	@echo "Built static library $@ ($(words $^) object files)"

# ---- Link: main.o first, then the library (the linker resolves symbols left to right) --------------
$(BIN): $(MAIN_OBJ) $(LIB)
	$(CC) $(CFLAGS) $(MAIN_OBJ) $(LDFLAGS) $(LDLIBS) -o $@

$(TEST_BIN): $(OBJDIR)/test_schedule.o $(LIB)
	$(CC) $(CFLAGS) $< $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD) $(OBJDIR) $(OBJDIR)/lib $(OUT):
	mkdir -p $@

# ---- Generate the schedule (re-runs only when the program or an input file changed) --------------
# One program run produces several files, so a stamp file stands in for the whole group.
# (Portable to any GNU make; avoids the make >= 4.3 grouped-target syntax.)
SCHED_STAMP := $(OUT)/.schedule.stamp
DOCS_STAMP  := $(OUT)/.docs.stamp

# Input file 1: export the department workbook to CSV whenever the workbook changes.
$(SYLLABUS): $(SCHEDULE_XLSX) scripts/xlsx_to_csv.py
	$(PYTHON) scripts/xlsx_to_csv.py $(SCHEDULE_XLSX) $@

csv: $(SYLLABUS)

# Exit status 3 = schedule written, but the input contains conflicts (listed in diagnostics.txt).
# That is a finding about the data, not a build failure, unless STRICT=1.
$(SCHED_STAMP): $(BIN) $(SYLLABUS) $(FACULTY) | $(OUT)
	@echo "./$(BIN) -o $(OUT) -b $(BUFFER) $(SYLLABUS) $(FACULTY)"; \
	 ./$(BIN) -o $(OUT) -b $(BUFFER) $(SYLLABUS) $(FACULTY); status=$$?; \
	 if [ $$status -eq 3 ] && [ "$(STRICT)" != "1" ]; then \
	   echo "NOTE: conflicts found in the input data - see $(OUT)/diagnostics.txt"; \
	 elif [ $$status -ne 0 ]; then exit $$status; fi
	@touch $@

$(TXT_OUT) $(CSV_OUT): $(SCHED_STAMP)

run: $(SCHED_STAMP)
	@echo "Schedule written to $(TXT_OUT)"

# ---- Convert C output to Word / Excel with Python -------------------------------------------------
$(DOCS_STAMP): $(SCHED_STAMP) scripts/export_docs.py
	$(PYTHON) scripts/export_docs.py --input $(OUT) --docx $(DOCX_OUT) --xlsx $(XLSX_OUT)
	@touch $@

$(DOCX_OUT) $(XLSX_OUT): $(DOCS_STAMP)

docs: $(DOCS_STAMP)
	@echo "Documents written to $(DOCX_OUT) and $(XLSX_OUT)"

# ---- Knowledge base: Python writes Prolog RULES + FACTS, Prolog answers questions ----------------
KB        := $(OUT)/knowledge_base.pl
RULES     := prolog/rules.pl
SYLLABI   := $(sort $(wildcard data/syllabi/*.pdf))
QUESTIONS := data/questions.txt
QA_OUT    := $(OUT)/qa_demo.txt

$(KB): $(SCHED_STAMP) $(FACULTY) $(RULES) $(SYLLABI) scripts/gen_prolog.py scripts/syllabus.py
	$(PYTHON) scripts/gen_prolog.py --input $(OUT) --faculty $(FACULTY) --rules $(RULES) -o $@ \
	  --syllabi $(SYLLABI)

kb: $(KB)

# One question:  make ask Q="Who teaches CTEC 350?"      Interactive:  make ask
ask: $(KB)
	@$(PYTHON) scripts/ask.py --kb $(KB) $(if $(Q),"$(Q)")

$(QA_OUT): $(KB) $(QUESTIONS) scripts/ask.py
	$(PYTHON) scripts/ask.py --kb $(KB) --file $(QUESTIONS) > $@
	@echo "Answers written to $@"

demo: $(QA_OUT)
	@cat $(QA_OUT)

# The tests use a knowledge base built from a small CS department (tests/data/cs_*.csv) and the course
# syllabi by the whole pipeline: C program -> gen_prolog.py -> Prolog. Then the Python unit tests and one
# end-to-end question.
KB_TEST := $(BUILD)/kb_test
test-kb: $(BIN)
	@rm -rf $(KB_TEST)
	@./$(BIN) -q -o $(KB_TEST) tests/data/cs_class_schedule.csv tests/data/cs_faculty.csv
	@$(PYTHON) scripts/gen_prolog.py --input $(KB_TEST) --faculty tests/data/cs_faculty.csv --rules $(RULES) \
	   -o $(KB_TEST)/knowledge_base.pl --syllabi $(SYLLABI)
	$(SWIPL) -q -g "consult('$(KB_TEST)/knowledge_base.pl'), consult('tests/test_rules.pl')" -g "run_tests" -t halt
	KB_TEST_KB=$(KB_TEST)/knowledge_base.pl $(PYTHON) -m unittest discover -s tests -p 'test_*.py'
	@echo "--- end-to-end: 'What classes does Dr Tankeh teach?' ---"
	@$(PYTHON) scripts/ask.py --kb $(KB_TEST)/knowledge_base.pl "What classes does Dr Tankeh teach?" \
	   | tee $(KB_TEST)/answer.txt
	@for course in "COSC 330 (Systems Programming I)" "COSC 369 (Software Engineering I)" \
	               "COSC 631 (Database Management)"; do \
	   grep -qF "$$course" $(KB_TEST)/answer.txt || { echo "FAIL: answer is missing $$course"; exit 1; }; \
	 done; echo "PASS: end-to-end question answering"
	@$(PYTHON) scripts/ask.py --kb $(KB_TEST)/knowledge_base.pl --file tests/data/cs_questions.txt \
	   > $(KB_TEST)/qa_demo.txt
	@echo "CS department answers written to $(KB_TEST)/qa_demo.txt"

venv:
	$(PYTHON) -m venv .venv && .venv/bin/pip install -r requirements.txt
	@echo "Now run: make docs PYTHON=.venv/bin/python"

# ---- Tests ---------------------------------------------------------------------------------------
test: $(TEST_BIN) $(BIN)
	./$(TEST_BIN) $(BUILD)
	@echo "--- end-to-end: conflicting inputs must be detected (expect exit status 3) ---"
	@./$(BIN) -q -o $(BUILD)/test_out tests/data/class_schedule_conflict.csv tests/data/faculty_conflict.csv; \
	 status=$$?; \
	 if [ $$status -ne 3 ]; then echo "FAIL: expected exit 3, got $$status"; exit 1; fi; \
	 for pattern in "double-booked" "not in the faculty file" "overlap a class" "read as" "listed as TBA"; do \
	   grep -q "$$pattern" $(BUILD)/test_out/diagnostics.txt || { echo "FAIL: missing '$$pattern'"; exit 1; }; \
	 done; \
	 echo "PASS: end-to-end conflict detection"
	@echo "--- end-to-end: program must reject a wrong number of input files (expect exit 1) ---"
	@./$(BIN) $(SYLLABUS) >/dev/null 2>&1; status=$$?; \
	 if [ $$status -ne 1 ]; then echo "FAIL: expected exit 1, got $$status"; exit 1; fi; \
	 echo "PASS: argument validation"

# ---- Show the contents of the static library ------------------------------------------------------
inspect: $(LIB)
	@echo "== Members of $(LIB) (ar -t) =="; $(AR) -t $(LIB)
	@echo; echo "== Exported functions (nm --defined-only, type T) =="; nm --defined-only $(LIB) | grep ' T '

# ---- Optional: fetch the input files from the Internet (not used by default) -----------------------
fetch:
	@if [ -z "$(SCHEDULE_URL)" ] || [ -z "$(FACULTY_URL)" ]; then \
	  echo "Usage: make fetch SCHEDULE_URL=https://.../schedule.xlsx FACULTY_URL=https://.../faculty.csv"; exit 1; fi
	curl --fail --silent --show-error --location -o $(SCHEDULE_XLSX).download "$(SCHEDULE_URL)"
	curl --fail --silent --show-error --location -o $(FACULTY).download "$(FACULTY_URL)"
	mv $(SCHEDULE_XLSX).download $(SCHEDULE_XLSX)
	mv $(FACULTY).download $(FACULTY)
	@echo "Downloaded inputs into data/. Run 'make docs' to regenerate."

# ---- Submission package --------------------------------------------------------------------------
# make dist: rebuild from scratch, run every test, regenerate all output, then zip the submission.
# The zip holds the static library built from exactly these sources, and sample_output/ from this run.
DIST_NAME := Carlson_Cox_COSC631_Midterm
DIST_DIR  := dist/$(DIST_NAME)
DIST_SRC  := Makefile README.md requirements.txt ruff.toml Dockerfile docker-entrypoint.sh .dockerignore $(wildcard include/*.h) src/main.c $(LIB_SRCS) \
             $(SCHEDULE_XLSX) $(SYLLABUS) $(FACULTY) $(QUESTIONS) $(SYLLABI) $(wildcard scripts/*.py) $(RULES) \
             $(TEST_SRC) $(wildcard tests/*.py tests/*.pl tests/data/*) docs/report.md docs/report.pdf
SAMPLES   := $(TXT_OUT) $(CSV_OUT) $(OUT)/diagnostics.txt $(DOCX_OUT) $(XLSX_OUT) $(KB) $(QA_OUT)

dist:
	$(MAKE) clean
	$(MAKE) all test test-kb demo docs
	rm -rf $(DIST_DIR) dist/$(DIST_NAME).zip
	mkdir -p $(DIST_DIR)/build $(DIST_DIR)/sample_output
	tar cf - $(DIST_SRC) | (cd $(DIST_DIR) && tar xf -)
	cp $(LIB) $(DIST_DIR)/build/
	cp $(SAMPLES) $(DIST_DIR)/sample_output/
	cp $(KB_TEST)/qa_demo.txt $(DIST_DIR)/sample_output/qa_demo_cs.txt
	cd dist && $(PYTHON) -m zipfile -c $(DIST_NAME).zip $(DIST_NAME)
	@echo "Submission package: dist/$(DIST_NAME).zip ($$(find $(DIST_DIR) -type f | wc -l | tr -d ' ') files)"

# ---- Housekeeping --------------------------------------------------------------------------------
clean:
	rm -rf $(BUILD)

distclean: clean
	rm -rf $(OUT) .venv dist

help:
	@sed -n '2,26p' Makefile

-include $(DEPS)
