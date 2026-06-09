/*
 * Copyright (c) 2026, Arm Limited and contributors.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Place the FreeRTOS (heap_4) ucHeap in the 7 MiB int_sram code region.
 *
 * NXP's linker_ram_r52.ld puts .sram_data (.data/.bss, the lwIP pool, the
 * controller node_stack, CycloneDDS/autoware initialised globals AND ucHeap)
 * into int_sram_dram, which is only 512 KiB and already ~500 KiB full. That
 * caps the FreeRTOS heap at ~96 KiB, which CycloneDDS exhausts during
 * dds_create_domain (pvPortMalloc -> NULL -> ddsrt_malloc -> abort).
 *
 * With configAPPLICATION_ALLOCATED_HEAP=1, freertos_main.cpp defines ucHeap in
 * the .freertos_heap input section; this fragment maps it into int_sram (the
 * ~7 MiB code RAM, of which only ~2.6 MiB is used by .sram_code). NOLOAD keeps
 * the multi-MiB array out of the ELF image. INSERT AFTER augments NXP's script
 * rather than replacing it, so the vendor linker file stays unmodified.
 */
SECTIONS
{
    .freertos_heap (NOLOAD) :
    {
        . = ALIGN(8);
        __freertos_heap_start = .;
        *(.freertos_heap)
        . = ALIGN(8);
        __freertos_heap_end = .;
    } > int_sram
} INSERT AFTER .sram_code;
