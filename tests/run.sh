#!/bin/sh
# Test runner for solace.
#
#   tests/cases/NAME.slc   must exit 0 and print exactly tests/cases/NAME.expected
#   tests/errors/NAME.slc  must exit with the code on the first line of
#                          tests/errors/NAME.expected, and its diagnostics must
#                          contain the rest of that file, one substring per line
#
# Every case is additionally compiled with --dump and re-run from the resulting
# bytecode, so the serializer is covered by the same expectations. Errors are
# only checked from source, since --dump has nothing to write for them.
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

report_fail() {
    fail=$((fail + 1))
    failed_names="$failed_names $1"
    echo "FAIL $1"
    shift
    for line in "$@"; do
        echo "     $line"
    done
}

# $1 test name, $2 source path, $3 expected stdout, $4 label for messages.
check_stdout() {
    "$solace" "$2" >"$work/out" 2>"$work/err"
    status=$?
    if [ "$status" -ne 0 ]; then
        report_fail "$1 ($4)" "exit status $status, expected 0" \
            "$(head -n 3 "$work/err" | tr -d '\033')"
        return 1
    fi
    if ! diff -u "$3" "$work/out" >"$work/diff" 2>&1; then
        report_fail "$1 ($4)" "stdout differs from $(basename "$3")" \
            "$(sed -n '1,12p' "$work/diff" | tr -d '\033')"
        return 1
    fi
    return 0
}

check_error() {
    name=$1
    source=$2
    expected=$3

    want_code=$(head -n 1 "$expected")
    tail -n +2 "$expected" >"$work/wants"

    "$solace" "$source" >"$work/out" 2>"$work/err"
    status=$?
    if [ "$status" -ne "$want_code" ]; then
        report_fail "$name" "exit status $status, expected $want_code" \
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
        report_fail "$name" "diagnostics$missing" \
            "$(head -n 3 "$work/err" | tr -d '\033')"
        return 1
    fi
    return 0
}

for source in "$tests_dir"/cases/*.slc; do
    [ -e "$source" ] || continue
    name=$(basename "$source" .slc)
    selected "$name" "$@" || continue
    expected=${source%.slc}.expected
    if [ ! -f "$expected" ]; then
        report_fail "$name" "missing $expected"
        continue
    fi

    before=$fail
    check_stdout "$name" "$source" "$expected" source || continue

    # The same program, run from serialized bytecode.
    cp "$source" "$work/$name.slc"
    if "$solace" --dump "$work/$name.slc" >/dev/null 2>"$work/err"; then
        check_stdout "$name" "$work/$name.slc.slb" "$expected" bytecode
    else
        report_fail "$name (bytecode)" "--dump failed" \
            "$(head -n 3 "$work/err" | tr -d '\033')"
    fi
    [ "$fail" -eq "$before" ] && pass=$((pass + 1))
done

for source in "$tests_dir"/errors/*.slc; do
    [ -e "$source" ] || continue
    name=$(basename "$source" .slc)
    selected "$name" "$@" || continue
    expected=${source%.slc}.expected
    if [ ! -f "$expected" ]; then
        report_fail "$name" "missing $expected"
        continue
    fi

    before=$fail
    check_error "$name" "$source" "$expected" || continue
    [ "$fail" -eq "$before" ] && pass=$((pass + 1))
done

if [ "$fail" -eq 0 ]; then
    echo "all $pass tests passed"
    exit 0
fi
echo "$pass passed, $fail failed:$failed_names"
exit 1
