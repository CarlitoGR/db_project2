% =================================================================================================
% plunit tests for prolog/rules.pl. Run by 'make test-kb' against facts generated from the small CS
% department in tests/data/cs_*.csv (C program -> gen_prolog.py), so they test the whole pipeline.
%
%   Dr. Tankeh (CS01): COSC 330.001 MW 9:30-10:45, COSC 369.001 TR 9:30-10:45, COSC 631.001 W 5:00-7:30
%                      office hours (lecturer-supplied) MTWR 10:45-1:00 and W 3:00-5:00
%   Jordan Ellis (CS02): COSC 112.001, COSC 112.002, COSC 450.001 (generated office hours)
% =================================================================================================

:- begin_tests(time).

test(clock_pm) :- clock(960, '4:00 PM').
test(clock_noon) :- clock(720, '12:00 PM').
test(clock_midnight) :- clock(0, '12:00 AM').
test(clock_pad_minutes) :- clock(545, '9:05 AM').
test(range) :- time_range(570, 645, '9:30 AM to 10:45 AM').
test(overlap) :- overlaps(570, 645, 600, 700).
test(touching_is_not_overlap, [fail]) :- overlaps(570, 645, 645, 700).

:- end_tests(time).

:- begin_tests(lecturers).

test(name_match) :- lecturer_named([tankeh], 'CS01').
test(name_from_alias) :- lecturer_named([ellis], 'CS02').
test(all_words_must_match, [fail]) :- lecturer_named([tankeh, ellis], _).
test(unknown_name, [fail]) :- lecturer_named([nobody], _).
test(empty_words, [fail]) :- lecturer_named([], _).

:- end_tests(lecturers).

:- begin_tests(teaching).

test(classes_of_tankeh, [true(Codes == ['COSC 330', 'COSC 369', 'COSC 631'])]) :-
    setof(Code, S^T^classes_of_lecturer([tankeh], S, Code, T), Codes).

test(who_teaches, [true(Names == ['Dr. Tankeh'])]) :-
    findall(N, lecturer_of('COSC 369', N), Names).

test(lecturer_listed_once, [true(Names == ['Jordan Ellis'])]) :-
    findall(N, lecturer_of('COSC 112', N), Names).          % two sections, one answer

test(teaches_on_friday, [fail]) :- teaches_on('CS01', friday).

test(title_search, [nondet, true(Code == 'COSC 631')]) :- course_title_contains(database, Code).

test(meeting_text) :- meeting_text('COSC 330.001', 'Monday & Wednesday 9:30 AM to 10:45 AM').

test(load, [true(S-C == 3-9)]) :- teaching_load('CS01', S, C).

:- end_tests(teaching).

:- begin_tests(office_hours).

test(wednesday_blocks, [true(Starts == [645, 900])]) :-
    findall(S, office_hours_on(wednesday, 'CS01', S, _, _), Starts0),
    msort(Starts0, Starts).

% Each block is labelled with the course nearest in time, as in the department's sample output.
test(morning_block_course, [nondet, true(Code == 'COSC 330')]) :- office_hours_course('CS01', wednesday, 645, Code).
test(afternoon_block_course, [nondet, true(Code == 'COSC 631')]) :- office_hours_course('CS01', wednesday, 900, Code).

test(in_office, [nondet]) :- in_office('CS01', wednesday, 960).
test(teaching_not_office, [fail]) :- in_office('CS01', monday, 600).
test(teaching_at, [nondet, true(S == 'COSC 330.001')]) :- teaching_at('CS01', monday, 600, S).

test(policy_met) :- meets_office_hours_policy('CS01').
test(total_minutes, [true(M == 660)]) :- office_hours_minutes('CS01', M).

:- end_tests(office_hours).

:- begin_tests(conflicts).

% The fixture is conflict-free; office hours never overlap the lecturer's own classes.
test(no_room_conflict, [fail]) :- room_conflict(_, _, _).
test(no_instructor_conflict, [fail]) :- instructor_conflict(_, _, _).
test(no_office_hours_clash, [fail]) :- office_hours_clash(_, _, _).

% COSC 330.001 and COSC 450.001 share room CS 311 but at different times on different days.
test(shared_room_no_clash, [fail]) :- clash('COSC 330.001', 'COSC 450.001').

% Inject a section that collides with COSC 631.001 (W 5:00-7:30 PM, CS 313, Dr. Tankeh), then remove it.
add_clash(Term) :-
    user:assertz(section('TEST 999.001', 'TEST 999', Term, scheduled, 1)),
    user:assertz(meets('TEST 999.001', wednesday, 1080, 1140)),           % W 6:00-7:00 PM
    user:assertz(room('TEST 999.001', 'CS 313')),
    user:assertz(teaches('CS01', 'TEST 999.001')).

remove_clash :-
    user:retractall(section('TEST 999.001', _, _, _, _)),
    user:retractall(meets('TEST 999.001', _, _, _)),
    user:retractall(room('TEST 999.001', _)),
    user:retractall(teaches(_, 'TEST 999.001')).

test(detects_room_conflict, [nondet, setup(add_clash('')), cleanup(remove_clash), true(R-A-B == 'CS 313'-'COSC 631.001'-'TEST 999.001')]) :-
    room_conflict(R, A, B).

test(detects_instructor_conflict, [nondet, setup(add_clash('')), cleanup(remove_clash), true(Id == 'CS01')]) :-
    instructor_conflict(Id, 'COSC 631.001', 'TEST 999.001').

% Part-of-term sessions never clash with each other (7R1 vs 7R2); an empty session clashes with any.
test(different_terms_no_clash, [setup((add_clash('7R2'), user:retract(section('COSC 631.001', C, _, M, E)),
                                       user:assertz(section('COSC 631.001', C, '7R1', M, E)))),
                                cleanup((remove_clash, user:retract(section('COSC 631.001', C2, _, M2, E2)),
                                         user:assertz(section('COSC 631.001', C2, '', M2, E2)))),
                                fail]) :-
    room_conflict(_, 'COSC 631.001', 'TEST 999.001').

:- end_tests(conflicts).
