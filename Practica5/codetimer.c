#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/kfifo.h>
#include <linux/uaccess.h>
#include <linux/timer.h>
#include <linux/workqueue.h>

MODULE_LICENSE("GPL");

#define MAX_CBUFFER_LEN 32
#define MAX_CODE_SIZE 8

static struct proc_dir_entry *codetimer_entry;
static struct proc_dir_entry *codeconfig_entry;
static struct list_head mylist; /* Lista enlazada */

/* Nodos de la lista */
struct list_item {
    char *data;
    struct list_head links;
};

struct kfifo cbuffer;
struct timer_list my_timer;
char * code_format;
int timer_period_ms, emergency_threshold;
int codesize;

struct work_struct my_work;
static struct workqueue_struct* my_wq;  

static void add_to_buffer() {
  /* Completar */
  kfifo_in(&cbuffer,kbuffer,len);
}

static void fire_timer() {
  timer_setup(&my_timer, add_to_buffer, 0);
  my_timer.expires = jiffies + timer_period_ms;
  add_timer(&my_timer);
}

static void copy_items_into_list() {
  char kbuf[MAX_CBUFFER_LEN];
  int nBytes, aux;

  nBytes = kfifo_out(&cbuffer,kbuf,MAX_CBUFFER_LEN);
  aux = nBytes / codesize;
  
  for (int i = 0; i < aux; i++) {
    struct list_item* item = vmalloc(sizeof(struct list_item));
    item->data = vmalloc(sizeof(char) * codesize);
    strncpy(item->data, kbuf[i * codesize]);
    list_add_tail(&item->links, &mylist);
  }

}

static void bottom_half() {
  int cpu_actual = smp_processor_id();
  my_wq = create_workqueue("my_queue");
  INIT_WORK(&my_work, copy_items_into_list);

  if (!work_pending(&my_work) && ((kfifo_len(&cbuffer) / MAX_CBUFFER_LEN) * 100) < emergency_threshold) {
    queue_work_on(cpu_actual % 2 + 1, my_wq, &my_work);    
  }
}

static const struct proc_ops codeconfig_entry_fops = {
    .proc_read = codeconfig_read,
    .proc_write = codeconfig_write,   
};

static const struct proc_ops codetimer_entry_fops = {
    .proc_read = codetimer_read,
    .proc_open = codetimer_open,
    .proc_release = codetimer_release,    
};

int init_fifoproc_module( void )
{
  int ret = 0;

  if ( kfifo_alloc(&cbuffer, MAX_CBUFFER_LEN, GFP_KERNEL) ) {
  		printk(KERN_INFO "codetimer: Can't allocate memory to fifo\n");
  		return -ENOMEM;
  }
  codetimer_entry = proc_create( "codetimer", 0666, NULL, &codetimer_entry_fops);
  if (proc_entry == NULL) {
        kfifo_free(&cbuffer);
        printk(KERN_INFO "codetimer: Can't create /proc/codetimer entry\n");
        return -ENOMEM;
  } 
  codeconfig_entry = proc_create( "codetimer", 0666, NULL, &codeconfig_entry_fops);
  if (proc_entry == NULL) {
        kfifo_free(&cbuffer);
        remove_proc_entry("codetimer", NULL);
        printk(KERN_INFO "codetimer: Can't create /proc/codeconfig entry\n");
        return -ENOMEM;
  }

  printk(KERN_INFO "codetimer: Module loaded\n");
  return ret;

}


void exit_fifoproc_module( void )
{
  kfifo_reset(&cbuffer);
  kfifo_free(&cbuffer);
  remove_proc_entry("codeconfig", NULL);
  remove_proc_entry("codetimer", NULL);
  printk(KERN_INFO "fifoproc: Module unloaded.\n");
}


module_init( init_fifoproc_module );
module_exit( exit_fifoproc_module );