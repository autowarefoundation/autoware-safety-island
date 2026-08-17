# dds-plausibility.awk -- field-plausibility parser for verify-x5h-dds.sh's
# "field plausibility" gate (the DDS_ROUNDTRIP_PASS/DDS_ROUNDTRIP_FAIL
# reason=implausible check). Kept in its own file, rather than inlined into
# verify-x5h-dds.sh, so this exact parser can be exercised directly by
# scripts/test-dds-plausibility.sh instead of a copy of its logic that could
# drift from what actually runs on the board.
#
# Parses subscriber log lines by KEY NAME, not by positional field index.
# Real captured lines carry an ANSI colour escape, a timestamp, and a '|'
# separator before the key (produced by actuation_module/test/dds_sub.cpp's
# log_success() calls):
#
#   [32m[00:04:09.069] | steering_tire_angle: -0.236017 rad
#   [32m[00:04:09.069] | accel: 0.000000 m/s^2  velocity: 5.000000 m/s
#
# A positional read ($2 / $5) breaks the instant that prefix's shape
# changes -- it already did once on real hardware: $2 landed on the literal
# '|', and an anchored /^accel:/ pattern never matched a prefixed line at
# all, so every steer sample was reported "implausible" and every accel
# sample went uncounted. Scanning every field on the line for an exact
# key-token match and taking the field right after it works whether or not
# the prefix is present, and survives the prefix changing shape again.
#
# ANSI handling: no explicit strip pass -- immune by construction instead.
# awk's default field splitting is on whitespace, and the ANSI colour
# escape (ESC '[' '32' 'm') never contains whitespace, so it is always
# glued onto whatever token precedes it (the timestamp, or -- on the
# trailing `ESC[0m` reset line -- nothing else on that line) and never
# lands where a key-name token match is checked. An explicit strip
# (gsub of the escape pattern) was considered and rejected: it adds a
# pass over every line for a case the field scan already can't be fooled
# by, and it would need to run before field-splitting to matter, which
# awk's `{ ... }` action does not let a per-record BEGIN-less script do
# without re-splitting via $0 assignment.
#
# Numeric extraction: a value token is validated with is_numeric() BEFORE
# any arithmetic touches it (see check() below). This matters because
# awk's own string number conversion is silent and total -- a non-numeric
# token like a stray '|' or a truncated "0.5abc" is not an error to awk,
# it is the number 0, which then sails through every range check. That
# silent coercion was the actual defect that let a mis-parsed log report
# "implausible" on values that were never even read: this parser now
# fails a non-numeric token outright instead of coercing it.
#
# Invocation contract (unchanged):
#   awk -v maxsteer=<MAX_STEER_RAD> -v maxaccel=<MAX_ACCEL> -v maxvel=<MAX_VEL> \
#       -f dds-plausibility.awk <sub_log>
# Exit 0 iff at least one steering_tire_angle line and one accel line were
# seen, AND none of steering_tire_angle/accel/velocity were non-finite,
# non-numeric, or outside their bound. Exit 1 otherwise (including the
# vacuous case: zero matching lines). Diagnostics go to stderr; the
# caller (verify-x5h-dds.sh) turns a non-zero exit here into
# `DDS_ROUNDTRIP_FAIL reason=implausible`.

function bad(v) {
    # v != v only catches NaN on awk implementations whose numeric
    # comparison actually produces an IEEE-754 unordered result for it --
    # verified empirically that mawk 1.3.4 (this project's baseline awk;
    # see check-image-budget.sh's header comment) does NOT: a field
    # holding the string "nan" compares != to itself as false, even
    # through a copied variable, even though `+0` on that same value does
    # yield a working NaN internally. glibc's printf("%f", ...) is what
    # actually produces these strings on the board (dds_sub.cpp's
    # log_success calls), and it prints "nan"/"-nan" (never "NaN"), so an
    # explicit, portable string check for that form is added alongside
    # v != v rather than instead of it -- v != v is kept in case a
    # different awk implementation does support it, but nothing here
    # depends on that.
    return (v != v) || (v == "nan") || (v == "-nan") || (v == "inf") || (v == "-inf")
}
function absv(v) { return v < 0 ? -v : v }

# Strict numeric-token check: the exact %f shape glibc's printf produces
# (dds_sub.cpp's log_success calls), an optional leading '-' followed by
# digits, optionally a '.' and more digits. Deliberately NOT "does awk's
# implicit string->number coercion produce something usable" -- that
# coercion is the historical bug. awk treats any string it cannot parse
# as numeric as the number 0, so a garbage token sitting where a value
# belongs (a stray '|', a truncated write, "NULL", "0.5abc") would
# silently compare as 0 and sail through every range check below. This
# regex is checked BEFORE any arithmetic touches the token, so a bad
# token is rejected on its own, never coerced into a value that then
# happens to look in-range.
function is_numeric(v) { return v ~ /^-?[0-9]+(\.[0-9]+)?$/ }

# check(name, v, limit) -- validates one extracted field. Order matters:
# non-finite (nan/inf) is checked first since those ARE recognised
# floating-point tokens just not finite ones; then numeric-format,
# which rejects anything bad() didn't already catch and arithmetic
# would otherwise coerce to 0; only a token that is both finite and a
# real number reaches the range comparison. Returns 1 (and prints a
# reason to stderr) on any failure, 0 when the field is fine.
function check(name, v, limit) {
    if (bad(v)) {
        printf "implausible %s: %s (non-finite)\n", name, v > "/dev/stderr"
        return 1
    }
    if (!is_numeric(v)) {
        printf "implausible %s: %s (not a numeric token)\n", name, v > "/dev/stderr"
        return 1
    }
    if (absv(v + 0) > limit) {
        printf "implausible %s: %s (limit %s)\n", name, v, limit > "/dev/stderr"
        return 1
    }
    return 0
}

{
    for (i = 1; i <= NF; i++) {
        if ($i == "steering_tire_angle:") {
            seen_steer++
            bad_n += check("steering_tire_angle", $(i + 1), maxsteer)
        } else if ($i == "accel:") {
            seen_accel++
            bad_n += check("accel", $(i + 1), maxaccel)
        } else if ($i == "velocity:") {
            bad_n += check("velocity", $(i + 1), maxvel)
        }
    }
}

END {
    # No parsed fields at all means one of two things: the log's field
    # shape changed and this parser silently stopped checking (a defect
    # in this script -- the exact way the original positional parser
    # failed), or the run genuinely produced no control_cmd samples (a
    # defect in the run). Either way the assertion below would be
    # vacuous, not a pass, so both are treated as failure here. They are
    # distinguished only in this message, not in the caller's reason=
    # marker: verify-x5h-dds.sh always reports reason=implausible for a
    # non-zero exit from this script, by design (see its reason=
    # vocabulary comment).
    if (seen_steer == 0 || seen_accel == 0) {
        printf "no steering_tire_angle/accel fields matched by key name (steer=%d accel=%d): " \
               "either the log's field shape changed and this parser silently stopped checking, " \
               "or the run produced no control_cmd samples -- treating both as failure, not a vacuous pass\n", \
               seen_steer, seen_accel > "/dev/stderr"
        exit 1
    }
    exit (bad_n > 0) ? 1 : 0
}
