// SPDX-License-Identifier: GPL-2.0-or-later
#define pr_fmt(fmt) "%s[%d] " fmt, __func__, __LINE__
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/of.h>

#include "ccsds123b2.h"

#define MODULE_NAME "ccsds123b2"
#define CDEV_NAME "ccsds"

static atomic_t cores = ATOMIC_INIT(0);

struct dev_ctx {
	int id;
	struct platform_device *pdev;
	struct miscdevice cdev;
	struct dentry *node_dir;
	void __iomem *iobase, *src_kbuf, *dst_kbuf;
	size_t src_len, dst_len;
};

static struct dentry *debugfs_dir;

static const struct of_device_id ccsds123b2_of_match[] = {
	{ .compatible = "xlnx,ccsds123b2" },
	{},
};

MODULE_DEVICE_TABLE(of, ccsds123b2_of_match);

static ssize_t dbg_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	char kbuf[16];
	void __iomem *reg = (u32 *)file->private_data;
	if (*ppos > 0)
		return 0;
	u32 val = ioread32(reg);
	snprintf(kbuf, 16, "%u\n", val);
	return simple_read_from_buffer(ubuf, count, ppos, kbuf, sizeof(kbuf) - 1);
}

static ssize_t dbg_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	int ret;
	u32 val;
	char kbuf[16];
	void __iomem *reg = (u32 *)file->private_data;
	if (count >= sizeof(kbuf) - 1)
		return -EINVAL;
	if (copy_from_user(kbuf, ubuf, count))
		return -EFAULT;
	kbuf[count] = '\0';
	ret = kstrtoint(kbuf, 0, &val);
	if (ret)
		return -EINVAL;
	iowrite32(val, reg);
	return count;
}



static int ccsds123b2_probe(struct platform_device *pdev)
{
	int ret;
	char snode[2];
	struct resource *res;
	struct device_node *np = pdev->dev.of_node;
	struct dev_ctx *ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	struct dentry *cfg_dir, *stats_dir, *parent_dir, *tmp_dir;
	const struct file_operations *fopsp;
	static const struct file_operations dbg_fops_ro = {
		.owner = THIS_MODULE,
		.read = dbg_read,
	};
	static const struct file_operations dbg_fops_rw = {
		.owner = THIS_MODULE,
		.read = dbg_read,
		.write = dbg_write,
	};
	static struct file_operations cdev_fops = {
		.owner = THIS_MODULE,
		.mmap = cdev_mmap,
	};

	ctx->id = atomic_fetch_add(1, &cores);
	ctx->pdev = pdev;

	// Map AXI-Lite controller interface
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_err("core %d: failed to discover device I/O resources\n", ctx->id);
		return -ENODEV;
	}

	ctx->iobase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(ctx->iobase))
		goto err_ioremap;

	// Generate debugfs entries
	snode[0] = ctx->id + '0';	// FIXME assumes id < 10
	snode[1] = '\0';
	ctx->node_dir = debugfs_create_dir(snode, debugfs_dir);
	if (!ctx->node_dir)
		goto err_debugfs_node;
	cfg_dir = debugfs_create_dir("cfg", ctx->node_dir);
	if (!cfg_dir)
		goto err_debugfs_node;
	stats_dir = debugfs_create_dir("stats", ctx->node_dir);
	if (!stats_dir)
		goto err_debugfs_node;

	for (int i = 0; i < sizeof(entry)/sizeof(entry[0]); ++i) {
		if (entry[i].value < 32) {
			fopsp = &dbg_fops_rw;
			parent_dir = ctx->node_dir;
		} else if (entry[i].value < 256) {
			fopsp = &dbg_fops_rw;
			parent_dir = cfg_dir;
		} else {
			fopsp = &dbg_fops_ro;
			parent_dir = stats_dir;
		}

		tmp_dir = debugfs_create_file(
				entry[i].name,
				0444,
				parent_dir,
				ctx->iobase + entry[i].value,
				fopsp);
		if (!tmp_dir)
			goto err_debugfs_node;
	}

	// Create character device entry
	ctx->cdev.name = CDEV_NAME;
	ctx->cdev.minor = ctx->id;
	ctx->cdev.fops = &cdev_fops;
	ret = misc_register(&ctx->cdev);
	if (ret)
		goto err_miscdev;

	platform_set_drvdata(pdev, ctx);
	pr_info("core %d: probed\n", ctx->id);
	return 0;
err_ioremap:
	pr_err("core %d: error mapping IP core control interface address\n", ctx->id);
	return PTR_ERR(ctx->iobase);
err_debugfs_node:
	pr_err("core %d: error creating debugfs entries\n", ctx->id);
	debugfs_remove_recursive(ctx->node_dir);
	return -ENOMEM;
err_miscdev:
	pr_err("core %d: error creating device node\n", ctx->id);
	debugfs_remove_recursive(ctx->node_dir);
	return ret;
}

// TODO older kernels expect return int
static void ccsds123b2_remove(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct dev_ctx *ctx = platform_get_drvdata(pdev);

	pr_info("core %d: removed\n", ctx->id);
	return;
}

static struct platform_driver ccsds123b2_driver = {
	.probe = ccsds123b2_probe,
	.remove = ccsds123b2_remove,
	.driver = {
		.name = "ccsds123b2",
		.of_match_table = ccsds123b2_of_match,
	},
};


static int __init ccsds123b2_init(void)
{
	int ret = 0;
	ret = platform_driver_register(&ccsds123b2_driver);
	if (ret)
		goto err_plat;
	debugfs_dir = debugfs_create_dir(MODULE_NAME, NULL);
	if (!debugfs_dir)
		goto err_debugfs_main;

	pr_info("started.\n");
	return 0;
err_plat:
	pr_err("error %d registering platform driver\n", ret);
	return ret;
err_debugfs_main:
	debugfs_remove_recursive(debugfs_dir);
	pr_err("error creating debugfs main directory, exiting.\n");
	return -ENOMEM;
}




static void __exit ccsds123b2_exit(void)
{
	debugfs_remove_recursive(debugfs_dir);
	platform_driver_unregister(&ccsds123b2_driver);
	pr_info("exited.\n");
}

module_init(ccsds123b2_init);
module_exit(ccsds123b2_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Ioannis Galanommatis");
MODULE_DESCRIPTION("CCSDS123.0-B-2 IP core driver");
