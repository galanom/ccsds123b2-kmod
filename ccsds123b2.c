// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/module.h>
#include <linux/init.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>

#include "ccsds123b2.h"

#define MODULE_NAME "ccsds123b2"


static struct dentry *dbg_dir, *dbg_cfg_dir, *dbg_stats_dir, *parent_dir, *tmp_dir;

static const struct file_operations fops = {
	.owner = THIS_MODULE,
};

static int __init ccsds123b2_debug_init(void)
{
	// Generate debugfs entries
	dbg_dir = debugfs_create_dir(MODULE_NAME, NULL);
	if (!dbg_dir)
		goto err;
	dbg_cfg_dir = debugfs_create_dir("cfg", dbg_dir);
	if (!dbg_cfg_dir)
		goto err;
	dbg_stats_dir = debugfs_create_dir("stats", dbg_dir);
	if (!dbg_stats_dir)
		goto err;

	for (int i = 0; i < sizeof(entry)/sizeof(entry[0]); ++i) {
		if (entry[i].value < 32)
			parent_dir = dbg_dir;
		else if (entry[i].value >= 256)
			parent_dir = dbg_stats_dir;
		else
			parent_dir = dbg_cfg_dir;
		tmp_dir = debugfs_create_file(
			entry[i].name,
			0444,
			parent_dir,
			&entry[i].value,
			&fops);
		if (!tmp_dir)
			goto err;
	}

	pr_info("%s: started.\n", MODULE_NAME);
	return 0;
err:
	pr_err("%s: error creating debugfs entries, exiting.\n", MODULE_NAME);
	debugfs_remove_recursive(dbg_dir);
	return -ENOMEM;
}

static void __exit ccsds123b2_debug_exit(void)
{
	debugfs_remove_recursive(dbg_dir);
	pr_info("%s: exited.\n", MODULE_NAME);
}

module_init(ccsds123b2_debug_init);
module_exit(ccsds123b2_debug_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Ioannis Galanommatis");
MODULE_DESCRIPTION("ccsds123b2 driver");

