#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>

MODULE_LICENSE("GPL");

struct list_head mylist; /* Lista enlazada */

/* Nodos de la lista */
struct list_item {
	int data;
	struct list_head links;
};

static ssize_t modlist_write(struct file *filp, const char __user *buf, size_t len, loff_t *off) {
	char kbuf[MAX_CHARS];
	if (copy_from_user( &kbuf[0], buf, len ))  {
    		return -EFAULT;	
	}
	char comando[6];
	int numero;

	sscanf(kbuf, "%s %d", comando, numero);

	if (comando == "add") {
		struct list_item* item = vmalloc( sizeof(list_item) );
		item->data = numero;
		list_add_tail(item, mylist);
	}
	

	  


}

static const struct proc_ops proc_entry_fops = {
    .proc_read = clipboard_read,
    .proc_write = clipboard_write,    
};

int init_modlist_module( void )
{
  int ret = 0;
  INIT_LIST_HEAD(mylist);
  proc_entry = proc_create( "modlist", 0666, NULL, &proc_entry_fops);
  if (proc_entry == NULL) {
	ret = -ENOMEM;
	vfree(modlist);
	printk(KERN_INFO "modlist: Can't create /proc entry\n");
  } else {
	printk(KERN_INFO "modlist: Module loaded\n");
  }
  return ret;

}


void exit_modlist_module( void )
{
  remove_proc_entry("modlist", NULL);
  vfree(modlist);
  printk(KERN_INFO "modlist: Module unloaded.\n");
}


module_init( init_modlist_module );
module_exit( exit_modlist_module );
