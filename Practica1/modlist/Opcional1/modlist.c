#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>


MODULE_LICENSE("GPL");

#define BUFFER_LENGTH 256

static struct proc_dir_entry* proc_entry;
struct list_head mylist; /* Lista enlazada */

/* Nodos de la lista */
struct list_item {
    #ifdef PARTE_OPCIONAL
        char* data;
    #else
        int data;
    #endif  
    struct list_head links;
};

void cleanup() {
    struct list_head* pos, * e;
    struct list_item* item = NULL;

    list_for_each_safe(pos, e, &mylist) {
        item = list_entry(pos, struct list_item, links);
        #ifdef PARTE_OPCIONAL
                vfree(item->data);
        #endif
        list_del(pos);
        vfree(item);
    }

}

void scanString(char * kbuf) {
    char pal[BUFFER_LENGTH];

    if (sscanf(kbuf, "add %256s", &pal) == 1) {
        struct list_item* item = vmalloc(sizeof(struct list_item));
        int tamano = stnlen(pal);
        item->data = vmalloc(sizeof(char) * tamano);
        strcpy(item->data, pal);
        list_add_tail(&item->links, &mylist);
    }
    else if (sscanf(kbuf, "remove %256s", &pal) == 1) {
        struct list_head* pos, * e;
        struct list_item* item = NULL;

        list_for_each_safe(pos, e, &mylist) {

            item = list_entry(pos, struct list_item, links);
            if (strcmp(item->data, pal)) {
                vfree(item->data);
                list_del(pos);
                vfree(item);
            }

        }
    }
    else if (strcmp(kbuf, "cleanup\0")) {
        cleanup();
    }
}

void scanInt(char* kbuf) {
    int numero;

    if (sscanf(kbuf, "add %d", &numero) == 1) {
        struct list_item* item = vmalloc(sizeof(struct list_item));
        item->data = pal;
        list_add_tail(&item->links, &mylist);
    }
    else if (sscanf(kbuf, "remove %d", &numero) == 1) {
        struct list_head* pos, * e;
        struct list_item* item = NULL;

        list_for_each_safe(pos, e, &mylist) {

            item = list_entry(pos, struct list_item, links);
            if (item->data == pal) {
                list_del(pos);
                vfree(item);
            }

        }
    }
    else if (strcmp(kbuf, "cleanup\0")) {
        cleanup();
    }
}

static ssize_t modlist_write(struct file* filp, const char __user* buf, size_t len, loff_t* off) {

    int available_space = BUFFER_LENGTH - 1;
    char* pal;
    int p;
    char kbuf[BUFFER_LENGTH];

    if ((*off) > 0) /* The application can write in this entry just once !! */
        return 0;

    if (len > available_space) {
        printk(KERN_INFO "clipboard: not enough space!!\n");
        return -ENOSPC;
    }


    if (copy_from_user(&kbuf[0], buf, len)) {
        return -EFAULT;
    }


    kbuf[len] = '\0'; /* Add the `\0' */
    *off += len;
   
    #ifdef PARTE_OPCIONAL
        scanString(kbuf);
    #else
        scanInt(kbuf);
    #endif
    return len;
}

static ssize_t modlist_read(struct file* filp, char __user* buf, size_t len, loff_t* off) {
    char kbuf[BUFFER_LENGTH];
    int nBytes = 0;
    struct list_head* pos;
    struct list_item* item = NULL;

    if ((*off) > 0) /* Tell the application that there is nothing left to read */
        return 0;

    list_for_each(pos, &mylist) {
        item = list_entry(pos, struct list_item, links);
        #ifdef PARTE_OPCIONAL
            nBytes += sprintf(&kbuf[nBytes], "%s\n", item->data);
        #else
            nBytes += sprintf(&kbuf[nBytes], "%d\n", item->data);
        #endif        
    }

    if (len < nBytes)
        return -ENOSPC;

    /* Transfer data from the kernel to userspace */
    if (copy_to_user(buf, kbuf, nBytes))
        return -EINVAL;

    (*off) += len;  /* Update the file pointer */

    return nBytes;
}

static const struct proc_ops proc_entry_fops = {
    .proc_read = modlist_read,
    .proc_write = modlist_write,
};

int init_modlist_module(void)
{
    int ret = 0;
    INIT_LIST_HEAD(&mylist);
    proc_entry = proc_create("modlist", 0666, NULL, &proc_entry_fops);
    if (proc_entry == NULL) {
        ret = -ENOMEM;
        printk(KERN_INFO "modlist: Can't create /proc entry\n");
    }
    else {
        printk(KERN_INFO "modlist: Module loaded\n");
    }
    return ret;

}


void exit_modlist_module(void)
{
    cleanup();
    remove_proc_entry("modlist", NULL);
    printk(KERN_INFO "modlist: Module unloaded.\n");
}


module_init(init_modlist_module);
module_exit(exit_modlist_module);
