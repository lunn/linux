/*
 * CAAM blob management module
 * Copyright (C) 2016 Zodiac Inflight Innovations. All Rights Reserved.
 */

#ifndef _CAAM_BLOB_H
#define _CAAM_BLOB_H

#include <linux/types.h>

#ifndef __KERNEL__
#define __user
#endif

#define	CAAM_BLOB_MINOR	MISC_DYNAMIC_MINOR

/*
 * CBIOENC/CBIODEC operation.
 *
 * The dst_len parameter acts as both input and output. In the input,
 * it should specify the size of the output buffer. In the output, it
 * contains tha actual amount of data that was written to the output
 * buffer.
 */
struct caam_blob_op {
	__u8 __user *keymod;  /* (in) pointer to the 8-byte key modifier */
	__u32        src_len; /* (in) length of source data */
	__u8 __user *src;     /* (in) source data */
	__u32        dst_len; /* (in/out) length of output buffer/data */
	__u8 __user *dst;     /* (out) pointer to output data */
};

/* Encapsulation */
#define CBIOENC _IOWR('c', 101, struct caam_blob_op)

/* Decapsulation */
#define CBIODEC _IOWR('c', 102, struct caam_blob_op)

/*
 * CBIOQRY operation
 */
struct caam_blob_query {
    __u32 max_src_len; /* (out) the maximum length allowed for input data */
};

/* Query */
#define CBIOQRY _IOWR('c', 103, struct caam_blob_query)

#endif /* _CAAM_BLOB_H */
