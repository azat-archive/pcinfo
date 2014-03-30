/**
 * dcinfo - dentry cache
 *
 * Tested on 3.14
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

/**
 * dentry cache info driver
 */

static int dcInfoInit(void) __init;
static void dcInfoExit(void) __exit;
static int dcInfoOpen(struct inode *inode, struct file *file);

module_init(dcInfoInit);
module_exit(dcInfoExit);

static struct file_operations dcInfoOperations = {
    .open = dcInfoOpen,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

static int __init dcInfoInit(void)
{
    proc_create("dentrycache_info", 0, NULL, &dcInfoOperations);
    return 0;
}
static void __exit dcInfoExit(void)
{
    remove_proc_entry("dentrycache_info", NULL);
}

static int dcInfoShow(struct seq_file *m, void *v)
{
    return 0;
}

static int dcInfoOpen(struct inode *inode, struct file *file)
{
    return single_open(file, dcInfoShow, NULL);
}
/**
 * \ dentry cache info driver
 */
