/*
 * Copyright (c) 2026, Arm Limited and contributors.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* Places .resource_table at the base of the demo role's CR52 carveout,
 * cr52_ram1@5da00000 (2 MiB, no-map). Replaces the vendor
 * lscript_rsc_table_vram2.ld, which targets the MEMORY region
 * remote_proc_rsc_table_1 at 0x96650000 -- an address the NPU device tree
 * hands to the NPU. An absolute section address is used instead of a new
 * MEMORY region so this script overlays the vendor lscript_vram2.ld
 * without redefining its MEMORY block. */
SECTIONS
{
.resource_table 0x5da00000 : {
    __resource_table_start = .;
    . = ALIGN(4);
    KEEP(*(.resource_table))
    __resource_table_end = .;
}
}
