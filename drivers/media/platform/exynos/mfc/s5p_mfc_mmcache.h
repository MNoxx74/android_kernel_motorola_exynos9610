/*
 * drivers/media/platform/exynos/mfc/s5p_mfc_mmcache.h
 *
 * Copyright (c) 2016 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __S5P_MFC_MMCACHE_H
#define __S5P_MFC_MMCACHE_H __FILE__

#include "s5p_mfc_common.h"

#define MMCACHE_GLOBAL_CTRL		0x0000
#define MMCACHE_INVALIDATE_STATUS	0x0008
#define MMCACHE_WAY0_CTRL		0x0010
#define MMCACHE_MASTER_GRP_CTRL2	0x0028
#define MMCACHE_MASTER_GRP0_RPATH0	0x0040
#define MMCACHE_CG_CONTROL		0x0400
#define MMCACHE_INVALIDATE		0x0114

#define MMCACHE_GLOBAL_CTRL_VALUE	0x2
#define MMCACHE_CG_CONTROL_VALUE	0x7FF
#define MMCACHE_INVALIDATE_VALUE	0x41

/* Need HW lock to call this function */

void s5p_mfc_mmcache_enable(struct s5p_mfc_dev *dev);
void s5p_mfc_mmcache_disable(struct s5p_mfc_dev *dev);


/* Need HW lock to call this function */
void s5p_mfc_invalidate_mmcache(struct s5p_mfc_dev *dev);

#endif /* __S5P_MFC_MMCACHE_H */
