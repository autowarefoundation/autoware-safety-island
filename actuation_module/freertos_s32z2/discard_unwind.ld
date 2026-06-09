/*
 * Copyright (c) 2026, Arm Limited and contributors.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Discard the C++ exception-unwind index/table sections.
 *
 * NXP's linker_ram_r52.ld has no output section for .ARM.exidx / .ARM.extab, so
 * they land as orphan sections. At -O0 ld's orphan placement coped, but with any
 * optimisation (-O1+) inlining eliminates the .text these SHF_LINK_ORDER exidx
 * entries point at, and ld segfaults while sorting/placing the dangling orphan
 * exidx. Unwinding is already disabled on this port (__exidx_start/__exidx_end are
 * defsym'd to 0, so a throw goes straight to std::terminate regardless), so the
 * tables are dead weight -- discard them. This both fixes the optimised-build ld
 * crash and drops ~3.5 MB of per-function unwind sections from the image.
 *
 * Given as an INSERT script (augments NXP's --default-script rather than replacing
 * it), like heap_in_sram.ld / node_stack_in_sram.ld.
 */
SECTIONS
{
    /DISCARD/ :
    {
        *(.ARM.exidx*)
        *(.ARM.extab*)
    }
} INSERT AFTER .sram_code;
