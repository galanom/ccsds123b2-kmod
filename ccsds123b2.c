// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

#include "ccsds123b2.h"

#define MODULE_NAME "ccsds123b2"


const phys_addr_t ipcore_phys_addr = 0xA0000000;
void *ipcore_virt_addr;

static struct dentry *dbg_dir, *dbg_cfg_dir, *dbg_stats_dir, *parent_dir, *tmp_dir;

static ssize_t dbg_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	char kbuf[16];
	u32 reg_off = *(u32 *)file->private_data;
	if (*ppos > 0)
		return 0;
	u32 val = ioread32(ipcore_virt_addr + reg_off);
	pr_info("Reading register %u returned 0x%x\n", reg_off, val);
	snprintf(kbuf, 16, "%u\n", val);
	return simple_read_from_buffer(ubuf, count, ppos, kbuf, sizeof(kbuf) - 1);
}

static ssize_t dbg_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	char kbuf[16];
	u32 reg_off = *(u32 *)file->private_data;
	if (count >= sizeof(kbuf) - 1)
		return -EINVAL;
	if (copy_from_user(kbuf, ubuf, count))
		return -EFAULT;
	kbuf[count] = '\0';
	pr_info("read from reg %d string %s\n", reg_off, kbuf);
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

static int __init ccsds123b2_init(void)
{
	// Generate debugfs entries
	const struct file_operations *fops;

	dbg_dir = debugfs_create_dir(MODULE_NAME, NULL);
	if (!dbg_dir)
		goto err_debugfs;
	dbg_cfg_dir = debugfs_create_dir("cfg", dbg_dir);
	if (!dbg_cfg_dir)
		goto err_debugfs;
	dbg_stats_dir = debugfs_create_dir("stats", dbg_dir);
	if (!dbg_stats_dir)
		goto err_debugfs;

	for (int i = 0; i < sizeof(entry)/sizeof(entry[0]); ++i) {
		if (entry[i].value < 32) {
			fops = &fops_rw;
			parent_dir = dbg_dir;
		} else if (entry[i].value < 256) {
			fops = &fops_rw;
			parent_dir = dbg_cfg_dir;
		} else {
			fops = &fops_ro;
			parent_dir = dbg_stats_dir;
		}

		tmp_dir = debugfs_create_file(
			entry[i].name,
			0444,
			parent_dir,
			&entry[i].value,
			fops);
		if (!tmp_dir)
			goto err_debugfs;
	}

	// Map AXI-Lite controller interface
	ipcore_virt_addr = ioremap(ipcore_phys_addr, PAGE_SIZE);
	if (!ipcore_virt_addr)
		goto err_ioremap;

	pr_info("%s: started.\n", MODULE_NAME);
	return 0;
err_ioremap:
	pr_err("%s: error mapping IP core control interface address\n", MODULE_NAME);
	return -ENOMEM;
err_debugfs:
	pr_err("%s: error creating debugfs entries, exiting.\n", MODULE_NAME);
	debugfs_remove_recursive(dbg_dir);
	return -ENOMEM;
}

static void __exit ccsds123b2_exit(void)
{
	debugfs_remove_recursive(dbg_dir);
	if (ipcore_virt_addr)
		iounmap(ipcore_virt_addr);
	pr_info("%s: exited.\n", MODULE_NAME);
}

module_init(ccsds123b2_init);
module_exit(ccsds123b2_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Ioannis Galanommatis");
MODULE_DESCRIPTION("ccsds123b2 driver");

