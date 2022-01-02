#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/kfifo.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");

#define MAX_KBUF 1024

static int max_entries = 5;
static int max_size = 32;
static int entry_counter = 0;

module_param(max_entries, int, 0000);
MODULE_PARM_DESC(max_entries, "Max number of created entries (must be greater than 0)");
module_param(max_size, int, 0000);
MODULE_PARM_DESC(max_size, "Max size of circular number (must be power of 2)");

static struct proc_dir_entry *multififo_dir;

static struct list_element {
    int prod_count, cons_count, nr_prod_waiting, nr_cons_waiting;
    struct kfifo cbuffer;
    struct semaphore mtx;
    struct semaphore sem_prod;
    struct semaphore sem_cons;
};

/*int prod_count=0,cons_count=0;
struct kfifo cbuffer;
struct semaphore mtx;
struct semaphore sem_prod;
struct semaphore sem_cons;
int nr_prod_waiting=0;
int nr_cons_waiting=0;

sema_init(&mtx, 1);
sema_init(&sem_cons, 0);
sema_init(&sem_prod, 0);

if ( kfifo_alloc(&cbuffer, MAX_CBUFFER_LEN, GFP_KERNEL) ) {
    printk(KERN_INFO "fifoproc: Can't allocate memory to fifo\n");
    return -ENOMEM;
}

*/

static int fifoproc_open(struct inode * inode, struct file * file) {
  if (down_interruptible(&mtx)) {
    return -EINTR;
  }

  if (file->f_mode & FMODE_READ) { // Consumidor
    cons_count++;

    if (nr_prod_waiting > 0) {
      up(&sem_prod);
      nr_prod_waiting--;
    }

    /* Esperar hasta que el productor abra su extremo de escritura */
    while (prod_count == 0) {
      nr_cons_waiting++;
      up(&mtx);
      if (down_interruptible(&sem_cons)) {
        down(&mtx);
        nr_cons_waiting--;
        cons_count--;
        up(&mtx);
        return -EINTR;
      }
      if (down_interruptible(&mtx)) {
        return -EINTR;
      }
    }
  }
  else { //Productor
    prod_count++;

    if (nr_cons_waiting > 0) {
      up(&sem_cons);
      nr_cons_waiting--;
    }

    /* Esperar hasta que el consumidor abra su extremo de escritura */
    while (cons_count == 0) {
      nr_prod_waiting++;
      up(&mtx);
      if (down_interruptible(&sem_prod)) {
        down(&mtx);
        nr_prod_waiting--;
        prod_count--;
        up(&mtx);
        return -EINTR;
      }
      if (down_interruptible(&mtx)) {
        return -EINTR;
      }
    }
  }

  up(&mtx);
  return 0;
}

static ssize_t fifoproc_write(struct file * file, const char * buf, size_t len, loff_t * off) {
  char kbuffer[MAX_KBUF];

  /* if ((*off) > 0) // The application can write in this entry just once !!
        return 0; */

  if (len> max_size || len> MAX_KBUF) {
    printk(KERN_INFO "fifoproc: not enough space!!\n");
       return -ENOSPC;
  }

  /* if (copy_from_user( &kbuffer[0], buf, len ))  {
            return -EFAULT; 
  }  
  *off+=len;  */

  if (copy_from_user( kbuffer, buf, len ))  {
            return -EFAULT; 
  }
  kbuffer[len] = '\0'; //Add the `\0' 
  
  if (down_interruptible(&mtx)) {
    return -EINTR;
  }

  /* Esperar hasta que haya hueco para insertar (debe haber consumidores) */
  while (kfifo_avail(&cbuffer)<len && cons_count>0 ){
    nr_prod_waiting++;
    up(&mtx);
    if (down_interruptible(&sem_prod)) {
      down(&mtx);
      nr_prod_waiting--;
      up(&mtx);
      return -EINTR;
    }
    if (down_interruptible(&mtx)) {
      return -EINTR;
    }
  }
  /* Detectar fin de comunicación por error (consumidor cierra FIFO antes) */
  if (cons_count==0) {up(&mtx); return -EPIPE;}

  kfifo_in(&cbuffer,kbuffer,len);

  /* Despertar a posible consumidor bloqueado */
  if (nr_cons_waiting > 0) {
    up(&sem_cons);
    nr_cons_waiting--;
  }

  up(&mtx);
  return len;
}

static ssize_t fifoproc_read(struct file * file, char* buff, size_t len, loff_t * off) {
  char kbuffer[MAX_KBUF];
  int nBytes;

  if ((*off) > 0) /* Tell the application that there is nothing left to read */
      return 0;
  
  if (down_interruptible(&mtx)) {
    return -EINTR;
  }
  /* Esperar hasta que el buffer contenga más bytes que los solicitados mediante read (debe haber productores) */
  while (kfifo_len(&cbuffer)<len && prod_count>0 ){
    nr_cons_waiting++;
    up(&mtx);
    if (down_interruptible(&sem_cons)) {
      down(&mtx);
      nr_cons_waiting--;
      up(&mtx);
      return -EINTR;
    }
    if (down_interruptible(&mtx)) {
      return -EINTR;
    }
  }
  /* Detectar fin de comunicación correcto (el extremo de escritura ha sido cerrado) */
  if (kfifo_is_empty(&cbuffer) && prod_count==0) {
    up(&mtx);
    return 0;
  }

  nBytes = kfifo_out(&cbuffer,kbuffer,len);

  /* Despertar a posible productor bloqueado */
  if (nr_prod_waiting > 0) {
    up(&sem_prod);
    nr_prod_waiting--;
  }

  if (copy_to_user(buff,kbuffer,len)) {up(&mtx); return -EFAULT;}

  up(&mtx);
  // (*off)+=len;  /* Update the file pointer */
  return len;
}



static int fifoproc_release(struct inode * inode, struct file * file) {
  if (down_interruptible(&mtx)) {
    return -EINTR;
  }

  if (file->f_mode & FMODE_READ) { // Consumidor        
    cons_count--;
    if (nr_prod_waiting > 0) {
      up(&sem_prod);
      nr_prod_waiting--;
    }
  }
  else { // Productor
    prod_count--;
    if (nr_cons_waiting > 0) {
      up(&sem_cons);
      nr_cons_waiting--;
    }
  }

  if (prod_count == 0 && cons_count == 0) {
      kfifo_reset(&cbuffer);
  }

  up(&mtx);
  return 0;
}

static const struct proc_ops proc_entry_fops = {
    .proc_read = fifoproc_read,
    .proc_write = fifoproc_write,
    .proc_open = fifoproc_open,
    .proc_release = fifoproc_release,    
};

int init_proc_entry(char * name) {

}

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
  kfifo_reset(&cbuffer);
  kfifo_free(&cbuffer);
  remove_proc_entry("fifoproc", NULL);
  printk(KERN_INFO "fifoproc: Module unloaded.\n");
}


module_init( init_fifoproc_module );
module_exit( exit_fifoproc_module );