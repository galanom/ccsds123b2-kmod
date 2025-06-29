// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include "ccsds123b2.h"

#define MODULE_NAME "ccsds123b2"
#define DEV_NODE "ccsds"

typedef u32 reg_t;
static atomic_t cores = ATOMIC_INIT(0);
const phys_addr_t ipcore_phys_addr = 0xA0000000;

struct dev_priv {
	int id;
	struct platform_device *pdev;
	struct dentry *node_dir;
	void __iomem *vaddr;
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
	void __iomem *reg = (reg_t *)file->private_data;
	if (*ppos > 0)
		return 0;
	reg_t val = ioread32(reg);
	snprintf(kbuf, 16, "%u\n", val);
	return simple_read_from_buffer(ubuf, count, ppos, kbuf, sizeof(kbuf) - 1);
}

static ssize_t dbg_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	int ret;
	reg_t val;
	char kbuf[16];
	void __iomem *reg = (reg_t *)file->private_data;
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

static const struct file_operations fops_ro = {
	.owner = THIS_MODULE,
	.read = dbg_read,
};

static const struct file_operations fops_rw = {
	.owner = THIS_MODULE,
	.read = dbg_read,
	.write = dbg_write,
};

static int ccsds123b2_probe(struct platform_device *pdev)
{
	char snode[2];
	struct device_node *np = pdev->dev.of_node;
	static struct dentry *cfg_dir, *stats_dir, *parent_dir, *tmp_dir;
	const struct file_operations *fops;
	struct resource *res;
	struct dev_priv *priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	priv->id = atomic_fetch_add(1, &cores);
	priv->pdev = pdev;

	// Map AXI-Lite controller interface
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		pr_err("core %d: failed to discover device I/O resources\n", priv->id);
		return -ENODEV;
	}

	priv->vaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->vaddr))
		goto err_ioremap;

	// Generate debugfs entries
	snode[0] = priv->id + '0';	// FIXME assumes id < 10
	snode[1] = '\0';
	priv->node_dir = debugfs_create_dir(snode, debugfs_dir);
	if (!priv->node_dir)
		goto err_debugfs_node;
	cfg_dir = debugfs_create_dir("cfg", priv->node_dir);
	if (!cfg_dir)
		goto err_debugfs_node;
	stats_dir = debugfs_create_dir("stats", priv->node_dir);
	if (!stats_dir)
		goto err_debugfs_node;

	for (int i = 0; i < sizeof(entry)/sizeof(entry[0]); ++i) {
		if (entry[i].value < 32) {
			fops = &fops_rw;
			parent_dir = priv->node_dir;
		} else if (entry[i].value < 256) {
			fops = &fops_rw;
			parent_dir = cfg_dir;
		} else {
			fops = &fops_ro;
			parent_dir = stats_dir;
		}

		tmp_dir = debugfs_create_file(
			entry[i].name,
			0444,
			parent_dir,
			priv->vaddr + entry[i].value,
			fops);
		if (!tmp_dir)
			goto err_debugfs_node;
	}


	platform_set_drvdata(pdev, priv);
	pr_info("core %d probed\n", priv->id);
	return 0;
err_ioremap:
	pr_err("%s: core %d: error mapping IP core control interface address\n", MODULE_NAME, priv->id);
	return PTR_ERR(priv->vaddr);
err_debugfs_node:
	pr_err("%s: error creating node %d debugfs entries, exiting.\n", MODULE_NAME, priv->id);
	debugfs_remove_recursive(priv->node_dir);
	return -ENOMEM;
}

// TODO older kernels expect return int
static void ccsds123b2_remove(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct dev_priv *priv = platform_get_drvdata(pdev);

	pr_info("core %d removed\n", priv->id);
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

	pr_info("%s: started.\n", MODULE_NAME);
	return 0;
err_plat:
	pr_err("%s: error %d registering platform driver\n", MODULE_NAME, ret);
	return ret;
err_debugfs_main:
	debugfs_remove_recursive(debugfs_dir);
	pr_err("%s: error creating debugfs main directory, exiting.\n", MODULE_NAME);
	return -ENOMEM;
}




static void __exit ccsds123b2_exit(void)
{
	debugfs_remove_recursive(debugfs_dir);
	platform_driver_unregister(&ccsds123b2_driver);
	pr_info("%s: exited.\n", MODULE_NAME);
}

module_init(ccsds123b2_init);
module_exit(ccsds123b2_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Ioannis Galanommatis");
MODULE_DESCRIPTION("ccsds123b2 driver");

