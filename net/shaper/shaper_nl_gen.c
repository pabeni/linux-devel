// SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-3-Clause)
/* Do not edit directly, auto-generated from: */
/*	Documentation/netlink/specs/shaper.yaml */
/* YNL-GEN kernel source */

#include <net/netlink.h>
#include <net/genetlink.h>

#include "shaper_nl_gen.h"

#include <uapi/linux/net_shaper.h>

/* Common nested types */
const struct nla_policy net_shaper_handle_nl_policy[NET_SHAPER_A_ID + 1] = {
	[NET_SHAPER_A_SCOPE] = NLA_POLICY_MAX(NLA_U32, 4),
	[NET_SHAPER_A_ID] = { .type = NLA_U32, },
};

const struct nla_policy net_shaper_ns_info_nl_policy[NET_SHAPER_A_WEIGHT + 1] = {
	[NET_SHAPER_A_HANDLE] = NLA_POLICY_NESTED(net_shaper_handle_nl_policy),
	[NET_SHAPER_A_METRIC] = NLA_POLICY_MAX(NLA_U32, 1),
	[NET_SHAPER_A_BW_MIN] = { .type = NLA_UINT, },
	[NET_SHAPER_A_BW_MAX] = { .type = NLA_UINT, },
	[NET_SHAPER_A_BURST] = { .type = NLA_UINT, },
	[NET_SHAPER_A_PRIORITY] = { .type = NLA_U32, },
	[NET_SHAPER_A_WEIGHT] = { .type = NLA_U32, },
};

const struct nla_policy net_shaper_ns_output_info_nl_policy[NET_SHAPER_A_PARENT + 1] = {
	[NET_SHAPER_A_PARENT] = NLA_POLICY_NESTED(net_shaper_handle_nl_policy),
	[NET_SHAPER_A_HANDLE] = NLA_POLICY_NESTED(net_shaper_handle_nl_policy),
	[NET_SHAPER_A_METRIC] = NLA_POLICY_MAX(NLA_U32, 1),
	[NET_SHAPER_A_BW_MIN] = { .type = NLA_UINT, },
	[NET_SHAPER_A_BW_MAX] = { .type = NLA_UINT, },
	[NET_SHAPER_A_BURST] = { .type = NLA_UINT, },
	[NET_SHAPER_A_PRIORITY] = { .type = NLA_U32, },
	[NET_SHAPER_A_WEIGHT] = { .type = NLA_U32, },
};

/* NET_SHAPER_CMD_GET - do */
static const struct nla_policy net_shaper_get_do_nl_policy[NET_SHAPER_A_HANDLE + 1] = {
	[NET_SHAPER_A_IFINDEX] = { .type = NLA_U32, },
	[NET_SHAPER_A_HANDLE] = NLA_POLICY_NESTED(net_shaper_handle_nl_policy),
};

/* NET_SHAPER_CMD_GET - dump */
static const struct nla_policy net_shaper_get_dump_nl_policy[NET_SHAPER_A_IFINDEX + 1] = {
	[NET_SHAPER_A_IFINDEX] = { .type = NLA_U32, },
};

/* NET_SHAPER_CMD_SET - do */
static const struct nla_policy net_shaper_set_nl_policy[NET_SHAPER_A_SHAPER + 1] = {
	[NET_SHAPER_A_IFINDEX] = { .type = NLA_U32, },
	[NET_SHAPER_A_SHAPER] = NLA_POLICY_NESTED(net_shaper_ns_info_nl_policy),
};

/* NET_SHAPER_CMD_DELETE - do */
static const struct nla_policy net_shaper_delete_nl_policy[NET_SHAPER_A_HANDLE + 1] = {
	[NET_SHAPER_A_IFINDEX] = { .type = NLA_U32, },
	[NET_SHAPER_A_HANDLE] = NLA_POLICY_NESTED(net_shaper_handle_nl_policy),
};

/* NET_SHAPER_CMD_GROUP - do */
static const struct nla_policy net_shaper_group_nl_policy[NET_SHAPER_A_OUTPUT + 1] = {
	[NET_SHAPER_A_IFINDEX] = { .type = NLA_U32, },
	[NET_SHAPER_A_INPUTS] = NLA_POLICY_NESTED(net_shaper_ns_info_nl_policy),
	[NET_SHAPER_A_OUTPUT] = NLA_POLICY_NESTED(net_shaper_ns_output_info_nl_policy),
};

/* NET_SHAPER_CMD_CAP_GET - do */
static const struct nla_policy net_shaper_cap_get_do_nl_policy[NET_SHAPER_A_CAPABILITIES_SCOPE + 1] = {
	[NET_SHAPER_A_CAPABILITIES_IFINDEX] = { .type = NLA_U32, },
	[NET_SHAPER_A_CAPABILITIES_SCOPE] = NLA_POLICY_MAX(NLA_U32, 4),
};

/* NET_SHAPER_CMD_CAP_GET - dump */
static const struct nla_policy net_shaper_cap_get_dump_nl_policy[NET_SHAPER_A_CAPABILITIES_IFINDEX + 1] = {
	[NET_SHAPER_A_CAPABILITIES_IFINDEX] = { .type = NLA_U32, },
};

/* Ops table for net_shaper */
static const struct genl_split_ops net_shaper_nl_ops[] = {
	{
		.cmd		= NET_SHAPER_CMD_GET,
		.doit		= net_shaper_nl_get_doit,
		.policy		= net_shaper_get_do_nl_policy,
		.maxattr	= NET_SHAPER_A_HANDLE,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= NET_SHAPER_CMD_GET,
		.dumpit		= net_shaper_nl_get_dumpit,
		.policy		= net_shaper_get_dump_nl_policy,
		.maxattr	= NET_SHAPER_A_IFINDEX,
		.flags		= GENL_CMD_CAP_DUMP,
	},
	{
		.cmd		= NET_SHAPER_CMD_SET,
		.doit		= net_shaper_nl_set_doit,
		.policy		= net_shaper_set_nl_policy,
		.maxattr	= NET_SHAPER_A_SHAPER,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= NET_SHAPER_CMD_DELETE,
		.doit		= net_shaper_nl_delete_doit,
		.policy		= net_shaper_delete_nl_policy,
		.maxattr	= NET_SHAPER_A_HANDLE,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= NET_SHAPER_CMD_GROUP,
		.doit		= net_shaper_nl_group_doit,
		.policy		= net_shaper_group_nl_policy,
		.maxattr	= NET_SHAPER_A_OUTPUT,
		.flags		= GENL_ADMIN_PERM | GENL_CMD_CAP_DO,
	},
	{
		.cmd		= NET_SHAPER_CMD_CAP_GET,
		.doit		= net_shaper_nl_cap_get_doit,
		.policy		= net_shaper_cap_get_do_nl_policy,
		.maxattr	= NET_SHAPER_A_CAPABILITIES_SCOPE,
		.flags		= GENL_CMD_CAP_DO,
	},
	{
		.cmd		= NET_SHAPER_CMD_CAP_GET,
		.dumpit		= net_shaper_nl_cap_get_dumpit,
		.policy		= net_shaper_cap_get_dump_nl_policy,
		.maxattr	= NET_SHAPER_A_CAPABILITIES_IFINDEX,
		.flags		= GENL_CMD_CAP_DUMP,
	},
};

struct genl_family net_shaper_nl_family __ro_after_init = {
	.name		= NET_SHAPER_FAMILY_NAME,
	.version	= NET_SHAPER_FAMILY_VERSION,
	.netnsok	= true,
	.parallel_ops	= true,
	.module		= THIS_MODULE,
	.split_ops	= net_shaper_nl_ops,
	.n_split_ops	= ARRAY_SIZE(net_shaper_nl_ops),
};
