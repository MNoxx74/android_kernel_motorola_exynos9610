/*
 * drivers/media/platform/exynos/mfc/mfc_ctrl.c
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mfc_ctrl.h"

#include "mfc_hwlock.h"
#include "mfc_nal_q.h"
#include "mfc_sync.h"

#include "mfc_pm.h"
#include "mfc_cmd.h"
#include "mfc_reg_api.h"
#include "mfc_hw_reg_api.h"

#include "mfc_utils.h"

/* Initialize hardware */
static int __mfc_init_hw(struct mfc_dev *dev, enum mfc_buf_usage_type buf_type)
{
	int fw_ver;
	int ret = 0;
	int curr_ctx_is_drm_backup;

	mfc_debug_enter();

	if (!dev) {
		mfc_err_dev("no mfc device to run\n");
		return -EINVAL;
	}

	curr_ctx_is_drm_backup = dev->curr_ctx_is_drm;

	if (!dev->fw_buf.dma_buf)
		return -EINVAL;

	/* 0. MFC reset */
	mfc_debug(2, "MFC reset...\n");

	/* At init time, do not call secure API */
	if (buf_type == MFCBUF_NORMAL)
		dev->curr_ctx_is_drm = 0;
	else if (buf_type == MFCBUF_DRM)
		dev->curr_ctx_is_drm = 1;

	ret = mfc_pm_clock_on(dev);
	if (ret) {
		mfc_err_dev("Failed to enable clock before reset(%d)\n", ret);
		dev->curr_ctx_is_drm = curr_ctx_is_drm_backup;
		return ret;
	}

	ret = mfc_reset_mfc(dev);
	if (ret) {
		mfc_err_dev("Failed to reset MFC - timeout\n");
		goto err_init_hw;
	}
	mfc_debug(2, "Done MFC reset...\n");

	/* 1. Set DRAM base Addr */
	mfc_set_risc_base_addr(dev, buf_type);

	/* 2. Release reset signal to the RISC */
	mfc_risc_on(dev);

	mfc_debug(2, "Will now wait for completion of firmware transfer\n");
	if (mfc_wait_for_done_dev(dev, MFC_REG_R2H_CMD_FW_STATUS_RET)) {
		mfc_err_dev("Failed to RISC_ON\n");
		mfc_clean_dev_int_flags(dev);
		ret = -EIO;
		goto err_init_hw;
	}

	/* 3. Initialize firmware */
	ret = mfc_cmd_sys_init(dev, buf_type);
	if (ret) {
		mfc_err_dev("Failed to send command to MFC - timeout\n");
		goto err_init_hw;
	}

	mfc_debug(2, "Ok, now will write a command to init the system\n");
	if (mfc_wait_for_done_dev(dev, MFC_REG_R2H_CMD_SYS_INIT_RET)) {
		mfc_err_dev("Failed to SYS_INIT\n");
		mfc_clean_dev_int_flags(dev);
		ret = -EIO;
		goto err_init_hw;
	}

	dev->int_condition = 0;
	if (dev->int_err != 0 || dev->int_reason != MFC_REG_R2H_CMD_SYS_INIT_RET) {
		/* Failure. */
		mfc_err_dev("Failed to init firmware - error: %d, int: %d\n",
				dev->int_err, dev->int_reason);
		ret = -EIO;
		goto err_init_hw;
	}

	dev->fw.fimv_info = mfc_get_fimv_info();
	if (dev->fw.fimv_info != 'D' && dev->fw.fimv_info != 'E')
		dev->fw.fimv_info = 'N';

	mfc_info_dev("[F/W] MFC v%x.%x, %02xyy %02xmm %02xdd (%c)\n",
		 MFC_VER_MAJOR(dev),
		 MFC_VER_MINOR(dev),
		 mfc_get_fw_ver_year(),
		 mfc_get_fw_ver_month(),
		 mfc_get_fw_ver_date(),
		 dev->fw.fimv_info);

	dev->fw.date = mfc_get_fw_ver_all();
	/* Check MFC version and F/W version */
	fw_ver = mfc_get_mfc_version();
	if (fw_ver != dev->pdata->ip_ver) {
		mfc_err_dev("Invalid F/W version(0x%x) for MFC H/W(0x%x)\n",
				fw_ver, dev->pdata->ip_ver);
		ret = -EIO;
		goto err_init_hw;
	}

#ifdef CONFIG_EXYNOS_CONTENT_PATH_PROTECTION
	/* Cache flush for base address change */
	mfc_cmd_cache_flush(dev);
	if (mfc_wait_for_done_dev(dev, MFC_REG_R2H_CMD_CACHE_FLUSH_RET)) {
		mfc_err_dev("Failed to CACHE_FLUSH\n");
		mfc_clean_dev_int_flags(dev);
		ret = -EIO;
		goto err_init_hw;
	}

	if (buf_type == MFCBUF_DRM && !curr_ctx_is_drm_backup) {
		mfc_pm_clock_off(dev);
		dev->curr_ctx_is_drm = curr_ctx_is_drm_backup;
		mfc_pm_clock_on_with_base(dev, MFCBUF_NORMAL);
	}
#endif

err_init_hw:
	mfc_pm_clock_off(dev);
	dev->curr_ctx_is_drm = curr_ctx_is_drm_backup;
	mfc_debug_leave();

	return ret;
}

/* Wrapper : Initialize hardware */
int mfc_init_hw(struct mfc_dev *dev)
{
	int ret;

	ret = __mfc_init_hw(dev, MFCBUF_NORMAL);
	if (ret)
		return ret;

#ifdef CONFIG_EXYNOS_CONTENT_PATH_PROTECTION
	if (dev->fw.drm_status) {
		ret = __mfc_init_hw(dev, MFCBUF_DRM);
		if (ret)
			return ret;
	}
#endif

	return ret;
}

/* Deinitialize hardware */
void mfc_deinit_hw(struct mfc_dev *dev)
{
	int ret;

	mfc_debug(2, "mfc deinit start\n");

	if (!dev) {
		mfc_err_dev("no mfc device to run\n");
		return;
	}

	ret = mfc_pm_clock_on(dev);
	if (ret) {
		mfc_err_dev("Failed to enable clock before reset(%d)\n", ret);
		return;
	}

	mfc_mfc_off(dev);

	mfc_pm_clock_off(dev);

	mfc_debug(2, "mfc deinit completed\n");
}

int mfc_sleep(struct mfc_dev *dev)
{
	struct mfc_ctx *ctx;
	int ret;
	int old_state, i;
	int need_cache_flush = 0;

	mfc_debug_enter();

	if (!dev) {
		mfc_err_dev("no mfc device to run\n");
		return -EINVAL;
	}

	ctx = dev->ctx[dev->curr_ctx];
	if (!ctx) {
		for (i = 0; i < MFC_NUM_CONTEXTS; i++) {
			if (dev->ctx[i]) {
				ctx = dev->ctx[i];
				break;
			}
		}
		if (!ctx) {
			mfc_err_dev("no mfc context to run\n");
			return -EINVAL;
		} else {
			mfc_info_dev("ctx is changed %d -> %d\n",
					dev->curr_ctx, ctx->num);
			dev->curr_ctx = ctx->num;
			if (dev->curr_ctx_is_drm != ctx->is_drm) {
				need_cache_flush = 1;
				mfc_info_dev("DRM attribute is changed %d->%d\n",
						dev->curr_ctx_is_drm, ctx->is_drm);
			}
		}
	}
	old_state = ctx->state;
	mfc_change_state(ctx, MFCINST_ABORT);
	MFC_TRACE_DEV_HWLOCK("**sleep (ctx:%d)\n", ctx->num);
	ret = mfc_get_hwlock_dev(dev);
	if (ret < 0) {
		mfc_err_dev("Failed to get hwlock\n");
		mfc_err_dev("dev.hwlock.dev = 0x%lx, bits = 0x%lx, owned_by_irq = %d, wl_count = %d, transfer_owner = %d\n",
				dev->hwlock.dev, dev->hwlock.bits, dev->hwlock.owned_by_irq,
				dev->hwlock.wl_count, dev->hwlock.transfer_owner);
		return -EBUSY;
	}

	mfc_info_dev("curr_ctx_is_drm:%d, hwlock.bits:%lu, hwlock.dev:%lu\n",
			dev->curr_ctx_is_drm, dev->hwlock.bits, dev->hwlock.dev);

	mfc_change_state(ctx, old_state);
	mfc_pm_clock_on(dev);

	if (need_cache_flush)
		mfc_cache_flush(dev, ctx->is_drm);

	mfc_cmd_sleep(dev);

	if (mfc_wait_for_done_dev(dev, MFC_REG_R2H_CMD_SLEEP_RET)) {
		mfc_err_dev("Failed to SLEEP\n");
		dev->logging_data->cause |= (1 << MFC_CAUSE_FAIL_SLEEP);
		call_dop(dev, dump_and_stop_always, dev);
		return -EIO;
	}

	dev->int_condition = 0;
	if (dev->int_err != 0 || dev->int_reason != MFC_REG_R2H_CMD_SLEEP_RET) {
		/* Failure. */
		mfc_err_dev("Failed to sleep - error: %d, int: %d\n",
				dev->int_err, dev->int_reason);
		ret = -EIO;
		goto err_mfc_sleep;
	}

	dev->sleep = 1;

err_mfc_sleep:
	mfc_mfc_off(dev);
	mfc_pm_clock_off(dev);
	mfc_release_hwlock_dev(dev);
	mfc_debug_leave();

	return ret;
}

int mfc_wakeup(struct mfc_dev *dev)
{
	enum mfc_buf_usage_type buf_type;
	int ret = 0;

	mfc_debug_enter();

	if (!dev) {
		mfc_err_dev("no mfc device to run\n");
		return -EINVAL;
	}
	mfc_info_dev("curr_ctx_is_drm:%d\n", dev->curr_ctx_is_drm);

	MFC_TRACE_DEV_HWLOCK("**wakeup\n");
	ret = mfc_get_hwlock_dev(dev);
	if (ret < 0) {
		mfc_err_dev("Failed to get hwlock\n");
		mfc_err_dev("dev.hwlock.dev = 0x%lx, bits = 0x%lx, owned_by_irq = %d, wl_count = %d, transfer_owner = %d\n",
				dev->hwlock.dev, dev->hwlock.bits, dev->hwlock.owned_by_irq,
				dev->hwlock.wl_count, dev->hwlock.transfer_owner);
		return -EBUSY;
	}

	dev->sleep = 0;

	/* 0. MFC reset */
	mfc_debug(2, "MFC reset...\n");

	mfc_pm_clock_on(dev);

	ret = mfc_reset_mfc(dev);
	if (ret) {
		mfc_err_dev("Failed to reset MFC - timeout\n");
		goto err_mfc_wakeup;
	}
	mfc_debug(2, "Done MFC reset...\n");
	if (dev->curr_ctx_is_drm)
		buf_type = MFCBUF_DRM;
	else
		buf_type = MFCBUF_NORMAL;

	/* 1. Set DRAM base Addr */
	mfc_set_risc_base_addr(dev, buf_type);

	/* 2. Release reset signal to the RISC */
	mfc_risc_on(dev);

	mfc_debug(2, "Will now wait for completion of firmware transfer\n");
	if (mfc_wait_for_done_dev(dev, MFC_REG_R2H_CMD_FW_STATUS_RET)) {
		mfc_err_dev("Failed to RISC_ON\n");
		dev->logging_data->cause |= (1 << MFC_CAUSE_FAIL_RISC_ON);
		call_dop(dev, dump_and_stop_always, dev);
		return -EIO;
	}

	mfc_debug(2, "Ok, now will write a command to wakeup the system\n");
	mfc_cmd_wakeup(dev);

	mfc_debug(2, "Will now wait for completion of firmware wake up\n");
	if (mfc_wait_for_done_dev(dev, MFC_REG_R2H_CMD_WAKEUP_RET)) {
		mfc_err_dev("Failed to WAKEUP\n");
		dev->logging_data->cause |= (1 << MFC_CAUSE_FAIL_WAKEUP);
		call_dop(dev, dump_and_stop_always, dev);
		return -EIO;
	}

	dev->int_condition = 0;
	if (dev->int_err != 0 || dev->int_reason != MFC_REG_R2H_CMD_WAKEUP_RET) {
		/* Failure. */
		mfc_err_dev("Failed to wakeup - error: %d, int: %d\n",
				dev->int_err, dev->int_reason);
		ret = -EIO;
		goto err_mfc_wakeup;
	}

err_mfc_wakeup:
	mfc_pm_clock_off(dev);

	mfc_release_hwlock_dev(dev);

	mfc_debug_leave();

	return ret;
}
