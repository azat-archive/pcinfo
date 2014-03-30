
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

static int __init pcInfoInit(void)
{
    proc_create("pagecache_info", 0, NULL, &pcInfoOperations);

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
    kmem_cache_destroy(pcBase.cachep);
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
                eInfo = rb_entry(existed, struct FileInfo, node);
                eInfo->size += info->size;

                kmem_cache_free(pcBase.cachep, info);
            }
        }
    }

    rbtree_postorder_for_each_entry_safe(eInfo, tmp, &pcBase.rbRoot, node) {
        struct dentry *dentry = d_find_alias(eInfo->host);

        seq_printf(m, "%pd4 (%lu): %zu\n",
                   dentry, eInfo->host->i_ino, eInfo->size);

        kmem_cache_free(pcBase.cachep, eInfo);
    }

    return 0;
}

static int pcInfoOpen(struct inode *inode, struct file *file)
{
    return single_open(file, pcInfoShow, NULL);
}
/**
 * \ pagecache info driver
 */
