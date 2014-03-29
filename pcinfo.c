
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
};
/** XXX: attach it to inode */
static struct Base base;


/** RB operations */
static struct rb_node** rbFind(const struct FileInfo *node, struct rb_root *root)
{
    struct rb_node **new = &root->rb_node, *parent = NULL;

    while (*new) {
        parent = *new;
        if (node->host < rb_entry(parent, struct FileInfo, node)->host) {
            new = &parent->rb_left;
        } else {
            new = &parent->rb_right;
        }
    }

    return new;
}
static void rbInsert(struct FileInfo *node, struct rb_root *root)
{
    struct rb_node **new, *parent = NULL;
    new = rbFind(node, root);

    rb_link_node(&node->node, parent, new);
    rb_insert_color(&node->node, root);
}
/** \ RB operations */


/** proc driver */
static struct file_operations operations = {
    .open = pcInfoOpen,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

static int __init pcInfoInit(void)
{
    proc_create("pagecache_info", 0, NULL, &operations);
    return 0;
}
static void __exit pcInfoExit(void)
{
    remove_proc_entry("pagecache_info", NULL);
}

static int pcInfoShow(struct seq_file *m, void *v)
{
    pg_data_t *pgd;
    struct FileInfo *eInfo, *tmp;

    for_each_online_pgdat(pgd) {
        struct page *page = pgdat_page_nr(pgd, 0);
        struct page *end = pgdat_page_nr(pgd, node_spanned_pages(pgd->node_id));

        for (; page != end; ++page) {
            struct address_space *mapping = page->mapping;
            /** XXX: slab or even avoid this */
            struct FileInfo *info;
            struct rb_node **existed;

            if (!mapping || !mapping->host) {
                continue;
            }

            info = kmalloc(sizeof(struct FileInfo), GFP_KERNEL);
            BUG_ON(!info);

            info->host = mapping->host;
            info->size += mapping->nrpages * PAGE_SIZE;

            existed = rbFind(info, &base.rbRoot);
            /** XXX: augment */
            if (*existed) {
                eInfo = rb_entry(*existed, struct FileInfo, node);
                eInfo->size += info->size;

                kfree(info);
            } else {
                rbInsert(info, &base.rbRoot);
            }
        }
    }

    rbtree_postorder_for_each_entry_safe(eInfo, tmp, &base.rbRoot, node) {
        seq_printf(m, "[%lu] %p: %zu\n",
                   eInfo->host->i_ino, eInfo->host, eInfo->size);

        rb_erase(&eInfo->node, &base.rbRoot);
    }

    return 0;
}

static int pcInfoOpen(struct inode *inode, struct file *file)
{
    return single_open(file, pcInfoShow, NULL);
}
/** \proc driver */
