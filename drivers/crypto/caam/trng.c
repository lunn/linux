// SPDX-License-Identifier: GPL-2.0+
/*
 * hw_random interface for TRNG generator in CAAM RNG block
 *
 * Copyright 2019 Zodiac Inflight Innovations
 *
 */

#include <linux/hw_random.h>

#include "compat.h"
#include "regs.h"
#include "intern.h"

struct caam_trng_ctx {
	struct rng4tst __iomem *r4tst;
	struct hwrng rng;
};

static bool caam_trng_busy(struct caam_trng_ctx *ctx)
{
	return !(rd_reg32(&ctx->r4tst->rtmctl) & RTMCTL_ENT_VAL);
}

static int caam_trng_read(struct hwrng *rng, void *data, size_t max, bool wait)
{
	struct caam_trng_ctx *ctx = (void *)rng->priv;
	u32 rtent[ARRAY_SIZE(ctx->r4tst->rtent)];
	size_t residue = max;

	if (!wait)
		return 0;

	clrsetbits_32(&ctx->r4tst->rtmctl, 0, RTMCTL_ACC);

	do {
		const size_t chunk = min(residue, sizeof(rtent));
		unsigned int i;

		do {
			/*
			 * It takes about 70 ms to finish on i.MX6 and
			 * i.MX8MQ
			 */
			msleep(70);
		} while (caam_trng_busy(ctx));

		for (i = 0; i < DIV_ROUND_UP(chunk, sizeof(u32)); i++)
			rtent[i] = rd_reg32(&ctx->r4tst->rtent[i]);

		memcpy(data, rtent, chunk);

		residue -= chunk;
		data    += chunk;
	} while (residue);

	clrsetbits_32(&ctx->r4tst->rtmctl, RTMCTL_ACC, 0);

	return max;
}

int caam_trng_register(struct device *ctrldev)
{
	struct caam_drv_private *priv = dev_get_drvdata(ctrldev);

	if (caam_has_rng(priv)) {
		struct caam_trng_ctx *ctx;
		int err;

		ctx = devm_kzalloc(ctrldev, sizeof(*ctx), GFP_KERNEL);
		if (!ctx)
			return -ENOMEM;

		ctx->r4tst = &priv->ctrl->r4tst[0];

		ctx->rng.name = "trng-caam";
		ctx->rng.read = caam_trng_read;
		ctx->rng.priv = (unsigned long)ctx;
		ctx->rng.quality = 999;

		dev_info(ctrldev, "registering %s\n", ctx->rng.name);

		err = devm_hwrng_register(ctrldev, &ctx->rng);
		if (err)
			return err;
	}

	return 0;
}
