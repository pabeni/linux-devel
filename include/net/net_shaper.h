/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _NET_SHAPER_H_
#define _NET_SHAPER_H_

#include <linux/types.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>

#include <uapi/linux/net_shaper.h>

/**
 * struct net_shaper_info - represents a shaping node on the NIC H/W
 * zeroed field are considered not set.
 * @handle: Unique identifier for the shaper, see @net_shaper_make_handle
 * @parent: Unique identifier for the shaper parent, usually implied. Only
 *   NET_SHAPER_SCOPE_QUEUE, NET_SHAPER_SCOPE_NETDEV and NET_SHAPER_SCOPE_DETACHED
 *   can have the parent handle explicitly set, placing such shaper under
 *   the specified parent.
 * @metric: Specify if the bw limits refers to PPS or BPS
 * @bw_min: Minimum guaranteed rate for this shaper
 * @bw_max: Maximum peak bw allowed for this shaper
 * @burst: Maximum burst for the peek rate of this shaper
 * @priority: Scheduling priority for this shaper
 * @weight: Scheduling weight for this shaper
 * @children: Number of nested shapers, accounted only for DETACHED scope
 */
struct net_shaper_info {
	u32 handle;
	u32 parent;
	enum net_shaper_metric metric;
	u64 bw_min;	/* minimum guaranteed bandwidth, according to metric */
	u64 bw_max;	/* maximum allowed bandwidth */
	u64 burst;	/* maximum burst in bytes for bw_max */
	u32 priority;	/* scheduling strict priority */
	u32 weight;	/* scheduling WRR weight*/
	u32 children;
};

/**
 * define NET_SHAPER_SCOPE_VF - Shaper scope
 *
 * This shaper scope is not exposed to user-space; the shaper is attached to
 * the given virtual function.
 */
#define NET_SHAPER_SCOPE_VF __NET_SHAPER_SCOPE_MAX

/**
 * struct net_shaper_ops - Operations on device H/W shapers
 * @set: Modify the existing shaper.
 * @delete: Delete the specified shaper.
 * @group: Group the specified input shapers under the specific output
 *
 * The initial shaping configuration ad device initialization is empty/
 * a no-op/does not constraint the b/w in any way.
 * The network core keeps track of the applied user-configuration in
 * per device storage.
 *
 * Each shaper is uniquely identified within the device with an 'handle',
 * dependent on the shaper scope and other data, see @shaper_make_handle()
 */
struct net_shaper_ops {
	/** group - create the specified shapers group
	 * @dev: Netdevice to operate on
	 * @nr_input: The number of items in the @inputs array
	 * @input: Configuration of input shapers
	 * @output: Configuration of output shaper
	 * @extack: Netlink extended ACK for reporting errors.
	 *
	 * Nest the specified @inputs shapers under the given @output shaper.
	 * Create either the @inputs and the @output shaper as needed,
	 * otherwise move them as needed.
	 *
	 * Return:
	 * * 0 - Group successfully created
	 * * %-EOPNOTSUPP - Operation is not supported by hardware, driver,
	 *                  or core for any reason. @extack should be set to
	 *                  text describing the reason.
	 * * Other negative error values on failure.
	 */
	int (*group)(struct net_device *dev, int nr_input,
		     const struct net_shaper_info *inputs,
		     const struct net_shaper_info *output,
		     struct netlink_ext_ack *extack);

	/** set - Update the specified shaper
	 * @dev: Netdevice to operate on
	 * @shaper: Configuration of shaper
	 * @extack: Netlink extended ACK for reporting errors.
	 *
	 * Return:
	 * * 0 - shaper successfully updated
	 * * %-EOPNOTSUPP - Operation is not supported by hardware, driver,
	 *                  or core for any reason. @extack should be set to
	 *                  text describing the reason.
	 * * Other negative error values on failure.
	 */
	int (*set)(struct net_device *dev,
		   const struct net_shaper_info *shaper,
		   struct netlink_ext_ack *extack);

	/** delete - Removes the specified shapers from the NIC
	 * @dev: netdevice to operate on
	 * @handle: The shapers identifier
	 * @extack: Netlink extended ACK for reporting errors.
	 *
	 * Removes the shapers configuration, restoring the default behavior
	 *
	 * Return:
	 * * 0 - shaper successfully deleted
	 * * %-EOPNOTSUPP - Operation is not supported by hardware, driver,
	 *                  or core for any reason. @extack should be set to
	 *                  text describing the reason.
	 * * Other negative error value on failure.
	 */
	int (*delete)(struct net_device *dev, u32 handle,
		      struct netlink_ext_ack *extack);
};

#define NET_SHAPER_SCOPE_SHIFT	26
#define NET_SHAPER_ID_MASK	GENMASK(NET_SHAPER_SCOPE_SHIFT - 1, 0)
#define NET_SHAPER_SCOPE_MASK	GENMASK(31, NET_SHAPER_SCOPE_SHIFT)

#define NET_SHAPER_ID_UNSPEC NET_SHAPER_ID_MASK

/**
 * net_shaper_make_handle - creates an unique shaper identifier
 * @scope: the shaper scope
 * @id: the shaper id number
 *
 * Return: an unique identifier for the shaper
 *
 * Combines the specified arguments to create an unique identifier for
 * the shaper. The @id argument semantic depends on the
 * specified scope.
 * For @NET_SHAPER_SCOPE_QUEUE_GROUP, @id is the queue group id
 * For @NET_SHAPER_SCOPE_QUEUE, @id is the queue number.
 * For @NET_SHAPER_SCOPE_VF, @id is virtual function number.
 */
static inline u32 net_shaper_make_handle(enum net_shaper_scope scope,
					 int id)
{
	return FIELD_PREP(NET_SHAPER_SCOPE_MASK, scope) |
		FIELD_PREP(NET_SHAPER_ID_MASK, id);
}

/**
 * net_shaper_handle_scope - extract the scope from the given handle
 * @handle: the shaper handle
 *
 * Return: the corresponding scope
 */
static inline enum net_shaper_scope net_shaper_handle_scope(u32 handle)
{
	return FIELD_GET(NET_SHAPER_SCOPE_MASK, handle);
}

/**
 * net_shaper_handle_id - extract the id number from the given handle
 * @handle: the shaper handle
 *
 * Return: the corresponding id number
 */
static inline int net_shaper_handle_id(u32 handle)
{
	return FIELD_GET(NET_SHAPER_ID_MASK, handle);
}

#endif

