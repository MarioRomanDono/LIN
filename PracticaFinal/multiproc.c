#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/kfifo.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");

#define MAX_KBUF 1024
#define MAX_CBUFFER_LEN 1024

static int max_entries = 5;
static int max_size = 32;

module_param(max_entries, int, 0000);
MODULE_PARM_DESC(max_entries, "Max number of created entries (must be greater than 0)");
module_param(max_size, int, 0000);
MODULE_PARM_DESC(max_size, "Max size of circular number (must be power of 2)");

static struct proc_dir_entry *proc_entry;

int prod_count=0,cons_count=0;
struct kfifo cbuffer;
struct semaphore mtx;
struct semaphore sem_prod;
struct semaphore sem_cons;
int nr_prod_waiting=0;
int nr_cons_waiting=0;

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

  if (len> MAX_CBUFFER_LEN || len> MAX_KBUF) {
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

int init_fifoproc_module( void )
{
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

    sema_init(&mtx, 1);
    sema_init(&sem_cons, 0);
    sema_init(&sem_prod, 0);

    if ( kfifo_alloc(&cbuffer, MAX_CBUFFER_LEN, GFP_KERNEL) ) {
        printk(KERN_INFO "fifoproc: Can't allocate memory to fifo\n");
        return -ENOMEM;
    }

    proc_entry = proc_create( "fifoproc", 0666, NULL, &proc_entry_fops);
    if (proc_entry == NULL) {
        kfifo_free(&cbuffer);
        printk(KERN_INFO "fifoproc: Can't create /proc entry\n");
        return -ENOMEM;
    } else {
        printk(KERN_INFO "fifoproc: Module loaded\n");
    }
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