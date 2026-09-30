#!/bin/sh
# Test runner for solace.
#
#   tests/cases/NAME.slc   must exit 0 and print exactly tests/cases/NAME.expected
#   tests/errors/NAME.slc  must exit with the code on the first line of
#                          tests/errors/NAME.expected, and its diagnostics must
#                          contain the rest of that file, one substring per line
#   tests/repl/NAME.in      a recorded REPL session, one input line per line;
#                          diagnostics must contain tests/repl/NAME.expected
#                          the same way, minus the exit-status line (the REPL
#                          always exits 0) and plus a prompt count per line
#
# Every case is additionally compiled with --dump and re-run from the resulting
# bytecode, so the serializer is covered by the same expectations. Errors are
# only checked from source, since --dump has nothing to write for them.
#
# Progress is one mark per test as it finishes: '.' passed, 'F' failed, 'E'
# could not be run at all. Failure detail is deferred until the mark line is
# complete, so the marks stay readable when they all pass.
#
# Usage: tests/run.sh [name-substring ...]   (no arguments runs everything)

set -u

tests_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
solace=${SOLACE:-$tests_dir/../bin/solace}
[ -x "$solace" ] || solace=$tests_dir/../solace
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT INT TERM

if [ ! -x "$solace" ]; then
    echo "no solace binary at $solace; run 'make' first" >&2
    exit 1
fi

filters=$#
pass=0
fail=0
failed_names=
marks=0
test_kind=

selected() {
    # $1 is the test name; no filters at all means everything.
    [ "$filters" -eq 0 ] && return 0
    name=$1
    shift
    for filter in "$@"; do
        case $name in
            *"$filter"*) return 0 ;;
        esac
    done
    return 1
}

# Start collecting a mark for the test about to run.
begin_test() {
    test_kind=.
}

# A test with several checks yields a single mark: the first problem decides it.
record_kind() {
    if [ "$test_kind" = "." ]; then
        test_kind=$1
    fi
}

# $1 mark character. Printed straight away so a slow test still shows life.
mark() {
    printf '%s' "$1"
    marks=$((marks + 1))
}

# Close off the mark line so the deferred failure detail can start at column 0.
end_marks() {
    [ "$marks" -eq 0 ] || printf '\n'
    marks=0
}

# $1 mark character (F or E), $2 test name, $3.. detail lines.
report_fail() {
    record_kind "$1"
    name=$2
    shift 2
    fail=$((fail + 1))
    failed_names="$failed_names $name"
    {
        echo "FAIL $name"
        for line in "$@"; do
            echo "     $line"
        done
    } >>"$work/failures"
}

# $1 test name, $2 source path, $3 expected stdout, $4 label for messages.
check_stdout() {
    "$solace" "$2" >"$work/out" 2>"$work/err"
    status=$?
    if [ "$status" -ne 0 ]; then
        report_fail F "$1 ($4)" "exit status $status, expected 0" \
            "$(head -n 3 "$work/err" | tr -d '\033')"
        return 1
    fi
    if ! diff -u "$3" "$work/out" >"$work/diff" 2>&1; then
        report_fail F "$1 ($4)" "stdout differs from $(basename "$3")" \
            "$(sed -n '1,12p' "$work/diff" | tr -d '\033')"
        return 1
    fi
    return 0
}

check_error() {
    name=$1
    source=$2
    expected=$3

    "$solace" "$source" >"$work/out" 2>"$work/err"
    status=$?
    check_diagnostics "$name" "$status" "$expected" "$1.source"
}

# Shared tail of check_error/check_repl: $1 name, $2 status, $3 expected file,
# $4 label. The first line of the expected file is the exit status, and every
# remaining non-empty line must appear verbatim in the captured stderr.
check_diagnostics() {
    name=$1
    status=$2
    expected=$3
    label=$4

    want_code=$(head -n 1 "$expected")
    tail -n +2 "$expected" >"$work/wants"

    if [ "$status" -ne "$want_code" ]; then
        report_fail F "$name ($label)" "exit status $status, expected $want_code" \
            "$(head -n 3 "$work/err" | tr -d '\033')"
        return 1
    fi
    missing=
    while IFS= read -r want; do
        [ -n "$want" ] || continue
        grep -qF -- "$want" "$work/err" || missing="$missing
    does not contain: $want"
    done <"$work/wants"
    if [ -n "$missing" ]; then
        report_fail F "$name ($label)" "diagnostics$missing" \
            "$(head -n 3 "$work/err" | tr -d '\033')"
        return 1
    fi
    return 0
}

# Feed a recorded REPL session (one input line per line of $2) through a
# solace process started with no arguments. The REPL always exits 0, so the exit
# status is checked as 0 and the real assertions are on the diagnostics: a line
# that fails to compile must still be reported. The prompt count catches a line
# that was never read; there is one more prompt than input line, because the
# prompt is printed before the read that hits EOF.
check_repl() {
    name=$1
    input=$2
    expected=$3

    "$solace" <"$input" >"$work/out" 2>"$work/err"
    status=$?
    check_diagnostics "$name" "$status" "$expected" repl || return 1

    prompts=$(tr -cd '>' <"$work/out" | wc -c | tr -d ' ')
    lines=$(($(wc -l <"$input" | tr -d ' ') + 1))
    if [ "$prompts" -ne "$lines" ]; then
        report_fail F "$name (repl)" "printed $prompts prompts, expected $lines"
        return 1
    fi
    return 0
}

for source in "$tests_dir"/cases/*.slc; do
    [ -e "$source" ] || continue
    name=$(basename "$source" .slc)
    selected "$name" "$@" || continue
    expected=${source%.slc}.expected
    begin_test
    if [ ! -f "$expected" ]; then
        report_fail E "$name" "missing $expected"
        mark "$test_kind"
        continue
    fi

    before=$fail
    if check_stdout "$name" "$source" "$expected" source; then
        # The same program, run from serialized bytecode.
        cp "$source" "$work/$name.slc"
        if "$solace" --dump "$work/$name.slc" >/dev/null 2>"$work/err"; then
            check_stdout "$name" "$work/$name.slc.slb" "$expected" bytecode
        else
            report_fail E "$name (bytecode)" "--dump failed" \
                "$(head -n 3 "$work/err" | tr -d '\033')"
        fi
        [ "$fail" -eq "$before" ] && pass=$((pass + 1))
    fi
    mark "$test_kind"
done

for source in "$tests_dir"/errors/*.slc; do
    [ -e "$source" ] || continue
    name=$(basename "$source" .slc)
    selected "$name" "$@" || continue
    expected=${source%.slc}.expected
    begin_test
    if [ ! -f "$expected" ]; then
        report_fail E "$name" "missing $expected"
        mark "$test_kind"
        continue
    fi

    before=$fail
    check_error "$name" "$source" "$expected"
    [ "$fail" -eq "$before" ] && pass=$((pass + 1))
    mark "$test_kind"
done

for input in "$tests_dir"/repl/*.in; do
    [ -e "$input" ] || continue
    name=$(basename "$input" .in)
    selected "$name" "$@" || continue
    expected=${input%.in}.expected
    begin_test
    if [ ! -f "$expected" ]; then
        report_fail E "$name" "missing $expected"
        mark "$test_kind"
        continue
    fi

    before=$fail
    check_repl "$name" "$input" "$expected"
    [ "$fail" -eq "$before" ] && pass=$((pass + 1))
    mark "$test_kind"
done

end_marks
[ -s "$work/failures" ] && cat "$work/failures"

if [ "$fail" -eq 0 ]; then
    echo "all $pass tests passed"
    exit 0
fi
echo "$pass passed, $fail failed:$failed_names"
exit 1
