/*
   fx-arcnet.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/param.h>
#include <stdbool.h>
#include <assert.h>

#include "chime.h"
#include "debug.h"
#include "fx-arcnet.h"

#define ARCNET_SPEED_BPS (2500000 / 8)
#define ARCNET_NODES_MAX 8

struct arcnet_recon_burst {
	uint8_t recon;
	uint8_t burst[3];
} __attribute__((packed));

/*************************************************************
  ArcNET PAC frame

  Information Symbol Unit (ISU)

  1 1 0 i0 i1 i2 i3 i4 i5 i6 i7

  ***********************************************************/

struct arcnet_pac_frm {
	uint8_t pac; /* Pac Frame Identifier (1 ISU, value = 0x01) */
	uint8_t sid;
	uint8_t did[2];
	uint8_t il;
	uint8_t sc; /* System Code (1 ISU) */
	uint8_t info_fsc[252 + 2];
} __attribute__((packed));

/* 3.1.5.1 Invitation To Transmit (ITT)
	[ SD | ITT | DID ]
	SD = Starting Delimiter (6 basic symbols)
	ITT = ITT Frame (1 ISU, value = 0x04)
	DID = Destination Identifier (2 ISUs)

	The ITT frame is the means by which the right to transmit (token)
	is passed from one station to another. */

struct arcnet_itt_frm {
	uint8_t itt; /* ITT Frame (1 ISU, value = 0x04) */
	uint8_t did[2]; /* Destination Identifier (2 ISUs)  */
} __attribute__((packed));

/* 3.1.5.2 Free Buffer Enquiry (FBE)
	[ SD | FBE | DID ]
	SD = Starting Delimiter (6 basic symbols)
	FBE = FBE Frame (1 ISU, value = 0x85)
	DID = Destination Identifier (2 ISUs)
	The FBE frame is used to determine if the receiver at the destination
	station is currently able to accept a data packet. */

struct arcnet_fbe_frm {
	uint8_t fbe; /* ITT Frame (1 ISU, value = 0x04) */
	uint8_t did[2]; /* Destination Identifier (2 ISUs)  */
} __attribute__((packed));

/* 3.1.5.4 Positive Acknowledgment (ACK)
	[ SD | ACK ]
	SD = Starting Delimiter (6 basic symbols)
	ACK = ACK Frame (1 ISU, value = 0x86)
	ACK frames are used to acknowledge successful receipt of PAC frames
	and as affirmative responses to FBE frames. */

/* 3.1.5.5 Negative Acknowledgment (NAK)
	[ SD | NAK ]
	SD = Starting Delimiter (6 basic symbols)
	NAK = NAK Frame (1 ISU, value = 0x15)
	NAK frames are used as negative responses to FBE frames. */

/************************************************************
  Arcnet Generic Frame
 ************************************************************/

enum {
	ARCNET_FRM_PAC = 0x01,
	ARCNET_FRM_ITT = 0x04,
	ARCNET_FRM_FBE = 0x85,
	ARCNET_FRM_ACK = 0x86,
	ARCNET_FRM_NAK = 0x15,
	ARCNET_RECON_BURST = 0xff
};

struct arcnet_frm {
	union {
		struct {
			uint8_t fid; /* Frame Identifier (1 ISU) */
			uint8_t fis[515]; /* Frame Information Sequence (0-515 ISUs) */
		};
		struct arcnet_pac_frm pac;
		struct arcnet_fbe_frm itt;
		struct arcnet_fbe_frm fbe;
	};
} __attribute__((packed));

/****************************************************************************
 * ARCnet simulation
 ****************************************************************************/

#define ARCNET_COMM 0

enum {
	ARCNET_RESET      =  0, /* Reset */
	ARCNET_WT_IDLE    =  1, /* Wait for Quiescent Medium */
	ARCNET_WT_ACT     =  2, /* Wait for Activity on Medium */
	ARCNET_DCD_TYPE   =  3, /* Decode Frame Type */
	ARCNET_DCD_DID    =  4, /* Decode Destination Identifier */
	ARCNET_RECON      =  5, /* Network Reconfiguration */
	ARCNET_RX_FBE     =  6, /* Respond to FBE */
	ARCNET_PASS_TOKEN =  7, /* The token is passed to NID */
	ARCNET_WT_TOKEN   =  8, /* Wait for Activity After Passing Token*/
	ARCNET_TX_FBE     =  9, /* Transmit FBE */
	ARCNET_WT_FBE     = 10, /* Wait for Reply to FBE */
	ARCNET_TX_PAC     = 11, /* Transmit PAC */
	ARCNET_WT_PAC     = 12, /* Wait for Reply to PAC */
	ARCNET_RX_PAC     = 13, /* Complete Reception of PAC */
	ARCNET_PAC_ACK    = 14  /* Send Reply to PAC */
};

/*
3.3 Timers
   The timers defined below are used at each station to control various
   operational characteristics of the network. Several of these timer values
   are fixed, while several others are variable, and must be set to equal
   values at all stations on the network. The variable timer values are
   referred to in terms of “extended timeouts”. Support for extended
   timeouts is optional, but if supported all extended time-out values
   must be selectable. Timer values listed assume a data rate of 2.5Mbps.
   The term reset when applied to timers, is to be understood to mean that
   the timer is reset to its initial value and (re)started. When a timer's
   interval expires it is said to have "timed out", which asserts a time-out
   condition which remains true until the timer is reset. A timer may be
   stopped prior to time-out in order to prevent its time-out
   condition from occurring.

3.3.1 Timer, Lost Token (TLT)
	Each station has a timer TLT to recover from error conditions related
	to non-receipt of the token. Timer TLT is reset each time the station
	receives the token, and is used to initiate network reconfiguration
	in cases where a time-out occurs before the next token reception. The
	operation of TLT is detailed in the operational finite- state machine.
	The default value of TLT is 840ms; the alternate value, for all levels
	of extended time-out, is 1680ms. The tolerance on both of these
	timer values is ±10ms.

3.3.2 Timer, Identifier Precedence (TIP)
	Each station has a timer TIP to provide time separation for initiation
	of network reconfiguration activity based on the station address.
	Operation of TIP is detailed in the operational finite-state machine.
	The value of TIP is determined (for default timeouts) by the station
	address according to the following equation:
	TIP = K * ( 255 − ID ) + 3 . 0 us

3.3.3 Timer, Activity Timeout (TAC)
	Each station has a timer TAC which is used to control the minimum period
	of time which the station will wait for media activity before assuming
	that such activity will not occur and commencing with network
	reconfiguration activity. Operation of the TAC is detailed in the
	operational finite-state machine.

3.3.4 Timer, Response Timeout (TRP)
	Each station has a timer TRP which is used to control the minimum
	period of time which the station will wait for a response to a
	transmitted ITT, FBE, or PAC frame before assuming that such response
	will not occur.
	Operation of the TRP is detailed in the operational finite-state machine.
 */

#define ARCNET_TLT 4 /* 3.3.1 Timer, Lost Token (TLT) */
#define ARCNET_TIP 5 /* 3.3.2 Timer, Identifier Precedence (TIP) */
#define ARCNET_TAC 6 /* .3.3 Timer, Activity Timeout (TAC) */
#define ARCNET_TRP 7 /* 3.3.4 Timer, Response Timeout (TRP) */
#define ARCNET_TRC -1 /* 3.3.5 Timer, Recovery Time (TRC) */
#define ARCNET_TTA -1 /* 3.3.6 Timer, Line Turnaround (TTA) */
#define ARCNET_TMQ -1 /* 3.3.7 Timer, Medium Quiescent (TMQ) */
#define ARCNET_TRB -1 /* 3.3.8 Timer, Receiver Blanking (TRB) */
#define ARCNET_TBR -1 /* 3.3.9 Timer, Broadcast Delay (TBR) */


/*
3.4 Flags
   Flags are used to remember the occurrence of a particular event. They are
   set when the event occurs and are cleared as specified in the finite
   state machine definitions.

3.4.1 Receiver Inhibited (RI)
	This flag is set upon successful receipt of a data packet addressed to
	this station, or a broadcast data packet, for which the reception and
	transfer to the station data buffer has been successfully completed and
	the frame check sequence verified. This flag is cleared by LLC functions
	when an empty buffer is available within the station data buffer and
	reception of data packets is again allowed. While this flag is set,
	no data packets are accepted by the station; however, invitation to
	transmit frames and free buffer enquiry frames, are accepted by
	the station at all times.
	If an 878.1 implementation permits LLC functions to set this flag,
	logic must be included to ensure that it is not possible to change
	the state of RI during the reception of a data packet.

3.4.2 Transmitter Available (TA)
	This flag is set upon completion of a data packet transmission attempt
	except in cases where a negative acknowledgment is received in response
	to a free buffer enquiry. This flag is cleared by LLC functions when
	a new data packet is available for transmission in the station's data
	buffer and packet transmission is to be performed on the next token
	reception. When this flag is set, this station passes the token
	immediately upon receipt, without attempting a transmission.
	If an 878.1 implementation permits LLC functions to set this flag,
	logic must be included to ensure that it is not possible to change the
	state of TA during the transmission of a data packet.

3.4.3 Transmitter Message Acknowledged (TMA)
	This flag is set coincident with TA in cases where the destination
	station has provided a positive acknowledgment to successful receipt of
	a data packet. TMA serves as indication that the data packet has been
	successfully copied into the receive buffer at the destination
	station. This flag may be cleared by LLC functions and is automatically
	cleared when TA is cleared.
	The case where both TA and TMA are set is indication of guaranteed
	delivery into a receiver buffer at the destination. The setting of TA
	while TMA remains clear is an indication of probable failure of the
	data packet delivery. However, the destination station may have
	successfully received the data packet even when TA is set while TMA
	remains clear at the source station (for example, in cases where the
	ACK frame is destroyed by a RECON Burst.).

3.4.4 Reconfiguration (RECON)
	This flag is set when the activity timer (TAC) expires, indicating that
	a network reconfiguration needs to occur. This flag is cleared by LLC
	or NMT functionality as appropriate.

3.4.5 Broadcast Enabled (BE)
	This flag is set and cleared as desired by LLC or NMT functionality. When
	this flag is set, reception of broadcast data packets by this station
	is enabled. When this flag is cleared, broadcast data packets are ignored
	by this station.

3.4.6 PAC Detected (PF)
	This is an internal flag which is set when the FID of an incoming frame
	is decoded as being a PAC. This flag is set and cleared by the
	operational finite-state machine.

3.4.7 ITT Detected (IF)
	This is an internal flag which is set when the FID of an incoming frame
	is decoded as being an ITT. This flag is set and cleared by the
	operational finite-state machine.

3.4.8 FBE Detected (FF)
	This is an internal flag which is set when the FID of an incoming frame
	is decoded as being an FBE. This flag is set and cleared by the
	operational finite-state machine.
*/

/*
3.5 Registers
	Registers are used to remember a particular value. They are loaded as
	specified in the finite state machine definitions.

3.5.1 My Identifier (MYID)
	This is an 8-bit register which contains the specified address of this
	station and is used to insert source identification field contents
	in outgoing data packets and as a reference for incoming address
	recognition.

3.5.2 Next Identifier (NID)
	This is an 8-bit register which holds the station address of the
	successor station on the logical ring and is used to address the token
	on token passes from this station.

3.5.3 Transmit Destination (TXD)
	This is an 8-bit register which holds the destination station address
	of the current outgoing data packet (if any).
	This is used to designate the intended recipient(s) of the outgoing packet.

3.5.4 Received Destination (RXD)
	This is an 8-bit register which holds the destination station address
	received in the current incoming data packet (if any). This is used to
	distinguish received broadcast packets from explicitly-addressed packets.
*/

__thread struct {
	struct {
		uint8_t myid;
		uint8_t nid;
		uint8_t txd;
		uint8_t rxd;
	} reg;
	struct {
		uint8_t ri:1;    /* 3.4.1 Receiver Inhibited (RI) */
		uint8_t ta:1;    /* 3.4.2 Transmitter Available (TA) */
		uint8_t tma:1;   /* 3.4.3 Transmitter Message Acknowledged (TMA) */
		uint8_t recon:1; /* 3.4.4 Reconfiguration (RECON) */
		uint8_t be:1;    /* 3.4.5 Broadcast Enabled (BE) */
		uint8_t _pf:1;   /* 3.4.6 PAC Detected (PF) */
		uint8_t _if:1;   /* 3.4.7 ITT Detected (IF) */
		uint8_t _ff:1;   /* 3.4.8 FBE Detected (FF) */
	} flag;
	uint8_t fsm_state;
	uint32_t speed_bps;
	struct {
		uint32_t tlt;
		uint32_t tip;
		uint32_t tac;
		uint32_t trp;
	} delay;
	void (* rcv_isr)(void);
	void (* eot_isr)(void);
	struct arcnet_frm rx_frm;
	struct arcnet_pac_frm tx_frm;
} arcnet_mac;

uint8_t arcnet_mac_addr(void)
{
	return arcnet_mac.reg.myid;
}

uint8_t arcnet_tx_stat(void)
{
	return (arcnet_mac.flag.tma) ? ARCNET_TX_OK : ARCNET_TX_FAIL;
}

/****************************************************************************
 * Timers
 ****************************************************************************/

static __thread uint32_t tlt_clk;
static __thread uint32_t tip_clk;
static __thread uint32_t tac_clk;
static __thread uint32_t trp_clk;
static __thread uint32_t xmt_clk;

static void __tmr_reset(int tmr_id)
{
	uint32_t delay;

	switch (tmr_id) {
	case ARCNET_TLT:
		delay = arcnet_mac.delay.tlt;
		tlt_clk = chime_cpu_cycles();
		break;
	case ARCNET_TIP:
		DBG2("<%d> reset TIP=%d (us)", chime_cpu_id(), arcnet_mac.delay.tip);
		delay = arcnet_mac.delay.tip;
		tip_clk = chime_cpu_cycles();
		break;
	case ARCNET_TAC:
		DBG2("<%d> reset TAC=%d (us)", chime_cpu_id(), arcnet_mac.delay.tac);
		tac_clk = chime_cpu_cycles();
		delay = arcnet_mac.delay.tac;
		break;
	case ARCNET_TRP:
		DBG2("<%d> reset TRP=%d (us)", chime_cpu_id(), arcnet_mac.delay.trp);
		trp_clk = chime_cpu_cycles();
		delay = arcnet_mac.delay.trp;

		break;
	default:
		return;
	}

	chime_tmr_reset(tmr_id, delay, 0);
}

static void __tmr_stop(int tmr_id)
{
	switch (tmr_id) {
	case ARCNET_TLT:
		DBG2("<%d> stop TLT!", chime_cpu_id());
		break;
	case ARCNET_TIP:
		DBG2("<%d> stop TIP!", chime_cpu_id());
		break;
	case ARCNET_TAC:
		DBG2("<%d> stop TAC!", chime_cpu_id());
	case ARCNET_TRP:
		DBG2("<%d> stop TRP!", chime_cpu_id());
		break;
	default:
		return;
	}

	chime_tmr_stop(tmr_id);
}

/****************************************************************************
 * CRC-16
 ****************************************************************************/

static const uint16_t crc16lut[256] = {
    0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
    0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
    0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
    0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
    0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
    0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
    0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
    0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
    0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
    0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
    0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
    0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
    0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
    0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
    0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
    0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
    0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
    0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
    0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
    0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
    0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
    0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
    0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
    0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
    0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
    0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
    0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
    0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
    0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
    0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
    0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
    0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};

static unsigned int __crc16(const uint8_t * data, size_t len)
{
	unsigned int crc = 0xffff;
	unsigned int idx;

    while (len--) {
        idx = (crc ^ *data) & 0xff;
        crc = crc16lut[idx] ^ (crc >> 8);
		data++;
    }
    return crc ^ 0xffff;
}

/****************************************************************************
 * ARCnet FSM
 ****************************************************************************/

void arcnet_fsm_wt_act(int event)
{
	DBG2("<%d> %d --> [WT_ACT]", chime_cpu_id(), event);
	arcnet_mac.fsm_state = ARCNET_WT_ACT;
	/* Wait for TAC timer timeout or line activity */
}

void arcnet_fsm_dcd_type(int event)
{
	DBG2("<%d> %d --> [DCD_TYPE]", chime_cpu_id(), event);
	arcnet_mac.fsm_state = ARCNET_DCD_TYPE;
}

void arcnet_fsm_dcd_did(int event)
{
	DBG2("<%d> %d --> [DCD_DID]", chime_cpu_id(), event);
	arcnet_mac.fsm_state = ARCNET_DCD_DID;
}

void arcnet_fsm_wt_idle(int event)
{
	DBG2("<%d> %d --> [WT_IDLE]", chime_cpu_id(), event);
	arcnet_mac.fsm_state = ARCNET_WT_IDLE;
	/* we don't implemented the TMQ timer,
	   so from WT_IDLE we go right to WT_ACT */
	/* 12 ---- T0(TMQ) ----> WT_ACT */
	__tmr_reset(ARCNET_TAC);
	arcnet_fsm_wt_act(12);
}

void arcnet_fsm_recon(int event)
{
	DBG1("<%d> %d --> [RECON]", chime_cpu_id(), event);
	arcnet_mac.fsm_state = ARCNET_RECON;
	/* Wait for TIP timer timeout or line activity */
}

void arcnet_fsm_wt_token(int event)
{
	DBG2("<%d> %d --> [WT_TOKEN]", chime_cpu_id(), event);
	arcnet_mac.fsm_state = ARCNET_WT_TOKEN;
	/* Wait for TRP timer timeout or line activity */
}

void arcnet_fsm_wt_pac(int event)
{
	DBG1("<%d> %d --> [WT_PAC]", chime_cpu_id(), event);
	arcnet_mac.fsm_state = ARCNET_WT_PAC;
}

void arcnet_fsm_wt_fbe(int event)
{
	DBG1("<%d> %d --> [WT_FBE]", chime_cpu_id(), event);
	arcnet_mac.fsm_state = ARCNET_WT_FBE;
	/* Wait for TRP timer timeout or line activity */
}

void arcnet_fsm_pass_token(int event)
{
	struct arcnet_itt_frm frm;

	DBG1("<%d> %d --> [PASS_TOKEN] -> <%d>",
		chime_cpu_id(), event, arcnet_mac.reg.nid);
	arcnet_mac.fsm_state = ARCNET_PASS_TOKEN;

	frm.itt = ARCNET_FRM_ITT;
	frm.did[0] = arcnet_mac.reg.nid;
	frm.did[1] = arcnet_mac.reg.nid;

	chime_comm_write(ARCNET_COMM, &frm, sizeof(frm));
	xmt_clk = chime_cpu_cycles();
	/* Wait for EOT isr */
}

void arcnet_fsm_tx_pac(int event)
{
	struct arcnet_pac_frm * pac = &arcnet_mac.tx_frm;

	DBG1("<%d> %d --> [TX_PAC]", chime_cpu_id(), event);
	arcnet_mac.fsm_state = ARCNET_TX_PAC;

	chime_comm_write(ARCNET_COMM, pac, pac->il + 8);
	xmt_clk = chime_cpu_cycles();
	/* Wait for EOT isr */
}

void arcnet_fsm_tx_fbe(int event)
{

	DBG1("<%d> %d --> [TX_FBE]", chime_cpu_id(), event);
	arcnet_mac.fsm_state = ARCNET_TX_FBE;

	if (arcnet_mac.reg.txd == ARCNET_BCAST_ADDR) {
		/* 92 ---- TXD=0 ----> TX_PAC */
		arcnet_fsm_tx_pac(92);
	} else {
		struct arcnet_fbe_frm frm;

		frm.fbe = ARCNET_FRM_FBE;
		frm.did[0] = arcnet_mac.reg.txd;
		frm.did[1] = arcnet_mac.reg.txd;
		chime_comm_write(ARCNET_COMM, &frm, sizeof(frm));
		xmt_clk = chime_cpu_cycles();
		/* Wait for EOT isr */
	}
}

void arcnet_fsm_rx_fbe(int event)
{
	uint8_t frm[1];

	DBG1("<%d> %d --> [RX_FBE]", chime_cpu_id(), event);
	arcnet_mac.fsm_state = ARCNET_RX_FBE;

	if (arcnet_mac.flag.ri) {
		frm[0] = ARCNET_FRM_NAK;
	} else {
		frm[0] = ARCNET_FRM_ACK;
	}

	chime_comm_write(ARCNET_COMM, &frm, sizeof(frm));
	xmt_clk = chime_cpu_cycles();
	/* Wait for EOT isr */
}

void arcnet_fsm_pac_ack(int event)
{
	uint8_t frm[1];

	DBG1("<%d> %d --> [PAC_ACK]", chime_cpu_id(), event);
	arcnet_mac.fsm_state = ARCNET_PAC_ACK;

	frm[0] = ARCNET_FRM_ACK;
	chime_comm_write(ARCNET_COMM, &frm, sizeof(frm));
	xmt_clk = chime_cpu_cycles();
	/* Wait for EOT isr */
}

void arcnet_fsm_rx_pac(int event)
{
	struct arcnet_pac_frm * frm = &arcnet_mac.rx_frm.pac;
	uint16_t fsc;
	int len;

	DBG1("<%d> %d --> [RX_PAC]", chime_cpu_id(), event);
	arcnet_mac.fsm_state = ARCNET_RX_PAC;

	/* Check CRC ... */
	len = frm->il;
	fsc = frm->info_fsc[len] | (frm->info_fsc[len + 1] << 8);

	if (fsc != __crc16(frm->info_fsc, len)) {
		WARN("<%d> FSC error", chime_cpu_id());
		/* 131 ---- RX(FR_BAD) ----> WT_IDLE */
		__tmr_reset(ARCNET_TMQ);
		arcnet_fsm_wt_idle(131);
	} else {
		if (arcnet_mac.reg.rxd == ARCNET_BCAST_ADDR) {
			/* 132 ---- RXD=0 ----> WT_IDLE */
			arcnet_mac.flag.ri = 1;
			__tmr_reset(ARCNET_TMQ);
			arcnet_fsm_wt_idle(132);
			arcnet_mac.rcv_isr();
		} else if (arcnet_mac.reg.rxd == arcnet_mac.reg.myid) {
			/* 133 ---- RXD=MID ----> PAC_ACK */
			__tmr_reset(ARCNET_TTA);
			arcnet_fsm_pac_ack(133);
		}
	}
}


void arcnet_fsm_reset(void)
{
	struct arcnet_recon_burst burst;

	DBG1("<%d> [RESET]", chime_cpu_id());
	arcnet_mac.fsm_state = ARCNET_RESET;

	burst.recon = ARCNET_RECON_BURST;
	burst.burst[0] = 0xff;
	burst.burst[1] = 0xff;
	burst.burst[2] = 0xff;

	chime_comm_write(ARCNET_COMM, &burst, 4);
	xmt_clk = chime_cpu_cycles();
	/* Wait for EOT isr */
}

void arcnet_mac_dcd_isr(void)
{
	DBG2("<%d> DCD", chime_cpu_id());

	switch (arcnet_mac.fsm_state) {
	case ARCNET_WT_TOKEN:
		/* 82 ---- T0(TRB) & RX("1") ----> DCD_TYPE */
		/* Reset(PF,IF,FF) */
		arcnet_mac.flag._pf = 0;
		arcnet_mac.flag._if = 0;
		arcnet_mac.flag._ff = 0;
		__tmr_stop(ARCNET_TRP);
		arcnet_fsm_dcd_type(82);
		break;

	case ARCNET_RECON:
		/* 51 ---- RX("1") ----> WT_IDLE */
		__tmr_reset(ARCNET_TMQ);
		__tmr_stop(ARCNET_TIP);
		arcnet_fsm_wt_idle(51);
		/* we don't implemented the TMQ timer,
		   so from WT_IDLE we go right to WT_ACT */
		/* fall through */

	case ARCNET_WT_ACT:
		/* 21 ---- RX("1") ----> WT_DCD_TYPE */
		/* Reset(PF,IF,FF) */
		arcnet_mac.flag._pf = 0;
		arcnet_mac.flag._if = 0;
		arcnet_mac.flag._ff = 0;
		arcnet_fsm_dcd_type(21);
		break;
	};
}

void arcnet_mac_rcv_isr(void)
{
	struct arcnet_frm * frm = &arcnet_mac.rx_frm;

	DBG2("<%d> RCV", chime_cpu_id());

	/* receive the frame */
	chime_comm_read(ARCNET_COMM, frm, sizeof(struct arcnet_frm));

	if (frm->fid == ARCNET_RECON_BURST) {
		DBG1("<%d> Recon Burst ##  ##  ##  ##  ##  ##  ##", chime_cpu_id());
		__tmr_stop(ARCNET_TTA);
		__tmr_stop(ARCNET_TRP);
		__tmr_stop(ARCNET_TRB);
		arcnet_mac.reg.nid = arcnet_mac.reg.myid;
		__tmr_reset(ARCNET_TLT);
		__tmr_reset(ARCNET_TMQ);
		arcnet_fsm_wt_idle(1);
		return;
	}

	switch (arcnet_mac.fsm_state) {

	case ARCNET_DCD_TYPE:
		switch (frm->fid) {
		case ARCNET_FRM_FBE:
			arcnet_mac.reg.rxd = frm->fbe.did[0];
			/* 32 ---- FID=FBE ----> WT_DCD_ID */
			/* Set(FF) */
			arcnet_mac.flag._ff = 1;
			arcnet_fsm_dcd_did(32);
			break;
		case ARCNET_FRM_ITT:
			arcnet_mac.reg.rxd = frm->itt.did[0];
			/* 33 ---- FID=ITT ----> WT_DCD_ID */
			/* Set(IF) */
			arcnet_mac.flag._if = 1;
			arcnet_fsm_dcd_did(33);
			break;
		case ARCNET_FRM_PAC:
			if (!arcnet_mac.flag.ri) {
				arcnet_mac.reg.rxd = frm->pac.did[0];
				/* 34 ---- FID=PAC & ~RI ----> WT_DCD_ID */
				/* Set(PF), Store(SID) */
				arcnet_mac.flag._pf = 1;
				arcnet_fsm_dcd_did(34);
				break;
			} /* fall through */
		default:
			/* 31 ---- RX(FR_BAD) | (FID ...) | (FID=PAC & RI) ----> WT_IDLE */
			__tmr_reset(ARCNET_TMQ);
			arcnet_fsm_wt_idle(31);
			return;
		} /* fall through */

	case ARCNET_DCD_DID:
		if (((arcnet_mac.reg.rxd != ARCNET_BCAST_ADDR) &&
			 (arcnet_mac.reg.rxd != arcnet_mac.reg.myid)) ||
			((arcnet_mac.reg.rxd == ARCNET_BCAST_ADDR) &&
			 !arcnet_mac.flag._pf) ||
			((arcnet_mac.reg.rxd == ARCNET_BCAST_ADDR) &&
			 !arcnet_mac.flag.be)) {
			/* 41 ---- (DID!=0 & DID!=MYID) | ... ---> WT_IDLE */
			__tmr_reset(ARCNET_TMQ);
			arcnet_fsm_wt_idle(41);
			break;
		}

		switch (frm->fid) {

		case ARCNET_FRM_PAC:
			if ((arcnet_mac.reg.rxd == arcnet_mac.reg.myid) ||
				(arcnet_mac.reg.rxd == ARCNET_BCAST_ADDR)) {
				/* 45 ---- (DID=MYID & PF) | (DID=0 & PF & BE)---> RX_PAC */
				/* Store(DID, IL, Sc, INFO) */
				arcnet_fsm_rx_pac(45);
			}
			break;

		case ARCNET_FRM_ITT:
			if (arcnet_mac.reg.rxd == arcnet_mac.reg.myid) {
				if (arcnet_mac.flag.ta) {
					DBG2("<%d> ITT: {TOKEN} 43 --> PASS_TOKEN", chime_cpu_id());
					/* 43 ---- DID=MYID & IF & TA ---> PASS_TOKEN */
					__tmr_reset(ARCNET_TTA);
					__tmr_reset(ARCNET_TLT);
					arcnet_fsm_pass_token(43);
				} else {
					DBG2("<%d> ITT: {TOKEN} 44 --> TX_FBE", chime_cpu_id());
					/* 44 ---- DID=MYID & IF & ~TA ---> TX_FBE */
					__tmr_reset(ARCNET_TTA);
					__tmr_reset(ARCNET_TLT);
					/* TXD=Load(DID) */
					arcnet_mac.reg.txd = arcnet_mac.tx_frm.did[0];
					arcnet_fsm_tx_fbe(44);
				}
			}
			break;

		case ARCNET_FRM_FBE:
			if (arcnet_mac.reg.rxd == arcnet_mac.reg.myid) {
				/* 42 ---- DID=MYID & FF ---> RX_FBE */
				__tmr_reset(ARCNET_TTA);
				arcnet_fsm_rx_fbe(42);
			}
			break;

		case ARCNET_FRM_ACK:
		case ARCNET_FRM_NAK:
		default:
			assert("Invalid frame" == NULL);
			/* FIXME: ... */
		}
		break;

	case ARCNET_WT_FBE:
		switch (frm->fid) {
		case ARCNET_FRM_NAK:
			/* 102 ---- T0(TRB) & RX(NAK) ----> PASS_TOKEN */
			__tmr_reset(ARCNET_TTA);
			__tmr_stop(ARCNET_TRP);
			arcnet_fsm_pass_token(102);
			break;
		case ARCNET_FRM_ACK:
			/* 103 ---- T0(TRB) & RX(ACK) ----> TX_PAC */
			__tmr_reset(ARCNET_TTA);
			__tmr_stop(ARCNET_TRP);
			arcnet_fsm_tx_pac(103);
			break;
		default:
			/* 104 ---- RX(FR_BAD) | (FID!=ACK & FID!= NAK) ----> WT_IDLE */
			__tmr_reset(ARCNET_TMQ);
			__tmr_stop(ARCNET_TRB);
			arcnet_fsm_wt_idle(104);
		}
		break;

	case ARCNET_WT_PAC:
		switch (frm->fid) {
		case ARCNET_FRM_ACK:
			/* 122 ---- RX(ACK) & ~RX(FR_BAD) ----> PASS_TOKEN */
			__tmr_reset(ARCNET_TTA);
			__tmr_stop(ARCNET_TRP);
			arcnet_mac.flag.ta = 1;
			arcnet_mac.flag.tma = 1;
			arcnet_fsm_pass_token(122);
			arcnet_mac.eot_isr();
			break;
		default:
			/* 121 ---- RX(FID!=ACK) | T0(TRP) | RX(FR_BAD) ----> PASS_TOKEN */
			arcnet_mac.flag.ta = 1;
			__tmr_reset(ARCNET_TRC);
			arcnet_fsm_pass_token(121);
			arcnet_mac.eot_isr();
			break;
		}
		break;

	default:
		/* FIXME: ... */
//		arcnet_mac.eot_isr();
		DBG1("invalid state!");
	}
}

void arcnet_mac_eot_isr(void)
{
	DBG2("<%d> EOT: %d us", chime_cpu_id(), chime_cpu_cycles() - xmt_clk);

	switch (arcnet_mac.fsm_state) {

	case ARCNET_RESET:
		/* 01 -----------> WR_IDLE */
		arcnet_mac.reg.nid = arcnet_mac.reg.myid;
		__tmr_reset(ARCNET_TLT);
		__tmr_reset(ARCNET_TMQ);
		arcnet_fsm_wt_idle(1);
		break;

	case ARCNET_TX_FBE:
		/* 91 ---- T0(TTA) & TXD!=0 ----> WT_FBE */
		__tmr_reset(ARCNET_TRP);
		__tmr_reset(ARCNET_TRB);
		arcnet_fsm_wt_fbe(91);
		break;

	case ARCNET_TX_PAC:
		if (arcnet_mac.reg.txd == ARCNET_BCAST_ADDR) {
			/* 112 ---- T0(TTA) & TXD=0 ----> PASS_TOKEN */
			__tmr_reset(ARCNET_TBR);
			arcnet_mac.flag.ta = 1;
			arcnet_fsm_pass_token(112);
			arcnet_mac.eot_isr();
		} else {
			/* 111 ---- T0(TTA) & TXD!=0 ----> WT_PAC */
			__tmr_reset(ARCNET_TRP);
			arcnet_fsm_wt_pac(111);
		}
		break;

	case ARCNET_PASS_TOKEN:
		/* 71 ---- T0(TTA) | T0(TRC) | T0(TBR) ----> WT_TOKEN */
		__tmr_reset(ARCNET_TRP);
		__tmr_reset(ARCNET_TRB);
		__tmr_stop(ARCNET_TTA);
		__tmr_stop(ARCNET_TRC);
		__tmr_stop(ARCNET_TBR);
		arcnet_fsm_wt_token(71);
		break;

	case ARCNET_RX_FBE:
		if (arcnet_mac.flag.ri) {
			/* 62 ---- T0(TTA) & RI ----> WT_IDLE */
			__tmr_reset(ARCNET_TMQ);
			arcnet_fsm_wt_idle(62);
		} else {
			/* 61 ---- T0(TTA) & ~RI ----> WT_IDLE */
			arcnet_mac.flag.tma = 0;
			arcnet_fsm_wt_idle(61);
		}
		break;

	case ARCNET_PAC_ACK:
		/* 141 ---- T0(TTA) ----> WT_IDLE */
		__tmr_reset(ARCNET_TMQ);
		arcnet_mac.flag.ri = 1;
		arcnet_fsm_wt_idle(141);
		arcnet_mac.rcv_isr();
		break;
	}
}

void arcnet_mac_tlt_isr(void)
{
	DBG2("<%d> TLT: %d us", chime_cpu_id(), chime_cpu_cycles() - tlt_clk);
	/* ---- T0(TLT) ----> Reset */
	arcnet_fsm_reset();
}

void arcnet_mac_tip_isr(void)
{
	DBG1("<%d> TIP: %d us >>>> >>>> >>>> >>>> >>>> >>>>",
		chime_cpu_id(), chime_cpu_cycles() - tip_clk);

	switch (arcnet_mac.fsm_state) {
	case ARCNET_RECON:
		/* 52 ---- T0(TIP) ----> PASS_TOKEN */
		__tmr_reset(ARCNET_TLT);
		__tmr_reset(ARCNET_TRC);
		arcnet_fsm_pass_token(52);
		break;
	}
}

void arcnet_mac_tac_isr(void)
{
	uint32_t cpu_clk = chime_cpu_cycles();
	int32_t ticks;

	ticks = cpu_clk - tac_clk;
	DBG2("<%d> TAC: %d us", chime_cpu_id(), ticks);
	if (ticks != arcnet_mac.delay.tac) {
		DBG1("cpu_clk=%d tac_clk=%d", cpu_clk, tac_clk);
		DBG1("cpu_cycles=%d clk=%d", chime_cpu_cycles(), tac_clk);
		chime_cpu_self_destroy();
	}

	switch (arcnet_mac.fsm_state) {
	case ARCNET_WT_ACT:
		/* 22 ---- T0(TAC) ----> Recon */
		__tmr_reset(ARCNET_TIP);
		arcnet_mac.reg.nid = arcnet_mac.reg.myid;
		arcnet_mac.flag.recon = 1;
		arcnet_fsm_recon(22);
		break;
	}
}

void arcnet_mac_trp_isr(void)
{
	int32_t ticks;

	ticks = chime_cpu_cycles() - trp_clk;
	DBG2("<%d> TRP: %d us", chime_cpu_id(), ticks);
	if (ticks != arcnet_mac.delay.trp) {
		chime_cpu_self_destroy();
	}

	switch (arcnet_mac.fsm_state) {

	case ARCNET_WT_TOKEN:
		/* 81 ---- T0(TRP) ----> PASS_TOKEN */
		arcnet_mac.reg.nid = (arcnet_mac.reg.nid + 1) % ARCNET_NODES_MAX;
		__tmr_reset(ARCNET_TRC);
		arcnet_fsm_pass_token(81);
		break;

	case ARCNET_WT_FBE:
		/* 101 ---- T0(TRP) ----> PASS_TOKEN */
		arcnet_mac.flag.ta = 1;
		arcnet_fsm_pass_token(101);
		arcnet_mac.eot_isr();
		break;

	case ARCNET_WT_PAC:
		/* 121 ---- RX(FID!=ACK) | T0(TRP) | RX(FR_BAD) ----> PASS_TOKEN */
		arcnet_mac.flag.ta = 1;
		__tmr_reset(ARCNET_TRC);
		arcnet_fsm_pass_token(121);
		arcnet_mac.eot_isr();
		break;

	}
}

/****************************************************************************
 * ARCnet API
 ****************************************************************************/

void arcnet_pkt_read(struct arcnet_pkt * pkt)
{
	struct arcnet_pac_frm * frm = &arcnet_mac.rx_frm.pac;

	pkt->hdr.src = frm->sid;
	pkt->hdr.dst = frm->did[0];
	pkt->hdr.typ = frm->sc;
	pkt->hdr.len = frm->il;
	memcpy(pkt->data, frm->info_fsc, frm->il);

	INF("<%d> src=%d dst=%d.", chime_cpu_id(), pkt->hdr.src, pkt->hdr.dst);

	arcnet_mac.flag.ri = 0;
}

void arcnet_pkt_write(const struct arcnet_pkt * pkt)
{
	struct arcnet_pac_frm * frm = &arcnet_mac.tx_frm;
	uint16_t fsc;
	int len;

	INF("<%d> src=%d dst=%d.", chime_cpu_id(), pkt->hdr.src, pkt->hdr.dst);

	len = pkt->hdr.len;
	frm->pac = 0x01;
	frm->sid = pkt->hdr.src;
	frm->did[0] = pkt->hdr.dst;
	frm->did[1] = pkt->hdr.dst;
	frm->il = len;
	frm->sc = pkt->hdr.typ;
	memcpy(frm->info_fsc, pkt->data, len);
	/* Calculate CRC 16 ... */
	fsc = __crc16(frm->info_fsc, len);
	frm->info_fsc[len] = fsc;
	frm->info_fsc[len + 1] = fsc >> 8;

	arcnet_mac.flag.ta = 0;
	arcnet_mac.flag.tma = 0;
}

void arcnet_mac_open(int addr, void (* rcv_isr)(void), void (* eot_isr)(void))
{
	if (addr == 0)
		addr = chime_cpu_id();

	/* Store local node address */
	arcnet_mac.reg.myid = addr;
	arcnet_mac.rcv_isr = rcv_isr;
	arcnet_mac.eot_isr = eot_isr;
	arcnet_mac.speed_bps = ARCNET_SPEED_BPS;

	arcnet_mac.flag.ta = 1;
	arcnet_mac.flag.tma = 0;
	arcnet_mac.flag.ri = 0;

	arcnet_mac.flag.recon = 0;

	arcnet_mac.flag._pf = 0;
	arcnet_mac.flag._if = 0;
	arcnet_mac.flag._ff = 0;

	arcnet_mac.flag.be = 1;

	/* TLT = 820 ms */
	arcnet_mac.delay.tlt = (2100000LL * 1000000LL) / arcnet_mac.speed_bps;
	chime_tmr_init(ARCNET_TLT, arcnet_mac_tlt_isr, 0, 0);

	/* TIP = K * ( 255 − ID ) + 3.0 us (K = 146) */
	/* TIP = 365 * (255 - ID) + 7.5 bits */
	arcnet_mac.delay.tip = (365000000LL * (255 - arcnet_mac.reg.myid) +
							7500000) / arcnet_mac.speed_bps;
	chime_tmr_init(ARCNET_TIP, arcnet_mac_tip_isr, 0, 0);

	/* TAC = 82.2 us (205.5 bits) */
	arcnet_mac.delay.tac = (205500000 / arcnet_mac.speed_bps);
	chime_tmr_init(ARCNET_TAC, arcnet_mac_tac_isr, 0, 0);

	/* TRP = 74.6 us (186.5 bits) */
	arcnet_mac.delay.trp = (186500000 / arcnet_mac.speed_bps);
	chime_tmr_init(ARCNET_TRP, arcnet_mac_trp_isr, 0, 0);

	/* Open ARCnet network Comm point */
	chime_comm_attach(ARCNET_COMM, "ARCnet",
					  arcnet_mac_rcv_isr, arcnet_mac_eot_isr,
					  arcnet_mac_dcd_isr);


	arcnet_fsm_reset();
}

int fx_arcnet_sim_init(void)
{
	struct comm_attr attr = {
		.wr_cyc_per_byte = 0,
		.wr_cyc_overhead = 0,
		.rd_cyc_per_byte = 0,
		.rd_cyc_overhead = 0,
		.bits_overhead = 6,
		.bits_per_byte = 11,
		.bytes_max = 256,
		.speed_bps = ARCNET_SPEED_BPS, /* bits per second */
		.max_jitter = 0.0, /* seconds */
		.min_delay = 0.0,  /* minimum delay in seconds */
		.nod_delay = 0.0,  /* per node delay in seconds */
		.hist_en = false,
		.txbuf_en = false,
		.dcd_en = true, /* enable data carrier detect */
		.exp_en = false /* enable exponential distribution */
	};


	/* create an arcnet communication simulation */
	if (chime_comm_create("ARCnet", &attr) < 0) {
		fprintf(stderr, "chime_comm_create() failed!\n");
		fflush(stderr);
		return -1;
	}

	DBG1("done.");

	return 0;
}


