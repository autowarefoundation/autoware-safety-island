#!/usr/bin/env bash
# Verify the frozen X5H DDS wire constants are present -- and, for every DDS
# XML config that exists, that it is well-formed -- on both sides of the
# RPMsg-backed DDS link:
#
#   FreeRTOS/CR52 side: actuation_module/freertos_x5h/CMakeLists.txt's
#     CONFIG_DDS_* CACHE vars + compile definitions. Unlike the S32Z2
#     precedent's edge_ecu_peer, this side has no DDS XML config at all --
#     see that CMakeLists.txt's Task 8 comment for why (a point-to-point
#     RPMsg link cannot fall back to multicast the way S32Z2's switched
#     bench can, so the peer + multicast-disable knobs are compiled in
#     directly, exactly the mechanism the S32Z2 target already uses for its
#     own CONFIG_DDS_PEER).
#
#   Linux/AutoSD side: actuation_module/freertos_x5h/scripts/
#     build-edge-ecu-peer-arm64.sh's `cmake -S .../edge_ecu_peer -B ...`
#     invocation. Review round 2 (Important #1): this -- not
#     edge_ecu_peer/cyclonedds-x5h.xml -- is the actual mechanism that wires
#     the Linux side, because this codebase's DDS wrapper
#     (dds_create_domain_with_rawconfig()) never parses CYCLONEDDS_URI/XML at
#     runtime; the XML file's own header comment says so explicitly. The
#     wire constants only take effect on the Linux side via the -D flags
#     baked into that build script, so this checker asserts against the
#     script first and treats the XML as a secondary documentation
#     cross-check, not the primary assertion.
#
# This intentionally does more than validate XML syntax: a config file that
# parses cleanly but has the wrong peer address, wrong domain, or leaves
# multicast on is exactly the failure this script exists to catch, so every
# check below fails loudly when a constant is missing OR wrong -- not only
# when a file is malformed.
#
# Review round 2 (Important #2): the XML-side checks used to be
# comment-stripping grep, which a `<!-- ... -->`-wrapped node defeats (XML
# comments don't start with '#', so the CMake-oriented strip logic below
# never touched them -- a commented-out <Peers> node produced a false PASS).
# XML checks now go through `xmllint --xpath`, which evaluates the parsed
# DOM: comments are invisible to the DOM the same way they are invisible to
# any real XML consumer (including this project's own CycloneDDS-if-XML-were
# ever read), so this is structurally immune to the comment-evasion bug,
# not just patched against today's one known shape of it.
set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
X5H_DIR="${REPO_ROOT}/actuation_module/freertos_x5h"
X5H_CMAKE="${X5H_DIR}/CMakeLists.txt"
ARM64_BUILD_SCRIPT="${X5H_DIR}/scripts/build-edge-ecu-peer-arm64.sh"

fail=0
fail_reasons=()

XMLLINT_ERR=$(mktemp)
trap 'rm -f "${XMLLINT_ERR}"' EXIT

record_fail() {
    fail=1
    fail_reasons+=("$1")
}

require_match() {
    # require_match <file> <description> <grep -E pattern>
    #
    # Strips comments -- both full-line ('#...') and trailing
    # ('code  # comment') -- before matching, so a stale/commented-out
    # reference to the right text -- e.g. `# add_compile_definitions(...)`
    # or `-DCONFIG_DDS_PEER=172.16.52.9  # -DCONFIG_DDS_PEER=172.16.52.2` --
    # cannot satisfy the check the way an active, uncommented line would.
    # (Review round 2, Minor #3: the previous version only stripped
    # full-line comments, so a trailing comment containing the right text
    # could falsely satisfy a check on the *actual*, wrong, code before it
    # on the same line.)
    #
    # sed -E 's/(^|[[:space:]])#.*$//' is used instead of the previous
    # `grep -Ev '^[[:space:]]*#'` for a second reason (review round 2, Minor
    # #2): grep exits 1 when it selects zero lines (e.g. a file that is
    # ALL comments), and under this script's `set -e`,
    # `stripped=$(grep -Ev ...)` failing would abort the whole script with
    # no diagnostic. sed has no equivalent failure mode -- it always exits 0
    # regardless of how much of its input it blanked out -- so switching to
    # it fixes Minor #2 and Minor #3 together.
    #
    # The comment-stripping sed and the pattern-match grep are deliberately
    # NOT chained in a live pipe (`sed ... | grep -Eq ...`): `grep -q` exits
    # the instant it finds a match and closes its read end, which can send
    # SIGPIPE to a still-writing upstream process on a larger file; under
    # this script's `set -o pipefail` that races the pipeline's exit status
    # to a spurious non-zero (i.e. a false FAIL on a constant that IS
    # present) depending on kernel pipe-buffer/scheduling timing. Capturing
    # the stripped text into a variable first, then matching against that
    # static string, removes the second process entirely -- no concurrent
    # readers/writers, no race. (Caught via repeated re-runs of this script
    # during round-1 perturbation testing producing a different, incorrect
    # extra failure each time.)
    local file="$1" desc="$2" pattern="$3"
    local stripped
    stripped=$(sed -E 's/(^|[[:space:]])#.*$//' "${file}")
    if ! grep -Eq -- "${pattern}" <<<"${stripped}"; then
        record_fail "${desc}: pattern not found in an active (non-comment) line of ${file#${REPO_ROOT}/} (looked for: ${pattern})"
    fi
}

require_xpath() {
    # require_xpath <xml-file> <description> <xpath-string()-expr> <expected>
    #
    # Evaluates the given XPath expression (expected to be wrapped in
    # string(...) by the caller) against the XML's parsed DOM via
    # `xmllint --xpath`, then compares the printed value against <expected>
    # by equality -- not by exit code. Verified empirically:
    # `xmllint --xpath 'string(...)'` exits 0 whether or not the path
    # resolves (a non-matching path just prints an empty string), so exit
    # code alone cannot distinguish "found and correct", "found and wrong",
    # and "structurally absent" (e.g. the whole node commented out). Reading
    # and comparing the printed value catches all three.
    #
    # `|| actual=""` guards the assignment under `set -e`: xmllint could
    # still exit non-zero on some malformed-XML edge case even though the
    # earlier `xmllint --noout` pass already checks well-formedness
    # separately -- this keeps that failure mode from aborting the whole
    # script before its own record_fail can run.
    local file="$1" desc="$2" xpath="$3" expected="$4"
    local actual
    actual=$(xmllint --xpath "${xpath}" "${file}" 2>"${XMLLINT_ERR}") || actual=""
    if [ "${actual}" != "${expected}" ]; then
        record_fail "${desc}: expected '${expected}', got '${actual:-<empty -- node/attribute missing or commented out>}' (xpath: ${xpath}) in ${file#${REPO_ROOT}/}"
    fi
}

# ---- 1. xmllint --noout every DDS XML config that exists ----
# Scoped to freertos_x5h/ itself and edge_ecu_peer/ (not the rcar_bsp/lwip
# vendor trees, which carry unrelated XML that is not DDS configuration).
xml_configs=()
while IFS= read -r -d '' f; do
    xml_configs+=("${f}")
done < <(find "${X5H_DIR}" -maxdepth 2 -name '*.xml' -print0 2>/dev/null)

if [ "${#xml_configs[@]}" -eq 0 ]; then
    record_fail "no DDS XML config found under ${X5H_DIR#${REPO_ROOT}/} (expected at least edge_ecu_peer/cyclonedds-x5h.xml)"
fi

for xml in "${xml_configs[@]}"; do
    if ! xmllint --noout "${xml}" 2>"${XMLLINT_ERR}"; then
        record_fail "xmllint --noout failed for ${xml#${REPO_ROOT}/}: $(cat "${XMLLINT_ERR}")"
    fi
done

# ---- 2. the four frozen wire constants ----
# CR52 172.16.52.2, Linux 172.16.52.1, DDS domain 2, multicast disabled.
# Checked on each side that carries that constant in its own config form.

if [ ! -f "${X5H_CMAKE}" ]; then
    record_fail "missing FreeRTOS-side config: ${X5H_CMAKE#${REPO_ROOT}/}"
else
    require_match "${X5H_CMAKE}" "FreeRTOS side: own IP (interface) is not 172.16.52.2" \
        'CONFIG_DDS_NETWORK_INTERFACE "172\.16\.52\.2"'
    require_match "${X5H_CMAKE}" "FreeRTOS side: unicast SPDP peer is not 172.16.52.1" \
        'CONFIG_DDS_PEER "172\.16\.52\.1"'
    require_match "${X5H_CMAKE}" "FreeRTOS side: DDS domain is not 2" \
        'CONFIG_DDS_DOMAIN_ID 2 CACHE'
    require_match "${X5H_CMAKE}" "FreeRTOS side: multicast is not disabled" \
        'CONFIG_DDS_DISABLE_MULTICAST=1'
fi

# ---- 2b. Linux side: the arm64 build script's -D flags (Important #1) ----
# This is the mechanism that actually reaches the Linux-side binaries; the
# XML checks in step 3 below are a secondary cross-check only.
if [ ! -f "${ARM64_BUILD_SCRIPT}" ]; then
    record_fail "missing Linux-side build script: ${ARM64_BUILD_SCRIPT#${REPO_ROOT}/}"
else
    require_match "${ARM64_BUILD_SCRIPT}" "Linux side: DDS domain is not 2" \
        '\-DCONFIG_DDS_DOMAIN_ID=2'
    require_match "${ARM64_BUILD_SCRIPT}" "Linux side: RPMsg-netif interface is not tap0" \
        '\-DCONFIG_DDS_NETWORK_INTERFACE=tap0'
    require_match "${ARM64_BUILD_SCRIPT}" "Linux side: unicast SPDP peer is not 172.16.52.2 (CR52)" \
        '\-DCONFIG_DDS_PEER=172\.16\.52\.2'
    require_match "${ARM64_BUILD_SCRIPT}" "Linux side: multicast is not disabled" \
        'CONFIG_DDS_DISABLE_MULTICAST=1'
fi

# ---- 3. Linux side, secondary cross-check: cyclonedds-x5h.xml (Important #2) ----
# Not read at runtime (see the XML file's own header comment and this
# script's Important #1 note above), but kept as a documentation artifact
# that must itself state the same constants, checked via xmllint --xpath so
# a commented-out node cannot pass.
for xml in "${xml_configs[@]}"; do
    if [[ "${xml}" == *"cyclonedds-x5h.xml" ]]; then
        require_xpath "${xml}" "Linux side (XML doc): DDS domain is not 2" \
            'string(/CycloneDDS/Domain/@Id)' '2'
        require_xpath "${xml}" "Linux side (XML doc): unicast SPDP peer is not 172.16.52.2 (CR52)" \
            'string(/CycloneDDS/Domain/Discovery/Peers/Peer/@Address)' '172.16.52.2'
        require_xpath "${xml}" "Linux side (XML doc): multicast is not disabled" \
            'string(/CycloneDDS/Domain/General/AllowMulticast/text())' 'false'
    fi
done

if [ "${fail}" -ne 0 ]; then
    echo "FAIL: check-dds-config.sh" >&2
    for reason in "${fail_reasons[@]}"; do
        echo "  - ${reason}" >&2
    done
    exit 1
fi

echo "PASS: check-dds-config.sh"
echo "  XML validated: ${#xml_configs[@]} file(s)"
echo "  FreeRTOS side (${X5H_CMAKE#${REPO_ROOT}/}): interface=172.16.52.2 peer=172.16.52.1 domain=2 multicast=disabled"
echo "  Linux side    (${ARM64_BUILD_SCRIPT#${REPO_ROOT}/}): interface=tap0 peer=172.16.52.2 domain=2 multicast=disabled"
echo "  Linux side    (edge_ecu_peer/cyclonedds-x5h.xml, doc cross-check): domain=2 peer=172.16.52.2 multicast=disabled"
