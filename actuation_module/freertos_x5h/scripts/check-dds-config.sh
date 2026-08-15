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
# ever read), so this fixes the comment-evasion bug for the case that
# motivated it.
#
# CORRECTED (review finding, Important #7): a previous revision of this
# comment claimed that switching to `xmllint --xpath` made these checks
# "structurally immune" to comment-evasion in general -- that overclaimed.
# `xmllint --xpath 'string(...)'` reads only the FIRST node the path
# resolves to; a document with a second, uncommented `<Peer Address="...">`
# added after the correct one would still make `string(...)` return the
# first (correct) value and PASS, even though a real CycloneDDS parser
# reading the same file could behave differently with two peers present.
# The require_xpath calls below now also assert `count(...)=1` on every
# path where more than one match would be a real ambiguity (the `<Peer>`
# node), so a duplicate is caught as its own failure rather than silently
# passing because the first match happened to be right.
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
    # Comment syntax is per-language, and getting this wrong is not a
    # theoretical risk: the `#` rule below is correct for CMake/shell but
    # catastrophic for C, where it strips every `#define` -- i.e. exactly the
    # lines these checks exist to find. Not hypothetical either: it made all
    # six C-side assertions unsatisfiable the first time they ran, reporting
    # "not found" for constants that were present and correct.
    #
    # C sources therefore get `//` stripping instead. Block comments (/* */)
    # are deliberately NOT handled -- doing that correctly needs multi-line
    # state, and a `#define` of a wire constant sitting inside a block comment
    # is a shape this codebase does not have. Said plainly so nobody reads
    # this helper as more general than it is.
    local file="$1" desc="$2" pattern="$3"
    local stripped
    case "${file}" in
        *.c|*.h) stripped=$(sed -E 's://.*$::' "${file}") ;;
        *)       stripped=$(sed -E 's/(^|[[:space:]])#.*$//' "${file}") ;;
    esac
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
    # Task 21 fix round 1 (Important #3): the three DDS wire-sizing literals
    # derived in common/dds/config.hpp -- previously only cross-checked by a
    # one-shot flags.make inspection at review time, which this plan has
    # already been bitten by once (a peer built without its -D flags
    # silently reverting to CycloneDDS's own defaults). Same mechanism this
    # script already uses for the four constants above.
    require_match "${X5H_CMAKE}" "FreeRTOS side: DDS max message size is not 434" \
        'CONFIG_DDS_MAX_MSG_SIZE 434 CACHE'
    require_match "${X5H_CMAKE}" "FreeRTOS side: DDS max rexmit message size is not 434" \
        'CONFIG_DDS_MAX_REXMIT_MSG_SIZE 434 CACHE'
    require_match "${X5H_CMAKE}" "FreeRTOS side: DDS fragment size is not 348" \
        'CONFIG_DDS_FRAGMENT_SIZE 348 CACHE'
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
    # Task 21 fix round 1 (Important #3): same three DDS wire-sizing
    # literals as the FreeRTOS-side check above, on this side's own -D
    # mechanism (see this script's header comment for why the -D flags
    # here, not cyclonedds-x5h.xml, are what actually reaches the binaries).
    require_match "${ARM64_BUILD_SCRIPT}" "Linux side: DDS max message size is not 434" \
        '\-DCONFIG_DDS_MAX_MSG_SIZE=434'
    require_match "${ARM64_BUILD_SCRIPT}" "Linux side: DDS max rexmit message size is not 434" \
        '\-DCONFIG_DDS_MAX_REXMIT_MSG_SIZE=434'
    require_match "${ARM64_BUILD_SCRIPT}" "Linux side: DDS fragment size is not 348" \
        '\-DCONFIG_DDS_FRAGMENT_SIZE=348'
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
        # count()=1 (review finding, Important #7): string(...) alone only
        # ever reads the FIRST matching <Peer>, so a second, uncommented
        # <Peer> node added after a correct one would still PASS the value
        # check below even though it makes the document genuinely
        # ambiguous. Asserted before the value check so a duplicate is
        # reported as its own distinct failure.
        require_xpath "${xml}" "Linux side (XML doc): expected exactly one <Peer> node" \
            'string(count(/CycloneDDS/Domain/Discovery/Peers/Peer))' '1'
        require_xpath "${xml}" "Linux side (XML doc): unicast SPDP peer is not 172.16.52.2 (CR52)" \
            'string(/CycloneDDS/Domain/Discovery/Peers/Peer/@Address)' '172.16.52.2'
        require_xpath "${xml}" "Linux side (XML doc): multicast is not disabled" \
            'string(/CycloneDDS/Domain/General/AllowMulticast/text())' 'false'
    fi
done

# ---- 4. CR52 side, lwIP static netif address (Important #7) ----
# lwip_bringup.c's LWIP_STATIC_IP/LWIP_STATIC_NETMASK/LWIP_STATIC_GW are the
# actual constants that set the CR52's netif address at runtime -- previously
# never cross-checked here at all, despite this script's own header claiming
# to verify the wire config "on both sides".
LWIP_BRINGUP="${X5H_DIR}/lwip_bringup.c"
if [ ! -f "${LWIP_BRINGUP}" ]; then
    record_fail "missing FreeRTOS-side lwIP bring-up: ${LWIP_BRINGUP#${REPO_ROOT}/}"
else
    require_match "${LWIP_BRINGUP}" "FreeRTOS side (lwIP): static IP is not 172.16.52.2" \
        'LWIP_STATIC_IP[[:space:]]+"172\.16\.52\.2"'
    require_match "${LWIP_BRINGUP}" "FreeRTOS side (lwIP): static netmask is not 255.255.255.0" \
        'LWIP_STATIC_NETMASK[[:space:]]+"255\.255\.255\.0"'
    require_match "${LWIP_BRINGUP}" "FreeRTOS side (lwIP): static gateway is not 172.16.52.1" \
        'LWIP_STATIC_GW[[:space:]]+"172\.16\.52\.1"'
fi

# ---- 5. RPMsg wire-format constants (Important #7) ----
# rpmsg_netif_core.h's RPMSG_ETH_SERVICE/RPMSG_ETH_MTU/RPMSG_ETH_MAX_FRAME and
# rpmsg_netif.c's CR52 MAC literal are frozen wire constants both sides must
# agree on (the Linux peer's side of the MTU/frame-size agreement is
# implicit in the tap0 MTU it is brought up with; the service name and MAC
# are consumed on the Linux side via the rpmsg-eth chardev and the kernel's
# own ARP resolution of whatever the CR52 announces). Previously unchecked.
RPMSG_NETIF_CORE_H="${X5H_DIR}/rpmsg_netif_core.h"
RPMSG_NETIF_C="${X5H_DIR}/rpmsg_netif.c"
if [ ! -f "${RPMSG_NETIF_CORE_H}" ]; then
    record_fail "missing RPMsg wire-format header: ${RPMSG_NETIF_CORE_H#${REPO_ROOT}/}"
else
    require_match "${RPMSG_NETIF_CORE_H}" "RPMsg side: service name is not rpmsg-eth" \
        'RPMSG_ETH_SERVICE[[:space:]]+"rpmsg-eth"'
    require_match "${RPMSG_NETIF_CORE_H}" "RPMsg side: MTU is not 462" \
        'RPMSG_ETH_MTU[[:space:]]+462'
    require_match "${RPMSG_NETIF_CORE_H}" "RPMsg side: max frame is not (RPMSG_ETH_MTU + 14)" \
        'RPMSG_ETH_MAX_FRAME[[:space:]]+\(RPMSG_ETH_MTU \+ 14\)'
fi
if [ ! -f "${RPMSG_NETIF_C}" ]; then
    record_fail "missing RPMsg netif glue: ${RPMSG_NETIF_C#${REPO_ROOT}/}"
else
    require_match "${RPMSG_NETIF_C}" "RPMsg side: CR52 MAC is not 02:5c:52:00:00:02" \
        '0x02, 0x5c, 0x52, 0x00, 0x00, 0x02'
fi

if [ "${fail}" -ne 0 ]; then
    echo "FAIL: check-dds-config.sh" >&2
    for reason in "${fail_reasons[@]}"; do
        echo "  - ${reason}" >&2
    done
    exit 1
fi

echo "PASS: check-dds-config.sh"
echo "  XML validated: ${#xml_configs[@]} file(s)"
echo "  FreeRTOS side (${X5H_CMAKE#${REPO_ROOT}/}): interface=172.16.52.2 peer=172.16.52.1 domain=2 multicast=disabled max_msg_size=434 max_rexmit_msg_size=434 fragment_size=348"
echo "  Linux side    (${ARM64_BUILD_SCRIPT#${REPO_ROOT}/}): interface=tap0 peer=172.16.52.2 domain=2 multicast=disabled max_msg_size=434 max_rexmit_msg_size=434 fragment_size=348"
echo "  Linux side    (edge_ecu_peer/cyclonedds-x5h.xml, doc cross-check): domain=2 peer=172.16.52.2 multicast=disabled (exactly one <Peer>)"
echo "  FreeRTOS side (${LWIP_BRINGUP#${REPO_ROOT}/}): ip=172.16.52.2 netmask=255.255.255.0 gw=172.16.52.1"
echo "  RPMsg side    (${RPMSG_NETIF_CORE_H#${REPO_ROOT}/}): service=rpmsg-eth mtu=462 max_frame=(mtu+14)"
echo "  RPMsg side    (${RPMSG_NETIF_C#${REPO_ROOT}/}): CR52 mac=02:5c:52:00:00:02"
