
/**
 * pcinfo - pagecache info
 *
 * XXX: use /proc or debufs?
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
#include <asm/pgtable.h>
#include "compat.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Azat Khuzhin <a3at.mail@gmail.com>");

// Move to header
static int pcInfoInit(void) __init;
static void pcInfoExit(void) __exit;
static int deviceOpen(struct inode *inode, struct file *file);
static int deviceRelease(struct inode *inode, struct file *file);
static ssize_t deviceRead(struct file *filePtr, char *buffer,
                          size_t length, loff_t *offset);
static ssize_t deviceWrite(struct file *filePtr, const char *buffer,
                           size_t length, loff_t *offset);

static struct file_operations deviceOperations = {
    .read = deviceRead,
    .write = deviceWrite,
    .open = deviceOpen,
    .release = deviceRelease
};

module_init(pcInfoInit);
module_exit(pcInfoExit);

#define SUCCESS 0
#define DEVICE_NAME "pcinfo"

static int majorNumber;
struct FileInfo
{
    struct rb_node node;

    struct inode *host;
    size_t size;
};
struct Base
{
    int deviceOpened; /* prevent multiple access to device */

    struct rb_root rbRoot;
};

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

/** XXX: attach it to inode */
static struct Base base;


/**
 * Add MKDEV()
 */
static int __init pcInfoInit(void)
{
    majorNumber = register_chrdev(0, DEVICE_NAME, &deviceOperations);

    if (majorNumber < 0) {
        printk(KERN_ALERT "Registering char device failed with %d\n", majorNumber);
        return majorNumber;
    }

    printk(KERN_INFO "pcInfo: loaded (major number: %d)", majorNumber);
    return SUCCESS;
}

static void __exit pcInfoExit(void)
{
    printk(KERN_INFO "pcInfo: exit\n");
}

static int deviceOpen(struct inode *inode, struct file *file)
{
    if (base.deviceOpened) {
        return -EBUSY;
    }

    base.deviceOpened++;
    try_module_get(THIS_MODULE);

    printk(KERN_INFO "pcInfo: device is opened\n");
    return SUCCESS;
}

static int deviceRelease(struct inode *inode, struct file *file)
{
    base.deviceOpened--;
    module_put(THIS_MODULE);

    printk(KERN_INFO "pcInfo: device is released\n");
    return SUCCESS;
}

static ssize_t deviceRead(struct file *filePtr, char *buffer,
                          size_t length, loff_t *offset)
{
    pg_data_t *pgd;

    for_each_online_pgdat(pgd) {
        struct page *page = pgdat_page_nr(pgd, 0);
        struct page *end = pgdat_page_nr(pgd, node_spanned_pages(pgd->node_id));

        for (; page != end; ++page) {
            struct address_space *mapping = page->mapping;
            /** XXX: slab or even avoid this */
            struct FileInfo *info = kmalloc(sizeof(struct FileInfo), GFP_KERNEL);
            struct rb_node **existed;
            BUG_ON(!info);

            if (!mapping || !mapping->host) {
                continue;
            }

            info->host = mapping->host;
            info->size += mapping->nrpages * PAGE_SIZE;
            existed = rbFind(info, &base.rbRoot);
            /** XXX: augment */
            if (*existed) {
                struct FileInfo *eInfo = rb_entry(*existed, struct FileInfo, node);
                eInfo->size += info->size;

                kfree(info);
            } else {
                rbInsert(info, &base.rbRoot);
            }
        }
    }

    return -EINVAL;
}

static ssize_t deviceWrite(struct file *filePtr, const char *buffer,
                           size_t length, loff_t *offset)
{
    printk(KERN_ALERT "Sorry, this operation isn't supported.\n");
    return -EINVAL;
}
