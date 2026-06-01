// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Minimal EthIf / OsIf glue for the NETC <-> lwIP datapath.
//
// The RTD NETC driver (Eth_43_NETC.c / Eth_43_NETC_Ipw.c) reports received
// frames and completed transmissions through the AUTOSAR EthIf upper-layer
// callbacks EthIf_RxIndication() / EthIf_TxConfirmation(). In a full AUTOSAR
// stack those live in EthIf.c, which classifies the frame and forwards it to
// the TcpIp layer. The NXP RTD lwIP port short-circuits that: its eth_port.c
// implements Tcpip_RxIndication() / Tcpip_TxConfirmation() (which push the
// frame into lwIP via netif->input). We build no EthIf.c and S32CT generates
// none, so we provide a thin pass-through bridge here. The argument lists are
// identical, and the driver already hands EthIf_RxIndication the payload
// pointer + length that Tcpip_RxIndication expects (it backs the pointer up by
// the Ethernet-header offset internally).
//
// OsIf_TimeDelay() is referenced by eth_port.c's poll thread (poll mode only)
// but is not shipped by this RTD's BaseNXP OsIf; map it to vTaskDelay.

#include "EthIf.h"      // EthIf_* prototypes + Eth_FrameType/Eth_BufIdxType/etc.

#include "FreeRTOS.h"
#include "task.h"

// Defined in the NXP RTD lwIP port (eth_port.c); no public header declares them.
extern void Tcpip_RxIndication(uint8 CtrlIdx,
                               Eth_FrameType FrameType,
                               boolean IsBroadcast,
                               const uint8 *PhysAddrPtr,
                               const uint8 *DataPtr,
                               uint16 LenByte);
extern void Tcpip_TxConfirmation(uint8 CtrlIdx,
                                 Eth_BufIdxType BufIdx,
                                 Std_ReturnType Result);

void EthIf_RxIndication(uint8 CtrlIdx,
                        Eth_FrameType FrameType,
                        boolean IsBroadcast,
                        const uint8 *PhysAddrPtr,
                        const Eth_DataType *DataPtr,
                        uint16 LenByte) {
    Tcpip_RxIndication(CtrlIdx, FrameType, IsBroadcast, PhysAddrPtr,
                       (const uint8 *)DataPtr, LenByte);
}

void EthIf_TxConfirmation(uint8 CtrlIdx,
                          Eth_BufIdxType BufIdx,
                          Std_ReturnType Result) {
    Tcpip_TxConfirmation(CtrlIdx, BufIdx, Result);
}

// The driver also signals controller mode transitions (Eth_SetControllerMode);
// lwIP does not need it, so absorb it.
void EthIf_CtrlModeIndication(uint8 CtrlIdx, Eth_ModeType CtrlMode) {
    (void)CtrlIdx;
    (void)CtrlMode;
}

// eth_port.c poll thread: OsIf_TimeDelay(1) between Eth_Receive sweeps.
void OsIf_TimeDelay(uint32 milliseconds) {
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
}

// Poll-mode (ETH_43_NETC_RX_IRQ_ENABLED=STD_OFF) link stub. The generated PB
// config Netc_Eth_Ip_VS_0_PBcfg.c was produced with RX interrupts enabled, so
// its RX ring-config struct still names Eth_43_NETC_RxIrqCallback in a function
// pointer. In poll mode the driver neither compiles nor invokes that callback
// (RX is drained by ethif_poll_thread -> Eth_Receive), so a no-op satisfies the
// linker without regenerating the config. It is never called: no RX MSI-X is
// routed to the R52 GIC.
void Eth_43_NETC_RxIrqCallback(const uint8 CtrlIdx, const uint8 DMAChannel) {
    (void)CtrlIdx;
    (void)DMAChannel;
}
