% =================================================================================================
% Prolog RULE LIBRARY (hand-written).
%
% scripts/gen_prolog.py writes the knowledge base output/knowledge_base.pl: these rules, followed by
% the FACTS it generates from the schedule data. Load the knowledge base, then query:
%
%     swipl output/knowledge_base.pl
%     ?- classes_of_lecturer([stone], Section, Code, Title).
%
% Python (scripts/ask.py) turns an English question into one of the queries below and prints the
% answers. Prolog is the interpreter: it searches the facts with these rules to answer the question.
% Times are minutes after midnight (960 = 4:00 PM).
% =================================================================================================

% Every fact predicate is declared dynamic, so a query against an empty or partial knowledge base fails
% cleanly instead of raising an "unknown procedure" error.
:- dynamic faculty/6, alias/2, name_token/2, course/3, section/5, listed_instructor/2, teaches/2,
           meets/4, room_listed/2, room/2, office_hours/6, office_hours_required/2, data_issue/2.
% Facts read from the course syllabi.
:- dynamic syllabus_course/5, syllabus_teaches/3, cross_listed/2, prerequisite/2, textbook/2, reference/2,
           description/2, grade_component/3, grade_scale/4, week_topic/4, week_reading/3, important_date/3,
           syllabus_office_hours_course/4, office_hours_note/2.

% ---- Calendar ------------------------------------------------------------------------------------

day_order(monday, 1).
day_order(tuesday, 2).
day_order(wednesday, 3).
day_order(thursday, 4).
day_order(friday, 5).
day_order(saturday, 6).
day_order(sunday, 7).

% clock(+Minutes, -Text): 960 -> '4:00 PM'.
clock(Minutes, Text) :-
    H24 is Minutes // 60,
    M is Minutes mod 60,
    ( H24 >= 12 -> Meridiem = 'PM' ; Meridiem = 'AM' ),
    H0 is H24 mod 12,
    ( H0 =:= 0 -> H12 = 12 ; H12 = H0 ),
    format(atom(Text), '~d:~|~`0t~d~2+ ~w', [H12, M, Meridiem]).

% time_range(+Start, +End, -Text): '4:00 PM to 6:50 PM'.
time_range(Start, End, Text) :-
    clock(Start, A),
    clock(End, B),
    format(atom(Text), '~w to ~w', [A, B]).

% overlaps(+S1, +E1, +S2, +E2): two half-open time intervals [S, E) share at least one minute.
overlaps(S1, E1, S2, E2) :-
    S1 < E2,
    S2 < E1.

% ---- Lecturers -----------------------------------------------------------------------------------

lecturer(Id, Name) :-
    faculty(Id, Name, _, _, _, _).

% lecturer_named(+Words, -Id): every word in Words is part of the lecturer's name or one of the
% spellings used in the class schedule. [stone] and [d, stone] fail/succeed as expected; titles such as
% "Dr" are removed by the Python front end before the query is made.
lecturer_named(Words, Id) :-
    Words \== [],
    lecturer(Id, _),
    forall(member(Word, Words), name_token(Id, Word)).

% ---- Courses and sections ------------------------------------------------------------------------

% course_title_contains(+Fragment, -Code): case-insensitive substring match on the course title.
course_title_contains(Fragment, Code) :-
    course(Code, Title, _),
    downcase_atom(Title, Lower),
    downcase_atom(Fragment, Needle),
    sub_atom(Lower, _, _, _, Needle).

% section_of_course(?Section, ?Code)
section_of_course(Section, Code) :-
    section(Section, Code, _, _, _).

% ---- Who teaches what ----------------------------------------------------------------------------

% teaches_course(?Id, ?Code): the lecturer teaches a section of the course (class schedule) or is the
% instructor named on its syllabus.
teaches_course(Id, Code) :-
    teaches(Id, Section),
    section(Section, Code, _, _, _).
teaches_course(Id, Code) :-
    syllabus_teaches(Id, Code, _).

% lecturer_of(?Code, ?Name): "Who teaches CTEC 350?" Each lecturer once, however many sections.
lecturer_of(Code, Name) :-
    distinct(Id-Code, teaches_course(Id, Code)),
    lecturer(Id, Name).

% classes_of_lecturer(+Words, -Section, -Code, -Title): "What classes does Dr Stone teach?"
classes_of_lecturer(Words, Section, Code, Title) :-
    lecturer_named(Words, Id),
    teaches(Id, Section),
    section(Section, Code, _, _, _),
    course(Code, Title, _).

% teaches_on(?Id, ?Day): the lecturer has a class that day.
teaches_on(Id, Day) :-
    teaches(Id, Section),
    meets(Section, Day, _, _).

% ---- When and where classes meet -----------------------------------------------------------------

% class_on(?Day, ?Section, ?Start, ?End): "Which classes meet on Tuesday?"
class_on(Day, Section, Start, End) :-
    meets(Section, Day, Start, End).

% class_in_room(+Room, ?Section): "Which classes are in room CS 311?" Case-insensitive; a split room
% such as 201/112 is in both rooms.
class_in_room(Room, Section) :-
    downcase_atom(Room, Wanted),
    room(Section, Actual),
    downcase_atom(Actual, Wanted).

% meeting_text(+Section, -Text): 'Monday & Wednesday 4:00 PM to 6:50 PM' (or 'TBA' / 'Asynchronous').
meeting_text(Section, Text) :-
    findall(Order-Day, (meets(Section, Day, _, _), day_order(Day, Order)), Pairs0),
    sort(Pairs0, Pairs),
    (   Pairs = [_|_], meets(Section, _, Start, End)
    ->  pairs_values(Pairs, Days),
        maplist(day_label, Days, Labels),
        atomic_list_concat(Labels, ' & ', DayText),
        time_range(Start, End, Range),
        format(atom(Text), '~w ~w', [DayText, Range])
    ;   section(Section, _, _, Meeting, _),
        meeting_label(Meeting, Text)
    ).

% meeting_label(+Meeting, -Text): sections without a weekly meeting time.
meeting_label(tba, 'TBA') :- !.
meeting_label(asynchronous, 'Online asynchronous (no set time)') :- !.
meeting_label(Other, Text) :- day_label(Other, Text).

% section_sort_key(+Section, -DayOrder, -Start): the earliest weekly meeting, for listing classes in week
% order. Sections with no meeting time (TBA, asynchronous) sort last.
section_sort_key(Section, Order, Start) :-
    findall(O-S, (meets(Section, Day, S, _), day_order(Day, O)), Keys),
    (   msort(Keys, [Order-Start|_])
    ->  true
    ;   Order = 9, Start = 0
    ).

% day_label(+Atom, -Label): monday -> 'Monday'.
day_label(Atom, Label) :-
    atom_codes(Atom, [First|Rest]),
    code_type(Upper, to_upper(First)),
    atom_codes(Label, [Upper|Rest]).

% room_text(+Section, -Text): the room as the schedule lists it: '108', '201/112', 'Online', 'Dept'.
room_text(Section, Text) :-
    (   room_listed(Section, Listed)
    ->  Text = Listed
    ;   Text = 'TBA'
    ).

% ---- Office hours --------------------------------------------------------------------------------

% office_hours_on(?Day, ?Id, ?Start, ?End, ?Location): "Which office hours are on Wednesday?"
office_hours_on(Day, Id, Start, End, Location) :-
    office_hours(Id, Day, Start, End, Location, _).

% gap(+S1, +E1, +S2, +E2, -Minutes): time between two intervals on the same day (0 if they touch/overlap).
gap(S1, E1, S2, E2, Minutes) :-
    (   E1 =< S2 -> Minutes is S2 - E1
    ;   E2 =< S1 -> Minutes is S1 - E2
    ;   Minutes = 0
    ).

% office_hours_course(?Id, ?Day, ?Start, ?Code): the office-hours block starting at Start serves the
% course whose class that day is closest in time to the block. Labels office hours the way the
% department's sample does: "Wednesday: 10:45 AM - 1:00 PM (COSC 330) & 3:00 - 5:00 PM (COSC 631)".
office_hours_course(Id, Day, Start, Code) :-
    office_hours(Id, Day, Start, End, _, _),
    findall(Gap-C,
            ( teaches(Id, Section),
              meets(Section, Day, CS, CE),
              section(Section, C, _, _, _),
              gap(Start, End, CS, CE, Gap) ),
            Pairs),
    msort(Pairs, [Best-_|_]),
    member(Best-Code, Pairs).
% Office hours published on a syllabus belong to that syllabus's course(s).
office_hours_course(Id, Day, Start, Code) :-
    syllabus_office_hours_course(Id, Day, Start, Code).

% in_office(?Id, ?Day, +Time): the lecturer is holding office hours at that time.
in_office(Id, Day, Time) :-
    office_hours(Id, Day, Start, End, _, _),
    Start =< Time,
    Time < End.

% ---- Availability --------------------------------------------------------------------------------

% busy(?Id, ?Day, ?Start, ?End, ?What): the lecturer is teaching or holding office hours.
busy(Id, Day, Start, End, class(Section)) :-
    teaches(Id, Section),
    meets(Section, Day, Start, End).
busy(Id, Day, Start, End, office_hours) :-
    office_hours(Id, Day, Start, End, _, _).

% teaching_at(?Id, ?Day, +Time, ?Section): the lecturer is in class at that time ("not to be interrupted").
teaching_at(Id, Day, Time, Section) :-
    teaches(Id, Section),
    meets(Section, Day, Start, End),
    Start =< Time,
    Time < End.

% ---- Conflicts (the same checks the C program makes, expressed as rules) --------------------------

% same_term(+A, +B): the two sections run in overlapping parts of the term (7R1 vs 7R2 never clash).
same_term(A, B) :-
    section(A, _, TermA, _, _),
    section(B, _, TermB, _, _),
    ( TermA == '' ; TermB == '' ; TermA == TermB ).

% clash(+A, +B): the two sections meet on a common day at overlapping times. Succeeds at most once.
clash(A, B) :-
    same_term(A, B),
    once(( meets(A, Day, S1, E1),
           meets(B, Day, S2, E2),
           overlaps(S1, E1, S2, E2) )).

% room_conflict(?Room, ?A, ?B): a physical room is double-booked. A @< B reports each pair once.
room_conflict(Room, A, B) :-
    room(A, Room),
    room(B, Room),
    A @< B,
    clash(A, B).

% instructor_conflict(?Id, ?A, ?B): a lecturer is scheduled in two places at once.
instructor_conflict(Id, A, B) :-
    teaches(Id, A),
    teaches(Id, B),
    A @< B,
    clash(A, B).

% office_hours_clash(?Id, ?Day, ?Section): office hours overlap one of the lecturer's own classes.
% The C program never generates these; the rule exists to verify that it did not.
office_hours_clash(Id, Day, Section) :-
    office_hours(Id, Day, S1, E1, _, _),
    teaches(Id, Section),
    meets(Section, Day, S2, E2),
    overlaps(S1, E1, S2, E2).

% ---- Workload and policy -------------------------------------------------------------------------

% teaching_load(+Id, -Sections, -Credits)
teaching_load(Id, Sections, Credits) :-
    lecturer(Id, _),
    aggregate_all(count, teaches(Id, _), Sections),
    aggregate_all(sum(C), (teaches(Id, S), section(S, Code, _, _, _), course(Code, _, C)), Credits).

% office_hours_minutes(+Id, -Minutes): total office hours per week.
office_hours_minutes(Id, Minutes) :-
    lecturer(Id, _),
    aggregate_all(sum(End - Start), office_hours(Id, _, Start, End, _, _), Minutes).

% meets_office_hours_policy(?Id): the weekly office hours cover the requirement. The requirement is a
% fact (office_hours_required/2) because it can be overridden per lecturer in the faculty file; the
% default is max(3 h, 1 h per section).
meets_office_hours_policy(Id) :-
    office_hours_required(Id, Need),
    office_hours_minutes(Id, Have),
    Have >= Need.

% ---- Course syllabi --------------------------------------------------------------------------------
% Facts read from the syllabus PDFs: prerequisite/2, grade_component/3, grade_scale/4, week_topic/4, ...

% course_level(+Code, -Level): 100-499 are undergraduate courses, 500 and above graduate courses.
course_level(Code, Level) :-
    sub_atom(Code, _, 3, 0, Digits),
    atom_number(Digits, Number),
    ( Number >= 500 -> Level = graduate ; Level = undergraduate ).

% requires(?Code, ?Prerequisite): a direct prerequisite, or a prerequisite of a prerequisite.
requires(Code, Pre) :-
    prerequisite(Code, Pre).
requires(Code, Pre) :-
    prerequisite(Code, Mid),
    requires(Mid, Pre).

% missing_prerequisite(+Code, +Completed, -Pre): a direct prerequisite not in the list of completed courses.
missing_prerequisite(Code, Completed, Pre) :-
    prerequisite(Code, Pre),
    \+ memberchk(Pre, Completed).

% can_take(+Code, +Completed): every direct prerequisite of the course has been completed.
can_take(Code, Completed) :-
    course(Code, _, _),
    \+ missing_prerequisite(Code, Completed, _).

% letter_grade(+Code, +Score, -Letter): the syllabus's grade scale ("80 - 89 = B" covers 80 up to 89.99...).
letter_grade(Code, Score, Letter) :-
    grade_scale(Code, Letter, Min, Max),
    Score >= Min,
    Score < Max + 1,
    !.

% grade_total(+Code, -Percent): the grade components should add up to 100.
grade_total(Code, Total) :-
    syllabus_course(Code, _, _, _, _),
    aggregate_all(sum(P), grade_component(Code, _, P), Total).

% heaviest_component(+Code, -Name, -Percent): the component that counts most toward the grade.
heaviest_component(Code, Name, Percent) :-
    aggregate_all(max(P, N), grade_component(Code, N, P), max(Percent, Name)).

% component_weight(+Code, +Fragment, -Name, -Percent): "How much is the midterm worth in COSC 631?"
component_weight(Code, Fragment, Name, Percent) :-
    grade_component(Code, Name, Percent),
    downcase_atom(Name, Lower),
    downcase_atom(Fragment, Needle),
    sub_atom(Lower, _, _, _, Needle).

% topic_week(?Code, +Fragment, -Week, -Date, -Topic): "When is quicksort covered?" (case-insensitive).
topic_week(Code, Fragment, Week, Date, Topic) :-
    week_topic(Code, Week, Date, Topic),
    downcase_atom(Topic, Lower),
    downcase_atom(Fragment, Needle),
    sub_atom(Lower, _, _, _, Needle).

% syllabus_instructor(?Code, ?Name): the instructor named on the course's syllabus.
syllabus_instructor(Code, Name) :-
    syllabus_teaches(Id, Code, _),
    lecturer(Id, Name).
