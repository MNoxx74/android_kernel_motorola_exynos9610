/*
 * Samsung Exynos SoC series VIPx driver
 *
 * Copyright (c) 2018 Samsung Electronics Co., Ltd
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/exynos_iovmm.h>

#include "vipx-log.h"
#include "vipx-graph.h"
#include "vipx-device.h"

void __vipx_fault_handler(struct vipx_device *device)
{
	vipx_enter();
	vipx_leave();
}

static int __attribute__((unused)) vipx_fault_handler(
		struct iommu_domain *domain, struct device *dev,
		unsigned long fault_addr, int fault_flag, void *token)
{
	struct vipx_device *device;

	pr_err("< VIPX FAULT HANDLER >\n");
	pr_err("Device virtual(0x%lX) is invalid access\n", fault_addr);

	device = dev_get_drvdata(dev);
	vipx_debug_dump_debug_regs();

	__vipx_fault_handler(device);

	return -EINVAL;
}

#if defined(CONFIG_PM_SLEEP)
static int vipx_device_suspend(struct device *dev)
{
	vipx_enter();
	vipx_leave();
	return 0;
}

static int vipx_device_resume(struct device *dev)
{
	vipx_enter();
	vipx_leave();
	return 0;
}
#endif

static int vipx_device_runtime_suspend(struct device *dev)
{
	int ret;
	struct vipx_device *device;

	vipx_enter();
	device = dev_get_drvdata(dev);

	ret = vipx_system_suspend(&device->system);
	if (ret)
		goto p_err;

	vipx_leave();
p_err:
	return ret;
}

static int vipx_device_runtime_resume(struct device *dev)
{
	int ret;
	struct vipx_device *device;

	vipx_enter();
	device = dev_get_drvdata(dev);

	ret = vipx_system_resume(&device->system);
	if (ret)
		goto p_err;

	vipx_leave();
p_err:
	return ret;
}

static const struct dev_pm_ops vipx_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(
			vipx_device_suspend,
			vipx_device_resume)
	SET_RUNTIME_PM_OPS(
			vipx_device_runtime_suspend,
			vipx_device_runtime_resume,
			NULL)
};

static int __vipx_device_start(struct vipx_device *device)
{
	int ret;

	vipx_enter();
	if (!test_bit(VIPX_DEVICE_STATE_OPEN, &device->state)) {
		ret = -EINVAL;
		vipx_err("device was not opend yet (%lx)\n", device->state);
		goto p_err_state;
	}

	if (test_bit(VIPX_DEVICE_STATE_START, &device->state))
		return 0;

#ifdef TEMP_RT_FRAMEWORK_TEST
	set_bit(VIPX_DEVICE_STATE_START, &device->state);
	return 0;
#endif
	ret = vipx_system_start(&device->system);
	if (ret)
		goto p_err_system;

	ret = vipx_debug_start(&device->debug);
	if (ret)
		goto p_err_debug;

	set_bit(VIPX_DEVICE_STATE_START, &device->state);

	vipx_leave();
	return 0;
p_err_debug:
	vipx_system_stop(&device->system);
p_err_system:
p_err_state:
	return ret;
}

static int __vipx_device_stop(struct vipx_device *device)
{
	vipx_enter();
	if (!test_bit(VIPX_DEVICE_STATE_START, &device->state))
		return 0;

#ifdef TEMP_RT_FRAMEWORK_TEST
	clear_bit(VIPX_DEVICE_STATE_START, &device->state);
	return 0;
#endif
	vipx_debug_stop(&device->debug);
	vipx_system_stop(&device->system);
	clear_bit(VIPX_DEVICE_STATE_START, &device->state);

	vipx_leave();
	return 0;
}

static int __vipx_device_power_on(struct vipx_device *device)
{
	int ret;

	vipx_enter();
#if defined(CONFIG_PM)
	ret = pm_runtime_get_sync(device->dev);
	if (ret) {
		vipx_err("Failed to get pm_runtime sync (%d)\n", ret);
		goto p_err;
	}
#else
	ret = vipx_device_runtime_resume(device->dev);
	if (ret)
		goto p_err;
#endif

	vipx_leave();
p_err:
	return ret;
}

static int __vipx_device_power_off(struct vipx_device *device)
{
	int ret;

	vipx_enter();
#if defined(CONFIG_PM)
	ret = pm_runtime_put_sync(device->dev);
	if (ret) {
		vipx_err("Failed to put pm_runtime sync (%d)\n", ret);
		goto p_err;
	}
#else
	ret = vipx_device_runtime_suspend(device->dev);
	if (ret)
		goto p_err;
#endif

	vipx_leave();
p_err:
	return ret;
}

int vipx_device_start(struct vipx_device *device)
{
	int ret;

	vipx_enter();
	ret = __vipx_device_start(device);
	if (ret)
		goto p_err;

	vipx_leave();
p_err:
	return ret;
}

int vipx_device_stop(struct vipx_device *device)
{
	int ret;

	vipx_enter();
	ret = __vipx_device_stop(device);
	if (ret)
		goto p_err;

	vipx_leave();
p_err:
	return ret;
}

int vipx_device_open(struct vipx_device *device)
{
	int ret;

	vipx_enter();
	if (test_bit(VIPX_DEVICE_STATE_OPEN, &device->state)) {
		ret = -EINVAL;
		vipx_warn("device was already opened\n");
		goto p_err_state;
	}

#ifdef TEMP_RT_FRAMEWORK_TEST
	device->system.graphmgr.current_model = NULL;
	set_bit(VIPX_DEVICE_STATE_OPEN, &device->state);
	return 0;
#endif

	ret = vipx_system_open(&device->system);
	if (ret)
		goto p_err_system;

	ret = vipx_debug_open(&device->debug);
	if (ret)
		goto p_err_debug;

	ret = __vipx_device_power_on(device);
	if (ret)
		goto p_err_power;

	ret = vipx_system_fw_bootup(&device->system);
	if (ret)
		goto p_err_boot;

	set_bit(VIPX_DEVICE_STATE_OPEN, &device->state);

	vipx_leave();
	return 0;
p_err_boot:
	__vipx_device_power_off(device);
p_err_power:
	vipx_debug_close(&device->debug);
p_err_debug:
	vipx_system_close(&device->system);
p_err_system:
p_err_state:
	return ret;
}

int vipx_device_close(struct vipx_device *device)
{
	vipx_enter();
	if (!test_bit(VIPX_DEVICE_STATE_OPEN, &device->state)) {
		vipx_warn("device is already closed\n");
		goto p_err;
	}

	if (test_bit(VIPX_DEVICE_STATE_START, &device->state))
		__vipx_device_stop(device);

#ifdef TEMP_RT_FRAMEWORK_TEST
	clear_bit(VIPX_DEVICE_STATE_OPEN, &device->state);
	return 0;
#endif
	__vipx_device_power_off(device);
	vipx_debug_close(&device->debug);
	vipx_system_close(&device->system);
	clear_bit(VIPX_DEVICE_STATE_OPEN, &device->state);

	vipx_leave();
p_err:
	return 0;
}

static int vipx_device_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev;
	struct vipx_device *device;

	vipx_enter();
	dev = &pdev->dev;

	device = devm_kzalloc(dev, sizeof(*device), GFP_KERNEL);
	if (!device) {
		ret = -ENOMEM;
		vipx_err("Fail to alloc device structure\n");
		goto p_err_alloc;
	}

	get_device(dev);
	device->dev = dev;
	dev_set_drvdata(dev, device);

	ret = vipx_system_probe(device);
	if (ret)
		goto p_err_system;

	ret = vipx_core_probe(device);
	if (ret)
		goto p_err_core;

	ret = vipx_debug_probe(device);
	if (ret)
		goto p_err_debug;

	iovmm_set_fault_handler(dev, vipx_fault_handler, NULL);
	pm_runtime_enable(dev);

	vipx_leave();
	vipx_info("vipx device is initilized\n");
	return 0;
p_err_debug:
	vipx_core_remove(&device->core);
p_err_core:
	vipx_system_remove(&device->system);
p_err_system:
	devm_kfree(dev, device);
p_err_alloc:
	vipx_err("vipx device is not registered (%d)\n", ret);
	return ret;
}

static int vipx_device_remove(struct platform_device *pdev)
{
	struct vipx_device *device;

	vipx_enter();
	device = dev_get_drvdata(&pdev->dev);

	vipx_debug_remove(&device->debug);
	vipx_core_remove(&device->core);
	vipx_system_remove(&device->system);
	devm_kfree(device->dev, device);
	vipx_leave();
	return 0;
}

static void vipx_device_shutdown(struct platform_device *pdev)
{
	struct vipx_device *device;

	vipx_enter();
	device = dev_get_drvdata(&pdev->dev);
	vipx_leave();
}

#if defined(CONFIG_OF)
static const struct of_device_id exynos_vipx_match[] = {
	{
		.compatible = "samsung,exynos-vipx",
	},
	{}
};
MODULE_DEVICE_TABLE(of, exynos_vipx_match);
#endif

static struct platform_driver vipx_driver = {
	.probe		= vipx_device_probe,
	.remove		= vipx_device_remove,
	.shutdown	= vipx_device_shutdown,
	.driver = {
		.name	= "exynos-vipx",
		.owner	= THIS_MODULE,
		.pm	= &vipx_pm_ops,
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(exynos_vipx_match)
#endif
	}
};

// TODO temp code
extern int __init vertex_device_init(void);
extern void __exit vertex_device_exit(void);

static int __init vipx_device_init(void)
{
	int ret;

	vipx_enter();
	ret = platform_driver_register(&vipx_driver);
	if (ret)
		vipx_err("platform driver for vipx is not registered(%d)\n",
				ret);

	vertex_device_init();

	vipx_leave();
	return ret;
}

static void __exit vipx_device_exit(void)
{
	vipx_enter();
	vertex_device_exit();
	platform_driver_unregister(&vipx_driver);
	vipx_leave();
}

#if defined(MODULE)
module_init(vipx_device_init);
#else
late_initcall(vipx_device_init);
#endif
module_exit(vipx_device_exit);

MODULE_AUTHOR("@samsung.com>");
MODULE_DESCRIPTION("Exynos VIPx driver");
MODULE_LICENSE("GPL");
