#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/kfifo.h>
#include <linux/workqueue.h>

#define MAX_CBUFFER_LEN 32

struct timer_list my_timer; /* Structure that describes the kernel timer */
struct kfifo cbuffer;
struct work_struct my_work;
static struct workqueue_struct* my_wq; 

void noinline trace_message(char * message) {
    asm(" ");
}

void noinline trace_workqueue(char * message) {
    asm(" ");
}

static void prueba(struct work_struct * wq) {
    trace_workqueue("Llamada a workqueue");
    kfifo_reset(&cbuffer);
}

/* Function invoked when timer expires (fires) */
static void fire_timer(struct timer_list *timer)
{
    int cpu_actual;
    char msg[] = "Hola";
    if (kfifo_is_full(&cbuffer)) {
        flush_workqueue(my_wq);
        cpu_actual = smp_processor_id();
        INIT_WORK(&my_work, prueba);
        queue_work_on(!(cpu_actual & 0x1), my_wq, &my_work); // Toma el último bit de la cpu (paridad) y lo alterna
    }
    
    kfifo_in(&cbuffer, msg, 4);
    trace_message(msg);

    
    /* Re-activate the timer one second from now */
    mod_timer(timer, jiffies + HZ); 
}

int init_timer_module( void )
{
    if ( kfifo_alloc(&cbuffer, MAX_CBUFFER_LEN, GFP_KERNEL) ) {
        printk(KERN_INFO "Can't allocate memory to fifo\n");
        return -ENOMEM;
    }
    /* Create workqueue*/
    my_wq = create_workqueue("my_queue");

    /* Create timer */
    timer_setup(&my_timer, fire_timer, 0);

    my_timer.expires=jiffies + HZ;  /* Activate it one second from now */
    /* Activate the timer for the first time */
    add_timer(&my_timer); 
    return 0;
}


void cleanup_timer_module( void ){
  /* Wait until completion of the timer function (if it's currently running) and delete timer */
  del_timer_sync(&my_timer);
}

module_init( init_timer_module );
module_exit( cleanup_timer_module );

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("timermod Module");
MODULE_AUTHOR("Juan Carlos Saez");
