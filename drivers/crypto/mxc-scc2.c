// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Pengutronix, Michael Grzeschik <kernel@pengutronix.de>
 *
 * The driver is based on information gathered from
 * drivers/mxc/security/scc2_driver.c which can be found in
 * the Freescale linux-2.6-imx.git in the imx_2.6.38_caf branch.
 *
 */
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/crypto.h>
#include <linux/interrupt.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <crypto/algapi.h>
#include <crypto/aes.h>

/**
 * SCM Registers
 *
 * These values are offsets into the SCC for the Secure Memory
 * (SCM) registers.  They are used in the @c register_offset parameter of
 * #scc_read_register() and #scc_write_register().
 */

#define SCC_SCM_VERSION			0x000 /* Version ID */
#define SCC_SCM_INTR_CTL		0x008 /* Interrupt Control */
#define SCC_SCM_STATUS			0x00c /* Status */
#define SCC_SCM_ERROR_STATUS		0x010 /* Error Status */
#define SCC_SCM_FAULT_ADR		0x014 /* Fault Address */
#define SCC_SCM_PART_OWNERS		0x018 /* Partition Owners */
#define SCC_SCM_PART_ENGAGED		0x01c /* Partitions Engaged */
#define SCC_SCM_UNIQUE_ID0		0x020 /* Unique Number 0 */
#define SCC_SCM_UNIQUE_ID1		0x024 /* Unique Number 1 */
#define SCC_SCM_UNIQUE_ID2		0x028 /* Unique Number 2 */
#define SCC_SCM_UNIQUE_ID3		0x02c /* Unique Number 3 */
#define SCC_SCM_ZCMD			0x050 /* Zeroize Command */
#define SCC_SCM_CCMD			0x054 /* Cipher Command */
#define SCC_SCM_C_BLACK_ST		0x058 /* Cipher Black RAM Start Address */
#define SCC_SCM_DBG_STATUS		0x05c /* Internal Debug */
#define SCC_SCM_AES_CBC_IV0		0x060 /* Cipher IV 0 */
#define SCC_SCM_AES_CBC_IV1		0x064 /* Cipher IV 1 */
#define SCC_SCM_AES_CBC_IV2		0x068 /* Cipher IV 2 */
#define SCC_SCM_AES_CBC_IV3		0x06c /* Cipher IV 3 */
#define SCC_SCM_SMID(part)		(0x080 + (part) * 8) /* SMID Partition 0 */
#define SCC_SCM_ACC(part)		(0x084 + (part) * 8) /* Partition 0 Access Permissions */
#define SCC_SCM_REG_BANK_SIZE		0x100 /* Number of bytes of register space for the SCM. */

/**
 * SMN Registers
 *
 * These values are offsets into the SCC for the Security Monitor
 * (SMN) registers.  They are used in the @c register_offset parameter of the
 * #scc_read_register() and #scc_write_register().
 */
#define SCC_SMN_STATUS			0x100 /* Offset of SMN Status */
#define SCC_SMN_COMMAND			0x104 /* Offset of SMH Command */
#define SCC_SMN_SEQ_START		0x108 /* Offset of SMH Sequence Start */
#define SCC_SMN_SEQ_END			0x10c /* Offset of SMH Sequence End */
#define SCC_SMN_SEQ_CHECK		0x110 /* Offset of SMH Sequence Check */
#define SCC_SMN_BITBANK_CNT		0x114 /* Offset of SMH BitBank Count */
#define SCC_SMN_BITBANK_INC_SIZE	0x118 /* Offset of SMH BitBank Increment */
#define SCC_SMN_BITBANK_DECREMENT	0x11c /* Offset of SMH BitBank Decrement */
#define SCC_SMN_COMPARE_SIZE		0x120 /* Offset of SMH Compare */
#define SCC_SMN_PLAINTEXT_CHECK		0x124 /* Offset of SMH Plaintext Check */
#define SCC_SMN_CIPHERTEXT_CHECK	0x128 /* Offset of SMH Ciphertext Check */
#define SCC_SMN_TIMER_IV		0x12c /* Offset of SMH Timer Initial Value */
#define SCC_SMN_TIMER_CONTROL		0x130 /* Offset of SMH Timer Control */
#define SCC_SMN_SEC_VIO			0x134 /* Offset of SMH Security Violation */
#define SCC_SMN_TIMER			0x138 /* Offset of SMH Timer */
#define SCC_SMN_HAC			0x13c /* Offset of SMH High-Assurance Control */
#define SCC_SMN_REG_BANK_SIZE		0x40  /* Number of bytes allocated to the SMN registers */

#define SCC_ADDRESS_RANGE			(SMN_ADDR_OFFSET + SMN_REG_BANK_SIZE) /** Number of bytes of total register space for the SCC. */

/**
 * SMN Status Register definitions (SMN_STATUS)
 */
#define SMN_STATUS_VERSION_ID_MASK		0xfc000000 /* SMN version id. */
#define SMN_STATUS_VERSION_ID_SHIFT		28         /* Number of bits to shift #SMN_STATUS_VERSION_ID_MASK to get it to LSB */
#define SMN_STATUS_ILLEGAL_MASTER		0x01000000 /* Illegal bus master access attempted. */
#define SMN_STATUS_SCAN_EXIT			0x00800000 /* Scan mode entered/exited since last reset. */
#define SMN_STATUS_PERIP_INIT			0x00010000 /* Some security peripheral is initializing */
#define SMN_STATUS_SMN_ERROR			0x00004000 /* Internal error detected in SMN. */
#define SMN_STATUS_SMN_STATUS_IRQ		0x00004000 /* SMN has an outstanding interrupt. */
#define SMN_STATUS_SOFTWARE_ALARM		0x00002000 /* Software Alarm was triggered. */
#define SMN_STATUS_TIMER_ERROR			0x00001000 /* Timer has expired. */
#define SMN_STATUS_PC_ERROR			0x00000800 /* Plaintext/Ciphertext compare failed. */
#define SMN_STATUS_BITBANK_ERROR		0x00000400 /* Bit Bank detected overflow or underflow */
#define SMN_STATUS_ASC_ERROR			0x00000200 /* Algorithm Sequence Check failed. */
#define SMN_STATUS_SECURITY_POLICY_ERROR	0x00000100 /* Security Policy Block detected error. */
#define SMN_STATUS_SEC_VIO_ACTIVE_ERROR		0x00000080 /* Security Violation Active error. */
#define SMN_STATUS_INTERNAL_BOOT		0x00000020 /* Processor booted from internal ROM. */

#define SCC_SMN_STATUS_STATE_MASK	0x0000001F
#define SCC_SMN_STATE_START		0x0
/* The SMN is zeroizing its RAM during reset */
#define SCC_SMN_STATE_ZEROIZE_RAM	0x5
/* SMN has passed internal checks */
#define SCC_SMN_STATE_HEALTH_CHECK	0x6
/* Fatal Security Violation. SMN is locked, SCM is inoperative. */
#define SCC_SMN_STATE_FAIL		0x9
/* SCC is in secure state. SCM is using secret key. */
#define SCC_SMN_STATE_SECURE		0xA
/* SCC is not secure. SCM is using default key. */
#define SCC_SMN_STATE_NON_SECURE	0xC

#define SCM_STATUS_KST_DEFAULT_KEY	0x80000000 /* SCM Status bit: Key Status is Default Key in Use */
#define SCM_STATUS_KST_RESERVED1	0x40000000 /* SCM Status bit: Key Status is (reserved) */
#define SCM_STATUS_KST_WRONG_KEY	0x20000000 /* SCM Status bit: Key status is Wrong Key */
#define SCM_STATUS_KST_BAD_KEY		0x10000000 /* SCM Status bit: Bad Key detected */
#define SCM_STATUS_ERR			0x00008000 /* SCM Status bit: Error has occurred */
#define SCM_STATUS_MSS_FAIL		0x00004000 /* SCM Status bit: Monitor State is Failed */
#define SCM_STATUS_MSS_SEC		0x00002000 /* SCM Status bit: Monitor State is Secure */
#define SCM_STATUS_RSS_FAIL		0x00000400 /* SCM Status bit: Secure Storage is Failed */
#define SCM_STATUS_RSS_SEC		0x00000200 /* SCM Status bit: Secure Storage is Secure */
#define SCM_STATUS_RSS_INIT		0x00000100 /* SCM Status bit: Secure Storage is Initializing */
#define SCM_STATUS_UNV			0x00000080 /* SCM Status bit: Unique Number Valid */
#define SCM_STATUS_BIG			0x00000040 /* SCM Status bit: Big Endian mode */
#define SCM_STATUS_USK			0x00000020 /* SCM Status bit: Using Secret Key */
#define SCM_STATUS_BAR			0x00000010 /* SCM Status bit: Ram is being blocked */
#define SCM_STATUS_SRS_MASK		0x0000000F /* Bit mask of SRS */
#define SCM_STATUS_SRS_SHIFT		0          /* Number of bits to shift SRS to/from MSb */

#define SCM_STATUS_SRS_RESET		0x0 /* Reset, Zeroise All */
#define SCM_STATUS_SRS_READY		0x1 /* All Ready */
#define SCM_STATUS_SRS_ZBUSY		0x2 /* Zeroize Busy (Partition Only) */
#define SCM_STATUS_SRS_CBUSY		0x3 /* Cipher Busy */
#define SCM_STATUS_SRS_ABUSY		0x4 /* All Busy */
#define SCM_STATUS_SRS_ZDONE		0x5 /* Zeroize Done, Cipher Ready */
#define SCM_STATUS_SRS_CDONE		0x6 /* Cipher Done, Zeroize Ready */
#define SCM_STATUS_SRS_ZDONE2		0x7 /* Zeroize Done, Cipher Busy */
#define SCM_STATUS_SRS_CDONE2		0x8 /* Cipher Done, Zeroize Busy */
#define SCM_STATUS_SRS_ADONE		0xD /* All Done */
#define SCM_STATUS_SRS_FAIL		0xF /* Fail State */

/* Format of the SCM VERSION ID REGISTER */
#define SCM_VER_BPP_MASK		0xFF000000 /* Bytes Per Partition Mask */
#define SCM_VER_BPP_SHIFT		24         /* Bytes Per Partition Shift */
#define SCM_VER_BPCB_MASK		0x001F0000 /* Bytes Per Cipher Block Mask */
#define SCM_VER_BPCB_SHIFT		16         /* Bytes Per Cipher Block Shift */
#define SCM_VER_NP_MASK			0x0000F000 /* Number of Partitions Mask */
#define SCM_VER_NP_SHIFT		12         /* Number of Partitions Shift */
#define SCM_VER_MAJ_MASK		0x00000F00 /* Major Version Mask */
#define SCM_VER_MAJ_SHIFT		8          /* Major Version Shift */
#define SCM_VER_MIN_MASK		0x000000FF /* Minor Version Mask */
#define SCM_VER_MIN_SHIFT		0          /* Minor Version Shift */

/* SCC Hardware version supported by this driver */
#define SCM_MAJOR_VERSION_2			2

/* Format of the CIPHER COMMAND REGISTER */
#define SCM_CCMD_LENGTH_MASK		0xFFF00000 /* Cipher Length Mask */
#define SCM_CCMD_LENGTH_SHIFT		20         /* Cipher Length Shift */
#define SCM_CCMD_OFFSET_MASK		0x000FFF00 /* Block Offset Mask */
#define SCM_CCMD_OFFSET_SHIFT		8          /* Block Offset Shift */
#define SCM_CCMD_PART_MASK		0x000000F0 /* Partition Number Mask */
#define SCM_CCMD_PART_SHIFT		4          /* Partition Number Shift */
#define SCM_CCMD_CCMD_MASK		0x0000000F /* Cipher Command Mask */
#define SCM_CCMD_CCMD_SHIFT		0          /* Cipher Command Shift */

/* Values for SCM_CCMD_CCMD field */
#define SCM_CCMD_AES_DEC_ECB		1 /* Decrypt without Chaining (ECB) */
#define SCM_CCMD_AES_ENC_ECB		3 /* Encrypt without Chaining (ECB) */
#define SCM_CCMD_AES_DEC_CBC		5 /* Decrypt with Chaining (CBC) */
#define SCM_CCMD_AES_ENC_CBC		7 /* Encrypt with Chaining (CBC) */

#define SCM_CCMD_AES			1 /* Use AES Mode */
#define SCM_CCMD_DEC			0 /* Decrypt */
#define SCM_CCMD_ENC			2 /* Encrypt */
#define SCM_CCMD_ECB			0 /* Perform operation without chaining (ECB) */
#define SCM_CCMD_CBC			4 /* Perform operation with chaining (CBC) */

/* Format of the ZEROIZE COMMAND REGISTER */
#define SCM_ZCMD_PART_MASK		0x000000F0  /* Target Partition Mask */
#define SCM_ZCMD_PART_SHIFT		4           /* Target Partition Shift */
#define SCM_ZCMD_CCMD_MASK		0x0000000F  /* Zeroize Command Mask */
#define SCM_ZCMD_CCMD_SHIFT		0           /* Zeroize Command Shift */

/* MASTER ACCESS PERMISSIONS REGISTER */
/* Note that API users should use the FSL_PERM_ defines instead of these */
#define SCM_PERM_NO_ZEROIZE		0x10000000 /* SCM Access Permission: Do not zeroize/deallocate partition on SMN Fail state */
#define SCM_PERM_HD_SUP_DISABLE		0x00000800 /* SCM Access Permission: Ignore Supervisor/User mode in permission determination */
#define SCM_PERM_HD_READ		0x00000400 /* SCM Access Permission: Allow Read Access to  Host Domain */
#define SCM_PERM_HD_WRITE		0x00000200 /* SCM Access Permission: Allow Write Access to  Host Domain */
#define SCM_PERM_HD_EXECUTE		0x00000100 /* SCM Access Permission: Allow Execute Access to  Host Domain */
#define SCM_PERM_TH_READ		0x00000040 /* SCM Access Permission: Allow Read Access to Trusted Host Domain */
#define SCM_PERM_TH_WRITE		0x00000020 /* SCM Access Permission: Allow Write Access to Trusted Host Domain */
#define SCM_PERM_OT_READ		0x00000004 /* SCM Access Permission: Allow Read Access to Other/World Domain */
#define SCM_PERM_OT_WRITE		0x00000002 /* SCM Access Permission: Allow Write Access to Other/World Domain */
#define SCM_PERM_OT_EXECUTE		0x00000001 /* SCM Access Permission: Allow Execute Access to Other/World Domain */
#define SCM_PERM_MASK			0xC0000F67 /* Valid bits that can be set in the Permissions register */

#define	FSL_PERM_NO_ZEROIZE		0x80000000 /*! SCM Access Permission: Do not zeroize/deallocate partition on SMN Fail state */
#define	FSL_PERM_TRUSTED_KEY_READ	0x40000000 /*! SCM Access Permission: Enforce trusted key read in  */

/* Zeroize Command register definitions */
#define ZCMD_DEALLOC_PART		3	   /* Deallocate Partition */
#define Z_INT_EN			0x00000002 /* Zero Interrupt Enable */

/* SCM Partition Owners Register */
#define SCM_POWN_SHIFT			2 /* Number of bits to shift partition number to get to its field. */
#define SCM_POWN_MASK			3 /* Mask for a field once the register has been shifted. */
#define SCM_POWN_PART_FREE		0 /* Partition is free */
#define SCM_POWN_PART_UNUSABLE		1 /* Partition is unable to be allocated */
#define SCM_POWN_PART_OTHER		2 /* Partition is owned by another master */
#define SCM_POWN_PART_OWNED		3 /* Partition is owned by this master */

/* SCM Partitions Engaged Register */
#define SCM_PENG_SHIFT			1 /* Number of bits to shift partition number to get to its field. */
#define SCM_PENG_ENGAGED		1 /* Engaged value for a field once the register has been shifted. */

/* SMN Command Register Definitions (SMN_COMMAND_REG) */
#define SCC_SMN_COMMAND_ZEROS_MASK	0xfffffff0 /* These bits are unimplemented or reserved */
#define SCC_SMN_COMMAND_CLR_INTR	0x8        /* Clear SMN Interrupt */
#define SCC_SMN_COMMAND_CAR_BIT_BANK	0x4        /* Clear SMN Bit Bank */
#define SCC_SMN_COMMAND_EN_INTR		0x2        /* Enable SMN Interrupts */
#define SCC_SMN_COMMAND_SET_SOFT_ALARM	0x1        /* Set Software Alarm */

enum mxc_scc_state {
	SCC_STATE_OK,
	SCC_STATE_UNIMPLEMENTED,
	SCC_STATE_FAILED
};

struct scc_iram_buf {
	void			*vaddr;
	dma_addr_t		paddr;
	unsigned int		size;
	struct debugfs_blob_wrapper blob;
	struct dentry		*dentry;
};

struct mxc_scc {
	struct device		*dev;
	void __iomem		*base;
	struct clk		*clk;
	bool			hw_busy;
	spinlock_t		lock;
	struct crypto_queue	queue;
	struct crypto_async_request *req;

	struct gen_pool		*iram_pool;
	struct scc_iram_buf	iram;
	int			block_size_bytes;
	int			black_ram_size_blocks;
	int			memory_size_bytes;

	void __iomem		*red_memory;
	void __iomem		*black_memory;
	dma_addr_t		black_hw_addr;

	struct dentry		*debugfs_root;
};

struct mxc_scc_ctx {
	struct mxc_scc		*scc;
	struct scatterlist	*sg_src;
	size_t			src_nents;
	struct scatterlist	*sg_dst;
	size_t			dst_nents;
	unsigned int		offset;
	unsigned int		size;
	unsigned int		ctrl;
	unsigned int		part;
};

struct mxc_scc_crypto_tmpl {
	struct mxc_scc *scc;
	struct crypto_alg alg;
};

static bool mxc_scc_host_owns_part(struct mxc_scc *scc, unsigned int part)
{
	unsigned int value;

	if (part < scc->black_ram_size_blocks) {
		/* Check the partition owners register */
		value = readl(scc->base + SCC_SCM_PART_OWNERS);
		if (((value >> (part * SCM_POWN_SHIFT)) & SCM_POWN_MASK)
		    == SCM_POWN_PART_OWNED)
			return true;
	}

	return false;
}

static bool mxc_scc_part_engaged(struct mxc_scc *scc, unsigned int part)
{
	unsigned int value;

	if (part < scc->black_ram_size_blocks) {
		/* Check the partition engaged register */
		value = readl(scc->base + SCC_SCM_PART_ENGAGED);
		if (((value >> (part * SCM_PENG_SHIFT)) & 0x1)
		    == SCM_PENG_ENGAGED)
			return true;
	}

	return false;
}

/**
 * Allocate a partition of secure memory
 *
 * @param       smid_value  Value to use for the SMID register.  Must be 0 for
 *                          kernel mode access.
 * @param[out]  part_no     (If successful) Assigned partition number.
 * @param[out]  part_base   Kernel virtual address of the partition.
 * @param[out]  part_phys   Physical address of the partition.
 *
 * @return
 */
static int scc_allocate_partition(struct mxc_scc *scc, unsigned int smid, int *part)
{
	unsigned long flags;
	int local_part;
	int ret = 0;
	unsigned int val;
	unsigned int i = 0;

	local_part = -1;

	/* ACQUIRE LOCK to prevent others from using crypto or acquiring a
	 * partition.  Note that crypto operations could take a long time, so the
	 * calling process could potentially spin for some time.
	 */
	spin_lock_irqsave(&scc->lock, flags);
	do {
		/* Find current state of partition ownership */
		val = readl(scc->base + SCC_SCM_PART_OWNERS);

		/* Search for a free one */
		for (i = 0; i < scc->black_ram_size_blocks; i++) {
			if (((val >> (SCM_POWN_SHIFT * i))
			    & SCM_POWN_MASK) == SCM_POWN_PART_FREE)
				break;
		}

		if (i == local_part) {
			/* found this one last time and failed to allocated */
			dev_dbg(scc->dev, "Partition %d cannot be allocated\n", i);
			goto out;
		}

		if (i >= scc->black_ram_size_blocks) {
			ret = -ENOMEM;
			goto out;
		}

		dev_dbg(scc->dev,
			"Attempting to allocate partition %i, owners:%08x\n",
			i, readl(scc->base + SCC_SCM_PART_OWNERS));

		local_part = i;
		/* Store SMID to grab a partition */
		writel(smid, scc->base + SCC_SCM_SMID(local_part));
		mdelay(2);

		/* Now make sure it is ours... ? */
		val = readl(scc->base + SCC_SCM_PART_OWNERS);

		if (((val >> (SCM_POWN_SHIFT * (local_part))) &
		      SCM_POWN_MASK) != SCM_POWN_PART_OWNED)
			continue;

		*part = local_part;

		break;
	} while (1);

	dev_dbg(scc->dev, "Part owners: %08x, engaged: %08x\n",
		val, readl(scc->base + SCC_SCM_PART_ENGAGED));

	dev_dbg(scc->dev, "MAP for part %d: %08x\n", local_part,
		readl(scc->base + SCC_SCM_ACC(local_part)));
out:

	spin_unlock_irqrestore(&scc->lock, flags);

	return ret;
}

static int scc_release_partition(struct mxc_scc_ctx *ctx)
{
	struct mxc_scc *scc = ctx->scc;

	dev_dbg(scc->dev, "Attempting to release partition %i, owners:%08x\n",
		ctx->part, readl(scc->base + SCC_SCM_PART_OWNERS));

	/* check that the partition is ours to de-establish */
	if (!mxc_scc_host_owns_part(scc, ctx->part))
		return -EINVAL;

	/* TODO: The state of the zeroize engine (SRS field in the Command Status
	 * Register) should be examined before issuing the zeroize command here.
	 * To make the driver thread-safe, a lock should be taken out before
	 * issuing the check and released after the zeroize command has been
	 * issued.
	 */

	/* Zero the partition to release it */
	writel((ctx->part << SCM_ZCMD_PART_SHIFT) |
		(ZCMD_DEALLOC_PART << SCM_ZCMD_CCMD_SHIFT),
		scc->base + SCC_SCM_ZCMD);
	mdelay(2);

	dev_dbg(scc->dev, "done releasing partition %i, owners:%08x\n",
		ctx->part, readl(scc->base + SCC_SCM_PART_OWNERS));

	/* Check that the de-assignment went correctly */
	if (mxc_scc_host_owns_part(scc, ctx->part))
		return -EIO;

#ifdef DEBUG
	print_hex_dump(KERN_ERR,
		       "memory after release@" __stringify(__LINE__) ": ",
		       DUMP_PREFIX_ADDRESS, 16, 4,
		       scc->red_memory,
		       ctx->size, 1);
#endif

	return 0;
}

/**
 * @param part_base (kernel) Virtual
 * @param UMID NULL, or 16-byte UMID for partition security
 * @param permissions ORed values from fsl_shw_permission_t which
 * will be used as initial partiition permissions.
 */
int scc_engage_partition(struct mxc_scc *scc, unsigned int part,
			 const unsigned char *UMID, unsigned int perms)
{
	unsigned char *UMID_base = scc->red_memory + 0x10;
	unsigned int *MAP_base = scc->red_memory;
	int i;

	if (!(readl(scc->base + SCC_SCM_SMID(part)) == 0) ||
	    !mxc_scc_host_owns_part(scc, part) || mxc_scc_part_engaged(scc, part))
		return -EBUSY;

	if (UMID) {
		for (i = 0; i < 16; i++)
			UMID_base[i] = UMID[i];
	}

	MAP_base[0] = perms;

	usleep_range(20, 21);

	/* Check that the partition was engaged correctly, and that it
	 * has the proper permissions.
	 */
	if (perms != readl(scc->base + SCC_SCM_ACC(part)) ||
	    (!mxc_scc_part_engaged(scc, part)))
		return -EIO;

	return 0;
}

/* Crypt a region into/out of secure memory
 *
 * @param   part_base    Kernel virtual address of the partition.
 * @param   offset_bytes Offset from the start of the partition to store the
 *                       plaintext data.
 * @param   byte_counts  Length of the region (octets).
 * @param   black_data   Physical location of the encrypted data.
 * @param   IV           Value to use for the IV.
 * @param   cypher_mode  Cyphering mode to use, specified by type
 *                       #scc_cypher_mode_t
 *
 * @return  SCC_RET_OK if successful.
 */
static int scc_crypt_region(struct mxc_scc_ctx *ctx,
			    struct ablkcipher_request *ablkreq)
{
	struct mxc_scc *scc = ctx->scc;
	int offset_blocks = ctx->offset / scc->block_size_bytes;
	int block_count = ctx->size / scc->block_size_bytes;
	unsigned long flags;
	int ret = 0;

	ctx->ctrl |= ((offset_blocks << SCM_CCMD_OFFSET_SHIFT) |
		       (ctx->part << SCM_CCMD_PART_SHIFT));

	dev_dbg(scc->dev, "Received crypt request. SCM_C_BLACK_ST_REG: %p, command: %08x, length: %i (part: %08x,offset: %i)\n",
		scc->red_memory, ctx->ctrl, ctx->size, ctx->part, offset_blocks);

	/* ACQUIRE LOCK to prevent others from using crypto or releasing slot */
	spin_lock_irqsave(&scc->lock, flags);

	writel((uint32_t)scc->black_hw_addr, scc->base + SCC_SCM_C_BLACK_ST);

	/* Set modes and kick off the decryption */

	/* In length register, 0 means 1, etc. */
	ctx->ctrl |= (block_count - 1) << SCM_CCMD_LENGTH_SHIFT;

	/* set modes and kick off the operation */
	writel(ctx->ctrl, scc->base + SCC_SCM_CCMD);

	dev_dbg(scc->dev, "Decrypted %d bytes\n", ctx->size);

	spin_unlock_irqrestore(&scc->lock, flags);

	return ret;
}

static int mxc_scc_get_data(struct mxc_scc_ctx *ctx,
			    struct crypto_async_request *req)
{
	struct ablkcipher_request *ablkreq = ablkcipher_request_cast(req);
	struct mxc_scc *scc = ctx->scc;
	size_t len;
	void __iomem *from;

	if (!(ctx->ctrl & SCM_CCMD_ENC))
		from = scc->red_memory;
	else
		from = scc->black_memory;

	dev_dbg(scc->dev, "pcopy: from 0x%p %d bytes\n", from,
		ctx->dst_nents * 8);

	len = sg_pcopy_from_buffer(ablkreq->dst, ctx->dst_nents,
				   from, ctx->size, ctx->offset);
	if (!len) {
		dev_err(scc->dev, "pcopy err from 0x%p (len=%d)\n", from, len);
		return -EINVAL;
	}

	ctx->offset += len;

	if (ctx->offset < ablkreq->nbytes)
		return -EINPROGRESS;

	return 0;
}

static int mxc_scc_ablkcipher_req_init(struct ablkcipher_request *req,
				       struct mxc_scc_ctx *ctx)
{
	struct mxc_scc *scc = ctx->scc;
	int nents;

	nents = sg_nents_for_len(req->src, req->nbytes);
	if (nents < 0) {
		dev_err(scc->dev, "Invalid number of src SC");
		return nents;
	}
	ctx->src_nents = nents;

	nents = sg_nents_for_len(req->dst, req->nbytes);
	if (nents < 0) {
		dev_err(scc->dev, "Invalid number of dst SC");
		return nents;
	}
	ctx->dst_nents = nents;

	ctx->size = 0;
	ctx->offset = 0;

	return 0;
}

static int mxc_scc_ablkcipher_req_complete(struct crypto_async_request *req,
					   struct mxc_scc_ctx *ctx,
					   int result)
{
	struct ablkcipher_request *ablkreq = ablkcipher_request_cast(req);
	struct mxc_scc *scc = ctx->scc;
	int ret;

	scc->req = NULL;

	if (ctx->ctrl & SCM_CCMD_CBC && ablkreq)
		memcpy(&ablkreq->info, scc->base + SCC_SCM_AES_CBC_IV0,
		       scc->block_size_bytes);

	req->complete(req, result);
	scc->hw_busy = false;

	writel(0x0, scc->base + SCC_SCM_INTR_CTL);

	ret = scc_release_partition(ctx);
	if (ret)
		dev_err(scc->dev, "release error %d", ret);

	dma_free_coherent(scc->dev, ablkreq->nbytes, scc->black_memory,
			  scc->black_hw_addr);

	return 0;
}

static int mxc_scc_put_data(struct mxc_scc_ctx *ctx,
			     struct ablkcipher_request *req)
{
	size_t len = req->nbytes - ctx->offset;
	struct mxc_scc *scc = ctx->scc;
	void __iomem *to;

	if (!(ctx->ctrl & SCM_CCMD_ENC))
		to = scc->black_memory;
	else
		to = scc->red_memory;

	if (ctx->ctrl & SCM_CCMD_CBC && req->info)
		memcpy(scc->base + SCC_SCM_AES_CBC_IV0, req->info,
		       scc->block_size_bytes);

	len = sg_pcopy_to_buffer(req->src, ctx->src_nents,
				 to, len, ctx->offset);
	if (!len) {
		dev_err(scc->dev, "pcopy err to 0x%p (len=%d)\n",
			to, len);
		return -EINVAL;
	}

	ctx->size = len;

#ifdef DEBUG
	dev_dbg(scc->dev, "copied %d bytes to 0x%p\n", len, to);
	print_hex_dump(KERN_ERR,
		       "init vector0@" __stringify(__LINE__) ": ",
		       DUMP_PREFIX_ADDRESS, 16, 4,
		       scc->base + SCC_SCM_AES_CBC_IV0, scc->block_size_bytes,
		       1);

	print_hex_dump(KERN_ERR,
		       "data to crypt@" __stringify(__LINE__) ": ",
		       DUMP_PREFIX_ADDRESS, 16, 4,
		       to, ctx->size, 1);
#endif

	return 0;
}

static void mxc_scc_ablkcipher_next(struct mxc_scc_ctx *ctx,
				    struct crypto_async_request *req)
{
	struct ablkcipher_request *ablkreq = ablkcipher_request_cast(req);
	struct mxc_scc *scc = ctx->scc;
	int err;

	dev_dbg(scc->dev, "dispatch request (nbytes=%d, src=%p, dst=%p)\n",
		ablkreq->nbytes, ablkreq->src, ablkreq->dst);

	err = readl(scc->base + SCC_SCM_ERROR_STATUS);

	err = mxc_scc_put_data(ctx, ablkreq);
	if (err) {
		mxc_scc_ablkcipher_req_complete(req, ctx, err);
		return;
	}

	err = readl(scc->base + SCC_SCM_ERROR_STATUS);

	/* clear interrupt control registers */
	writel(SCC_SMN_COMMAND_CLR_INTR, scc->base + SCC_SMN_COMMAND);

	dev_dbg(scc->dev, "Process %d block(s)",
		ctx->size / ctx->scc->block_size_bytes);

	err = scc_crypt_region(ctx, ablkreq);
	if (err) {
		dev_err(scc->dev, "crypt error %d", err);
		return;
	}
}

static irqreturn_t mxc_scc_int(int irq, void *priv)
{
	struct crypto_async_request *req;
	struct mxc_scc_ctx *ctx;
	struct mxc_scc *scc = priv;
	unsigned int status;
	unsigned int done = 0;
	unsigned int crypto_status;
	unsigned int status_srs;
	int i = 0;
	int ret;

	status = readl(scc->base + SCC_SMN_STATUS);
	if (status & SMN_STATUS_SMN_STATUS_IRQ)
		writel(SCC_SMN_COMMAND_CLR_INTR, scc->base + SCC_SMN_COMMAND);

	dev_dbg(scc->dev, "%s status !: %08x\n", __func__, status);

	/* clear interrupt control registers */
	ret = readl(scc->base + SCC_SCM_INTR_CTL);
	dev_dbg(scc->dev, "interrupt!: %08x\n", ret);

	/* check for completion by polling */
	do {
		crypto_status = readl(scc->base + SCC_SCM_STATUS);
		status_srs = (crypto_status & SCM_STATUS_SRS_MASK) >> SCM_STATUS_SRS_SHIFT;

		dev_dbg(scc->dev, "%s crypto_status !: %08x\n",
			__func__, crypto_status);
		done = ((status != SCM_STATUS_SRS_ZBUSY) &&
			(status != SCM_STATUS_SRS_CBUSY) &&
			(status != SCM_STATUS_SRS_ABUSY));

		if (done)
			break;
		/* TODO: shorten this delay */
		usleep_range(999, 1000);
	} while (i++ < 100);

	dev_dbg(scc->dev, "Polled status %d times\n", i);

	if (crypto_status & SCM_STATUS_ERR || !done) {
		crypto_status = readl(scc->base + SCC_SCM_ERROR_STATUS);
		dev_err(scc->dev, "ERR_STATUS: 0x%x\n", crypto_status);
		//ret = -EBUSY;
	}

	req = scc->req;
	if (req && done) {
		ctx = crypto_tfm_ctx(req->tfm);
		ret = mxc_scc_get_data(ctx, req);
		if (ret != -EINPROGRESS)
			mxc_scc_ablkcipher_req_complete(req, ctx, ret);
		else
			mxc_scc_ablkcipher_next(ctx, req);
	}

	return IRQ_HANDLED;
}

static int mxc_scc_cra_init(struct crypto_tfm *tfm)
{
	struct mxc_scc_ctx *ctx = crypto_tfm_ctx(tfm);
	struct crypto_alg *alg = tfm->__crt_alg;
	struct mxc_scc_crypto_tmpl *algt;

	algt = container_of(alg, struct mxc_scc_crypto_tmpl, alg);

	ctx->scc = algt->scc;

	return 0;
}

static void mxc_scc_dequeue_req_unlocked(struct mxc_scc_ctx *ctx)
{
	struct crypto_async_request *req, *backlog;

	if (ctx->scc->hw_busy)
		return;

	spin_lock_bh(&ctx->scc->lock);
	backlog = crypto_get_backlog(&ctx->scc->queue);
	req = crypto_dequeue_request(&ctx->scc->queue);
	ctx->scc->req = req;
	ctx->scc->hw_busy = true;
	spin_unlock_bh(&ctx->scc->lock);

	if (!req)
		return;

	if (backlog)
		backlog->complete(backlog, -EINPROGRESS);

	mxc_scc_ablkcipher_next(ctx, req);
}

static int mxc_scc_queue_req(struct mxc_scc_ctx *ctx,
			     struct crypto_async_request *req)
{
	int ret;

	spin_lock_bh(&ctx->scc->lock);
	ret = crypto_enqueue_request(&ctx->scc->queue, req);
	spin_unlock_bh(&ctx->scc->lock);

	if (ret != -EINPROGRESS)
		return ret;

	mxc_scc_dequeue_req_unlocked(ctx);

	return -EINPROGRESS;
}

static int mxc_scc_aes_op(struct mxc_scc_ctx *ctx,
			  struct ablkcipher_request *req)
{
	struct mxc_scc *scc = ctx->scc;
	int err;

	err = mxc_scc_ablkcipher_req_init(req, ctx);
	if (err)
		return err;

	err = scc_allocate_partition(scc, 0, &ctx->part);
	if (err) {
		dev_dbg(scc->dev, "error on allocate: %d\n", err);
		return err;
	}

	scc->red_memory = scc->iram.vaddr + ctx->part * scc->memory_size_bytes;
	scc->black_memory = dma_alloc_coherent(scc->dev, req->nbytes,
					   &scc->black_hw_addr, GFP_KERNEL);
	if (!scc->black_memory) {
		dev_err(scc->dev, "could not register algorithms");
		return -ENOMEM;
	}

	scc_engage_partition(scc, ctx->part, NULL,
			     SCM_PERM_TH_READ | SCM_PERM_TH_WRITE |
			     SCM_PERM_HD_READ | SCM_PERM_HD_WRITE);
	if (err) {
		dev_dbg(scc->dev, "error on engage: %d\n", err);
		return err;
	}

	writel(0x1, scc->base + SCC_SCM_INTR_CTL);

	return mxc_scc_queue_req(ctx, &req->base);
}

static int mxc_scc_ecb_aes_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *cipher = crypto_ablkcipher_reqtfm(req);
	struct mxc_scc_ctx *ctx = crypto_ablkcipher_ctx(cipher);

	ctx->ctrl = SCM_CCMD_AES_ENC_ECB;

	return mxc_scc_aes_op(ctx, req);
}

static int mxc_scc_ecb_aes_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *cipher = crypto_ablkcipher_reqtfm(req);
	struct mxc_scc_ctx *ctx = crypto_ablkcipher_ctx(cipher);

	ctx->ctrl = SCM_CCMD_AES_DEC_ECB;

	return mxc_scc_aes_op(ctx, req);
}

static int mxc_scc_cbc_aes_encrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *cipher = crypto_ablkcipher_reqtfm(req);
	struct mxc_scc_ctx *ctx = crypto_ablkcipher_ctx(cipher);

	ctx->ctrl = SCM_CCMD_AES_ENC_CBC;

	return mxc_scc_aes_op(ctx, req);
}

static int mxc_scc_cbc_aes_decrypt(struct ablkcipher_request *req)
{
	struct crypto_ablkcipher *cipher = crypto_ablkcipher_reqtfm(req);
	struct mxc_scc_ctx *ctx = crypto_ablkcipher_ctx(cipher);

	ctx->ctrl = SCM_CCMD_AES_DEC_CBC;

	return mxc_scc_aes_op(ctx, req);
}

static int mxc_scc_get_config(struct mxc_scc *scc)
{
	unsigned int version;

	version = readl(scc->base + SCC_SCM_VERSION);
	dev_dbg(scc->dev, "SCM version is 0x%08x\n", version);

	/* save sizes and versions information for later use */
	scc->block_size_bytes = 16;	/* BPCP ? */
	scc->black_ram_size_blocks = 1 +
		((version & SCM_VER_NP_MASK) >> SCM_VER_NP_SHIFT);
	scc->memory_size_bytes = 1 << ((version & SCM_VER_BPP_MASK) >>
		  SCM_VER_BPP_SHIFT);
	version = (version & SCM_VER_MAJ_MASK) >> SCM_VER_MAJ_SHIFT;

	if (version != SCM_MAJOR_VERSION_2)
		return -EINVAL;

	return 0;
}

static enum mxc_scc_state mxc_scc_get_state(struct mxc_scc *scc)
{
	enum mxc_scc_state state;
	int status;

	status = readl(scc->base + SCC_SMN_STATUS) &
		       SCC_SMN_STATUS_STATE_MASK;

	/* If in Health Check, try to bringup to secure state */
	if (status & SCC_SMN_STATE_HEALTH_CHECK) {
		/*
		 * Write a simple algorithm to the Algorithm Sequence
		 * Checker (ASC)
		 */
		writel(0xaaaa, scc->base + SCC_SMN_SEQ_START);
		writel(0x5555, scc->base + SCC_SMN_SEQ_END);
		writel(0x5555, scc->base + SCC_SMN_SEQ_CHECK);

		status = readl(scc->base + SCC_SMN_STATUS) &
			       SCC_SMN_STATUS_STATE_MASK;
	}

	/*
	 * State should be SECURE or NON_SECURE for operation of the part.  If
	 * FAIL, mark failed (i.e. limited access to registers).  Any other
	 * state, mark unimplemented, as the SCC is unuseable.
	 */
	switch (status) {
	case SCC_SMN_STATE_NON_SECURE:
	case SCC_SMN_STATE_SECURE:
		state = SCC_STATE_OK;
		break;
	case SCC_SMN_STATE_FAIL:
		state = SCC_STATE_FAILED;
		break;
	default:
		state = SCC_STATE_UNIMPLEMENTED;
		break;
	}

	return state;
}

static struct mxc_scc_crypto_tmpl scc_ecb_aes = {
	.alg = {
		.cra_name = "ecb(aes)",
		.cra_driver_name = "ecb-aes-scc",
		.cra_priority = 300,
		.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct mxc_scc_ctx),
		.cra_alignmask = 0,
		.cra_type = &crypto_ablkcipher_type,
		.cra_module = THIS_MODULE,
		.cra_init = mxc_scc_cra_init,
		.cra_u.ablkcipher = {
			.encrypt = mxc_scc_ecb_aes_encrypt,
			.decrypt = mxc_scc_ecb_aes_decrypt,
		}
	}
};

static struct mxc_scc_crypto_tmpl scc_cbc_aes = {
	.alg = {
		.cra_name = "cbc(paes)",
		.cra_driver_name = "cbc-aes-scc",
		.cra_flags = CRYPTO_ALG_TYPE_ABLKCIPHER | CRYPTO_ALG_ASYNC,
		.cra_blocksize = AES_BLOCK_SIZE,
		.cra_ctxsize = sizeof(struct mxc_scc_ctx),
		.cra_alignmask = 0,
		.cra_type = &crypto_ablkcipher_type,
		.cra_module = THIS_MODULE,
		.cra_init = mxc_scc_cra_init,
		.cra_u.ablkcipher = {
			.encrypt = mxc_scc_cbc_aes_encrypt,
			.decrypt = mxc_scc_cbc_aes_decrypt,
			.ivsize	= AES_BLOCK_SIZE,
		}
	}
};

static struct mxc_scc_crypto_tmpl *scc_crypto_algs[] = {
	&scc_ecb_aes,
	&scc_cbc_aes,
};

static int mxc_scc_crypto_register(struct mxc_scc *scc)
{
	int i;
	int err = 0;

	for (i = 0; i < ARRAY_SIZE(scc_crypto_algs); i++) {
		scc_crypto_algs[i]->scc = scc;
		err = crypto_register_alg(&scc_crypto_algs[i]->alg);
		if (err)
			goto err_out;
	}

	return 0;

err_out:
	while (--i >= 0)
		crypto_unregister_alg(&scc_crypto_algs[i]->alg);

	return err;
}

static void mxc_scc_crypto_unregister(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(scc_crypto_algs); i++)
		crypto_unregister_alg(&scc_crypto_algs[i]->alg);
}

#define SCM_RD_DELAY	1000000 /* in nanoseconds */
#define SEC_TO_NANOSEC  1000000000 /*Second to nanoseconds */
static void scc_init_iram(struct mxc_scc *scc)
{
	unsigned int reg_value;
	unsigned int reg_mask = 0;
	unsigned char *UMID_base;
	unsigned int *MAP_base;
	unsigned int part;
	struct timespec stime;
	struct timespec curtime;
	long scm_rd_timeout = 0;
	long cur_ns = 0;
	long start_ns = 0;
	int i;
	int scc_partno;

	scc->iram.size = scc->black_ram_size_blocks * scc->memory_size_bytes;
	scc->iram.vaddr = gen_pool_dma_alloc(scc->iram_pool, scc->iram.size,
					     &scc->iram.paddr);
	if (!scc->iram.vaddr) {
		dev_warn(scc->dev, "unable to alloc iram\n");
	} else {
		scc->debugfs_root = debugfs_create_dir("scc2", NULL);
		if (!scc->debugfs_root)
			dev_warn(scc->dev, "failed to create debugfs root\n");

		memset(scc->iram.vaddr, 0, scc->iram.size);
		scc->iram.blob.data = scc->iram.vaddr;
		scc->iram.blob.size = scc->iram.size;
		scc->iram.dentry = debugfs_create_blob("iram", 0644,
						       scc->debugfs_root,
						       &scc->iram.blob);
	}

	/* Wait for any running SCC operations to finish or fail */
	getnstimeofday(&stime);
	do {
		reg_value = readl(scc->base + SCC_SCM_STATUS);
		getnstimeofday(&curtime);
		if (curtime.tv_nsec > stime.tv_nsec) {
			scm_rd_timeout = curtime.tv_nsec - stime.tv_nsec;
		} else {
			/* Converted second to nanosecond and add to
			 * nsec when current nanosec is less than
			 * start time nanosec.
			 */
			cur_ns = (curtime.tv_sec * SEC_TO_NANOSEC) +
			curtime.tv_nsec;
			start_ns = (stime.tv_sec * SEC_TO_NANOSEC) +
				stime.tv_nsec;
			scm_rd_timeout = cur_ns - start_ns;
		}
	} while (((reg_value & SCM_STATUS_SRS_MASK) != SCM_STATUS_SRS_READY) &&
		((reg_value & SCM_STATUS_SRS_MASK) != SCM_STATUS_SRS_FAIL));

	/* Check for failures */
	if ((reg_value & SCM_STATUS_SRS_MASK) != SCM_STATUS_SRS_READY) {
		/* Special message for bad secret key fuses */
		if (reg_value & SCM_STATUS_KST_BAD_KEY)
			dev_err(scc->dev, "invalid scc key fuse pattern\n");
		else
			dev_err(scc->dev, "secure ram failure\n");

		return;
	}

	scm_rd_timeout = 0;

	for (part = 0; part < scc->black_ram_size_blocks; part++) {
		reg_value = (((part << SCM_ZCMD_PART_SHIFT) &
			SCM_ZCMD_PART_MASK) | ((0x03 << SCM_ZCMD_CCMD_SHIFT) &
			SCM_ZCMD_CCMD_MASK));
		writel(reg_value, scc->base + SCC_SCM_ZCMD);
		udelay(1);
		/* Wait for zeroization to complete */
		getnstimeofday(&stime);
		do {
			reg_value = readl(scc->base + SCC_SCM_STATUS);
			getnstimeofday(&curtime);
			if (curtime.tv_nsec > stime.tv_nsec) {
				scm_rd_timeout = curtime.tv_nsec -
				stime.tv_nsec;
			} else {
				/* Converted second to nanosecond and add to
				 * nsec when current nanosec is less than
				 * start time nanosec.
				 */
				cur_ns = (curtime.tv_sec * SEC_TO_NANOSEC) +
				curtime.tv_nsec;
				start_ns = (stime.tv_sec * SEC_TO_NANOSEC) +
					stime.tv_nsec;
				scm_rd_timeout = cur_ns - start_ns;
			}
		} while (((reg_value & SCM_STATUS_SRS_MASK) !=
		SCM_STATUS_SRS_READY) && ((reg_value & SCM_STATUS_SRS_MASK) !=
		SCM_STATUS_SRS_FAIL) && (scm_rd_timeout <= SCM_RD_DELAY));

		if (scm_rd_timeout > SCM_RD_DELAY)
			dev_err(scc->dev, "SCM Status Register Read timeout for Partition No: %d",
				part);

		if ((reg_value & SCM_STATUS_SRS_MASK) != SCM_STATUS_SRS_READY)
			break;
	}

	/* Check all expected partitions released */
	reg_value = readl(scc->base + SCC_SCM_PART_OWNERS);
	if ((reg_value & reg_mask) != 0) {
		dev_err(scc->dev, "failed to release iram partitons\n");
		return;
	}

	scc_partno = scc->black_ram_size_blocks - (SZ_16K / scc->memory_size_bytes);

	reg_mask = 0;
	scm_rd_timeout = 0;
	/* Allocate remaining partitions for general use */
	for (part = 0; part < scc_partno; part++) {
		/* Supervisor mode claims a partition for it's own use
		 * by writing zero to SMID register.
		 */
		writel(0, scc->base + (SCC_SCM_SMID(part)));

		/* Wait for any zeroization to complete */
		getnstimeofday(&stime);
		do {
			reg_value = readl(scc->base + SCC_SCM_STATUS);
			getnstimeofday(&curtime);
			if (curtime.tv_nsec > stime.tv_nsec) {
				scm_rd_timeout = curtime.tv_nsec -
				stime.tv_nsec;
			} else {
				/* Converted second to nanosecond and add to
				 * nsec when current nanosec is less than
				 * start time nanosec.
				 */
				cur_ns = (curtime.tv_sec * SEC_TO_NANOSEC) +
				curtime.tv_nsec;
				start_ns = (stime.tv_sec * SEC_TO_NANOSEC) +
					stime.tv_nsec;
				scm_rd_timeout = cur_ns - start_ns;
			}
		} while (((reg_value & SCM_STATUS_SRS_MASK) !=
			 SCM_STATUS_SRS_READY) &&
			 ((reg_value & SCM_STATUS_SRS_MASK) !=
			 SCM_STATUS_SRS_FAIL) &&
			 (scm_rd_timeout <= SCM_RD_DELAY));

		if (scm_rd_timeout > SCM_RD_DELAY)
			dev_err(scc->dev, "SCM Status Register Read timeout for Partition No:%d",
				part);

		if ((reg_value & SCM_STATUS_SRS_MASK) != SCM_STATUS_SRS_READY)
			break;
		/* Set UMID=0 and permissions for universal data rw access */
		MAP_base = scc->iram.vaddr +
			(uint32_t)(part * scc->memory_size_bytes);
		UMID_base = (uint8_t *)MAP_base + 0x10;
		for (i = 0; i < 16; i++)
			UMID_base[i] = 0;

		MAP_base[0] = (SCM_PERM_NO_ZEROIZE | SCM_PERM_HD_SUP_DISABLE |
			SCM_PERM_HD_READ | SCM_PERM_HD_WRITE |
			SCM_PERM_HD_EXECUTE | SCM_PERM_TH_READ |
			SCM_PERM_TH_WRITE);
		reg_mask |= (3 << (2 * (part)));
	}

	/* Check all expected partitions allocated */
	reg_value = readl(scc->base + SCC_SCM_PART_OWNERS);
	if ((reg_value & reg_mask) != reg_mask) {
		dev_err(scc->dev, "failed to acquire iram partition\n");
		return;
	}

	/* we are done if this is MX51, since no sharing of IRAM and SCC_RAM */
	dev_info(scc->dev, "IRAM partitions ready\n");
}

static int mxc_scc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct mxc_scc *scc;
	enum mxc_scc_state state;
	struct gen_pool *pool;
	int irq;
	int ret;
	int i;

	scc = devm_kzalloc(dev, sizeof(*scc), GFP_KERNEL);
	if (!scc)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	scc->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(scc->base))
		return PTR_ERR(scc->base);

	scc->clk = devm_clk_get(&pdev->dev, "ipg");
	if (IS_ERR(scc->clk)) {
		dev_err(dev, "Could not get ipg clock\n");
		return PTR_ERR(scc->clk);
	}

	ret = clk_prepare_enable(scc->clk);
	if (ret)
		return ret;

	/* clear interrupt control registers */
	writel(0, scc->base + SCC_SCM_INTR_CTL);

	/* clear error status register */
	readl(scc->base + SCC_SCM_ERROR_STATUS);
	writel(0x0, scc->base + SCC_SCM_ERROR_STATUS);

	writel(SCC_SMN_COMMAND_CLR_INTR |
	       SCC_SMN_COMMAND_EN_INTR,
	       scc->base + SCC_SMN_COMMAND);

	scc->dev = dev;
	platform_set_drvdata(pdev, scc);

	/* Get IRAM pool from device tree or platform data */
	pool = of_gen_pool_get(np, "iram", 0);
	if (!pool) {
		dev_err(&pdev->dev, "iram pool not available\n");
		return -ENOMEM;
	}
	scc->iram_pool = pool;

	ret = mxc_scc_get_config(scc);
	if (ret)
		goto err_out;

	state = mxc_scc_get_state(scc);
	if (state != SCC_STATE_OK) {
		dev_err(dev, "SCC in unusable state %d\n", state);
		ret = -EINVAL;
		goto err_out;
	}

	spin_lock_init(&scc->lock);
	/* FIXME: calculate queue from RAM slots */
	crypto_init_queue(&scc->queue, 50);

	for (i = 0; i < 2; i++) {
		irq = platform_get_irq(pdev, i);
		if (irq < 0) {
			dev_err(dev, "failed to get irq resource: %d\n", irq);
			ret = irq;
			goto err_out;
		}

		ret = devm_request_threaded_irq(dev, irq, NULL,
						mxc_scc_int, IRQF_ONESHOT,
						dev_name(dev), scc);
		if (ret)
			goto err_out;
	}

	scc_init_iram(scc);

	ret = mxc_scc_crypto_register(scc);
	if (ret) {
		dev_err(dev, "could not register algorithms");
		goto err_out;
	}

	dev_info(dev, "registered successfully.\n");

	return 0;

err_out:
	clk_disable_unprepare(scc->clk);

	return ret;
}

static int mxc_scc_remove(struct platform_device *pdev)
{
	struct mxc_scc *scc = platform_get_drvdata(pdev);

	mxc_scc_crypto_unregister();

	clk_disable_unprepare(scc->clk);

	return 0;
}

static const struct of_device_id mxc_scc_dt_ids[] = {
	{ .compatible = "fsl,imx51-scc", .data = NULL, },
	{ .compatible = "fsl,imx53-scc", .data = NULL, },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, mxc_scc_dt_ids);

static struct platform_driver mxc_scc_driver = {
	.probe	= mxc_scc_probe,
	.remove	= mxc_scc_remove,
	.driver	= {
		.name		= "mxc-scc2",
		.of_match_table	= mxc_scc_dt_ids,
	},
};

module_platform_driver(mxc_scc_driver);
MODULE_AUTHOR("Michael Grzeschik <kernel@pengutronix.de>");
MODULE_DESCRIPTION("Freescale i.MX25 SCC Crypto driver");
MODULE_LICENSE("GPL v2");
