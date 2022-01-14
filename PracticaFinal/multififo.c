#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/kfifo.h>
#include <linux/uaccess.h>
#include <linux/list.h>

MODULE_LICENSE("GPL");

#define MAX_KBUF 256

static int max_entries = 5;
static int max_size = 32;
static int entry_counter = 0;

module_param(max_entries, int, 0000);
MODULE_PARM_DESC(max_entries, "Max number of created entries (must be greater than 0)");
module_param(max_size, int, 0000);
MODULE_PARM_DESC(max_size, "Max size of circular number (must be power of 2)");

static struct proc_dir_entry *multififo_dir;

struct proc_dir_entry_data {
    int prod_count, cons_count, nr_prod_waiting, nr_cons_waiting;
    struct kfifo cbuffer;
    struct semaphore mtx;
    struct semaphore sem_prod;
    struct semaphore sem_cons;
};

/* Nodos de la lista */
struct list_item {
    struct proc_dir_entry * entry;
    struct proc_dir_entry_data * data;
    char * name;
    struct list_head links;
};

LIST_HEAD(entry_list);
DEFINE_SPINLOCK(sp);

void noinline trace_private_data(int value) { asm(" "); };

static int fifoproc_open(struct inode * inode, struct file * file) {
    struct proc_dir_entry_data * private_data = (struct proc_dir_entry_data *) PDE_DATA(file->f_inode);

    trace_private_data(private_data->cons_count);
    trace_private_data(private_data->prod_count);
    trace_private_data(private_data->nr_prod_waiting);
    trace_private_data(private_data->nr_cons_waiting);

    if (down_interruptible(&private_data->mtx)) {
        return -EINTR;
    }

    if (file->f_mode & FMODE_READ) { // Consumidor
        private_data->cons_count++;

        if (private_data->nr_prod_waiting > 0) {
            up(&private_data->sem_prod);
            private_data->nr_prod_waiting--;
        }

        /* Esperar hasta que el productor abra su extremo de escritura */
        while (private_data->cons_count == 0) {
            private_data->nr_cons_waiting++;
            up(&private_data->mtx);
            if (down_interruptible(&private_data->sem_cons)) {
                down(&private_data->mtx);
                private_data->nr_cons_waiting--;
                private_data->cons_count--;
                up(&private_data->mtx);
                return -EINTR;
            }
            if (down_interruptible(&private_data->mtx)) {
                return -EINTR;
            }
        }
    }
    else { //Productor
        private_data->cons_count++;

        if (private_data->nr_cons_waiting > 0) {
          up(&private_data->sem_cons);
          private_data->nr_cons_waiting--;
        }

        /* Esperar hasta que el consumidor abra su extremo de escritura */
        while (private_data->cons_count == 0) {
            private_data->nr_prod_waiting++;
            up(&private_data->mtx);
            if (down_interruptible(&private_data->sem_prod)) {
                down(&private_data->mtx);
                private_data->nr_prod_waiting--;
                private_data->cons_count--;
                up(&private_data->mtx);
                return -EINTR;
            }
            if (down_interruptible(&private_data->mtx)) {
                return -EINTR;
            }
            }
    }

    up(&private_data->mtx);
    return 0;
}

static ssize_t fifoproc_write(struct file * file, const char * buf, size_t len, loff_t * off) {
    struct proc_dir_entry_data * private_data = (struct proc_dir_entry_data *) PDE_DATA(file->f_inode);

    char kbuffer[MAX_KBUF];

    /* if ((*off) > 0) // The application can write in this entry just once !!
        return 0; */

    if (len > max_size || len> MAX_KBUF) {
        trace_private_data(len);
        printk(KERN_INFO "fifoproc: not enough space!!\n");
        return -ENOSPC;
    }

    if (copy_from_user( kbuffer, buf, len ))  {
        return -EFAULT; 
    }
    kbuffer[len] = '\0'; //Add the `\0' 

    if (down_interruptible(&private_data->mtx)) {
        return -EINTR;
    }

    /* Esperar hasta que haya hueco para insertar (debe haber consumidores) */
    while (kfifo_avail(&private_data->cbuffer)<len && private_data->cons_count>0 ){
        private_data->nr_prod_waiting++;
        up(&private_data->mtx);
        if (down_interruptible(&private_data->sem_prod)) {
            down(&private_data->mtx);
            private_data->nr_prod_waiting--;
            up(&private_data->mtx);
            return -EINTR;
        }
        if (down_interruptible(&private_data->mtx)) {
            return -EINTR;
        }
    }
    /* Detectar fin de comunicación por error (consumidor cierra FIFO antes) */
    if (private_data->cons_count==0) {up(&private_data->mtx); return -EPIPE;}

    kfifo_in(&private_data->cbuffer,kbuffer,len);

    /* Despertar a posible consumidor bloqueado */
    if (private_data->nr_cons_waiting > 0) {
        up(&private_data->sem_cons);
        private_data->nr_cons_waiting--;
    }

    up(&private_data->mtx);
    return len;
}

static ssize_t fifoproc_read(struct file * file, char* buff, size_t len, loff_t * off) {
    struct proc_dir_entry_data * private_data = (struct proc_dir_entry_data *) PDE_DATA(file->f_inode);

    char kbuffer[MAX_KBUF];
    int nBytes;

    if ((*off) > 0) /* Tell the application that there is nothing left to read */
        return 0;

    if (down_interruptible(&private_data->mtx)) {
        return -EINTR;
    }
    /* Esperar hasta que el buffer contenga más bytes que los solicitados mediante read (debe haber productores) */
    while (kfifo_len(&private_data->cbuffer)<len && private_data->cons_count>0 ){
        private_data->nr_cons_waiting++;
        up(&private_data->mtx);
        if (down_interruptible(&private_data->sem_cons)) {
            down(&private_data->mtx);
            private_data->nr_cons_waiting--;
            up(&private_data->mtx);
            return -EINTR;
        }
        if (down_interruptible(&private_data->mtx)) {
            return -EINTR;
        }
    }
    /* Detectar fin de comunicación correcto (el extremo de escritura ha sido cerrado) */
    if (kfifo_is_empty(&private_data->cbuffer) && private_data->cons_count==0) {
        up(&private_data->mtx);
        return 0;
    }

    nBytes = kfifo_out(&private_data->cbuffer,kbuffer,len);

    /* Despertar a posible productor bloqueado */
    if (private_data->nr_prod_waiting > 0) {
        up(&private_data->sem_prod);
        private_data->nr_prod_waiting--;
    }

    if (copy_to_user(buff,kbuffer,len)) {up(&private_data->mtx); return -EFAULT;}

    up(&private_data->mtx);
    // (*off)+=len;  /* Update the file pointer */
    return len;
}

static int fifoproc_release(struct inode * inode, struct file * file) {
    struct proc_dir_entry_data * private_data = (struct proc_dir_entry_data*)PDE_DATA(file->f_inode);

    if (down_interruptible(&private_data->mtx)) {
        return -EINTR;
    }

    if (file->f_mode & FMODE_READ) { // Consumidor        
        private_data->cons_count--;
        if (private_data->nr_prod_waiting > 0) {
            up(&private_data->sem_prod);
            private_data->nr_prod_waiting--;
        }
    }
    else { // Productor
        private_data->cons_count--;
        if (private_data->nr_cons_waiting > 0) {
            up(&private_data->sem_cons);
            private_data->nr_cons_waiting--;
        }
    }

    if (private_data->cons_count == 0 && private_data->cons_count == 0) {
        kfifo_reset(&private_data->cbuffer);
    }

    up(&private_data->mtx);
    return 0;
}

static const struct proc_ops proc_entry_fops = {
    .proc_read = fifoproc_read,
    .proc_write = fifoproc_write,
    .proc_open = fifoproc_open,
    .proc_release = fifoproc_release,    
};

/***********************************************************************************
*************************** PARTE NUEVA DE LA PRÁCTICA *****************************
************************************************************************************/

static int init_proc_entry(char * name) {
    struct proc_dir_entry * entry;
    struct list_item * item;
    struct proc_dir_entry_data * data = vmalloc(sizeof(struct proc_dir_entry_data));

    // Se inicializa la estructura privada de la entrada /proc
    if (!data) {
        printk(KERN_INFO "fifoproc: Could not create %s entry\n", name);
        return -ENOMEM;
    }
    data->prod_count = data->cons_count = data->nr_cons_waiting = data->nr_prod_waiting = 0;

    trace_private_data(data->cons_count);
    trace_private_data(data->prod_count);
    trace_private_data(data->nr_prod_waiting);
    trace_private_data(data->nr_cons_waiting);

    sema_init(&data->mtx, 1);
    sema_init(&data->sem_cons, 0);
    sema_init(&data->sem_prod, 0);

    if ( kfifo_alloc(&data->cbuffer, max_size, GFP_KERNEL) ) {
        printk(KERN_INFO "fifoproc: Could not create %s entry\n", name);
        vfree(data);
        return -ENOMEM;
    }

    // Se crea la entrada /proc
    entry = proc_create_data(name, 0666, multififo_dir, &proc_entry_fops, data);
    if (!entry) {        
        printk(KERN_INFO "fifoproc: Could not create %s entry\n", name);
        kfifo_free(&data->cbuffer);
        vfree(data);
        return -ENOMEM;
    }

    // Se añade a la lista dicha entrada
    item = vmalloc(sizeof(struct list_item));
    if (!item) {
      printk(KERN_INFO "fifoproc: Could not create %s entry\n", name);
      kfifo_free(&data->cbuffer);
      vfree(data);
      remove_proc_entry(name, multififo_dir);
      return -ENOMEM;
  }

  item->entry = entry;
  item->data = data;
  item->name = vmalloc(strlen(name) + 1);
  if (!item->name) {
      printk(KERN_INFO "fifoproc: Could not create %s entry\n", name);
      kfifo_free(&data->cbuffer);
      vfree(data);
      vfree(item);
      remove_proc_entry(name, multififo_dir);
      return -ENOMEM;
  }
  strcpy(item->name, name);

  spin_lock(&sp);
  list_add_tail(&item->links, &entry_list);
  spin_unlock(&sp);

  entry_counter++;
  printk(KERN_INFO "fifoproc: Entry %s created\n", name);
  return 0;
}

static int delete_proc_entry(char * name) {
    struct list_head *pos, * e;
    struct list_item* item=NULL;
    struct proc_dir_entry * entry;
    struct proc_dir_entry_data * data;
    int encontrado = 0;

    spin_lock(&sp);

    list_for_each_safe(pos, e, &entry_list){
      item = list_entry(pos, struct list_item, links);
      if (strcmp(name, item->name) == 0) {
        encontrado = 1;

        /*
        // Se comprueba si la entrada está siendo usada antes de borrarse
        if (atomic_read(item->in_use)) {
            spin_unlock(&sp);
            printk(KERN_INFO "fifoproc: Cannot remove entry %s\n while opened", name);
            return -EPERM;
        } */

        entry = item->entry;
        data = item->data;
        list_del(pos);

        break;        
    }
}

    spin_unlock(&sp);

    if (!encontrado) {
        printk(KERN_INFO "fifoproc: Entry %s not found\n", name);
        return -EINVAL;
    }

    kfifo_reset(&data->cbuffer);
    kfifo_free(&data->cbuffer);
    vfree(data); // Liberar memoria de la estructura asociada a la entrada /proc
    remove_proc_entry(item->name, multififo_dir);
    vfree(item->name);
    vfree(item); // Liberar memoria asociada al elemento de la lista

    entry_counter--;
    printk(KERN_INFO "fifoproc: Deleted entry %s\n", name);    

    return 0;
}

static ssize_t admin_write(struct file * file, const char * buf, size_t len, loff_t * off) {
    char kbuf[MAX_KBUF];
    char entry[MAX_KBUF];
    int res = 0;

    if ((*off) > 0) // The application can write in this entry just once !!
        return 0;

    if (len> MAX_KBUF) {
        printk(KERN_INFO "fifoproc: not enough space!!\n");
        return -ENOSPC;
    }

    if (copy_from_user( kbuf, buf, len ))  {
        return -EFAULT; 
    }
    kbuf[len] = '\0'; //Add the `\0'

    if (sscanf(kbuf, "new %s", entry) == 1) {
        if (entry_counter >= max_entries) {
            printk(KERN_INFO "fifoproc: cannot create more than %d entries", max_entries);
            return -EPERM;
        }
        res = init_proc_entry(entry);
        if (res) {
            printk(KERN_INFO "fifoproc: cannot create %s entry", entry);
            return res;
        }
    }
    else if (sscanf(kbuf, "delete %s", entry) == 1) {
        res = delete_proc_entry(entry);
        if (res) {
            printk(KERN_INFO "fifoproc: cannot delete %s entry", entry);
            return res;
        }
    }
    else {
        printk(KERN_INFO "fifoproc: invalid argument");
        return -EINVAL;
    }

    (*off) += len;
    return len;
}

static const struct proc_ops admin_entry_fops = {
    .proc_write = admin_write
};

int init_fifoproc_module( void )
{
    struct proc_dir_entry * proc_entry;

    if (max_entries < 1) {
        printk(KERN_INFO "fifoproc: max_entries must be greater than 0\n");
        return -EINVAL;
    }

    /* Comprueba si el número es 0 o no es una potencia de 2.
    Fuente: http://graphics.stanford.edu/~seander/bithacks.html#DetermineIfPowerOf2
    */
    if (!max_size || (max_size & (max_size - 1) )) { 
        printk(KERN_INFO "fifoproc: max_size must be a power of 2\n");
        return -EINVAL;
    }    

    multififo_dir = proc_mkdir("multififo", NULL);
    if (multififo_dir == NULL) {
        printk(KERN_INFO "fifoproc: Can't create /proc/multififo directory\n");
        return -ENOMEM;
    }

    proc_entry = proc_create("admin", 0666, multififo_dir, &admin_entry_fops);
    if (proc_entry == NULL) {
        remove_proc_entry("multififo", NULL);
        printk(KERN_INFO "fifoproc: Can't create /proc/multififo/admin entry\n");
        return -ENOMEM;
    }

    if ( init_proc_entry("test") ) {
        remove_proc_entry("admin", multififo_dir);
        remove_proc_entry("multififo", NULL);
        printk(KERN_INFO "fifoproc: Can't create /proc/multififo/test entry\n");
        return -ENOMEM;
    }

    printk(KERN_INFO "fifoproc: Module loaded\n");
    
    return 0;
}

void exit_fifoproc_module( void )
{
    struct list_item* item = NULL;
    struct proc_dir_entry * entry;
    struct proc_dir_entry_data * data;
    int quedan_elementos = 1;

    while (quedan_elementos) {
        spin_lock(&sp);

        if (list_empty(&entry_list)) {
            spin_unlock(&sp);
            quedan_elementos = 0;
        }
        else {
            item = list_first_entry(&entry_list, struct list_item, links);

            /*
            // Se comprueba si la entrada está siendo usada antes de borrarse
            if (atomic_read(item->in_use)) {
                spin_unlock(&sp);
                printk(KERN_INFO "fifoproc: Cannot remove entry %s\n while opened", item->name);
            } */

            entry = item->entry;
            data = item->data;
            list_del(entry_list.next);

            spin_unlock(&sp);

            kfifo_reset(&data->cbuffer);
            kfifo_free(&data->cbuffer);
            vfree(data);// Liberar memoria de la estructura asociada a la entrada /proc
            remove_proc_entry(item->name, multififo_dir);
            vfree(item->name);
            vfree(item); // Liberar memoria asociada al elemento de la lista

            entry_counter--;
        }
    }

    remove_proc_entry("admin", multififo_dir);
    remove_proc_entry("multififo", NULL); 
    printk(KERN_INFO "fifoproc: Module unloaded\n");
}


module_init( init_fifoproc_module );
module_exit( exit_fifoproc_module );
