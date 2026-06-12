/* SPDX-License-Identifier: GPL-2.0 */
/*
 * TCP out options definition
 */
#ifndef __TCP_OUT_OPTIONS_H
#define __TCP_OUT_OPTIONS_H 1

#include <linux/bits.h>
#include <linux/stddef.h>
#include <net/mptcp.h>

#define OPTION_SACK_ADVERTISE	BIT(0)
#define OPTION_TS		BIT(1)
#define OPTION_MD5		BIT(2)
#define OPTION_WSCALE		BIT(3)
#define OPTION_FAST_OPEN_COOKIE	BIT(8)
#define OPTION_SMC		BIT(9)
#define OPTION_MPTCP		BIT(10)
#define OPTION_AO		BIT(11)
#define OPTION_ACCECN		BIT(12)

struct tcp_out_options {
	/* Following group is cleared in __tcp_transmit_skb() */
	struct_group(cleared,
		u16 mss;		/* 0 to disable */
		u8 bpf_opt_len;		/* length of BPF hdr option */
		u8 num_sack_blocks;	/* number of SACK blocks to include */
	);

	/* Caution: following fields are not cleared in __tcp_transmit_skb() */
	u16 options;		/* bit field of OPTION_* */
	u8 ws;			/* window scale, 0 to disable */
	u8 num_accecn_fields:7,	/* number of AccECN fields needed */
	   use_synack_ecn_bytes:1; /* Use synack_ecn_bytes or not */
	__u8 *hash_location;	/* temporary pointer, overloaded */
	__u32 tsval, tsecr;	/* need to include OPTION_TS */
	struct tcp_fastopen_cookie *fastopen_cookie;	/* Fast open cookie */
	struct mptcp_out_options mptcp;
};

#endif
