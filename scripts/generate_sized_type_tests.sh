#!/usr/bin/env bash
#
# generate_sized_type_tests.sh - Generate the sized-type test grid
#
# Writes every sized type (i8..i256, u8..u256, f32, f64) at every slot a value
# can be stored into (STANDARD.md, "Sized types") with each case:
#
#   - an in-range literal            (compiles)
#   - an out-of-range literal        (E3036)
#   - a widening value               (compiles)
#   - a narrowing value              (E3155)
#   - a signedness-crossing value    (E3019)
#   - a binary expression with a literal (compiles)
#   - a compound assignment at the type's maximum value (overflow panic)
#   - a shift by a count at and beyond the type's width  (P0092)
#
# A range() bound or step is a slot whose target is i64, or the widest wide
# integer type among the range's bounds: each type is stored into the start,
# the end and the step, and into a bound of a range whose other bound is i128.
# A runtime panic file pins its panic code with an expect-contains marker.
#
# into integration-tests/:
#
#   pass/core/sized_grid_<slot>.gray            every case that compiles
#   fail/errors/<code>_sized_grid_<slot>_<case>.gray
#                                               every case expected to fail
#                                               with <code>, one file per code
#   fail/errors/<panic>_sized_grid_<shape>_<type>.gray
#                                               one runtime panic per file
#
# A new slot or sized type is added here, and the grid is regenerated. The
# generated files are checked in; CI runs them like any other test.
#
# Usage: ./scripts/generate_sized_type_tests.sh
#
# Copyright (c) 2025-Present Marshall A Burns
# Licensed under the MIT License. See LICENSE for details.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PASS_DIR="$ROOT/integration-tests/pass/core"
FAIL_DIR="$ROOT/integration-tests/fail/errors"

TYPES="i8 i16 i32 i64 i128 i256 u8 u16 u32 u64 u128 u256 f32 f64"
INT_TYPES="i8 i16 i32 i64 i128 i256 u8 u16 u32 u64 u128 u256"

# --- Type facts --------------------------------------------------------------

max_literal() {
    case "$1" in
        i8) echo 127 ;; i16) echo 32767 ;; i32) echo 2147483647 ;;
        i64) echo 9223372036854775807 ;;
        i128) echo 170141183460469231731687303715884105727 ;;
        i256) echo 57896044618658097711785492504343953926634992332820282019728792003956564819967 ;;
        u8) echo 255 ;; u16) echo 65535 ;; u32) echo 4294967295 ;;
        u64) echo 18446744073709551615 ;;
        u128) echo 340282366920938463463374607431768211455 ;;
        u256) echo 115792089237316195423570985008687907853269984665640564039457584007913129639935 ;;
        f32) echo 3.0e38 ;; f64) echo 1.0e308 ;;
    esac
}

# A literal one past the type's range: a negative one for an unsigned type,
# max + 1 for a signed one, beyond the float range for a float.
over_literal() {
    case "$1" in
        u*) echo "-1" ;;
        i8) echo 128 ;; i16) echo 32768 ;; i32) echo 2147483648 ;;
        i64) echo 9223372036854775808 ;;
        i128) echo 170141183460469231731687303715884105728 ;;
        i256) echo 57896044618658097711785492504343953926634992332820282019728792003956564819968 ;;
        f32) echo 1.0e39 ;; f64) echo "1.0e308 * 10.0" ;;
    esac
}

one() { case "$1" in f*) echo "1.0" ;; *) echo 1 ;; esac; }
zero() { case "$1" in f*) echo "0.0" ;; *) echo 0 ;; esac; }

# The next narrower / wider type of the same family, or "".
narrower() {
    case "$1" in
        i16) echo i8 ;; i32) echo i16 ;; i64) echo i32 ;; i128) echo i64 ;; i256) echo i128 ;;
        u16) echo u8 ;; u32) echo u16 ;; u64) echo u32 ;; u128) echo u64 ;; u256) echo u128 ;;
        f64) echo f32 ;; *) echo "" ;;
    esac
}
wider() {
    case "$1" in
        i8) echo i16 ;; i16) echo i32 ;; i32) echo i64 ;; i64) echo i128 ;; i128) echo i256 ;;
        u8) echo u16 ;; u16) echo u32 ;; u32) echo u64 ;; u64) echo u128 ;; u128) echo u256 ;;
        f32) echo f64 ;; *) echo "" ;;
    esac
}
# The integer type of the same width and the other signedness, or "".
other_sign() {
    case "$1" in
        i*) echo "u${1#i}" ;; u*) echo "i${1#u}" ;; *) echo "" ;;
    esac
}

# --- Slots ------------------------------------------------------------------
#
# Each slot stores a value of the target type T. slot_case prints one case as
# Grayscale: any file-scope declarations, then `do case_<id>() { ... }`.
# $1 slot, $2 id, $3 T, $4 setup statement (or ""), $5 value expression.

SLOTS="var_decl module_var_decl reassign compound_assign array_element map_value
map_key deref_assign field_assign struct_literal field_default default_param
enum_payload when_arm destructure array_literal map_literal_key
map_literal_value function_arg struct_function_arg func_value_arg return
map_index"

# Slots whose target is always i64, whatever the value's type T.
I64_SLOTS="array_index string_index builtin_arg stdlib_arg"

# range() slots: the start, end and step of an i64 range, and a bound of a
# range whose other bound is i128.
RANGE_SLOTS="range_start range_end range_step range_bound_with_i128"

# Slots where the value must be a literal (a constant position).
literal_only() {
    case "$1" in field_default|default_param|when_arm) return 0 ;; *) return 1 ;; esac
}
# Slots where the value comes from a typed function result.
typed_only() { [ "$1" = destructure ]; }

slot_case() {
    local slot="$1" id="$2" t="$3" setup="$4" e="$5"
    local z; z="$(zero "$t")"
    case "$slot" in
        var_decl)
            printf 'do case_%s() {\n    %s\n    mut v %s = %s\n    println(v)\n}\n' "$id" "$setup" "$t" "$e" ;;
        module_var_decl)
            local src_decl=""
            if [ -n "$setup" ]; then src_decl="${setup/src_V/msrc$id}"; e="${e//src_V/msrc$id}"; fi
            printf '%s\nmut g_%s %s = %s\ndo case_%s() {\n    println(g_%s)\n}\n' "$src_decl" "$id" "$t" "$e" "$id" "$id" ;;
        reassign)
            printf 'do case_%s() {\n    %s\n    mut v %s = %s\n    v = %s\n    println(v)\n}\n' "$id" "$setup" "$t" "$z" "$e" ;;
        compound_assign)
            printf 'do case_%s() {\n    %s\n    mut v %s = %s\n    v += %s\n    println(v)\n}\n' "$id" "$setup" "$t" "$z" "$e" ;;
        array_element)
            printf 'do case_%s() {\n    %s\n    mut xs [%s] = {%s}\n    xs[0] = %s\n    println(xs)\n}\n' "$id" "$setup" "$t" "$z" "$e" ;;
        map_value)
            printf 'do case_%s() {\n    %s\n    mut m map[string:%s] = {:}\n    m["k"] = %s\n    println(m)\n}\n' "$id" "$setup" "$t" "$e" ;;
        map_key)
            printf 'do case_%s() {\n    %s\n    mut m map[%s:string] = {:}\n    m[%s] = "v"\n    println(len(m))\n}\n' "$id" "$setup" "$t" "$e" ;;
        deref_assign)
            printf 'do case_%s() {\n    %s\n    mut x %s = %s\n    mut p ^%s = addr(x)\n    p^ = %s\n    println(x)\n}\n' "$id" "$setup" "$t" "$z" "$t" "$e" ;;
        field_assign)
            printf 'const F%s struct {\n    f %s\n}\ndo case_%s() {\n    %s\n    mut s F%s = F%s{f: %s}\n    s.f = %s\n    println(s.f)\n}\n' "$id" "$t" "$id" "$setup" "$id" "$id" "$z" "$e" ;;
        struct_literal)
            printf 'const S%s struct {\n    f %s\n}\ndo case_%s() {\n    %s\n    mut s S%s = S%s{f: %s}\n    println(s.f)\n}\n' "$id" "$t" "$id" "$setup" "$id" "$id" "$e" ;;
        field_default)
            printf 'const D%s struct {\n    f %s = %s\n}\ndo case_%s() {\n    mut s D%s = D%s{}\n    println(s.f)\n}\n' "$id" "$t" "$e" "$id" "$id" "$id" ;;
        default_param)
            printf 'do take%s(x %s = %s) -> %s {\n    return x\n}\ndo case_%s() {\n    println(take%s())\n}\n' "$id" "$t" "$e" "$t" "$id" "$id" ;;
        enum_payload)
            printf 'const E%s enum {\n    V(%s)\n    N\n}\ndo case_%s() {\n    %s\n    mut e E%s = E%s.V(%s)\n    when e {\n        is V(x) { println(x) }\n        default { println("n") }\n    }\n}\n' "$id" "$t" "$id" "$setup" "$id" "$id" "$e" ;;
        when_arm)
            printf 'do case_%s() {\n    mut w %s = %s\n    when w {\n        is %s { println("match") }\n        default { println("default") }\n    }\n}\n' "$id" "$t" "$z" "$e" ;;
        destructure)
            # The value's type is the function's result type; $e names it.
            # The second position keeps the source type, so each case has
            # exactly one conversion.
            printf 'do two%s() -> (%s, %s) {\n    return %s, %s\n}\ndo case_%s() {\n    mut a %s, b %s = two%s()\n    println(a)\n    println(b)\n}\n' "$id" "$e" "$e" "$(one "$e")" "$(one "$e")" "$id" "$t" "$e" "$id" ;;
        array_literal)
            printf 'do case_%s() {\n    %s\n    mut xs [%s] = {%s}\n    println(xs)\n}\n' "$id" "$setup" "$t" "$e" ;;
        map_literal_key)
            printf 'do case_%s() {\n    %s\n    mut m map[%s:string] = {%s: "v"}\n    println(len(m))\n}\n' "$id" "$setup" "$t" "$e" ;;
        map_literal_value)
            printf 'do case_%s() {\n    %s\n    mut m map[string:%s] = {"k": %s}\n    println(m)\n}\n' "$id" "$setup" "$t" "$e" ;;
        function_arg)
            printf 'do take%s(x %s) {\n    println(x)\n}\ndo case_%s() {\n    %s\n    take%s(%s)\n}\n' "$id" "$t" "$id" "$setup" "$id" "$e" ;;
        struct_function_arg)
            printf 'const H%s struct {\n    n i64\n\n    do take(x %s) {\n        println(x)\n    }\n}\ndo case_%s() {\n    %s\n    H%s.take(%s)\n}\n' "$id" "$t" "$id" "$setup" "$id" "$e" ;;
        func_value_arg)
            printf 'do show%s(x %s) {\n    println(x)\n}\ndo run%s(g func(%s)) {\n    %s\n    g(%s)\n}\ndo case_%s() {\n    run%s(()show%s)\n}\n' "$id" "$t" "$id" "$t" "$setup" "$e" "$id" "$id" "$id" ;;
        return)
            printf 'do give%s() -> %s {\n    %s\n    return %s\n}\ndo case_%s() {\n    println(give%s())\n}\n' "$id" "$t" "$setup" "$e" "$id" "$id" ;;
        map_index)
            printf 'do case_%s() {\n    %s\n    mut m map[%s:string] = {%s: "v"}\n    if len(m) > 5 {\n        println(m[%s])\n    }\n}\n' "$id" "$setup" "$t" "$z" "$e" ;;
        array_index)
            printf 'do case_%s() {\n    %s\n    mut xs [i64] = {7, 8}\n    println(xs[%s])\n}\n' "$id" "$setup" "$e" ;;
        string_index)
            printf 'do case_%s() {\n    %s\n    mut s string = "ab"\n    println(s[%s])\n}\n' "$id" "$setup" "$e" ;;
        builtin_arg)
            printf 'do case_%s() {\n    %s\n    sleep_ns(%s)\n}\n' "$id" "$setup" "$e" ;;
        stdlib_arg)
            printf 'do case_%s() {\n    %s\n    println(fmt.int_to_hex(%s))\n}\n' "$id" "$setup" "$e" ;;
        range_start|range_end|range_step|range_bound_with_i128)
            local bounds
            case "$slot" in
                range_start) bounds="$e, 3" ;;
                range_end) bounds="0, $e" ;;
                range_step) bounds="0, 3, $e" ;;
                range_bound_with_i128) bounds="$e, i128(3)" ;;
            esac
            printf 'do case_%s() {\n    %s\n    for i in range(%s) {\n        println(i)\n    }\n}\n' "$id" "$setup" "$bounds" ;;
    esac
}

# --- Expected outcomes --------------------------------------------------------

# The code a value of type $2 stored into an i64 slot $1 gets, or "ok".
i64_slot_outcome() {
    local slot="$1" s="$2"
    case "$s" in
        i8|i16|i32|i64|u8|u16|u32) echo ok ;;
        u64) echo E3019 ;;
        i128|i256|u128|u256) echo E3155 ;;
        f32|f64) case "$slot" in array_index|string_index) echo E3003 ;; *) echo E5026 ;; esac ;;
    esac
}

# The code a value of type $2 stored into range slot $1 gets, or "ok". An
# integer widens into i64 or makes the range wide; a u64 crosses signedness
# against i64, and a u128 or u256 against an i128 bound.
range_slot_outcome() {
    local slot="$1" s="$2"
    case "$s" in
        f32|f64) echo E5026 ;;
        u64) [ "$slot" = range_bound_with_i128 ] && echo ok || echo E3019 ;;
        u128|u256) [ "$slot" = range_bound_with_i128 ] && echo E3019 || echo ok ;;
        *) echo ok ;;
    esac
}

# --- Generation -----------------------------------------------------------------

rm -f "$PASS_DIR"/sized_grid_*.gray "$FAIL_DIR"/*_sized_grid_*.gray

HEADER_NOTE="Generated by scripts/generate_sized_type_tests.sh — do not edit; regenerate."

# Cases are buffered per output file in a scratch directory (bash 3 has no
# associative arrays): <key>.body holds the case functions, <key>.calls the
# main() calls, <key>.meta the destination path and expected code.
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

add_case() {
    local file="$1" code="$2" id="$3" text="$4"
    local key; key="$(basename "$file" .gray)"
    printf '%s\n' "$text" >> "$WORK/$key.body"
    printf '    case_%s()\n' "$id" >> "$WORK/$key.calls"
    printf '%s %s\n' "$file" "$code" > "$WORK/$key.meta"
}

case_number=0

emit() {
    # emit slot outcome case_label t setup expr
    local slot="$1" outcome="$2" label="$3" t="$4" setup="$5" e="$6"
    case_number=$((case_number + 1))
    local id="$case_number"
    local text; text="$(slot_case "$slot" "$id" "$t" "$setup" "$e")"
    if [ "$outcome" = ok ]; then
        add_case "$PASS_DIR/sized_grid_${slot}.gray" ok "$id" "$text"
    else
        add_case "$FAIL_DIR/${outcome}_sized_grid_${slot}_${label}.gray" "$outcome" "$id" "$text"
    fi
}

for slot in $SLOTS; do
    for t in $TYPES; do
        if typed_only "$slot"; then
            n="$(narrower "$t")"; [ -n "$n" ] && emit "$slot" ok widening "$t" "" "$n"
            w="$(wider "$t")"; [ -n "$w" ] && emit "$slot" E3155 narrowing "$t" "" "$w"
            o="$(other_sign "$t")"; [ -n "$o" ] && emit "$slot" E3019 sign_crossing "$t" "" "$o"
            continue
        fi
        # map keys of a float type are allowed, but only an integer is a
        # sensible when arm or index
        emit "$slot" ok in_range "$t" "" "$(max_literal "$t")"
        if [ "$slot" = when_arm ] && [ "${t:0:1}" = f ]; then :; else
            emit "$slot" E3036 out_of_range "$t" "" "$(over_literal "$t")"
        fi
        literal_only "$slot" && continue
        n="$(narrower "$t")"
        [ -n "$n" ] && emit "$slot" ok widening "$t" "mut src_V $n = $(one "$n")" "src_V"
        w="$(wider "$t")"
        [ -n "$w" ] && emit "$slot" E3155 narrowing "$t" "mut src_V $w = $(one "$w")" "src_V"
        o="$(other_sign "$t")"
        [ -n "$o" ] && emit "$slot" E3019 sign_crossing "$t" "mut src_V $o = $(one "$o")" "src_V"
        emit "$slot" ok binary "$t" "mut src_V $t = $(one "$t")" "src_V + $(one "$t")"
    done
done

for slot in $I64_SLOTS; do
    emit "$slot" ok in_range i64 "" 0
    emit "$slot" E3036 out_of_range i64 "" 9223372036854775808
    for s in $TYPES; do
        case "$s" in f*) v="0.0" ;; *) v=0 ;; esac
        outcome="$(i64_slot_outcome "$slot" "$s")"
        label="value_$outcome"; [ "$outcome" = ok ] && label=value
        emit "$slot" "$outcome" "$label" "$s" "mut src_V $s = $v" "src_V"
    done
done

for slot in $RANGE_SLOTS; do
    emit "$slot" ok in_range i64 "" 1
    [ "$slot" = range_bound_with_i128 ] || emit "$slot" E3036 out_of_range i64 "" 9223372036854775808
    for s in $TYPES; do
        outcome="$(range_slot_outcome "$slot" "$s")"
        label="value_$outcome"; [ "$outcome" = ok ] && label=value
        emit "$slot" "$outcome" "$label" "$s" "mut src_V $s = $(one "$s")" "src_V"
    done
done

# Every case names its own source variable.
fix_ids() { sed -E 's/src_V/src/g'; }

for meta in "$WORK"/*.meta; do
    key="$(basename "$meta" .meta)"
    read -r file code < "$meta"
    count="$(wc -l < "$WORK/$key.calls" | tr -d ' ')"
    imports=""
    case "$file" in *stdlib_arg*) imports=$'import @fmt\n\n' ;; esac
    {
        printf '// %s\n' "$HEADER_NOTE"
        if [ "$code" != ok ]; then
            printf '// expect-error: %s\n// expect-error-count: %s\n' "$code" "$count"
        fi
        printf '\n%s' "$imports"
        fix_ids < "$WORK/$key.body"
        printf '\ndo main() {\n'
        cat "$WORK/$key.calls"
        printf '}\n'
    } > "$file"
done

# --- Runtime cases --------------------------------------------------------------

# The overflow panic `+` raises for each integer type.
add_overflow_panic() {
    case "$1" in
        i8|i16|i32) echo P0011 ;; i64) echo P0004 ;;
        u8|u16|u32) echo P0015 ;; u64) echo P0008 ;;
        i128) echo P0021 ;; u128) echo P0024 ;; i256) echo P0027 ;; u256) echo P0030 ;;
    esac
}

# A compound assignment at the type's maximum, for every shape of target.
SHAPES="variable element map_value field deref element_through_pointer"
for shape in $SHAPES; do
    for t in $INT_TYPES; do
        code="$(add_overflow_panic "$t")"
        max="$(max_literal "$t")"
        file="$FAIL_DIR/${code}_sized_grid_compound_max_${shape}_${t}.gray"
        {
            printf '// %s\n// expect-contains: panic[%s]\n\n' "$HEADER_NOTE" "$code"
            case "$shape" in
                field) printf 'const Box struct {\n    f %s\n}\n\n' "$t" ;;
            esac
            printf 'do main() {\n'
            case "$shape" in
                variable) printf '    mut v %s = %s\n    v += 1\n    println(v)\n' "$t" "$max" ;;
                element) printf '    mut xs [%s] = {%s}\n    xs[0] += 1\n    println(xs)\n' "$t" "$max" ;;
                map_value) printf '    mut m map[string:%s] = {"k": %s}\n    m["k"] += 1\n    println(m)\n' "$t" "$max" ;;
                field) printf '    mut b Box = Box{f: %s}\n    b.f += 1\n    println(b.f)\n' "$max" ;;
                deref) printf '    mut x %s = %s\n    mut p ^%s = addr(x)\n    p^ += 1\n    println(x)\n' "$t" "$max" "$t" ;;
                element_through_pointer) printf '    mut xs [%s] = {%s}\n    mut p ^[%s] = addr(xs)\n    p^[0] += 1\n    println(xs)\n' "$t" "$max" "$t" ;;
            esac
            printf '}\n'
        } > "$file"
    done
done

# A shift by a count at, and beyond, the type's width.
width() { case "$1" in *128) echo 128 ;; *256) echo 256 ;; *16) echo 16 ;; *32) echo 32 ;; *64) echo 64 ;; *8) echo 8 ;; esac; }
for t in $INT_TYPES; do
    w="$(width "$t")"
    for which in at beyond; do
        count="$w"; [ "$which" = beyond ] && count=$((w + 1))
        file="$FAIL_DIR/P0092_sized_grid_shift_${which}_width_${t}.gray"
        printf '// %s\n// expect-contains: panic[P0092]\n\ndo main() {\n    mut v %s = 1\n    mut n i64 = %s\n    println(v bit_shift_left n)\n}\n' \
            "$HEADER_NOTE" "$t" "$count" > "$file"
    done
done

echo "generate_sized_type_tests.sh: wrote $(ls "$PASS_DIR"/sized_grid_*.gray | wc -l | tr -d ' ') pass and $(ls "$FAIL_DIR"/*_sized_grid_*.gray | wc -l | tr -d ' ') fail files"
