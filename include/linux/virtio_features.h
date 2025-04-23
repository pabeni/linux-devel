/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_VIRTIO_FEATURES_H
#define _LINUX_VIRTIO_FEATURES_H

#include <linux/types.h>
#include <linux/bitmap.h>

#define VIRTIO_FEATURES_MAX	128
#define VIRTIO_FEATURES_SIZE	BITS_TO_LONGS(VIRTIO_FEATURES_MAX)

struct virtio_features {
	unsigned long mask[VIRTIO_FEATURES_SIZE];
};

#define VIRTIO_FEATURES_VALIDATE_IDX(idx)				\
	({								\
		if (__builtin_constant_p(idx))				\
			BUILD_BUG_ON(idx >= VIRTIO_FEATURES_SIZE);	\
		else							\
			BUG_ON(idx >= VIRTIO_FEATURES_SIZE);		\
	})
static inline void virtio_features_from_u64(struct virtio_features *features,
					    unsigned int i, u64 fmask)
{
	int idx = i * 64 / BITS_PER_LONG;

	VIRTIO_FEATURES_VALIDATE_IDX(idx);
	features->mask[idx] = fmask;
	if (BITS_PER_LONG == 32)
		features->mask[idx + 1] |= fmask >> 32;
}

static inline u64
virtio_features_to_u64(const struct virtio_features *features, unsigned int i)
{
	int idx = i * 64 / BITS_PER_LONG;
	u64 ret;

	VIRTIO_FEATURES_VALIDATE_IDX(idx);
	ret = features->mask[idx];
	if (BITS_PER_LONG == 32)
		ret |= features->mask[idx + 1] << 32ULL;
	return ret;
}

#define VIRTIO_FEATURES_VALIDATE_BIT(fbit)				\
	({								\
		if (__builtin_constant_p(fbit))				\
			BUILD_BUG_ON(fbit >= VIRTIO_FEATURES_MAX);	\
		else							\
			BUG_ON(fbit >= VIRTIO_FEATURES_MAX);		\
	})

static inline void
virtio_features_zero(struct virtio_features *features)
{
	memset(features, 0, sizeof(*features));
}

static inline void
virtio_features_set_bit(struct virtio_features *features, unsigned int fbit)
{
	VIRTIO_FEATURES_VALIDATE_BIT(fbit);
	__set_bit(fbit, features->mask);
}

static inline bool
virtio_features_test_bit(const struct virtio_features *features,
			unsigned int fbit)
{
	VIRTIO_FEATURES_VALIDATE_BIT(fbit);
	return !!test_bit(fbit, features->mask);
}

static inline void
virtio_features_clear_bit(struct virtio_features *features, unsigned int fbit)
{
	VIRTIO_FEATURES_VALIDATE_BIT(fbit);
	__clear_bit(fbit, features->mask);
}

static inline bool
virtio_features_equal(const struct virtio_features *features,
		      const struct virtio_features *others)
{
	return memcmp(features, others, sizeof(*features));
}

static inline void
virtio_features_and(struct virtio_features *dst,
		    const struct virtio_features *features1,
		    const struct virtio_features *features2)
{
	__bitmap_and(dst->mask, features1->mask, features2->mask,
		     VIRTIO_FEATURES_MAX);
}

static inline void
virtio_features_andnot(struct virtio_features *dst,
		    const struct virtio_features *features1,
		    const struct virtio_features *features2)
{
	__bitmap_andnot(dst->mask, features1->mask, features2->mask,
			VIRTIO_FEATURES_MAX);
}
#endif

