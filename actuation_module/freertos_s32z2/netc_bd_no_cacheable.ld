/*
 * Relocate the NXP NETC buffer-descriptor (BD) rings into the non-cacheable
 * SRAM region so they are coherent with the NETC DMA once the R52 L1 D-cache
 * is enabled.
 *
 * Why this is needed: the generated Netc_Eth_Ip_Buffers.c wraps the RX/TX BD
 * ring descriptors (Netc_Eth_*_RxRing_*_DescBuffer / TxRing_*_DescBuffer /
 * Netc_Eth_CommandBDR) in the AUTOSAR MemMap section
 * ETH_43_NETC_*_VAR_CLEARED_UNSPECIFIED_NO_CACHEABLE, but that section is
 * realised with a Green Hills `#pragma ghs section` (gated on
 * _GREENHILLS_C_S32ZE_). We build with arm-none-eabi-gcc, which ignores the
 * pragma, so the descriptors fell into the cacheable .sram_data region
 * (int_sram_dram, ~0x3178_d000). With the D-cache enabled (commit 4511075) the
 * CPU then read STALE descriptors — the NETC DMA advanced the producer index
 * (RBPIR) but the poll thread's Eth_Receive saw stale "not ready" BDs, so the
 * consumer index (RBCIR) never advanced and RX stalled after the ring filled
 * (observed on HW: RBPIR=15 / RBCIR=0 with D-cache ON; RBPIR==RBCIR cycling
 * with D-cache OFF).
 *
 * The .non_cacheable / int_sram_no_cacheable region (0x3180_0000) is already
 * marked non-cacheable in the MPU tables; the RX data buffers (ethif_DataBuffer
 * in eth_port.c) already live there via an explicit attribute. This fragment
 * does the same for the BD ring descriptors (the driver's own *_DataBuffer
 * arrays are unused — eth_port.c provides ethif_DataBuffer instead).
 *
 * Placed via INSERT BEFORE .sram_data so this output section claims
 * Netc_Eth_Ip_Buffers.o's .data/.bss before the default script's greedy
 * .sram_data ( *(.data*) / *(.bss*) ) can pull them into cacheable memory.
 * Total payload is tiny (~1.5 KB of descriptors).
 */
SECTIONS
{
    .netc_bd_no_cacheable :
    {
        . = ALIGN(64);
        /* RX/TX BD ring descriptors (Netc_Eth_Ip_Buffers.c) and the NTMP
         * command BD ring (Netc_Eth_CommandBDR, static in the *_PBcfg.c). */
        *Netc_Eth_Ip_Buffers*(.data .data.* .bss .bss.* COMMON)
        *Netc_Eth_Ip*PBcfg*(.data .data.* .bss .bss.* COMMON)
        . = ALIGN(64);
    } > int_sram_no_cacheable
} INSERT BEFORE .sram_data;
