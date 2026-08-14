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
#   Linux/AutoSD side: actuation_module/freertos_x5h/edge_ecu_peer/
#     cyclonedds-x5h.xml.
#
# This intentionally does more than validate XML syntax: a config file that
# parses cleanly but has the wrong peer address, wrong domain, or leaves
# multicast on is exactly the failure this script exists to catch, so every
# check below fails loudly when a constant is missing OR wrong -- not only
# when a file is malformed.
set -euo pipefail

REPO_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
X5H_DIR="${REPO_ROOT}/actuation_module/freertos_x5h"
X5H_CMAKE="${X5H_DIR}/CMakeLists.txt"

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
    # Strips full-line comments (CMake '#', XML content never starts a line
    # with '#') before matching, so a stale/commented-out reference to the
    # right text -- e.g. `# add_compile_definitions(CONFIG_DDS_..._1)` --
    # cannot satisfy the check the way an active line would.
    #
    # The comment-stripping grep and the pattern-match grep are deliberately
    # NOT chained in a live pipe (`grep ... | grep -Eq ...`): `grep -q` exits
    # the instant it finds a match and closes its read end, which can send
    # SIGPIPE to a still-writing upstream grep on a larger file; under this
    # script's `set -o pipefail` that races the pipeline's exit status to a
    # spurious non-zero (i.e. a false FAIL on a constant that IS present)
    # depending on kernel pipe-buffer/scheduling timing. Capturing the
    # stripped text into a variable first, then matching against that static
    # string, removes the second process entirely -- no concurrent readers/
    # writers, no race. (Caught via repeated re-runs of this script during
    # perturbation testing producing a different, incorrect extra failure
    # each time.)
    local file="$1" desc="$2" pattern="$3"
    local stripped
    stripped=$(grep -Ev '^[[:space:]]*#' "${file}")
    if ! grep -Eq -- "${pattern}" <<<"${stripped}"; then
        record_fail "${desc}: pattern not found in an active (non-comment) line of ${file#${REPO_ROOT}/} (looked for: ${pattern})"
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

for xml in "${xml_configs[@]}"; do
    if [[ "${xml}" == *"cyclonedds-x5h.xml" ]]; then
        require_match "${xml}" "Linux side: DDS domain is not 2" \
            '<Domain Id="2">'
        require_match "${xml}" "Linux side: unicast SPDP peer is not 172.16.52.2 (CR52)" \
            '<Peer Address="172\.16\.52\.2"/>'
        require_match "${xml}" "Linux side: multicast is not disabled" \
            '<AllowMulticast>false</AllowMulticast>'
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
echo "  Linux side    (edge_ecu_peer/cyclonedds-x5h.xml): domain=2 peer=172.16.52.2 multicast=disabled"
