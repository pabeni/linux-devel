// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/kernel.h>
#include <linux/idr.h>
#include <linux/skbuff.h>
#include <linux/xarray.h>
#include <net/net_shaper.h>

#include "shaper_nl_gen.h"

#include "../core/dev.h"

struct net_shaper_data {
	struct xarray shapers;
	struct idr detached_ids;
};

struct net_shaper_nl_ctx {
	u32 start_handle;
};

static u32 default_parent(u32 handle)
{
	enum net_shaper_scope parent, scope = net_shaper_handle_scope(handle);

	switch (scope) {
	case NET_SHAPER_SCOPE_PORT:
	case NET_SHAPER_SCOPE_UNSPEC:
		parent = NET_SHAPER_SCOPE_UNSPEC;
		break;

	case NET_SHAPER_SCOPE_QUEUE:
	case NET_SHAPER_SCOPE_DETACHED:
		parent = NET_SHAPER_SCOPE_NETDEV;
		break;

	case NET_SHAPER_SCOPE_NETDEV:
	case NET_SHAPER_SCOPE_VF:
		parent = NET_SHAPER_SCOPE_PORT;
		break;
	}

	return net_shaper_make_handle(parent, 0);
}

static bool is_detached(u32 handle)
{
	return net_shaper_handle_scope(handle) == NET_SHAPER_SCOPE_DETACHED;
}

static int fill_handle(struct sk_buff *msg, u32 handle, u32 type,
		       const struct genl_info *info)
{
	struct nlattr *handle_attr;

	if (!handle)
		return 0;

	handle_attr = nla_nest_start_noflag(msg, type);
	if (!handle_attr)
		return -EMSGSIZE;

	if (nla_put_u32(msg, NET_SHAPER_A_SCOPE,
			net_shaper_handle_scope(handle)) ||
	    nla_put_u32(msg, NET_SHAPER_A_ID,
			net_shaper_handle_id(handle)))
		goto handle_nest_cancel;

	nla_nest_end(msg, handle_attr);
	return 0;

handle_nest_cancel:
	nla_nest_cancel(msg, handle_attr);
	return -EMSGSIZE;
}

static int
net_shaper_fill_one(struct sk_buff *msg, struct net_shaper_info *shaper,
		    const struct genl_info *info)
{
	void *hdr;

	hdr = genlmsg_iput(msg, info);
	if (!hdr)
		return -EMSGSIZE;

	if (fill_handle(msg, shaper->parent, NET_SHAPER_A_PARENT, info) ||
	    fill_handle(msg, shaper->handle, NET_SHAPER_A_HANDLE, info) ||
	    nla_put_u32(msg, NET_SHAPER_A_METRIC, shaper->metric) ||
	    nla_put_uint(msg, NET_SHAPER_A_BW_MIN, shaper->bw_min) ||
	    nla_put_uint(msg, NET_SHAPER_A_BW_MAX, shaper->bw_max) ||
	    nla_put_uint(msg, NET_SHAPER_A_BURST, shaper->burst) ||
	    nla_put_u32(msg, NET_SHAPER_A_PRIORITY, shaper->priority) ||
	    nla_put_u32(msg, NET_SHAPER_A_WEIGHT, shaper->weight))
		goto nla_put_failure;

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

/* On success sets pdev to the relevant device and acquires a reference
 * to it
 */
static int fetch_dev(const struct genl_info *info, int type,
		     struct net_device **pdev)
{
	struct net *ns = genl_info_net(info);
	struct net_device *dev;
	int ifindex;

	if (GENL_REQ_ATTR_CHECK(info, type))
		return -EINVAL;

	ifindex = nla_get_u32(info->attrs[type]);
	dev = dev_get_by_index(ns, ifindex);
	if (!dev) {
		GENL_SET_ERR_MSG_FMT(info, "device %d not found", ifindex);
		return -EINVAL;
	}

	if (!dev->netdev_ops->net_shaper_ops) {
		GENL_SET_ERR_MSG_FMT(info, "device %s does not support H/W shaper",
				     dev->name);
		dev_put(dev);
		return -EOPNOTSUPP;
	}

	*pdev = dev;
	return 0;
}

static int parse_handle(const struct nlattr *attr, const struct genl_info *info,
			u32 *handle)
{
	struct nlattr *tb[NET_SHAPER_A_ID + 1];
	struct nlattr *scope_attr, *id_attr;
	enum net_shaper_scope scope;
	u32 id = 0;
	int ret;

	ret = nla_parse_nested(tb, NET_SHAPER_A_ID, attr,
			       net_shaper_handle_nl_policy, info->extack);
	if (ret < 0)
		return ret;

	scope_attr = tb[NET_SHAPER_A_SCOPE];
	if (!scope_attr) {
		GENL_SET_ERR_MSG(info, "Missing 'scope' attribute for handle");
		return -EINVAL;
	}

	scope = nla_get_u32(scope_attr);

	/* the default id for detached scope shapers is an invalid one
	 * to help the 'group' operation discriminate request for new
	 * detached shaper creation and re-use of existing shaper
	 */
	id_attr = tb[NET_SHAPER_A_ID];
	if (id_attr)
		id =  nla_get_u32(id_attr);
	else if (scope == NET_SHAPER_SCOPE_DETACHED)
		id = NET_SHAPER_ID_UNSPEC;

	*handle = net_shaper_make_handle(scope, id);
	return 0;
}

int net_shaper_nl_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct net_shaper_info *shaper;
	struct net_device *dev;
	struct sk_buff *msg;
	u32 handle;
	int ret;

	ret = fetch_dev(info, NET_SHAPER_A_IFINDEX, &dev);
	if (ret)
		return ret;

	if (GENL_REQ_ATTR_CHECK(info, NET_SHAPER_A_HANDLE))
		goto put;

	ret = parse_handle(info->attrs[NET_SHAPER_A_HANDLE], info, &handle);
	if (ret < 0)
		goto put;

	if (!dev->net_shaper_data) {
		GENL_SET_ERR_MSG_FMT(info, "no shaper is initialized on device %s",
				     dev->name);
		ret = -EINVAL;
		goto put;
	}

	shaper = xa_load(&dev->net_shaper_data->shapers, handle);
	if (!shaper) {
		GENL_SET_ERR_MSG_FMT(info, "Can't find shaper for handle %x", handle);
		ret = -EINVAL;
		goto put;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto put;
	}

	ret = net_shaper_fill_one(msg, shaper, info);
	if (ret)
		goto free_msg;

	ret =  genlmsg_reply(msg, info);
	if (ret)
		goto free_msg;

put:
	dev_put(dev);
	return ret;

free_msg:
	nlmsg_free(msg);
	goto put;
}

int net_shaper_nl_get_dumpit(struct sk_buff *skb,
			     struct netlink_callback *cb)
{
	struct net_shaper_nl_ctx *ctx = (struct net_shaper_nl_ctx *)cb->ctx;
	const struct genl_info *info = genl_info_dump(cb);
	struct net_shaper_info *shaper;
	struct net_device *dev;
	unsigned long handle;
	int ret;

	ret = fetch_dev(info, NET_SHAPER_A_IFINDEX, &dev);
	if (ret)
		return ret;

	BUILD_BUG_ON(sizeof(struct net_shaper_nl_ctx) > sizeof(cb->ctx));

	if (!dev->net_shaper_data) {
		ret = 0;
		goto put;
	}

	xa_for_each_range(&dev->net_shaper_data->shapers, handle, shaper,
			  ctx->start_handle, U32_MAX) {
		ret = net_shaper_fill_one(skb, shaper, info);
		if (ret)
			goto put;

		ctx->start_handle = handle;
	}

put:
	dev_put(dev);
	return ret;
}

/* count the number of [multi] attributes of the given type */
static int attr_list_len(struct genl_info *info, int type)
{
	struct nlattr *attr;
	int rem, cnt = 0;

	nla_for_each_attr_type(attr, type, genlmsg_data(info->genlhdr),
			       genlmsg_len(info->genlhdr), rem)
		cnt++;
	return cnt;
}

static struct xarray *__sc_container(struct net_device *dev)
{
	return dev->net_shaper_data ? &dev->net_shaper_data->shapers : NULL;
}

/* lookup the given shaper inside the cache */
static struct net_shaper_info *sc_lookup(struct net_device *dev, u32 handle)
{
	struct xarray *xa = __sc_container(dev);

	return xa ? xa_load(xa, handle): NULL;
}

/* allocate on demand the per device shaper's cache */
static struct xarray *__sc_init(struct net_device *dev,
				struct netlink_ext_ack *extack)
{
	if (!dev->net_shaper_data) {
		dev->net_shaper_data = kmalloc(sizeof(*dev->net_shaper_data),
					       GFP_KERNEL);
		if (!dev->net_shaper_data) {
			NL_SET_ERR_MSG(extack, "Can't allocate memory for shaper data");
			return NULL;
		}

		xa_init(&dev->net_shaper_data->shapers);
		idr_init(&dev->net_shaper_data->detached_ids);
	}
	return &dev->net_shaper_data->shapers;
}

/* prepare the cache to actually insert the given shaper, doing
 * in advance the needed allocations
 */
static int sc_prepare_insert(struct net_device *dev, u32 *handle,
			     struct netlink_ext_ack *extack)
{
	enum net_shaper_scope scope = net_shaper_handle_scope(*handle);
	struct xarray *xa = __sc_init(dev, extack);
	struct net_shaper_info *prev, *cur;
	bool id_allocated = false;
	int ret, id;

	if (!xa)
		return -ENOMEM;

	cur = xa_load(xa, *handle);
	if (cur)
		return 0;

	/* allocated a new id, if needed */
	if (scope == NET_SHAPER_SCOPE_DETACHED &&
	    net_shaper_handle_id(*handle) == NET_SHAPER_ID_UNSPEC) {
		xa_lock(xa);
		id = idr_alloc(&dev->net_shaper_data->detached_ids, NULL,
			       0, NET_SHAPER_ID_UNSPEC, GFP_ATOMIC);
		xa_unlock(xa);

		if (id < 0) {
			NL_SET_ERR_MSG(extack, "Can't allocate new id for detached shaper");
			return id;
		}

		*handle = net_shaper_make_handle(scope, id);
		id_allocated = true;
	}

	cur = kmalloc(sizeof(*cur), GFP_KERNEL | __GFP_ZERO);
	if (!cur) {
		NL_SET_ERR_MSG(extack, "Can't allocate memory for cached shaper");
		ret = -ENOMEM;
		goto free_id;
	}

	/* mark 'tentative' shaper inside the cache */
	xa_lock(xa);
	prev = __xa_store(xa, *handle, cur, GFP_KERNEL);
	__xa_set_mark(xa, *handle, XA_MARK_0);
	xa_unlock(xa);
	if (xa_err(prev)) {
		NL_SET_ERR_MSG(extack, "Can't insert shaper into cache");
		kfree(cur);
		ret = xa_err(prev);
		goto free_id;
	}
	return 0;

free_id:
	if (id_allocated) {
		xa_lock(xa);
		idr_remove(&dev->net_shaper_data->detached_ids,
			   net_shaper_handle_id(*handle));
		xa_unlock(xa);
	}
	return ret;
}

/* rollback all the tentative inserts from the shaper cache */
static void sc_rollback(struct net_device *dev)
{
	struct xarray *xa = __sc_container(dev);
	struct net_shaper_info *cur;
	unsigned long index;

	if (!xa)
		return;

	xa_lock(xa);
	xa_for_each_marked(xa, index, cur, XA_MARK_0) {
		if (is_detached(index))
			idr_remove(&dev->net_shaper_data->detached_ids,
				   net_shaper_handle_id(index));
		__xa_erase(xa, index);
		kfree(cur);
	}
	xa_unlock(xa);
}

/* commit the tentative insert with the actual values
 * parent handle, if still unspecified.
 * Must be called only after a successful sc_prepare_insert()
 */
static void sc_commit(struct net_device *dev, int nr_shapers,
		      const struct net_shaper_info *shapers)
{
	struct xarray *xa = __sc_container(dev);
	struct net_shaper_info *cur;
	int i;

	xa_lock(xa);
	for (i = 0; i < nr_shapers; ++i) {
		cur = xa_load(xa, shapers[i].handle);
		if (WARN_ON_ONCE(!cur))
			continue;

		/* successful update: drop the tentative mark
		 * and update the cache
		 */
		__xa_clear_mark(xa, shapers[i].handle, XA_MARK_0);
		*cur = shapers[i];
	}
	xa_unlock(xa);
}

static int __parse_shaper(struct net_device *dev, struct nlattr **tb,
		        const struct genl_info *info,
		        struct net_shaper_info *shaper)
{
	struct net_shaper_info *old;
	int ret;

	/* the shaper handle is the only mandatory attribute */
	if (NL_REQ_ATTR_CHECK(info->extack, NULL, tb, NET_SHAPER_A_HANDLE))
		return -EINVAL;

	ret = parse_handle(tb[NET_SHAPER_A_HANDLE], info, &shaper->handle);
	if (ret)
		return ret;

	/* fetch existing data, if any, so that user provide info will
	 * incrementally update the existing shaper configuration
	 */
	old = sc_lookup(dev, shaper->handle);
	if (old)
		*shaper = *old;
	else
		shaper->parent = default_parent(shaper->handle);

	if (tb[NET_SHAPER_A_METRIC])
		shaper->metric = nla_get_u32(tb[NET_SHAPER_A_METRIC]);

	if (tb[NET_SHAPER_A_BW_MIN])
		shaper->bw_min = nla_get_uint(tb[NET_SHAPER_A_BW_MIN]);

	if (tb[NET_SHAPER_A_BW_MAX])
		shaper->bw_max = nla_get_uint(tb[NET_SHAPER_A_BW_MAX]);

	if (tb[NET_SHAPER_A_BURST])
		shaper->burst = nla_get_uint(tb[NET_SHAPER_A_BURST]);

	if (tb[NET_SHAPER_A_PRIORITY])
		shaper->priority = nla_get_u32(tb[NET_SHAPER_A_PRIORITY]);

	if (tb[NET_SHAPER_A_WEIGHT])
		shaper->weight = nla_get_u32(tb[NET_SHAPER_A_WEIGHT]);
	return 0;
}

/* fetch the cached shaper info and update them with the user-provided
 * attributes
 */
static int parse_shaper(struct net_device *dev, const struct nlattr *attr,
		        const struct genl_info *info,
		        struct net_shaper_info *shaper)
{
	struct nlattr *tb[NET_SHAPER_A_WEIGHT + 1];
	int ret;

	ret = nla_parse_nested(tb, NET_SHAPER_A_WEIGHT, attr,
			       net_shaper_ns_info_nl_policy, info->extack);
	if (ret < 0)
		return ret;

	return __parse_shaper(dev, tb, info, shaper);
}

/* alikie parse_shaper(), but additionally allow the user specifying
 * the shaper's parent handle
 */
static int parse_output_shaper(struct net_device *dev,
			       const struct nlattr *attr,
			       const struct genl_info *info,
			       struct net_shaper_info *shaper)
{
	struct nlattr *tb[NET_SHAPER_A_PARENT + 1];
	int ret;

	ret = nla_parse_nested(tb, NET_SHAPER_A_PARENT, attr,
			       net_shaper_ns_output_info_nl_policy,
			       info->extack);
	if (ret < 0)
		return ret;

	ret = __parse_shaper(dev, tb, info, shaper);
	if (ret)
		return ret;

	if (tb[NET_SHAPER_A_PARENT]) {
		ret = parse_handle(tb[NET_SHAPER_A_PARENT], info,
				   &shaper->parent);
		if (ret)
			return ret;
	}
	return 0;
}

/* Update the H/W and on success update the local cache, too */
static int net_shaper_set(struct net_device *dev,
			  const struct net_shaper_info *shaper,
			  struct netlink_ext_ack *extack)
{
	enum net_shaper_scope scope;
	u32 handle = shaper->handle;
	int ret;

	scope = net_shaper_handle_scope(handle);
	if (scope == NET_SHAPER_SCOPE_PORT ||
	    scope == NET_SHAPER_SCOPE_UNSPEC) {
		NL_SET_ERR_MSG_FMT(extack, "can't set shaper with scope %d",
				   scope);
		return -EINVAL;
	}
	if (scope == NET_SHAPER_SCOPE_DETACHED && !sc_lookup(dev, handle)) {
		NL_SET_ERR_MSG_FMT(extack, "Use 'group' to create a scope detached shaper");
		return -EINVAL;
	}

	ret = sc_prepare_insert(dev, &handle, extack);
	if (ret)
		return ret;

	ret = dev->netdev_ops->net_shaper_ops->set(dev, shaper, extack);
	sc_commit(dev, 1, shaper);
	return ret;
}

int net_shaper_nl_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct net_shaper_info shaper;
	struct net_device *dev;
	struct nlattr *attr;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, NET_SHAPER_A_SHAPER))
		return -EINVAL;

	ret = fetch_dev(info, NET_SHAPER_A_IFINDEX, &dev);
	if (ret)
		return ret;

	attr = info->attrs[NET_SHAPER_A_SHAPER];
	ret = parse_shaper(dev, attr, info, &shaper);
	if (ret)
		goto put;

	ret = net_shaper_set(dev, &shaper, info->extack);

put:
	dev_put(dev);
	return ret;
}

static int net_shaper_delete(struct net_device *dev, u32 handle,
			     struct netlink_ext_ack *extack)
{
	struct net_shaper_info *parent, *shaper = sc_lookup(dev, handle);
	struct xarray *xa = __sc_container(dev);
	enum net_shaper_scope pscope;
	u32 parent_handle;
	int ret;

	if (!xa || !shaper) {
		NL_SET_ERR_MSG_FMT(extack, "Shaper %x not found", handle);
		return -EINVAL;
	}

	if (is_detached(handle) && shaper->children > 0) {
		NL_SET_ERR_MSG_FMT(extack, "Can't delete detached shaper with chidren node, %x has %d",
				   handle, shaper->children);
		return -EINVAL;
	}

	while (shaper) {
		parent_handle = shaper->parent;
		pscope = net_shaper_handle_scope(parent_handle);

		ret = dev->netdev_ops->net_shaper_ops->delete(dev, handle, extack);
		if (ret < 0)
			return ret;

		xa_lock(xa);
		__xa_erase(xa, handle);
		if (is_detached(handle))
			idr_remove(&dev->net_shaper_data->detached_ids,
				   net_shaper_handle_id(handle));
		xa_unlock(xa);
		kfree(shaper);
		shaper = NULL;

		if (pscope == NET_SHAPER_SCOPE_DETACHED) {
			parent = sc_lookup(dev, parent_handle);
			if (parent && !--parent->children) {
				shaper = parent;
				handle = parent_handle;
			}
		}
	}
	return 0;
}

int net_shaper_nl_delete_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct net_device *dev;
	u32 handle;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, NET_SHAPER_A_HANDLE))
		return -EINVAL;

	ret = fetch_dev(info, NET_SHAPER_A_IFINDEX, &dev);
	if (ret)
		return ret;

	ret = parse_handle(info->attrs[NET_SHAPER_A_HANDLE], info, &handle);
	if (ret)
		goto put;

	ret = net_shaper_delete(dev, handle, info->extack);

put:
	dev_put(dev);
	return ret;
}

/* Update the H/W and on success update the local cache, too */
static int net_shaper_group(struct net_device *dev, int nr_inputs,
			    struct net_shaper_info *inputs,
			    struct net_shaper_info *output,
			    struct netlink_ext_ack *extack)
{
	enum net_shaper_scope scope, output_scope, output_pscope;
	struct net_shaper_info *parent = NULL;
	int i, ret;

	output_scope = net_shaper_handle_scope(output->handle);
	if (output_scope != NET_SHAPER_SCOPE_DETACHED &&
	    output_scope != NET_SHAPER_SCOPE_NETDEV) {
		NL_SET_ERR_MSG_FMT(extack, "Invalid scope for output shaper %d",
				   output_scope);
		return -EINVAL;
	}
	if (output_scope == NET_SHAPER_SCOPE_DETACHED &&
	    net_shaper_handle_id(output->handle) != NET_SHAPER_ID_UNSPEC &&
	    !sc_lookup(dev, output->handle)) {
		NL_SET_ERR_MSG_FMT(extack, "Output shaper %x does not exists",
				   output->handle);
		return -EINVAL;
	}

	output_pscope = net_shaper_handle_scope(output->parent);
	if (output_pscope != NET_SHAPER_SCOPE_DETACHED &&
	    output_pscope != NET_SHAPER_SCOPE_NETDEV) {
		NL_SET_ERR_MSG_FMT(extack, "Invalid scope for output parent shaper %d",
				   output_pscope);
		return -EINVAL;
	}
	if (output_pscope == NET_SHAPER_SCOPE_DETACHED) {
		parent = sc_lookup(dev, output->parent);
		if (!parent) {
			NL_SET_ERR_MSG_FMT(extack, "Output parent shaper %x does not exists",
				   output->parent);
			return -EINVAL;
		}
	}

	/* for newly created detached scope shaper, the following will update the
	 * handle, due to id allocation
	 */
	ret = sc_prepare_insert(dev, &output->handle, extack);
	if (ret)
		goto rollback;

	for (i = 0; i < nr_inputs; ++i) {
		scope = net_shaper_handle_scope(inputs[i].handle);
		if (scope != NET_SHAPER_SCOPE_QUEUE &&
		    scope != NET_SHAPER_SCOPE_DETACHED) {
			ret = -EINVAL;
			NL_SET_ERR_MSG_FMT(extack, "Invalid scope for input shaper %d",
					   scope);
			goto rollback;
		}
		if (scope == NET_SHAPER_SCOPE_DETACHED &&
		    !sc_lookup(dev, inputs[i].handle)) {
			ret = -EINVAL;
			NL_SET_ERR_MSG_FMT(extack, "Can't create set a new detached shaper as input, handle %x",
					   inputs[i].handle);
			goto rollback;
		}

		ret = sc_prepare_insert(dev, &inputs[i].handle, extack);
		if (ret)
			goto rollback;

		/* the inputs shapers will be nested to the output */
		if (inputs[i].parent != output->handle) {
			inputs[i].parent = output->handle;
			output->children++;
		}
	}

	ret = dev->netdev_ops->net_shaper_ops->group(dev, nr_inputs,
						     inputs, output,
						     extack);
	if (ret < 0)
		goto rollback;

	if (parent)
		parent->children++;
	sc_commit(dev, 1, output);
	sc_commit(dev, nr_inputs, inputs);
	return ret;

rollback:
	sc_rollback(dev);
	return ret;
}

static int nla_handle_total_size(void)
{
	return nla_total_size(
			      nla_total_size(sizeof(u32)) +
			      nla_total_size(sizeof(u32)));
}

static int group_send_reply(struct genl_info *info, u32 handle, int id)
{
	struct nlattr *handle_attr;
	struct sk_buff *msg;
	int ret = -EMSGSIZE;
	void *hdr;

	/* prepare the msg reply in adavance, to avoid device operation rollback */
	msg = genlmsg_new(nla_handle_total_size(), GFP_KERNEL);
	if (!msg)
		return ret;

	hdr = genlmsg_iput(msg, info);
	if (!hdr)
		goto free_msg;

	handle_attr = nla_nest_start(msg, NET_SHAPER_A_HANDLE);
	if (!handle_attr)
		goto free_msg;

	if (nla_put_u32(msg, NET_SHAPER_A_SCOPE,
			net_shaper_handle_scope(handle)))
		goto free_msg;

	if (nla_put_u32(msg, NET_SHAPER_A_ID, id))
		goto free_msg;

	nla_nest_end(msg, handle_attr);
	genlmsg_end(msg, hdr);

	ret =  genlmsg_reply(msg, info);
	if (ret)
		goto free_msg;

	return ret;

free_msg:
	nlmsg_free(msg);
	return ret;
}

int net_shaper_nl_group_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct net_shaper_info *inputs, output;
	int i, ret, rem, nr_inputs;
	struct net_device *dev;
	struct nlattr *attr;

	if (GENL_REQ_ATTR_CHECK(info, NET_SHAPER_A_INPUTS) ||
	    GENL_REQ_ATTR_CHECK(info, NET_SHAPER_A_OUTPUT))
		return -EINVAL;

	ret = fetch_dev(info, NET_SHAPER_A_IFINDEX, &dev);
	if (ret)
		return ret;

	nr_inputs = attr_list_len(info, NET_SHAPER_A_INPUTS);
	inputs = kcalloc(nr_inputs, sizeof(struct net_shaper_info), GFP_KERNEL);
	if (!inputs) {
		GENL_SET_ERR_MSG_FMT(info, "Can't allocate memory for %d input shapers",
				     nr_inputs);
		ret = -ENOMEM;
		goto put;
	}

	ret = parse_output_shaper(dev, info->attrs[NET_SHAPER_A_OUTPUT], info,
				  &output);
	if (ret)
		goto free_shapers;

	i = 0;
	nla_for_each_attr_type(attr, NET_SHAPER_A_INPUTS,
			       genlmsg_data(info->genlhdr),
			       genlmsg_len(info->genlhdr), rem) {
		if (WARN_ON_ONCE(i >= nr_inputs))
			goto free_shapers;

		ret = parse_shaper(dev, attr, info, &inputs[i++]);
		if (ret)
			goto free_shapers;
	}

	ret = net_shaper_group(dev, nr_inputs, inputs, &output, info->extack);
	if (ret < 0)
		goto free_shapers;

	ret = group_send_reply(info, output.handle, ret);
	if (ret) {
		/* Error on reply is not fatal to avoid rollback a successful
		 * configuration */
		GENL_SET_ERR_MSG_FMT(info, "Can't send reply %d", ret);
		ret = 0;
	}

free_shapers:
	kfree(inputs);

put:
	dev_put(dev);
	return ret;
}

static int
net_shaper_cap_fill_one(struct sk_buff *msg, unsigned long flags,
			const struct genl_info *info)
{
	unsigned long cur;
	void *hdr;

	hdr = genlmsg_iput(msg, info);
	if (!hdr)
		return -EMSGSIZE;

	for (cur = NET_SHAPER_A_CAPABILITIES_SUPPORT_METRIC_BPS;
	     cur <= NET_SHAPER_A_CAPABILITIES_MAX; ++cur) {
		if (flags & BIT(cur) && nla_put_flag(msg, cur))
			goto nla_put_failure;
	}

	genlmsg_end(msg, hdr);

	return 0;

nla_put_failure:
	genlmsg_cancel(msg, hdr);
	return -EMSGSIZE;
}

int net_shaper_nl_cap_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	const struct net_shaper_ops *ops;
	enum net_shaper_scope scope;
	struct net_device *dev;
	struct sk_buff *msg;
	unsigned long flags;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, NET_SHAPER_A_CAPABILITIES_SCOPE))
		return -EINVAL;

	ret = fetch_dev(info, NET_SHAPER_A_CAPABILITIES_IFINDEX, &dev);
	if (ret)
		return ret;

	ops = dev->netdev_ops->net_shaper_ops;
	ret = ops->capabilities(dev, scope, &flags);
	if (ret)
		goto put;

	msg = genlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg)
		goto put;

	ret = net_shaper_cap_fill_one(msg, flags, info);
	if (ret)
		goto free_msg;

	ret =  genlmsg_reply(msg, info);
	if (ret)
		goto free_msg;

put:
	dev_put(dev);
	return ret;

free_msg:
	nlmsg_free(msg);
	goto put;
}

int net_shaper_nl_cap_get_dumpit(struct sk_buff *skb,
				 struct netlink_callback *cb)
{
	const struct genl_info *info = genl_info_dump(cb);
	const struct net_shaper_ops *ops;
	enum net_shaper_scope scope;
	struct net_device *dev;
	unsigned long flags;
	int ret;

	ret = fetch_dev(info, NET_SHAPER_A_CAPABILITIES_IFINDEX, &dev);
	if (ret)
		return ret;

	ops = dev->netdev_ops->net_shaper_ops;
	for (scope = 0; scope <= NET_SHAPER_SCOPE_MAX; ++scope) {
		if (ops->capabilities(dev, scope, &flags))
			continue;

		ret = net_shaper_cap_fill_one(skb, flags, info);
		if (ret)
			goto put;
	}

put:
	dev_put(dev);
	return ret;
}

void dev_shaper_flush(struct net_device *dev)
{
	struct xarray *xa = __sc_container(dev);
	struct net_shaper_info *cur;
	unsigned long index;

	if (!xa)
		return;

	xa_lock(xa);
	xa_for_each(xa, index, cur) {
		__xa_erase(xa, index);
		kfree(cur);
	}
	idr_destroy(&dev->net_shaper_data->detached_ids);
	xa_unlock(xa);
	kfree(xa);
}

static int __init shaper_init(void)
{
	return genl_register_family(&net_shaper_nl_family);
}

subsys_initcall(shaper_init);
