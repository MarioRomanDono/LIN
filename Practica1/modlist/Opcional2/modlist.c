#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>


MODULE_LICENSE("GPL");

#define BUFFER_LENGTH 256

static struct proc_dir_entry *proc_entry;
static struct list_head mylist; /* Lista enlazada */
static int lista_contador;

/* Nodos de la lista */
struct list_item {
  int data;
  struct list_head links;
};

static void *mylist_seq_start(struct seq_file *f, loff_t *pos)
{
  return (*pos < lista_contador) ? mylist.next : NULL;
}

static void mylist_seq_stop(struct seq_file *f, void *v)
{

}

static void *mylist_seq_next(struct seq_file *f, void *v, loff_t *pos)
{
  struct list_head *nodo = v;
  (*pos)++;
  if (*pos >= lista_contador)
    return NULL;
  return nodo->next;
}

static int show_modlist(struct seq_file *f, void *v) {
  struct list_item *item = NULL;
  struct list_head *cur_node = v;

  item = list_entry(cur_node, struct list_item, links);
  seq_printf(f, "%d\n", item->data);
  
  return 0;
}

static struct seq_operations modlist_seq_ops = {
  .start = mylist_seq_start,
  .next = mylist_seq_next,
  .stop = mylist_seq_stop,
  .show = show_modlist
};

static int modlist_open(struct inode *inode, struct file *filp) {
  return seq_open(filp, &modlist_seq_ops);
}

static void add(int numero) {
  struct list_item* item = vmalloc(sizeof(struct list_item));
  item->data = numero;
  list_add_tail(&item->links, &mylist);
  ++lista_contador;
  printk(KERN_INFO "modlist: Elemento %d agregado\n", numero);
}



static void remove(int numero) {
  struct list_head* pos, * e;
  struct list_item* item = NULL;

  list_for_each_safe(pos, e, &mylist) {
      item = list_entry(pos, struct list_item, links);
      if (item->data == numero) {
          list_del(pos);
          vfree(item);
          --lista_contador;
          printk(KERN_INFO "modlist: Elemento %d borrado\n", numero);
      }
  }
}

static void cleanup(void) {
    struct list_head* pos, * e;
    struct list_item* item = NULL;

    list_for_each_safe(pos, e, &mylist) {
        item = list_entry(pos, struct list_item, links);
        list_del(pos);
        vfree(item);
    }
    printk(KERN_INFO "modlist: Lista vaciada\n");
    lista_contador = 0;
}

static ssize_t modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
  int available_space = BUFFER_LENGTH-1;
  int numero;
  char kbuf[BUFFER_LENGTH];
  
  if ((*off) > 0) /* The application can write in this entry just once !! */
      return 0;
  
    if (len > available_space) {
     printk(KERN_INFO "modlist: not enough space!!\n");
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
  }
  
  return len;
}

static const struct proc_ops proc_entry_fops = {
    .proc_open = modlist_open,
    .proc_read_iter = seq_read_iter,
    .proc_lseek = seq_lseek,
    .proc_release = seq_release,
    .proc_write = modlist_write,    
};

int init_modlist_module( void )
{
  int ret = 0;
  INIT_LIST_HEAD(&mylist);
  lista_contador = 0;
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
