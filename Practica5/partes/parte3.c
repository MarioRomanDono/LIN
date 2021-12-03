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

struct kfifo cbuffer;
static struct list_head mylist;
struct list_item {
    char * data;
    struct list_head links;
};

struct timer_list my_timer; /* Structure that describes the kernel timer */
struct work_struct my_work;
static struct workqueue_struct* transfer_task; 
static struct proc_dir_entry *codetimer_entry;

DEFINE_SEMAPHORE(mtx);
DEFINE_SPINLOCK(sp);

void noinline trace_message(char * message) {
    asm(" ");
}

void noinline trace_workqueue(char * message) {
    asm(" ");
}

static void copy_items_into_list(struct work_struct * wq) {
    char kbuf[MAX_CBUFFER_LEN];
    int nBytes, aux, flags;

    spin_lock_irqsave(&sp, flags);
    nBytes = kfifo_out(&cbuffer,kbuf,MAX_CBUFFER_LEN);
    spin_unlock_irqsave(&sp, flags);

    aux = nBytes / CODESIZE;

    for (int i = 0; i < aux; i++) {
        struct list_item* item = vmalloc(sizeof(struct list_item));
        item->data = vmalloc(sizeof(char) * CODESIZE);
        strncpy(item->data, kbuf[i * CODESIZE], CODESIZE);

        if (down_interruptible(&mtx)) {
            return -EINTR;
        }
        list_add_tail(&item->links, &mylist);
        up(&mtx);
        trace_workqueue("Llamada a workqueue");
    }

    printk(KERN_INFO "modlist: Elemento %d agregado\n", numero);
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
    
    kfifo_in(&cbuffer, msg, 4);
    trace_message(msg);

    
    /* Re-activate the timer one second from now */
    mod_timer(timer, jiffies + HZ); 
}

static int codetimer_open(struct inode * inode, struct file * file) {
    try_module_get(THIS_MODULE);

    /* Create workqueue*/
    my_wq = create_workqueue("my_queue");
    INIT_WORK(&my_work, transfer_task);

    /* Create timer */
    timer_setup(&my_timer, fire_timer, 0);

    my_timer.expires=jiffies + HZ;  /* Activate it one second from now */
    /* Activate the timer for the first time */
    add_timer(&my_timer);
}

static int code

static int codetimer_release(struct inode * inode, struct file * file) {
        int flags;

        del_timer_sync(&my_timer); 
        flush_workqueue(my_wq);

        spin_lock_irqsave(&sp, flags);
        kfifo_reset(&cbuffer);
        kfifo_free(&cbuffer);
        spin_unlock_irqsave(&sp, flags);

        module_put(THIS_MODULE);
}

static const struct proc_ops proc_entry_fops = {
    .proc_open = codetimer_open,
    .proc_release = codetimer_release,    
};

int init_timer_module( void )
{
    if ( kfifo_alloc(&cbuffer, MAX_CBUFFER_LEN, GFP_KERNEL) ) {
        printk(KERN_INFO "Can't allocate memory to fifo\n");
        return -ENOMEM;
    }

    codetimer_entry = proc_create( "modlist", 0666, NULL, &proc_entry_fops);
    if (codetimer_entry == NULL) {
        kfifo_free(&cbuffer);
        printk(KERN_INFO "codetimer: Can't create /proc entry\n");
        return -ENOMEM;
    }
    printk(KERN_INFO "codetimer: Module loaded\n");
    return 0;
}


void cleanup_timer_module( void ){
    printk(KERN_INFO "codetimer: Module unloaded\n");
}

module_init( init_timer_module );
module_exit( cleanup_timer_module );

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("codetimer Module");