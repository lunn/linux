// SPDX-License-Identifier: GPL-2.0+
/*
 * Driver to expose SEC4 DRNG via crypto RNG API
 *
 * Copyright 2019 Zodiac Inflight Innovations
 *
 * Based on CAAM SEC4 hw_random driver
 *
 * Copyright 2011 Freescale Semiconductor, Inc.
 * Copyright 2018-2019 NXP
 *
 * Based on caamalg.c crypto API driver.
 *
 */

#include <linux/completion.h>
#include <linux/atomic.h>

#include <crypto/internal/rng.h>

#include "compat.h"

#include "regs.h"
#include "intern.h"
#include "desc_constr.h"
#include "jr.h"
#include "error.h"

#define CAAM_DRNG_MAX_FIFO_STORE_SIZE	((unsigned int)U16_MAX)

/* rng per-device context */
struct caam_drng_ctx {
	struct device *jrdev;
	struct completion done;
};

static void rng_done(struct device *jrdev, u32 *desc, u32 err, void *context)
{
	struct caam_drng_ctx *ctx = context;

	if (err)
		caam_jr_strstatus(jrdev, err);

	complete(&ctx->done);
}

static int caam_drng_generate(struct crypto_rng *tfm,
			     const u8 *src, unsigned int slen,
			     u8 *dst, unsigned int dlen)
{
	struct caam_drng_ctx *ctx = crypto_rng_ctx(tfm);
	struct device *jrdev = ctx->jrdev;
	unsigned int residue = dlen;
	dma_addr_t dst_dma, cur_dma;
	u32 *desc;
	int ret;

	desc = kzalloc(5 * CAAM_CMD_SZ + CAAM_PTR_SZ_MAX,
		       GFP_KERNEL | GFP_DMA);
	if (!desc)
		return -ENOMEM;

	cur_dma = dst_dma = dma_map_single(jrdev, dst, dlen, DMA_FROM_DEVICE);
	if (dma_mapping_error(jrdev, dst_dma)) {
		dev_err(jrdev, "unable to map destination memory\n");
		ret = -ENOMEM;
		goto free_mem;
	}

	do {
		const unsigned int chunk = min(residue,
					       CAAM_DRNG_MAX_FIFO_STORE_SIZE);

		init_job_desc(desc, 0);	/* 1 word */
		/* Generate random bytes */
		append_operation(desc, OP_ALG_ALGSEL_RNG | OP_TYPE_CLASS1_ALG |
				 OP_ALG_PR_ON); /* 1 word */
		/* Store bytes */
		append_seq_out_ptr_intlen(desc, cur_dma, chunk, 0);
		append_seq_fifo_store(desc, chunk, FIFOST_TYPE_RNGSTORE);

		print_hex_dump_debug("rng job desc@: ", DUMP_PREFIX_ADDRESS,
				     16, 4, desc, desc_bytes(desc), 1);

		init_completion(&ctx->done);
		ret = caam_jr_enqueue(jrdev, desc, rng_done, ctx);
		if (ret)
			break;

		wait_for_completion(&ctx->done);

		cur_dma += chunk;
		residue -= chunk;
	} while (residue);

	dma_unmap_single(jrdev, dst_dma, dlen, DMA_FROM_DEVICE);
free_mem:
	kfree(desc);
	return ret;
}

static int caam_drng_init(struct crypto_tfm *tfm)
{
	struct caam_drng_ctx *ctx = crypto_tfm_ctx(tfm);
	int ret;

	ctx->jrdev = caam_jr_alloc();
	ret = PTR_ERR_OR_ZERO(ctx->jrdev);
	if (ret) {
		pr_err("Job Ring Device allocation for transform failed\n");
		return ret;
	}

	return 0;
}

static void caam_drng_exit(struct crypto_tfm *tfm)
{
	struct caam_drng_ctx *ctx = crypto_tfm_ctx(tfm);

	caam_jr_free(ctx->jrdev);
}

static int caam_drng_seed(struct crypto_rng *tfm,
			 const u8 *seed, unsigned int slen)
{
	return 0;
}

static struct rng_alg caam_drng_alg = {
	.generate = caam_drng_generate,
	.seed = caam_drng_seed,
	.seedsize = 0,
	.base = {
		.cra_name = "stdrng",
		.cra_driver_name = "drng-caam",
		.cra_priority = 300,
		.cra_ctxsize = sizeof(struct caam_drng_ctx),
		.cra_module = THIS_MODULE,
		.cra_init = caam_drng_init,
		.cra_exit = caam_drng_exit,
	},
};

static void caam_drng_unregister(void *data)
{
	crypto_unregister_rng(&caam_drng_alg);
}

int caam_drng_register(struct device *ctrldev)
{
	struct caam_drv_private *priv = dev_get_drvdata(ctrldev);

	if (caam_has_rng(priv)) {
		int ret;

		ret = crypto_register_rng(&caam_drng_alg);
		if (ret) {
			dev_err(ctrldev,
				"couldn't register rng crypto alg: %d\n",
				ret);
			return ret;
		}

		ret = devm_add_action_or_reset(ctrldev, caam_drng_unregister,
					       NULL);
		if (ret)
			return ret;

		dev_info(ctrldev,
			 "registering %s\n", caam_drng_alg.base.cra_name);
	}

	return 0;
}
