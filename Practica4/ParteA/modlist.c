#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");

#define BUFFER_LENGTH 256

static struct proc_dir_entry *proc_entry;
static struct list_head mylist; /* Lista enlazada */
DEFINE_SPINLOCK(sp);

/* Nodos de la lista */
struct list_item {
    int data;
    struct list_head links;
};

static void add(int numero) {
    struct list_item* item = vmalloc(sizeof(struct list_item));
    item->data = numero;
    spin_lock(&sp);
    list_add_tail(&item->links, &mylist);
    spin_unlock(&sp);
    printk(KERN_INFO "modlist: Elemento %d agregado\n", numero);
}

static void remove(int numero) {
    struct list_head* pos, * e;
    struct list_item* item = NULL;

    spin_lock(&sp);
    list_for_each_safe(pos, e, &mylist) {
        item = list_entry(pos, struct list_item, links);
        if (item->data == numero) {
            list_del(pos);
            vfree(item);
            printk(KERN_INFO "modlist: Elemento %d borrado\n", numero);
        }
    }
    spin_unlock(&sp);
}

static void cleanup(void) {
    struct list_head* pos, * e;
    struct list_item* item = NULL;

    spin_lock(&sp);
    list_for_each_safe(pos, e, &mylist) {
        item = list_entry(pos, struct list_item, links);
        list_del(pos);
        vfree(item);
    }
    spin_unlock(&sp);
    printk(KERN_INFO "modlist: Lista vaciada\n");
}

static int getDigits(int numero) {
    int r = 1;
    while (numero > 9) {
        numero /= 10;
        r++;
    }
    return r;
}

static ssize_t modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
    int available_space = BUFFER_LENGTH-1;
    int numero;
    char kbuf[BUFFER_LENGTH];
    
    if ((*off) > 0) /* The application can write in this entry just once !! */
        return 0;
  
      if (len > available_space) {
       printk(KERN_INFO "clipboard: not enough space!!\n");
       return -ENOSPC;
      }	

    
    if (copy_from_user( &kbuf[0], buf, len ))  {
            return -EFAULT;	
    }
    

    kbuf[len] = '\0'; /* Add the `\0' */  
    *off+=len; 

    if (sscanf(kbuf, "add %d", &numero) == 1) {
        add(numero);
    } 
    else if (sscanf(kbuf, "remove %d", &numero) == 1){
        remove(numero);
    } 
    else if (strcmp(kbuf,"cleanup\n") == 0) {
        cleanup();
    }
    else {
        printk(KERN_INFO "modlist: Opcion no valida\n");
        return -EINVAL;
    }

    return len;
}

static ssize_t modlist_read(struct file *filp, char __user *buf, size_t len, loff_t *off) {
  char * kbuf;
  int nBytes = 0;
  struct list_head *pos;
  struct list_item* item=NULL;
  int size = 0;
  
  if ((*off) > 0) /* Tell the application that there is nothing left to read */
      return 0;
  
  spin_lock(&sp);
  list_for_each(pos,&mylist){
      item = list_entry(pos, struct list_item, links);
      size += getDigits(item->data) + 1;
  }
  spin_unlock(&sp);

  if (size > 0) // Solo se asigna espacio si la lista no est?? vac??a, para evitar errores
    kbuf = vmalloc(size);

  if (kbuf == NULL)
    return -ENOMEM;

  spin_lock(&sp);
  list_for_each(pos,&mylist){
      item = list_entry(pos, struct list_item, links);
      nBytes += sprintf(&kbuf[nBytes],"%d\n", item->data);
  }
  spin_unlock(&sp);    
    
  if (len<nBytes)
    return -ENOSPC;
  
    /* Transfer data from the kernel to userspace */  
  if (copy_to_user(buf, kbuf,nBytes))
    return -EINVAL;
    
  (*off)+=len;  /* Update the file pointer */

  if (size > 0) // Solo se libera si se ha hecho vmalloc, es decir, si la lista no es vac??a
    vfree(kbuf);

  return nBytes; 
}

static const struct proc_ops proc_entry_fops = {
    .proc_read = modlist_read,
    .proc_write = modlist_write,    
};

int init_modlist_module( void )
{
  int ret = 0;
  INIT_LIST_HEAD(&mylist);
  proc_entry = proc_create( "modlist", 0666, NULL, &proc_entry_fops);
  if (proc_entry == NULL) {
        ret = -ENOMEM;
        printk(KERN_INFO "modlist: Can't create /proc entry\n");
  } else {
        printk(KERN_INFO "modlist: Module loaded\n");
  }
  return ret;

}


void exit_modlist_module( void )
{
  cleanup();
  remove_proc_entry("modlist", NULL);
  printk(KERN_INFO "modlist: Module unloaded.\n");
}


module_init( init_modlist_module );
module_exit( exit_modlist_module );
