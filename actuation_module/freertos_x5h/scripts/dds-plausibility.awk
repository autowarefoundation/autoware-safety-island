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
# Invocation contract (unchanged):
#   awk -v maxsteer=<MAX_STEER_RAD> -v maxaccel=<MAX_ACCEL> -v maxvel=<MAX_VEL> \
#       -f dds-plausibility.awk <sub_log>
# Exit 0 iff at least one steering_tire_angle line and one accel line were
# seen, AND none of steering_tire_angle/accel/velocity were non-finite or
# outside their bound. Exit 1 otherwise (including the vacuous case: zero
# matching lines). Diagnostics go to stderr; the caller
# (verify-x5h-dds.sh) turns a non-zero exit here into
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

{
    for (i = 1; i <= NF; i++) {
        if ($i == "steering_tire_angle:") {
            v = $(i + 1); seen_steer++
            if (bad(v) || absv(v) > maxsteer) {
                printf "implausible steering_tire_angle: %s (limit %s)\n", v, maxsteer > "/dev/stderr"
                bad_n++
            }
        } else if ($i == "accel:") {
            a = $(i + 1); seen_accel++
            if (bad(a) || absv(a) > maxaccel) {
                printf "implausible accel: %s (limit %s)\n", a, maxaccel > "/dev/stderr"
                bad_n++
            }
        } else if ($i == "velocity:") {
            v = $(i + 1)
            if (bad(v) || absv(v) > maxvel) {
                printf "implausible velocity: %s (limit %s)\n", v, maxvel > "/dev/stderr"
                bad_n++
            }
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
