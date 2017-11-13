// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note

#include <linux/ethtool_netlink.h>
#include <linux/bitmap.h>
#include "netlink.h"
#include "bitset.h"

static bool ethnl_test_bit(const void *val, unsigned int index, bool is_u32)
{
	if (!val)
		return true;
	else if (is_u32)
		return ((const u32 *)val)[index / 32] & (1U << (index % 32));
	else
		return test_bit(index, val);
}

static void __bitmap_to_u32(u32 *dst, const void *src, unsigned int size,
			    bool is_u32)
{
	unsigned int full_words = size / 32;
	const u32 *src32 = src;

	if (!is_u32) {
		bitmap_to_arr32(dst, src, size);
		return;
	}

	memcpy(dst, src32, full_words * sizeof(u32));
	if (size % 32 != 0)
		dst[full_words] = src32[full_words] & ((1U << (size % 32)) - 1);
}

/* convert standard kernel bitmap (long sized words) to ethtool one (u32 words)
 * bitmap_to_arr32() is not guaranteed to do "in place" conversion correctly;
 * moreover, we can use the fact that the conversion is no-op except for 64-bit
 * big endian architectures
 */
#if BITS_PER_LONG == 64 && defined(__BIG_ENDIAN)
void ethnl_bitmap_to_u32(unsigned long *bitmap, unsigned int nwords)
{
	u32 *dst = (u32 *)bitmap;
	unsigned int i;

	for (i = 0; i < nwords; i++) {
		unsigned long tmp = READ_ONCE(bitmap[i]);

		dst[2 * i] = tmp & 0xffffffff;
		dst[2 * i + 1] = tmp >> 32;
	}
}
#endif

static const char *bit_name(const void *names, bool legacy, unsigned int idx)
{
	const char (*const legacy_names)[ETH_GSTRING_LEN] = names;
	const char *const *simple_names = names;

	return legacy ? legacy_names[idx] : simple_names[idx];
}

/* calculate size for a bitset attribute
 * see ethnl_put_bitset() for arguments
 */
static int __ethnl_bitset_size(unsigned int size, const void *val,
			       const void *mask, const void *names,
			       unsigned int flags)
{
	const bool legacy = flags & ETHNL_BITSET_LEGACY_NAMES;
	const bool compact = flags & ETHNL_BITSET_COMPACT;
	const bool is_list = flags & ETHNL_BITSET_LIST;
	const bool is_u32 = flags & ETHNL_BITSET_U32;
	unsigned int nwords = DIV_ROUND_UP(size, 32);
	unsigned int len = 0;

	if (WARN_ON(!compact && !names))
		return -EINVAL;
	/* list flag */
	if (flags & ETHNL_BITSET_LIST)
		len += nla_total_size(sizeof(u32));
	/* size */
	len += nla_total_size(sizeof(u32));

	if (compact) {
		/* values, mask */
		len += 2 * nla_total_size(nwords * sizeof(u32));
	} else {
		unsigned int bits_len = 0;
		unsigned int bit_len, i;

		for (i = 0; i < size; i++) {
			const char *name = bit_name(names, legacy, i) ?: "";

			if ((is_list || mask) &&
			    !ethnl_test_bit(is_list ? val : mask, i, is_u32))
				continue;
			/* index */
			bit_len = nla_total_size(sizeof(u32));
			/* name */
			bit_len += ethnl_str_size(name);
			/* value */
			if (!is_list && ethnl_test_bit(val, i, is_u32))
				bit_len += nla_total_size(0);

			/* bit nest */
			bits_len += nla_total_size(bit_len);
		}
		/* bits nest */
		len += nla_total_size(bits_len);
	}

	/* outermost nest */
	return nla_total_size(len);
}

int ethnl_bitset_size(unsigned int size, const unsigned long *val,
		      const unsigned long *mask, const void *names,
		      unsigned int flags)
{
	return __ethnl_bitset_size(size, val, mask, names,
				   flags & ~ETHNL_BITSET_U32);
}

int ethnl_bitset32_size(unsigned int size, const u32 *val, const u32 *mask,
			const void *names, unsigned int flags)
{
	return __ethnl_bitset_size(size, val, mask, names,
				   flags | ETHNL_BITSET_U32);
}

/**
 * __ethnl_put_bitset() - Put a bitset nest into a message
 * @skb:      skb with the message
 * @attrtype: attribute type for the bitset nest
 * @size:     size of the set in bits
 * @val:      bitset values
 * @mask:     mask of valid bits; NULL is interpreted as "all bits"
 * @names:    bit names (only used for verbose format)
 * @flags:    combination of ETHNL_BITSET_* flags
 *
 * This is the actual implementation of putting a bitset nested attribute into
 * a netlink message but callers are supposed to use either ethnl_put_bitset()
 * for unsigned long based bitmaps or ethnl_put_bitset32() for u32 based ones.
 * Cleans the nest up on error.
 *
 * Return:    0 on success, error value on error
 */
static int __ethnl_put_bitset(struct sk_buff *skb, int attrtype,
			      unsigned int size, const void *val,
			      const void *mask, const void *names,
			      unsigned int flags)
{
	const bool legacy = flags & ETHNL_BITSET_LEGACY_NAMES;
	const bool compact = flags & ETHNL_BITSET_COMPACT;
	const bool is_list = flags & ETHNL_BITSET_LIST;
	const bool is_u32 = flags & ETHNL_BITSET_U32;
	struct nlattr *nest;
	struct nlattr *attr;
	int ret;

	if (WARN_ON(!compact && !names))
		return -EINVAL;
	nest = ethnl_nest_start(skb, attrtype);
	if (!nest)
		return -EMSGSIZE;

	ret = -EMSGSIZE;
	if (is_list && nla_put_flag(skb, ETHA_BITSET_LIST))
		goto err;
	if (nla_put_u32(skb, ETHA_BITSET_SIZE, size))
		goto err;
	if (compact) {
		unsigned int bytesize = DIV_ROUND_UP(size, 32) * sizeof(u32);

		attr = nla_reserve(skb, ETHA_BITSET_VALUE, bytesize);
		if (!attr)
			goto err;
		__bitmap_to_u32(nla_data(attr), val, size, is_u32);
		if (mask) {
			attr = nla_reserve(skb, ETHA_BITSET_MASK, bytesize);
			if (!attr)
				goto err;
			__bitmap_to_u32(nla_data(attr), mask, size, is_u32);
		}
	} else {
		struct nlattr *bits;
		unsigned int i;

		bits = ethnl_nest_start(skb, ETHA_BITSET_BITS);
		if (!bits)
			goto err;
		for (i = 0; i < size; i++) {
			const char *name = bit_name(names, legacy, i) ?: "";

			if ((is_list || mask) &&
			    !ethnl_test_bit(is_list ? val : mask, i, is_u32))
				continue;
			attr = ethnl_nest_start(skb, ETHA_BITS_BIT);
			if (!attr ||
			    nla_put_u32(skb, ETHA_BIT_INDEX, i) ||
			    nla_put_string(skb, ETHA_BIT_NAME, name))
				goto err;
			if (!is_list && ethnl_test_bit(val, i, is_u32) &&
			    nla_put_flag(skb, ETHA_BIT_VALUE))
				goto err;
			nla_nest_end(skb, attr);
		}
		nla_nest_end(skb, bits);
	}

	nla_nest_end(skb, nest);
	return 0;
err:
	nla_nest_cancel(skb, nest);
	return ret;
}

int ethnl_put_bitset(struct sk_buff *skb, int attrtype, unsigned int size,
		     const unsigned long *val, const unsigned long *mask,
		     const void *names, unsigned int flags)
{
	return __ethnl_put_bitset(skb, attrtype, size, val, mask, names,
				  flags & ~ETHNL_BITSET_U32);
}

int ethnl_put_bitset32(struct sk_buff *skb, int attrtype, unsigned int size,
		       const u32 *val, const u32 *mask, const void *names,
		       unsigned int flags)
{
	return __ethnl_put_bitset(skb, attrtype, size, val, mask, names,
				  flags | ETHNL_BITSET_U32);
}

static const struct nla_policy bitset_policy[ETHA_BITSET_MAX + 1] = {
	[ETHA_BITSET_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_BITSET_LIST]		= { .type = NLA_FLAG },
	[ETHA_BITSET_SIZE]		= { .type = NLA_U32 },
	[ETHA_BITSET_BITS]		= { .type = NLA_NESTED },
	[ETHA_BITSET_VALUE]		= { .type = NLA_BINARY },
	[ETHA_BITSET_MASK]		= { .type = NLA_BINARY },
};

static const struct nla_policy bit_policy[ETHA_BIT_MAX + 1] = {
	[ETHA_BIT_UNSPEC]		= { .type = NLA_REJECT },
	[ETHA_BIT_INDEX]		= { .type = NLA_U32 },
	[ETHA_BIT_NAME]			= { .type = NLA_NUL_STRING },
	[ETHA_BIT_VALUE]		= { .type = NLA_FLAG },
};

static int ethnl_name_to_idx(const void *names, bool legacy,
			     unsigned int n_names, const char *name,
			     unsigned int name_len)
{
	unsigned int i;

	for (i = 0; i < n_names; i++) {
		const char *bname = bit_name(names, legacy, i);

		if (bname && !strncmp(bname, name, name_len) &&
		    strlen(bname) <= name_len)
			return i;
	}

	return n_names;
}

static int ethnl_update_bit(unsigned long *bitmap, unsigned long *bitmask,
			    unsigned int nbits, const struct nlattr *bit_attr,
			    bool is_list, const void *names, bool legacy,
			    struct genl_info *info)
{
	struct nlattr *tb[ETHA_BIT_MAX + 1];
	int ret, idx;

	if (nla_type(bit_attr) != ETHA_BITS_BIT) {
		ETHNL_SET_ERRMSG(info,
				 "ETHA_BITSET_BITS can contain only ETHA_BITS_BIT");
		return genl_err_attr(info, -EINVAL, bit_attr);
	}
	ret = nla_parse_nested_strict(tb, ETHA_BIT_MAX, bit_attr, bit_policy,
				      info->extack);
	if (ret < 0)
		return ret;

	if (tb[ETHA_BIT_INDEX]) {
		const char *name;

		idx = nla_get_u32(tb[ETHA_BIT_INDEX]);
		if (idx >= nbits) {
			ETHNL_SET_ERRMSG(info, "bit index too high");
			return genl_err_attr(info, -EOPNOTSUPP,
					     tb[ETHA_BIT_INDEX]);
		}
		name = bit_name(names, legacy, idx);
		if (tb[ETHA_BIT_NAME] && name &&
		    strncmp(nla_data(tb[ETHA_BIT_NAME]), name,
			    nla_len(tb[ETHA_BIT_NAME]))) {
			ETHNL_SET_ERRMSG(info, "bit index and name mismatch");
			return genl_err_attr(info, -EINVAL, bit_attr);
		}
	} else if (tb[ETHA_BIT_NAME]) {
		idx = ethnl_name_to_idx(names, legacy, nbits,
					nla_data(tb[ETHA_BIT_NAME]),
					nla_len(tb[ETHA_BIT_NAME]));
		if (idx >= nbits) {
			ETHNL_SET_ERRMSG(info, "bit name not found");
			return genl_err_attr(info, -EOPNOTSUPP,
					     tb[ETHA_BIT_NAME]);
		}
	} else {
		ETHNL_SET_ERRMSG(info, "neither bit index nor name specified");
		return genl_err_attr(info, -EINVAL, bit_attr);
	}

	if (is_list || tb[ETHA_BIT_VALUE])
		set_bit(idx, bitmap);
	else
		clear_bit(idx, bitmap);
	if (!is_list || bitmask)
		set_bit(idx, bitmask);
	return 0;
}

int ethnl_bitset_is_compact(const struct nlattr *bitset, bool *compact)
{
	struct nlattr *tb[ETHA_BITSET_MAX + 1];
	int ret;

	ret = nla_parse_nested_strict(tb, ETHA_BITSET_MAX, bitset,
				      bitset_policy, NULL);
	if (ret < 0)
		return ret;

	if (tb[ETHA_BITSET_BITS]) {
		if (tb[ETHA_BITSET_VALUE] || tb[ETHA_BITSET_MASK])
			return -EINVAL;
		*compact = false;
		return 0;
	}
	if (!tb[ETHA_BITSET_SIZE] || !tb[ETHA_BITSET_VALUE])
		return -EINVAL;

	*compact = true;
	return 0;
}

/* 64-bit long endian is the only case when u32 based bitmap and unsigned long
 * based bitmap layouts differ
 */
#if BITS_PER_LONG == 64 && defined(__BIG_ENDIAN)
/* dst &= src */
static void __bitmap_and_u32(unsigned long *dst, const u32 *src,
			     unsigned int nbits)
{
	unsigned long op;

	while (nbits >= BITS_PER_LONG) {
		op = src[0] | ((unsigned long)src[1] << 32);
		*dst &= op;

		dst++;
		src += 2;
		nbits -= BITS_PER_LONG;
	}

	if (!nbits)
		return;
	op = src[0];
	if (nbits > 32)
		op |= ((unsigned long)src[1] << 32);
	*dst = (op & BITMAP_LAST_WORD_MASK(nbits));
}

/* map1 == map2 */
static bool __bitmap_equal_u32(const unsigned long *map1, const u32 *map2,
			       unsigned int nbits)
{
	unsigned long dword;

	while (nbits >= BITS_PER_LONG) {
		dword = map2[0] | ((unsigned long)map2[1] << 32);
		if (*map1 != dword)
			return false;

		map1++;
		map2 += 2;
		nbits -= BITS_PER_LONG;
	}

	if (!nbits)
		return true;
	dword = map2[0];
	if (nbits > 32)
		dword |= ((unsigned long)map2[1] << 32);
	return !((*map1 ^ dword) & BITMAP_LAST_WORD_MASK(nbits));
}
#else
/* On 32-bit and 64-bit LE, unsigned long and u32 bitmap layout is the same
 * but we must not write past dst buffer if the number of words is odd.
 */
static void __bitmap_and_u32(unsigned long *dst, const u32 *src,
			     unsigned int nbits)
{
	u32 *dst32 = (u32 *)dst;

	while (nbits >= 32) {
		*dst32++ &= *src++;
		nbits -= 32;
	}
	if (!nbits)
		return;
	*dst32 &= (*src & ((1U << nbits) - 1));
}

static bool __bitmap_equal_u32(const unsigned long *map1, const u32 *map2,
			       unsigned int nbits)
{
	unsigned int full_words = nbits / 32;
	u32 last_word_mask;
	u32 *map1_32 = (u32 *)map1;

	if (memcmp(map1, map2, full_words * BITS_PER_BYTE))
		return false;
	if (!(nbits % 32))
		return true;
	last_word_mask = (1U << (nbits % 32)) - 1;
	return !((map1_32[full_words] ^ map2[full_words]) & last_word_mask);
}
#endif

/* copy unsigned long bitmap to unsigned long or u32 */
static void __bitmap_to_any(void *dst, const unsigned long *src,
			    unsigned int nbits, bool dst_is_u32)
{
	if (dst_is_u32)
		bitmap_to_arr32(dst, src, nbits);
	else
		bitmap_copy(dst, src, nbits);
}

static bool __bitmap_equal_any(const unsigned long *map1, const void *map2,
			       unsigned int nbits, bool is_u32)
{
	if (!is_u32)
		return bitmap_equal(map1, map2, nbits);
	else
		return __bitmap_equal_u32(map1, map2, nbits);
}

/**
 * __ethnl_update_bitset() - Apply a bitset nest to a bitmap
 * @bitmap:  bitmap to update
 * @bitmask: if not, mask from the nest is copied here
 * @nbits:   size of the updated bitmap in bits
 * @attr:    nest attribute to parse and apply
 * @err:     pointer to variable to put error value (or 0 on success) to
 * @names:   array of bit names; may be null for compact format
 * @legacy:  true if @names is ioctl style array of char[32], false if it is
 *           a simple array of (char *) strings
 * @info:    genetlink info (also used for extack error reporting)
 * @is_u32:  true: bitmaps are unsigned long based, false: u32 based bitmaps
 *
 * This is the actual implementation of bitset nested attribute parser but
 * callers are supposed to use ethnl_update_bitset() for unsigned long based
 * bitmaps or ethnl_update_bitset32() for u32 based ones.
 *
 * Return:   true if the bitmap contents was modified, false if not
 */
static bool __ethnl_update_bitset(void *bitmap, void *bitmask,
				  unsigned int nbits, const struct nlattr *attr,
				  int *err, const void *names, bool legacy,
				  struct genl_info *info, bool is_u32)
{
	struct nlattr *tb[ETHA_BITSET_MAX + 1];
	unsigned int change_bits = 0;
	unsigned int max_bits = 0;
	unsigned long *val, *mask;
	bool mod = false;
	bool is_list;

	*err = 0;
	if (!attr)
		return mod;
	*err = nla_parse_nested_strict(tb, ETHA_BITSET_MAX, attr, bitset_policy,
				       info->extack);
	if (*err < 0)
		return mod;
	*err = -EINVAL;
	if (tb[ETHA_BITSET_BITS] &&
	    (tb[ETHA_BITSET_VALUE] || tb[ETHA_BITSET_MASK]))
		return mod;
	if (!tb[ETHA_BITSET_BITS] &&
	    (!tb[ETHA_BITSET_SIZE] || !tb[ETHA_BITSET_VALUE]))
		return mod;
	is_list = (tb[ETHA_BITSET_LIST] != NULL);
	if (is_list && tb[ETHA_BITSET_MASK])
		return mod;

	/* To let new userspace to work with old kernel, we allow bitmaps
	 * from userspace to be longer than kernel ones and only issue an
	 * error if userspace actually tries to change a bit not existing
	 * in kernel.
	 */
	if (tb[ETHA_BITSET_SIZE])
		change_bits = nla_get_u32(tb[ETHA_BITSET_SIZE]);
	max_bits = max_t(unsigned int, nbits, change_bits);
	mask = bitmap_zalloc(max_bits, GFP_KERNEL);
	val = bitmap_zalloc(max_bits, GFP_KERNEL);

	if (tb[ETHA_BITSET_BITS]) {
		struct nlattr *bit_attr;
		int rem;

		if (is_list)
			bitmap_fill(mask, nbits);
		else if (is_u32)
			bitmap_from_arr32(val, bitmap, nbits);
		else
			bitmap_copy(val, bitmap, nbits);
		nla_for_each_nested(bit_attr, tb[ETHA_BITSET_BITS], rem) {
			*err = ethnl_update_bit(val, mask, nbits, bit_attr,
						is_list, names, legacy, info);
			if (*err < 0)
				goto out_free;
		}
		if (bitmask)
			__bitmap_to_any(bitmask, mask, nbits, is_u32);
	} else {
		unsigned int change_words = DIV_ROUND_UP(change_bits, 32);

		*err = 0;
		if (change_bits == 0 && tb[ETHA_BITSET_MASK])
			goto out_free;
		*err = -EINVAL;
		if (nla_len(tb[ETHA_BITSET_VALUE]) < change_words * sizeof(u32))
			goto out_free;
		if (tb[ETHA_BITSET_MASK] &&
		    nla_len(tb[ETHA_BITSET_MASK]) < change_words * sizeof(u32))
			goto out_free;

		bitmap_from_arr32(val, nla_data(tb[ETHA_BITSET_VALUE]),
				  change_bits);
		if (tb[ETHA_BITSET_MASK])
			bitmap_from_arr32(mask, nla_data(tb[ETHA_BITSET_MASK]),
					  change_bits);
		else
			bitmap_fill(mask, nbits);

		if (nbits < change_bits) {
			unsigned int idx = find_next_bit(mask, max_bits, nbits);

			*err = -EINVAL;
			if (idx < max_bits)
				goto out_free;
		}

		if (bitmask)
			__bitmap_to_any(bitmask, mask, nbits, is_u32);
		if (!is_list) {
			bitmap_and(val, val, mask, nbits);
			bitmap_complement(mask, mask, nbits);
			if (is_u32)
				__bitmap_and_u32(mask, bitmap, nbits);
			else
				bitmap_and(mask, mask, bitmap, nbits);
			bitmap_or(val, val, mask, nbits);
		}
	}

	mod = !__bitmap_equal_any(val, bitmap, nbits, is_u32);
	if (mod)
		__bitmap_to_any(bitmap, val, nbits, is_u32);

	*err = 0;
out_free:
	bitmap_free(val);
	bitmap_free(mask);
	return mod;
}

bool ethnl_update_bitset(unsigned long *bitmap, unsigned long *bitmask,
			 unsigned int nbits, const struct nlattr *attr,
			 int *err, const void *names, bool legacy,
			 struct genl_info *info)
{
	return __ethnl_update_bitset(bitmap, bitmask, nbits, attr, err, names,
				     legacy, info, false);
}

bool ethnl_update_bitset32(u32 *bitmap, u32 *bitmask, unsigned int nbits,
			   const struct nlattr *attr, int *err,
			   const void *names, bool legacy,
			   struct genl_info *info)
{
	return __ethnl_update_bitset(bitmap, bitmask, nbits, attr, err, names,
				     legacy, info, true);
}
