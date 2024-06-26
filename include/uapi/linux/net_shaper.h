/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause) */
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/shaper.yaml */
/* YNL-GEN uapi header */

#ifndef _UAPI_LINUX_NET_SHAPER_H
#define _UAPI_LINUX_NET_SHAPER_H

#define NET_SHAPER_FAMILY_NAME		"net-shaper"
#define NET_SHAPER_FAMILY_VERSION	1

/**
 * enum net_shaper_scope - the different scopes where a shaper can be attached
 * @NET_SHAPER_SCOPE_UNSPEC: The scope is not specified
 * @NET_SHAPER_SCOPE_PORT: The root shaper for the whole H/W.
 * @NET_SHAPER_SCOPE_NETDEV: The main shaper for the given network device.
 * @NET_SHAPER_SCOPE_QUEUE: The shaper is attached to the given device queue.
 * @NET_SHAPER_SCOPE_DETACHED: The shaper can be attached to port, netdev or
 *   other detached shapers, allowing nesting and grouping of netdev or queues.
 */
enum net_shaper_scope {
	NET_SHAPER_SCOPE_UNSPEC,
	NET_SHAPER_SCOPE_PORT,
	NET_SHAPER_SCOPE_NETDEV,
	NET_SHAPER_SCOPE_QUEUE,
	NET_SHAPER_SCOPE_DETACHED,

	/* private: */
	__NET_SHAPER_SCOPE_MAX,
	NET_SHAPER_SCOPE_MAX = (__NET_SHAPER_SCOPE_MAX - 1)
};

/**
 * enum net_shaper_metric - different metric each shaper can support
 * @NET_SHAPER_METRIC_BPS: Shaper operates on a bits per second basis
 * @NET_SHAPER_METRIC_PPS: Shaper operates on a packets per second basis
 */
enum net_shaper_metric {
	NET_SHAPER_METRIC_BPS,
	NET_SHAPER_METRIC_PPS,
};

enum {
	NET_SHAPER_A_IFINDEX = 1,
	NET_SHAPER_A_HANDLE,
	NET_SHAPER_A_METRIC,
	NET_SHAPER_A_BW_MIN,
	NET_SHAPER_A_BW_MAX,
	NET_SHAPER_A_BURST,
	NET_SHAPER_A_PRIORITY,
	NET_SHAPER_A_WEIGHT,
	NET_SHAPER_A_SCOPE,
	NET_SHAPER_A_ID,
	NET_SHAPER_A_PARENT,
	NET_SHAPER_A_INPUTS,
	NET_SHAPER_A_OUTPUT,
	NET_SHAPER_A_SHAPER,

	__NET_SHAPER_A_MAX,
	NET_SHAPER_A_MAX = (__NET_SHAPER_A_MAX - 1)
};

enum {
	NET_SHAPER_CMD_GET = 1,
	NET_SHAPER_CMD_SET,
	NET_SHAPER_CMD_DELETE,
	NET_SHAPER_CMD_GROUP,

	__NET_SHAPER_CMD_MAX,
	NET_SHAPER_CMD_MAX = (__NET_SHAPER_CMD_MAX - 1)
};

#endif /* _UAPI_LINUX_NET_SHAPER_H */
