#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/kfifo.h>
#include <linux/workqueue.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>

#define MAX_CBUFFER_LEN 32
#define CODESIZE 4
#define KBUF_SIZE 256

struct kfifo cbuffer;
static struct list_head mylist;
struct list_item {
    char * data;
    struct list_head links;
};

struct timer_list my_timer; /* Structure that describes the kernel timer */
struct work_struct my_work;
static struct workqueue_struct* my_wq; 
static struct proc_dir_entry *codetimer_entry;

int espera = 0;
struct semaphore queue;

DEFINE_SEMAPHORE(mtx);
DEFINE_SPINLOCK(sp);

void noinline trace_code_in_buffer(char* random_code, int cur_buf_size) { asm(" "); };

void noinline trace_code_in_list(char* random_code) { asm(" "); };

void noinline trace_code_read(char* random_code) { asm(" "); };

static void copy_items_into_list(struct work_struct * wq) {
    char kbuf[MAX_CBUFFER_LEN];
    int nBytes, aux, i, ret;
    unsigned long flags;

    spin_lock_irqsave(&sp, flags);
    nBytes = kfifo_out(&cbuffer,kbuf,MAX_CBUFFER_LEN);
    spin_unlock_irqrestore(&sp, flags);

    aux = nBytes / CODESIZE;

    ret = down_interruptible(&mtx);

    for (i = 0; i < aux; i++) {
        struct list_item* item = vmalloc(sizeof(struct list_item));
        item->data = vmalloc(sizeof(char) * CODESIZE);
        strncpy(item->data, &kbuf[i * CODESIZE], CODESIZE);

        list_add_tail(&item->links, &mylist);

        trace_code_in_list(item->data);
    }

    if (espera>0) {
        up(&queue);
        espera--;
    }

    up(&mtx);
}

/* Function invoked when timer expires (fires) */
static void fire_timer(struct timer_list *timer)
{
    int cpu_actual;
    char msg[] = "Hola";
    if (kfifo_is_full(&cbuffer)) {
        flush_workqueue(my_wq);
        cpu_actual = smp_processor_id();
        queue_work_on(!(cpu_actual & 0x1), my_wq, &my_work); // Toma el último bit de la cpu (paridad) y lo alterna
    }
    
    kfifo_in(&cbuffer, msg, CODESIZE);
    trace_code_in_buffer(msg, CODESIZE);

    
    /* Re-activate the timer one second from now */
    mod_timer(timer, jiffies + HZ); 
}

static int codetimer_open(struct inode * inode, struct file * file) {
    try_module_get(THIS_MODULE);

    INIT_LIST_HEAD(&mylist);

    if ( kfifo_alloc(&cbuffer, MAX_CBUFFER_LEN, GFP_KERNEL) ) {
        printk(KERN_INFO "Can't allocate memory to fifo\n");
        return -ENOMEM;
    }

    /* Create workqueue*/
    my_wq = create_workqueue("my_queue");
    INIT_WORK(&my_work, copy_items_into_list);

    /* Create timer */
    timer_setup(&my_timer, fire_timer, 0);

    my_timer.expires=jiffies + HZ;  /* Activate it one second from now */
    /* Activate the timer for the first time */
    add_timer(&my_timer);

    return 0;
}

static ssize_t codetimer_read(struct file* filp, char __user* buf, size_t len, loff_t* off)  {
    char kbuf[KBUF_SIZE];
    int nBytes = 0;
    struct list_head* pos, * e;
    struct list_item* item = NULL;

    if ((*off) > 0) /* Tell the application that there is nothing left to read */
        return 0;

    if (down_interruptible(&mtx)) {
            return -EINTR;
    }

    if (list_empty(&mylist)) {
        espera++;
        up(&mtx);
        if (down_interruptible(&queue)){
            down(&mtx);
            espera--;
            up(&mtx);
            return -EINTR;
        }
        if (down_interruptible(&mtx))
            return -EINTR;
    }

    list_for_each_safe(pos, e, &mylist) {
        item = list_entry(pos, struct list_item, links);
        nBytes += sprintf(&kbuf[nBytes], "%s\n", item->data);
        list_del(pos);
        trace_code_read(item->data);  
        vfree(item->data);
        vfree(item);
    }

    up(&mtx);

    if (len < nBytes)
        return -ENOSPC;

    /* Transfer data from the kernel to userspace */
    if (copy_to_user(buf, kbuf, nBytes))
        return -EINVAL;

    (*off) += len;  /* Update the file pointer */

    return nBytes;
}

static int codetimer_release(struct inode * inode, struct file * file) {
        unsigned long flags;

        del_timer_sync(&my_timer); 
        flush_workqueue(my_wq);

        spin_lock_irqsave(&sp, flags);
        kfifo_reset(&cbuffer);
        kfifo_free(&cbuffer);
        spin_unlock_irqrestore(&sp, flags);

        module_put(THIS_MODULE);

        return 0;
}

static const struct proc_ops proc_entry_fops = {
    .proc_open = codetimer_open,
    .proc_release = codetimer_release,    
    .proc_read = codetimer_read,
};

int init_timer_module( void )
{
    sema_init(&queue, 0);

    codetimer_entry = proc_create( "codetimer", 0666, NULL, &proc_entry_fops);
    if (codetimer_entry == NULL) {
        kfifo_free(&cbuffer);
        printk(KERN_INFO "codetimer: Can't create /proc entry\n");
        return -ENOMEM;
    }
    printk(KERN_INFO "codetimer: Module loaded\n");
    return 0;
}


void cleanup_timer_module( void ){
    remove_proc_entry("codetimer", NULL);
    printk(KERN_INFO "codetimer: Module unloaded\n");
}

module_init( init_timer_module );
module_exit( cleanup_timer_module );

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("codetimer Module");