
/**
 * pcinfo - pagecache info
 *
 * Tested on 3.14
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/mmzone.h>
#include <linux/mm.h>
#include <linux/rbtree.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/pgtable.h>
#include "compat.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Azat Khuzhin <a3at.mail@gmail.com>");

static int pcInfoInit(void) __init;
static void pcInfoExit(void) __exit;
static int pcInfoOpen(struct inode *inode, struct file *file);
static int pcInfoLruOpen(struct inode *inode, struct file *file);
static int pcInfoAllOpen(struct inode *inode, struct file *file);

module_init(pcInfoInit);
module_exit(pcInfoExit);

struct FileInfo
{
    struct rb_node node;

    struct inode *host;
    size_t size;
};
struct Base
{
    struct rb_root rbRoot;
    struct kmem_cache *cachep;
};


/** RB operations */
int rbInsert(struct FileInfo *node, struct rb_root *root, struct rb_node **e)
{
    size_t ino;
    struct rb_node **new = &root->rb_node, *parent = NULL;

    while (*new) {
        parent = *new;
        ino = rb_entry(parent, struct FileInfo, node)->host->i_ino;
        if (node->host->i_ino < ino) {
            new = &parent->rb_left;
        } else if (node->host->i_ino > ino) {
            new = &parent->rb_right;
        } else {
            *e = parent;
            return -EEXIST;
        }
    }

    rb_link_node(&node->node, parent, new);
    rb_insert_color(&node->node, root);
    return 0;
}
/** \ RB operations */


/**
 * pagecache info driver
 */

/** XXX: attach it to inode */
static struct Base pcBase;

static struct file_operations pcInfoOperations = {
    .open = pcInfoOpen,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};
static struct file_operations pcInfoLruOperations = {
    .open = pcInfoLruOpen,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};
static struct file_operations pcInfoAllOperations = {
    .open = pcInfoAllOpen,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

static int __init pcInfoInit(void)
{
    proc_create("pagecache_info", 0, NULL, &pcInfoOperations);
    proc_create("pagecache_info_lru", 0, NULL, &pcInfoLruOperations);
    proc_create("pagecache_info_all", 0, NULL, &pcInfoAllOperations);

    pcBase.cachep = kmem_cache_create("pagecache_info_rb_nodes",
                                    sizeof(struct FileInfo),
                                    0,
                                    (SLAB_RECLAIM_ACCOUNT| SLAB_MEM_SPREAD),
                                    NULL);
    if (!pcBase.cachep) {
        return -ENOMEM;
    }

    return 0;
}
static void __exit pcInfoExit(void)
{
    remove_proc_entry("pagecache_info", NULL);
    remove_proc_entry("pagecache_info_lru", NULL);
    remove_proc_entry("pagecache_info_all", NULL);
    kmem_cache_destroy(pcBase.cachep);
}

static inline u64 div(u64 num, int factor)
{
    do_div(num, factor);
    return num;
}
static int pcInfoShow(struct seq_file *m, void *v)
{
    pg_data_t *pgd;
    struct FileInfo *eInfo, *tmp;

    pcBase.rbRoot.rb_node = NULL;

    for_each_online_pgdat(pgd) {
        struct page *page = pgdat_page_nr(pgd, 0);
        struct page *end = pgdat_page_nr(pgd, node_spanned_pages(pgd->node_id));

        for (; page <= end; ++page) {
            struct address_space *mapping = page->mapping;
            struct FileInfo *info;
            struct rb_node *existed;

            if (PageAnon(page) || !PagePrivate(page) ||
                !mapping || !mapping->host) {
                continue;
            }

            info = kmem_cache_alloc(pcBase.cachep, GFP_KERNEL);
            if (!info) {
                return -ENOMEM;
            }

            /** XXX: we can lost it */
            info->host = mapping->host;
            info->size = mapping->nrpages * PAGE_SIZE;

            /** XXX: augment */
            if (rbInsert(info, &pcBase.rbRoot, &existed) == -EEXIST) {
                kmem_cache_free(pcBase.cachep, info);
            }
        }
    }

    rbtree_postorder_for_each_entry_safe(eInfo, tmp, &pcBase.rbRoot, node) {
        struct dentry *dentry = d_find_alias(eInfo->host);
        size_t inodeSize = eInfo->host->i_size;

        seq_printf(m, "%pd4 (%lu): %llu %% (%lluK from %lluK)\n",
                   dentry, eInfo->host->i_ino,
                   div(eInfo->size, inodeSize) * 100,
                   div(eInfo->size, 1024), div(inodeSize, 1024));

        kmem_cache_free(pcBase.cachep, eInfo);
    }

    return 0;
}

static int pcInfoOpen(struct inode *inode, struct file *file)
{
    return single_open(file, pcInfoShow, NULL);
}


/** LRU version */
static const char *supportedFs[] = { "ext4" };

static enum lru_status pcLruWalk(struct list_head *item, spinlock_t *lock, void *m)
{
   struct inode *inode = container_of(item, struct inode, i_lru);
   struct dentry *dentry = d_find_alias(inode);

   size_t cached = inode->i_mapping ? inode->i_mapping->nrpages * PAGE_SIZE : 0;
   seq_printf(m, "%pd4 (%lu): %llu %% (%lluK from %lluK)\n",
              dentry, inode->i_ino,
              div(cached, inode->i_size) * 100,
              div(cached, 1024), div(inode->i_size, 1024));

   return LRU_SKIP;
}
static int pcInfoLruShow(struct seq_file *m, void *v)
{
    size_t i;
    for (i = 0; i < ARRAY_SIZE(supportedFs); ++i) {
        struct file_system_type *fs;
        struct super_block *sb;
        const char *name = supportedFs[i];

        seq_printf(m, "Filesystem: %s\n", name);
        fs = get_fs_type(name);
        if (!fs) {
            continue;
        }

        hlist_for_each_entry(sb, &fs->fs_supers, s_instances) {
            seq_printf(m, "Device: %s\n", sb->s_id);

            rcu_read_lock();
            list_lru_walk(&sb->s_inode_lru, pcLruWalk, m, UINT_MAX);
            rcu_read_unlock();
        }
    }

    return 0;
}

static int pcInfoLruOpen(struct inode *inode, struct file *file)
{
    return single_open(file, pcInfoLruShow, NULL);
}
/** \ LRU version */

/** s_inodes version */
static int pcInfoAllShow(struct seq_file *m, void *v)
{
    size_t i;
    for (i = 0; i < ARRAY_SIZE(supportedFs); ++i) {
        struct file_system_type *fs;
        struct super_block *sb;
        const char *name = supportedFs[i];

        seq_printf(m, "Filesystem: %s\n", name);
        fs = get_fs_type(name);
        if (!fs) {
            continue;
        }

        hlist_for_each_entry(sb, &fs->fs_supers, s_instances) {
            struct inode *inode;
            seq_printf(m, "Device: %s\n", sb->s_id);

            list_for_each_entry(inode, &sb->s_inodes, i_sb_list) {
                /** XXX: avoid code duplication */
                size_t cached = inode->i_mapping ? inode->i_mapping->nrpages * PAGE_SIZE : 0;
                struct dentry *dentry = d_find_alias(inode);

                seq_printf(m, "%pd4 (%lu): %llu %% (%lluK from %lluK)\n",
                           dentry, inode->i_ino,
                           div(cached, inode->i_size) * 100,
                           div(cached, 1024), div(inode->i_size, 1024));
            }
        }
    }

    return 0;
}

static int pcInfoAllOpen(struct inode *inode, struct file *file)
{
    return single_open(file, pcInfoAllShow, NULL);
}
/** \ s_inodes version */

/**
 * \ pagecache info driver
 */
