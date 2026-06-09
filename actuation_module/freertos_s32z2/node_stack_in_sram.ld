/*
 * Copyright (c) 2026, Arm Limited and contributors.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Place the controller node thread's stack (node_stack, defined by
 * K_THREAD_STACK_DEFINE in controller_node.cpp) in the 7 MiB int_sram code
 * region instead of the 512 KiB int_sram_dram.
 *
 * The node thread runs the MPC lateral + PID longitudinal control cycle, which
 * is stack-heavy (large Eigen matrices + OSQP live on the stack). At 256 KiB in
 * int_sram_dram it overflowed by a few hundred bytes (saved SP below the stack
 * base, caught by vApplicationStackOverflowHook). int_sram_dram is nearly full
 * (~100 KiB free), so the stack is moved here and enlarged (CONFIG_THREAD_STACK_SIZE).
 *
 * Mirrors heap_in_sram.ld: a NOLOAD input section (.node_stack) mapped into
 * int_sram via INSERT AFTER (augments NXP's linker_ram_r52.ld rather than
 * replacing it). NOLOAD keeps the multi-hundred-KiB array out of the ELF image.
 * Placed AFTER .freertos_heap so the two large int_sram tenants pack in order.
 */
SECTIONS
{
    .node_stack (NOLOAD) :
    {
        . = ALIGN(16);
        __node_stack_start = .;
        *(.node_stack)
        . = ALIGN(16);
        __node_stack_end = .;
    } > int_sram
} INSERT AFTER .freertos_heap;
