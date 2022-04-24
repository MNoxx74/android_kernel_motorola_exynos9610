/*
 * Copyright (c) 2017 Samsung Electronics Co., Ltd.
 *
 * Boojin Kim <boojin.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "chub_ipc.h"

#if defined(CHUB_IPC)
#if defined(SEOS)
#include <seos.h>
#include <errno.h>
#elif defined(EMBOS)
#include <Device.h>
#endif
#include <mailboxDrv.h>
#include <csp_common.h>
#include <csp_printf.h>
#include <string.h>
#include <string.h>
#elif defined(AP_IPC)
#include <linux/delay.h>
#include <linux/io.h>
#include "chub.h"
#endif

/* ap-chub ipc */
struct ipc_area ipc_addr[IPC_REG_MAX];

struct ipc_owner_ctrl {
	enum ipc_direction src;
	void *base;
} ipc_own[IPC_OWN_MAX];

struct ipc_map_area *ipc_map;

#ifdef PACKET_LOW_DEBUG
#define GET_IPC_REG_STRING(a) (((a) == IPC_REG_IPC_C2A) ? "wt" : "rd")

static char *get_cs_name(enum channel_status cs)
{
	switch (cs) {
	case CS_IDLE:
		return "I";
	case CS_AP_WRITE:
		return "AW";
	case CS_CHUB_RECV:
		return "CR";
	case CS_CHUB_WRITE:
		return "CW";
	case CS_AP_RECV:
		return "AR";
	case CS_MAX:
		break;
	};
	return NULL;
}

void content_disassemble(struct ipc_content *content, enum ipc_region act)
{
	CSP_PRINTF_INFO("[content-%s-%d: status:%s: buf: 0x%x, size: %d]\n",
			GET_IPC_REG_STRING(act), content->num,
			get_cs_name(content->status),
			(unsigned int)content->buf, content->size);
}
#endif

/* ipc address control functions */
void ipc_set_base(void *addr)
{
	ipc_addr[IPC_REG_BL].base = addr;
}

inline void *ipc_get_base(enum ipc_region area)
{
	return ipc_addr[area].base;
}

inline u32 ipc_get_offset(enum ipc_region area)
{
	return ipc_addr[area].offset;
}

inline void *ipc_get_addr(enum ipc_region area, int buf_num)
{
#ifdef CHUB_IPC
	return (void *)((unsigned int)ipc_addr[area].base +
			ipc_addr[area].offset * buf_num);
#else
	return ipc_addr[area].base + ipc_addr[area].offset * buf_num;
#endif
}

u32 ipc_get_chub_mem_size(void)
{
	return ipc_addr[IPC_REG_DUMP].offset;
}

void ipc_set_chub_clk(u32 clk)
{
	struct chub_bootargs *map = ipc_get_base(IPC_REG_BL_MAP);

	map->chubclk = clk;
}

u32 ipc_get_chub_clk(void)
{
	struct chub_bootargs *map = ipc_get_base(IPC_REG_BL_MAP);

	return map->chubclk;
}

void ipc_set_chub_bootmode(u32 bootmode)
{
	struct chub_bootargs *map = ipc_get_base(IPC_REG_BL_MAP);

	map->bootmode = bootmode;
}

u32 ipc_get_chub_bootmode(void)
{
	struct chub_bootargs *map = ipc_get_base(IPC_REG_BL_MAP);

	return map->bootmode;
}

#if defined(LOCAL_POWERGATE)
u32 *ipc_get_chub_psp(void)
{
	struct chub_bootargs *map = ipc_get_base(IPC_REG_BL_MAP);

	return &(map->psp);
}

u32 *ipc_get_chub_msp(void)
{
	struct chub_bootargs *map = ipc_get_base(IPC_REG_BL_MAP);

	return &(map->msp);
}
#endif

void *ipc_get_chub_map(void)
{
	char *sram_base = ipc_get_base(IPC_REG_BL);
	struct chub_bootargs *map = (struct chub_bootargs *)(sram_base + MAP_INFO_OFFSET);

	if (strncmp(OS_UPDT_MAGIC, map->magic, sizeof(OS_UPDT_MAGIC))) {
		CSP_PRINTF_ERROR("%s: %p has wrong magic key: %s -> %s\n",
				 __func__, map, OS_UPDT_MAGIC, map->magic);
		return 0;
	}

	if (map->ipc_version != IPC_VERSION) {
		CSP_PRINTF_ERROR
		    ("%s: ipc_version doesn't match: AP %d, Chub: %d\n",
		     __func__, IPC_VERSION, map->ipc_version);
		return 0;
	}

	ipc_addr[IPC_REG_BL_MAP].base = map;
	ipc_addr[IPC_REG_OS].base = sram_base + map->code_start;
	ipc_addr[IPC_REG_SHARED].base = sram_base + map->shared_start;
	ipc_addr[IPC_REG_IPC].base = sram_base + map->ipc_start;
	ipc_addr[IPC_REG_RAM].base = sram_base + map->ram_start;
	ipc_addr[IPC_REG_DUMP].base = sram_base + map->dump_start;
	ipc_addr[IPC_REG_BL].offset = map->bl_end - map->bl_start;
	ipc_addr[IPC_REG_OS].offset = map->code_end - map->code_start;
	ipc_addr[IPC_REG_SHARED].offset = map->shared_end - map->shared_start;
	ipc_addr[IPC_REG_IPC].offset = map->ipc_end - map->ipc_start;
	ipc_addr[IPC_REG_RAM].offset = map->ram_end - map->ram_start;
	ipc_addr[IPC_REG_DUMP].offset = map->dump_end - map->dump_start;

	ipc_map = ipc_addr[IPC_REG_IPC].base;
	ipc_map->logbuf.size =
	    ipc_addr[IPC_REG_IPC].offset - sizeof(struct ipc_map_area);

	ipc_addr[IPC_REG_IPC_EVT_A2C].base = &ipc_map->evt[IPC_EVT_A2C].data;
	ipc_addr[IPC_REG_IPC_EVT_A2C].offset = 0;
	ipc_addr[IPC_REG_IPC_EVT_A2C_CTRL].base =
	    &ipc_map->evt[IPC_EVT_A2C].ctrl;
	ipc_addr[IPC_REG_IPC_EVT_A2C_CTRL].offset = 0;
	ipc_addr[IPC_REG_IPC_EVT_C2A].base = &ipc_map->evt[IPC_EVT_C2A].data;
	ipc_addr[IPC_REG_IPC_EVT_C2A].offset = 0;
	ipc_addr[IPC_REG_IPC_EVT_C2A_CTRL].base =
	    &ipc_map->evt[IPC_EVT_C2A].ctrl;
	ipc_addr[IPC_REG_IPC_EVT_C2A_CTRL].offset = 0;
	ipc_addr[IPC_REG_IPC_C2A].base = &ipc_map->data[IPC_DATA_C2A];
	ipc_addr[IPC_REG_IPC_A2C].base = &ipc_map->data[IPC_DATA_A2C];
#ifdef USE_IPC_BUF
	ipc_addr[IPC_REG_IPC_C2A].offset = sizeof(struct ipc_buf);
	ipc_addr[IPC_REG_IPC_A2C].offset = sizeof(struct ipc_buf);
#else
	ipc_addr[IPC_REG_IPC_C2A].offset = sizeof(struct ipc_content);
	ipc_addr[IPC_REG_IPC_A2C].offset = sizeof(struct ipc_content);
#endif

	ipc_addr[IPC_REG_LOG].base = &ipc_map->logbuf.buf;
	ipc_addr[IPC_REG_LOG].offset =
	    ipc_addr[IPC_REG_IPC].offset - sizeof(struct ipc_map_area);

#ifdef CHUB_IPC
	ipc_map->logbuf.token = 0;
	memset(ipc_addr[IPC_REG_LOG].base, 0, ipc_addr[IPC_REG_LOG].offset);
#endif

	CSP_PRINTF_INFO
	    ("contexthub map information(v%u)\n\tbl(%p %d)\n\tos(%p %d)\n\tipc(%p %d)\n\tram(%p %d)\n\tshared(%p %d)\n\tdump(%p %d)\n",
	     map->ipc_version,
	     ipc_addr[IPC_REG_BL].base, ipc_addr[IPC_REG_BL].offset,
	     ipc_addr[IPC_REG_OS].base, ipc_addr[IPC_REG_OS].offset,
	     ipc_addr[IPC_REG_IPC].base, ipc_addr[IPC_REG_IPC].offset,
	     ipc_addr[IPC_REG_RAM].base, ipc_addr[IPC_REG_RAM].offset,
	     ipc_addr[IPC_REG_SHARED].base, ipc_addr[IPC_REG_SHARED].offset,
	     ipc_addr[IPC_REG_DUMP].base, ipc_addr[IPC_REG_DUMP].offset);

	return ipc_map;
}

void ipc_dump(void)
{
	CSP_PRINTF_INFO("%s: a2x event\n", __func__);
	ipc_print_evt(IPC_EVT_A2C);
	CSP_PRINTF_INFO("%s: c2a event\n", __func__);
	ipc_print_evt(IPC_EVT_C2A);

#ifndef USE_IPC_BUF
	CSP_PRINTF_INFO("%s: active channel\n", __func__);
	ipc_print_channel();
#else
	CSP_PRINTF_INFO("%s: data buffer\n", __func__);
	ipc_print_databuf();
#endif
}

#ifdef USE_IPC_BUF
inline void ipc_copy_bytes(u8 *dst, u8 *src, int size)
{
	int i;

	/* max 2 bytes optimize */
	for (i = 0; i < size; i++)
		*dst++ = *src++;
}

#define INC_QIDX(i) (((i) == IPC_DATA_SIZE) ? 0 : (i))
static inline int ipc_io_data(enum ipc_data_list dir, u8 *buf, u16 length)
{
	struct ipc_buf *ipc_data;
	int eq;
	int dq;
	int useful = 0;
	u8 size_lower;
	u8 size_upper;
	u16 size_to_read;
	u32 size_to_copy_top;
	u32 size_to_copy_bottom;
	enum ipc_region reg;

	/* get ipc region */
	if (dir == IPC_DATA_C2A)
		reg = IPC_REG_IPC_C2A;
	else if (dir == IPC_DATA_A2C)
		reg = IPC_REG_IPC_A2C;
	else {
		CSP_PRINTF_ERROR("%s: invalid dir:%d\n", __func__, dir);
		return -1;
	}

	/* get ipc_data base */
	ipc_data = ipc_get_base(reg);
	eq = ipc_data->eq;
	dq = ipc_data->dq;

#ifdef USE_IPC_BUF_LOG
	CSP_PRINTF_INFO("%s: dir:%s(w:%d, r:%d, cnt:%d), e:%d d:%d, empty:%d, full:%d, ipc_data:%p, len:%d\n",
		__func__, dir ? "a2c" : "c2a", ipc_data->cnt_dbg_wt,
		ipc_data->cnt_dbg_rd, ipc_data->cnt, eq, dq, ipc_data->empty,
		ipc_data->full, ipc_data, length);
#endif

	if (length) {
		/* write data */
		/* calc the unused area on ipc buffer */
		if (eq > dq)
			useful = dq + (IPC_DATA_SIZE - eq);
		else if (eq < dq)
			useful = dq - eq;
		else if (ipc_data->full) {
			CSP_PRINTF_ERROR("%s is full\n", __func__);
			return -1;
		} else {
			useful = IPC_DATA_SIZE;
		}

#ifdef USE_IPC_BUF_LOG
		ipc_data->cnt_dbg_wt++;
		CSP_PRINTF_INFO("w: eq:%d, dq:%d, useful:%d\n",	eq, dq, useful);
#endif
		/* check length */
		if (length + sizeof(u16) > useful) {
			CSP_PRINTF_ERROR
				("%s: no buffer. len:%d, remain:%d, eq:%d, dq:%d\n",
				 __func__, length, useful, eq, dq);
			return -1;
		}

		size_upper = (length >> 8);
		size_lower = length & 0xff;
		ipc_data->buf[eq++] = size_lower;

		/* write size */
		if (eq == IPC_DATA_SIZE)
			eq = 0;

		ipc_data->buf[eq++] = size_upper;
		if (eq == IPC_DATA_SIZE)
			eq = 0;

		/* write data */
		if (eq + length > IPC_DATA_SIZE) {
			size_to_copy_top = IPC_DATA_SIZE - eq;
			size_to_copy_bottom = length - size_to_copy_top;
			ipc_copy_bytes(&ipc_data->buf[eq], buf, size_to_copy_top);
			ipc_copy_bytes(&ipc_data->buf[0], &buf[size_to_copy_top], size_to_copy_bottom);
			eq = size_to_copy_bottom;
		} else {
			ipc_copy_bytes(&ipc_data->buf[eq], buf, length);
			eq += length;
			if (eq == IPC_DATA_SIZE)
				eq = 0;
		}

		/* update queue index */
		ipc_data->eq = eq;

		if (ipc_data->eq == ipc_data->dq)
			ipc_data->full = 1;

		if (ipc_data->empty)
			ipc_data->empty = 0;

#ifdef USE_IPC_BUF_LOG
		CSP_PRINTF_INFO("w_out: eq:%d, dq:%d, f:%d, e:%d\n",
			ipc_data->eq, ipc_data->dq, ipc_data->full, ipc_data->empty);
#endif
		return 0;
	} else {
		/* read data */
		/* calc the unused area on ipc buffer */
		if (eq > dq)
			useful = eq - dq;
		else if (eq < dq)
			useful = (IPC_DATA_SIZE - dq) + eq;
		else if (ipc_data->empty) {
			CSP_PRINTF_ERROR("%s is empty\n", __func__);
			return 0;
		} else {
			useful = IPC_DATA_SIZE;
		}

		/* read size */
		size_lower = ipc_data->buf[dq++];
		if (dq == IPC_DATA_SIZE)
			dq = 0;

		size_upper = ipc_data->buf[dq++];
		if (dq == IPC_DATA_SIZE)
			dq = 0;

		size_to_read = (size_upper << 8) | size_lower;
		if (size_to_read >= PACKET_SIZE_MAX) {
			CSP_PRINTF_ERROR("%s: wrong size:%d\n",
				__func__, size_to_read);
			return -1;
		}

#ifdef USE_IPC_BUF_LOG
		ipc_data->cnt_dbg_rd++;
		CSP_PRINTF_INFO("r: eq:%d, dq:%d, useful:%d, size_to_read:%d\n",
			eq, dq, useful, size_to_read);
#endif

		if (useful < sizeof(u16) + size_to_read) {
			CSP_PRINTF_ERROR("%s: no enought read size: useful:%d, read_to_size:%d,%d\n",
				__func__, useful, size_to_read, sizeof(u16));
			return 0;
		}

		/* read data */
		if (dq + size_to_read > IPC_DATA_SIZE) {
			size_to_copy_top = IPC_DATA_SIZE - dq;
			size_to_copy_bottom = size_to_read - size_to_copy_top;

			ipc_copy_bytes(buf, &ipc_data->buf[dq], size_to_copy_top);
			ipc_copy_bytes(&buf[size_to_copy_top], &ipc_data->buf[0], size_to_copy_bottom);
			dq = size_to_copy_bottom;
		} else {
			ipc_copy_bytes(buf, &ipc_data->buf[dq], size_to_read);

			dq += size_to_read;
			if (dq == IPC_DATA_SIZE)
				dq = 0;
		}

		/* update queue index */
		ipc_data->dq = dq;
		if (ipc_data->eq == ipc_data->dq)
			ipc_data->empty = 1;

		if (ipc_data->full)
			ipc_data->full = 0;

#ifdef USE_IPC_BUF_LOG
		CSP_PRINTF_INFO("r_out (read_to_size:%d): eq:%d, dq:%d, f:%d, e:%d\n",
			size_to_read, ipc_data->eq, ipc_data->dq, ipc_data->full, ipc_data->empty);
#endif
		return size_to_read;
	}
}

int ipc_write_data(enum ipc_data_list dir, void *tx, u16 length)
{
	int ret = 0;
	enum ipc_evt_list evtq;

	if (length <= PACKET_SIZE_MAX)
		ret = ipc_io_data(dir, tx, length);
	else {
		CSP_PRINTF_INFO("%s: invalid size:%d\n",
			__func__, length);
		return -1;
	}

	if (!ret) {
		evtq = (dir == IPC_DATA_C2A) ? IPC_EVT_C2A : IPC_EVT_A2C;
		ret = ipc_add_evt(evtq, IRQ_EVT_CH0);
	} else {
		CSP_PRINTF_INFO("%s: error\n", __func__);
	}
	return ret;
}

int ipc_read_data(enum ipc_data_list dir, uint8_t *rx)
{
	int ret = ipc_io_data(dir, rx, 0);

	if (!ret || (ret < 0)) {
		CSP_PRINTF_INFO("%s: error\n", __func__);
		return 0;
	}
	return ret;
}

void ipc_print_databuf(void)
{
	struct ipc_buf *ipc_data = ipc_get_base(IPC_REG_IPC_A2C);

	CSP_PRINTF_INFO("a2c: eq:%d dq:%d full:%d empty:%d tx:%d rx:%d\n",
				ipc_data->eq, ipc_data->dq, ipc_data->full, ipc_data->empty,
				ipc_data->cnt_dbg_wt, ipc_data->cnt_dbg_rd);

	ipc_data = ipc_get_base(IPC_REG_IPC_C2A);

	CSP_PRINTF_INFO("c2a: eq:%d dq:%d full:%d empty:%d tx:%d rx:%d\n",
				ipc_data->eq, ipc_data->dq, ipc_data->full, ipc_data->empty,
				ipc_data->cnt_dbg_wt, ipc_data->cnt_dbg_rd);
}

#else
/* ipc channel functions */
#define GET_IPC_REG_NAME(c) (((c) == CS_WRITE) ? "W" : (((c) == CS_RECV) ? "R" : "I"))
#define GET_CH_NAME(c) (((c) == CS_AP) ? "A" : "C")
#define GET_CH_OWNER(o) (((o) == IPC_DATA_C2A) ? "C2A" : "A2C")

inline void ipc_update_channel_status(struct ipc_content *content,
				      enum channel_status next)
{
#ifdef PACKET_LOW_DEBUG
	unsigned int org = __raw_readl(&content->status);

	CSP_PRINTF_INFO("CH(%s)%d: %s->%s\n", GET_CH_NAME(org >> CS_OWN_OFFSET),
			content->num, GET_IPC_REG_NAME((org & CS_IPC_REG_CMP)),
			GET_IPC_REG_NAME((next & CS_IPC_REG_CMP)));
#endif

	__raw_writel(next, &content->status);
}

void *ipc_scan_channel(enum ipc_region area, enum channel_status target)
{
	int i;
	struct ipc_content *content = ipc_get_base(area);

	for (i = 0; i < IPC_BUF_NUM; i++, content++)
		if (__raw_readl(&content->status) == target)
			return content;

	return NULL;
}

void *ipc_get_channel(enum ipc_region area, enum channel_status target,
		      enum channel_status next)
{
	int i;
	struct ipc_content *content = ipc_get_base(area);

	for (i = 0; i < IPC_BUF_NUM; i++, content++) {
		if (__raw_readl(&content->status) == target) {
			ipc_update_channel_status(content, next);
			return content;
		}
	}

	return NULL;
}

void ipc_print_channel(void)
{
	int i, j, org;

	for (j = 0; j < IPC_DATA_MAX; j++) {
		for (i = 0; i < IPC_BUF_NUM; i++) {
			org = ipc_map->data[j][i].status;
			if (org & CS_IPC_REG_CMP)
				CSP_PRINTF_INFO("CH-%s:%x\n",
						GET_CH_OWNER(j), org);
		}
	}
}
#endif

void ipc_init(void)
{
	int i, j;

	if (!ipc_map)
		CSP_PRINTF_ERROR("%s: ipc_map is NULL.\n", __func__);

#ifdef USE_IPC_BUF
	for (i = 0; i < IPC_DATA_MAX; i++) {
		ipc_map->data[i].eq = 0;
		ipc_map->data[i].dq = 0;
		ipc_map->data[i].full = 0;
		ipc_map->data[i].empty = 1;
		ipc_map->data[i].cnt_dbg_wt = 0;
		ipc_map->data[i].cnt_dbg_rd = 0;
	}
#else
	for (i = 0; i < IPC_BUF_NUM; i++) {
		ipc_map->data[IPC_DATA_C2A][i].num = i;
		ipc_map->data[IPC_DATA_C2A][i].status = CS_CHUB_OWN;
		ipc_map->data[IPC_DATA_A2C][i].num = i;
		ipc_map->data[IPC_DATA_A2C][i].status = CS_AP_OWN;
	}
#endif

	ipc_hw_clear_all_int_pend_reg(AP);

	for (j = 0; j < IPC_EVT_MAX; j++) {
		ipc_map->evt[j].ctrl.dq = 0;
		ipc_map->evt[j].ctrl.eq = 0;
		ipc_map->evt[j].ctrl.full = 0;
		ipc_map->evt[j].ctrl.empty = 0;
		ipc_map->evt[j].ctrl.irq = 0;

		for (i = 0; i < IPC_EVT_NUM; i++) {
			ipc_map->evt[j].data[i].evt = IRQ_EVT_INVAL;
			ipc_map->evt[j].data[i].irq = IRQ_EVT_INVAL;
		}
	}
}

/* evt functions */
enum {
	IPC_EVT_DQ,		/* empty */
	IPC_EVT_EQ,		/* fill */
};

#define EVT_Q_INT(i) (((i) == IPC_EVT_NUM) ? 0 : (i))
#define IRQ_EVT_IDX_INT(i) (((i) == IRQ_EVT_END) ? IRQ_EVT_START : (i))
#define IRQ_C2A_WT_IDX_INT(i) (((i) == IRQ_C2A_END) ? IRQ_C2A_START : (i))

#define EVT_Q_DEC(i) (((i) == -1) ? IPC_EVT_NUM - 1 : (i - 1))

struct ipc_evt_buf *ipc_get_evt(enum ipc_evt_list evtq)
{
	struct ipc_evt *ipc_evt = &ipc_map->evt[evtq];
	struct ipc_evt_buf *cur_evt = NULL;

	if (ipc_evt->ctrl.dq != __raw_readl(&ipc_evt->ctrl.eq)) {
		cur_evt = &ipc_evt->data[ipc_evt->ctrl.dq];
		cur_evt->status = IPC_EVT_DQ;
		ipc_evt->ctrl.dq = EVT_Q_INT(ipc_evt->ctrl.dq + 1);
	} else if (__raw_readl(&ipc_evt->ctrl.full)) {
		cur_evt = &ipc_evt->data[ipc_evt->ctrl.dq];
		cur_evt->status = IPC_EVT_DQ;
		ipc_evt->ctrl.dq = EVT_Q_INT(ipc_evt->ctrl.dq + 1);
		__raw_writel(0, &ipc_evt->ctrl.full);
	}

	return cur_evt;
}

#define EVT_WAIT_TIME (10)
#define MAX_TRY_CNT (5)

int ipc_add_evt(enum ipc_evt_list evtq, enum irq_evt_chub evt)
{
	struct ipc_evt *ipc_evt = &ipc_map->evt[evtq];
	enum ipc_owner owner = (evtq < IPC_EVT_AP_MAX) ? AP : IPC_OWN_MAX;
	struct ipc_evt_buf *cur_evt = NULL;

	if (!ipc_evt) {
		CSP_PRINTF_ERROR("%s: invalid ipc_evt\n", __func__);
		return -1;
	}

	if (!__raw_readl(&ipc_evt->ctrl.full)) {
		cur_evt = &ipc_evt->data[ipc_evt->ctrl.eq];
		if (!cur_evt) {
			CSP_PRINTF_ERROR("%s: invalid cur_evt\n", __func__);
			return -1;
		}

		cur_evt->evt = evt;
		cur_evt->status = IPC_EVT_EQ;
		cur_evt->irq = ipc_evt->ctrl.irq;

		ipc_evt->ctrl.eq = EVT_Q_INT(ipc_evt->ctrl.eq + 1);
		ipc_evt->ctrl.irq = IRQ_EVT_IDX_INT(ipc_evt->ctrl.irq + 1);

		if (ipc_evt->ctrl.eq == __raw_readl(&ipc_evt->ctrl.dq))
			__raw_writel(1, &ipc_evt->ctrl.full);
	} else {
#if defined(CHUB_IPC)
		int trycnt = 0;

		do {
			trycnt++;
			msleep(EVT_WAIT_TIME);
		} while (ipc_evt->ctrl.full && (trycnt < MAX_TRY_CNT));

		if (!__raw_readl(&ipc_evt->ctrl.full)) {
			CSP_PRINTF_INFO("%s: evt %d during %d ms is full\n",
					__func__, evt, EVT_WAIT_TIME * trycnt);
			return -1;
		} else {
			CSP_PRINTF_ERROR("%s: fail to add evt\n", __func__);
			return -1;
		}
#else
		CSP_PRINTF_ERROR("%s: fail to add evt\n", __func__);
		return -1;
#endif
	}

	if (owner != IPC_OWN_MAX) {
#if defined(AP_IPC)
		ipc_write_val(AP, sched_clock());
#endif
		if (cur_evt)
			ipc_hw_gen_interrupt(owner, cur_evt->irq);
		else
			return -1;
	}

	return 0;
}

#define IPC_GET_EVT_NAME(a) (((a) == IPC_EVT_A2C) ? "A2C" : "C2A")

void ipc_print_evt(enum ipc_evt_list evtq)
{
	struct ipc_evt *ipc_evt = &ipc_map->evt[evtq];
	int i;

	CSP_PRINTF_INFO("evt-%s: eq:%d dq:%d full:%d irq:%d\n",
			IPC_GET_EVT_NAME(evtq), ipc_evt->ctrl.eq,
			ipc_evt->ctrl.dq, ipc_evt->ctrl.full,
			ipc_evt->ctrl.irq);

	for (i = 0; i < IPC_EVT_NUM; i++) {
		CSP_PRINTF_INFO("evt%d(evt:%d,irq:%d,f:%d)\n",
				i, ipc_evt->data[i].evt,
				ipc_evt->data[i].irq, ipc_evt->data[i].status);
	}

	(void)ipc_evt;
}

u32 ipc_logbuf_get_token(void)
{
	__raw_writel(ipc_map->logbuf.token + 1, &ipc_map->logbuf.token);

	return __raw_readl(&ipc_map->logbuf.token);
}

void ipc_logbuf_put_with_char(char ch)
{
	char *logbuf;
	int eqNext;

	if (ipc_map) {
		eqNext = ipc_map->logbuf.eq + 1;

#ifdef IPC_DEBUG
		if (eqNext == ipc_map->logbuf.dq) {
			ipc_write_debug_event(AP, IPC_DEBUG_CHUB_FULL_LOG);
			ipc_add_evt(IPC_EVT_C2A, IRQ_EVT_CHUB_TO_AP_DEBUG);
		}
#endif

		logbuf = ipc_map->logbuf.buf;

		*(logbuf + ipc_map->logbuf.eq) = ch;

		if (eqNext == ipc_map->logbuf.size)
			ipc_map->logbuf.eq = 0;
		else
			ipc_map->logbuf.eq = eqNext;
	}
}

void ipc_set_owner(enum ipc_owner owner, void *base, enum ipc_direction dir)
{
	ipc_own[owner].base = base;
	ipc_own[owner].src = dir;
}

int ipc_hw_read_int_start_index(enum ipc_owner owner)
{
	if (ipc_own[owner].src)
		return IRQ_EVT_CHUB_MAX;
	else
		return 0;
}

unsigned int ipc_hw_read_gen_int_status_reg(enum ipc_owner owner, int irq)
{
	if (ipc_own[owner].src)
		return __raw_readl((char *)ipc_own[owner].base +
				   REG_MAILBOX_INTSR1) & (1 << irq);
	else
		return __raw_readl((char *)ipc_own[owner].base +
				   REG_MAILBOX_INTSR0) & (1 << (irq +
								IRQ_EVT_CHUB_MAX));
}

void ipc_hw_write_shared_reg(enum ipc_owner owner, unsigned int val, int num)
{
	__raw_writel(val, (char *)ipc_own[owner].base + REG_MAILBOX_ISSR0 + num * 4);
}

unsigned int ipc_hw_read_shared_reg(enum ipc_owner owner, int num)
{
	return __raw_readl((char *)ipc_own[owner].base + REG_MAILBOX_ISSR0 + num * 4);
}

unsigned int ipc_hw_read_int_status_reg(enum ipc_owner owner)
{
	if (ipc_own[owner].src)
		return __raw_readl((char *)ipc_own[owner].base + REG_MAILBOX_INTSR0);
	else
		return __raw_readl((char *)ipc_own[owner].base + REG_MAILBOX_INTSR1);
}

unsigned int ipc_hw_read_int_gen_reg(enum ipc_owner owner)
{
	if (ipc_own[owner].src)
		return __raw_readl((char *)ipc_own[owner].base + REG_MAILBOX_INTGR0);
	else
		return __raw_readl((char *)ipc_own[owner].base + REG_MAILBOX_INTGR1);
}

void ipc_hw_clear_int_pend_reg(enum ipc_owner owner, int irq)
{
	if (ipc_own[owner].src)
		__raw_writel(1 << irq,
			     (char *)ipc_own[owner].base + REG_MAILBOX_INTCR0);
	else
		__raw_writel(1 << irq,
			     (char *)ipc_own[owner].base + REG_MAILBOX_INTCR1);
}

void ipc_hw_clear_all_int_pend_reg(enum ipc_owner owner)
{
	u32 val = 0xffff << ipc_hw_read_int_start_index(AP);
	/* hack: org u32 val = 0xff; */

	if (ipc_own[owner].src)
		__raw_writel(val, (char *)ipc_own[owner].base + REG_MAILBOX_INTCR0);
	else
		__raw_writel(val, (char *)ipc_own[owner].base + REG_MAILBOX_INTCR1);
}

void ipc_hw_gen_interrupt(enum ipc_owner owner, int irq)
{
	if (ipc_own[owner].src)
		__raw_writel(1 << irq,
			     (char *)ipc_own[owner].base + REG_MAILBOX_INTGR1);
	else
		__raw_writel(1 << (irq + IRQ_EVT_CHUB_MAX),
			     (char *)ipc_own[owner].base + REG_MAILBOX_INTGR0);
}

void ipc_hw_set_mcuctrl(enum ipc_owner owner, unsigned int val)
{
	__raw_writel(val, (char *)ipc_own[owner].base + REG_MAILBOX_MCUCTL);
}

void ipc_hw_mask_irq(enum ipc_owner owner, int irq)
{
	int mask;

	if (ipc_own[owner].src) {
		mask = __raw_readl((char *)ipc_own[owner].base + REG_MAILBOX_INTMR0);
		__raw_writel(mask | (1 << (irq + IRQ_EVT_CHUB_MAX)),
			     (char *)ipc_own[owner].base + REG_MAILBOX_INTMR0);
	} else {
		mask = __raw_readl((char *)ipc_own[owner].base + REG_MAILBOX_INTMR1);
		__raw_writel(mask | (1 << irq),
			     (char *)ipc_own[owner].base + REG_MAILBOX_INTMR1);
	}
}

void ipc_hw_unmask_irq(enum ipc_owner owner, int irq)
{
	int mask;

	if (ipc_own[owner].src) {
		mask = __raw_readl((char *)ipc_own[owner].base + REG_MAILBOX_INTMR0);
		__raw_writel(mask & ~(1 << (irq + IRQ_EVT_CHUB_MAX)),
			     (char *)ipc_own[owner].base + REG_MAILBOX_INTMR0);
	} else {
		mask = __raw_readl((char *)ipc_own[owner].base + REG_MAILBOX_INTMR1);
		__raw_writel(mask & ~(1 << irq),
			     (char *)ipc_own[owner].base + REG_MAILBOX_INTMR1);
	}
}

void ipc_write_debug_event(enum ipc_owner owner, enum ipc_debug_event action)
{
	ipc_hw_write_shared_reg(owner, action, SR_DEBUG_ACTION);
}

u32 ipc_read_debug_event(enum ipc_owner owner)
{
	return ipc_hw_read_shared_reg(owner, SR_DEBUG_ACTION);
}

void ipc_write_val(enum ipc_owner owner, u64 val)
{
	u32 low = val & 0xffffffff;
	u32 high = val >> 32;

	ipc_hw_write_shared_reg(owner, low, SR_DEBUG_VAL_LOW);
	ipc_hw_write_shared_reg(owner, high, SR_DEBUG_VAL_HIGH);
}

u64 ipc_read_val(enum ipc_owner owner)
{
	u32 low = ipc_hw_read_shared_reg(owner, SR_DEBUG_VAL_LOW);
	u64 high = ipc_hw_read_shared_reg(owner, SR_DEBUG_VAL_HIGH);
	u64 val = low | (high << 32);

	return val;
}
