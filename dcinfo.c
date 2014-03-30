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

static enum lru_status dcWalk(struct list_head *item, spinlock_t *lock, void *m)
{
   struct dentry *dentry = container_of(item, struct dentry, d_lru);
   seq_printf(m, "%pd4\n", dentry);

   return LRU_SKIP;
}
static int dcInfoShow(struct seq_file *m, void *v)
{
    char buf[PAGE_SIZE]; /** XXX dynamically */
    int len = get_filesystem_list(buf);

    if (len <= 0) {
        return 0;
    }

    while (len > 0) {
        char dev[PATH_MAX], name[PATH_MAX]; /** XXX dynamically */
        struct file_system_type *fs;
        struct super_block *sb;

        len -= sscanf(buf, "%s %s\n", dev, name);
        seq_printf(m, "Filesystem: %s\n", name);
        fs = get_fs_type(name);
        if (!fs) {
            continue;
        }

        hlist_for_each_entry(sb, &fs->fs_supers, s_instances) {
            rcu_read_lock();
            list_lru_walk(&sb->s_dentry_lru, dcWalk, m, UINT_MAX);
            rcu_read_unlock();
        }
    }

    return 0;
}

static int dcInfoOpen(struct inode *inode, struct file *file)
{
    return single_open(file, dcInfoShow, NULL);
}
/**
 * \ dentry cache info driver
 */
