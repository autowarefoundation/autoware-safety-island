// test_rpmsg_netif_core.c
#include <assert.h>
#include <string.h>
#include "rpmsg_netif_core.h"

static unsigned char last_tx[600]; static unsigned last_tx_len; static int tx_rc;
static int mock_tx(void *ctx, const void *f, unsigned l) { (void)ctx; memcpy(last_tx, f, l); last_tx_len = l; return tx_rc; }
static unsigned char last_rx[600]; static unsigned last_rx_len;
static void mock_rx(void *ctx, const void *f, unsigned l) { (void)ctx; memcpy(last_rx, f, l); last_rx_len = l; }

int main(void) {
    rpmsg_netif_ops ops = { mock_tx, mock_rx, 0 };
    rpmsg_netif_stats st = {0};
    unsigned char frame[RPMSG_ETH_MAX_FRAME + 1] = {0xAA};

    tx_rc = 0;
    assert(rpmsg_netif_core_tx(&ops, &st, frame, 60) == 0 && last_tx_len == 60 && st.tx_ok == 1);
    assert(rpmsg_netif_core_tx(&ops, &st, frame, RPMSG_ETH_MAX_FRAME) == 0 && st.tx_ok == 2);
    assert(rpmsg_netif_core_tx(&ops, &st, frame, RPMSG_ETH_MAX_FRAME + 1) == -1 && st.tx_drop_oversize == 1);
    tx_rc = -5;
    assert(rpmsg_netif_core_tx(&ops, &st, frame, 60) == -2 && st.tx_err == 1);

    assert(rpmsg_netif_core_rx(&ops, &st, frame, 60) == 0 && last_rx_len == 60 && st.rx_ok == 1);
    assert(rpmsg_netif_core_rx(&ops, &st, frame, RPMSG_ETH_MAX_FRAME) == 0 && last_rx_len == RPMSG_ETH_MAX_FRAME && st.rx_ok == 2);
    assert(rpmsg_netif_core_rx(&ops, &st, frame, RPMSG_ETH_MAX_FRAME + 1) == -1 && st.rx_drop_oversize == 1);
    return 0;
}
