/*****************************************************************************
 * Flexnet ARCnet simulation
 *****************************************************************************/

#ifndef __FX_ARCNET_H__
#define __FX_ARCNET_H__

#include <stdint.h>
#include <stdbool.h>

#define ARCNET_BCAST_ADDR 0

#define ARCNET_SC_BACNET    205 /* IP BACNet */

/* RFC 1051 */
#define	ARCNET_SC_IP_OLD    240 /* IP protocol */
#define	ARCNET_SC_ARP_OLD   241 /* address resolution protocol */

/* RFC 1201 */
#define	ARCNET_SC_IP        212 /* IP protocol */
#define	ARCNET_SC_ARP       213 /* address resolution protocol */
#define	ARCNET_SC_REVARP    214 /* reverse addr resolution protocol */

#define	ARCNET_SC_ATALK     221 /* Appletalk */
#define	ARCNET_SC_BANIAN    247 /* Banyan Vines */
#define	ARCNET_SC_IPX       250 /* Novell IPX */

#define ARCNET_SC_INET6     0xc4 /* IPng */
#define ARCNET_SC_DIAGNOSE  0x80 /* as per ANSI/ATA 878.1 */

#define	ARCNET_TX_OK 0
#define	ARCNET_TX_FAIL 1

struct arcnet_hdr {
	uint8_t src;
	uint8_t dst;
	uint8_t typ;
	uint8_t len;
};

struct arcnet_pkt {
	struct arcnet_hdr hdr;
	uint8_t data[252];
};

#ifdef __cplusplus
extern "C" {
#endif

int fx_arcnet_sim_init(void);

void arcnet_mac_open(int addr, void (* rcv_isr)(void), 
					 void (* eot_isr)(void));

uint8_t arcnet_mac_addr(void);

uint8_t arcnet_tx_stat(void);

void arcnet_pkt_write(const struct arcnet_pkt * pkt);

void arcnet_pkt_read(struct arcnet_pkt * pkt);

#ifdef __cplusplus
}
#endif	

#endif /* __FX_ARCNET_H__ */

