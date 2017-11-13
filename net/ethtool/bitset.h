/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */

#ifndef _NET_ETHTOOL_BITSET_H
#define _NET_ETHTOOL_BITSET_H

/* when set, value and mask bitmaps are arrays of u32, when not, arrays of
 * unsigned long
 */
#define ETHNL_BITSET_U32		BIT(0)
/* generate a compact format bitset */
#define ETHNL_BITSET_COMPACT		BIT(1)
/* generate a bit list */
#define ETHNL_BITSET_LIST		BIT(2)
/* when set, names are interpreted as legacy string set (an array of
 * char[ETH_GSTRING_LEN]), when not, as a simple array of char *
 */
#define ETHNL_BITSET_LEGACY_NAMES	BIT(3)

int ethnl_bitset_is_compact(const struct nlattr *bitset, bool *compact);
int ethnl_bitset_size(unsigned int size, const unsigned long *val,
		      const unsigned long *mask, const void *names,
		      unsigned int flags);
int ethnl_bitset32_size(unsigned int size, const u32 *val, const u32 *mask,
			const void *names, unsigned int flags);
int ethnl_put_bitset(struct sk_buff *skb, int attrtype, unsigned int size,
		     const unsigned long *val, const unsigned long *mask,
		     const void *names, unsigned int flags);
int ethnl_put_bitset32(struct sk_buff *skb, int attrtype, unsigned int size,
		       const u32 *val, const u32 *mask, const void *names,
		       unsigned int flags);
bool ethnl_update_bitset(unsigned long *bitmap, unsigned long *bitmask,
			 unsigned int nbits, const struct nlattr *attr,
			 int *err, const void *names, bool legacy,
			 struct genl_info *info);
bool ethnl_update_bitset32(u32 *bitmap, u32 *bitmask, unsigned int nbits,
			   const struct nlattr *attr, int *err,
			   const void *names, bool legacy,
			   struct genl_info *info);

#endif /* _NET_ETHTOOL_BITSET_H */
