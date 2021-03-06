#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/kfifo.h>
#include <linux/workqueue.h>
#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/random.h>

#define MAX_CBUFFER_LEN 32
#define KBUF_SIZE 256
#define MAX_CODESIZE 8

int timer_period_ms = 500;
int emergency_threshold = 75;

struct kfifo cbuffer;
static struct list_head mylist_even;
static struct list_head mylist_odd;
struct list_item {
    char * data;
    struct list_head links;
};

struct timer_list my_timer; /* Structure that describes the kernel timer */
struct work_struct my_work;
static struct proc_dir_entry *codetimer_entry, *codeconfig_entry;

static atomic_t abierto = ATOMIC_INIT(0); // Contador atómico que controla que /proc/codetimer solo se abra una vez

int espera_par = 0;
int espera_impar = 0;
int tarea_planificada = 0;
struct semaphore queue_par, queue_impar;

DEFINE_SEMAPHORE(mtx);
DEFINE_SPINLOCK(sp);

void noinline trace_code_in_buffer(char* random_code, int cur_buf_size) { asm(" "); };

void noinline trace_code_in_list(char* random_code) { asm(" "); };

void noinline trace_code_read(char* random_code) { asm(" "); };

/* Descomentar estas funciones de bpftrace si se quiere debuggear el formato aleatorio generado
o si se abre primero para leer los pares y luego los impares correctamente 

void noinline trace_code_format(char* random_code, int codesize) { asm(" "); };
void noinline trace_par(char* private_data) { asm(" "); };
*/


static void generate_code(char * code) {
    char code_format[MAX_CODESIZE + 1];
    char tmp[MAX_CODESIZE + 1];
    int random, i, value, shifted_bits, caracter, codesize;

    random = get_random_int();
    shifted_bits = 0;

    codesize = (random & 0x7) + 1; // Para obtener un tamaño entre 0 y 8 solo necesitamos los tres últimos bits
    random >>= 3;
    shifted_bits += 3;

    for (i = 0; i < codesize; i++) {
        if (shifted_bits + 2 > 32) { // Si vamos a superar los 32 bits del primer random, generamos uno nuevo
            random = get_random_int();
            shifted_bits = 0;
        }
        caracter = (random & 0x3) % 3; // Para obtener un tamaño entre 0 y 2 solo necesitamos los dos últimos bits y hacer módulo 3
        if (caracter == 0) code_format[i] = '0';
        if (caracter == 1) code_format[i] = 'a';
        if (caracter == 2) code_format[i] = 'A';
        random >>= 2;
        shifted_bits += 2;
    }
    code_format[codesize] = '\0';

    // trace_code_format(code_format, codesize);

    for (i = 0; i < codesize; i++) {
        if (code_format[i] == '0') {
            if (shifted_bits + 4 > 32) { // Si vamos a superar los 32 bits del primer random, generamos uno nuevo
                random = get_random_int();
                shifted_bits = 0;
            }

            value = random & 0xF; // Para obtener un número aleatorio solo necesitamos los últimos 4 bits (entre 0 y 15)
            tmp[i] = '0' + value % 10; // Si hacemos módulo 10 de esos 4 bits anteriores y se lo sumamos a 0, obtenemos un número aleatorio entre 0 y 9
            random >>= 4; // Desplazamos el número random 4 bits para no trabajar siempre con los mismos bits
            shifted_bits += 4;
        }
        else { // Letra
            if (shifted_bits + 5 > 32) { // Si vamos a superar los 32 bits del primer random, generamos uno nuevo
                random = get_random_int();
                shifted_bits = 0;
            }

            value = random & 0x1F; // Para obtener una letra aleatoria solo necesitamos los últimos 5 bits (entre 0 y 31)
            tmp[i] = code_format[i] + value % 25; // Si hacemos módulo 25 de esos 5 bits anteriores y se lo sumamos a 'a' o 'A', obtenemos una letra aleatoria
            random >>= 5; // Desplazamos el número random 5 bits para no trabajar siempre con los mismos bits
            shifted_bits += 5;
        }
    }
    strncpy(code, tmp, codesize);
    code[codesize] = '\0';
}

static void copy_items_into_list(struct work_struct * wq) {
    char kbuf[MAX_CBUFFER_LEN];
    int nBytes, pos, ret, size, hayPar, hayImpar;
    unsigned long flags;

    spin_lock_irqsave(&sp, flags);
    nBytes = kfifo_out(&cbuffer,kbuf,MAX_CBUFFER_LEN);
    spin_unlock_irqrestore(&sp, flags);

    ret = down_interruptible(&mtx);

    pos = 0;
    
    while (pos < nBytes) {
        struct list_item* item = vmalloc(sizeof(struct list_item));
        size = strlen(&kbuf[pos]) + 1;
        item->data = vmalloc(sizeof(char) * size);
        strncpy(item->data, &kbuf[pos], size);

        // Es necesario restar 1 para no contar el carácter nulo
        if ( (size - 1) & 0x1) {
            list_add_tail(&item->links, &mylist_odd);
            hayImpar = 1;
        }
        else {
            list_add_tail(&item->links, &mylist_even);
            hayPar = 1;
        }

        trace_code_in_list(item->data);

        pos += size;
    }

    if (hayPar && espera_par>0) {
        up(&queue_par);
        espera_par--;
    }

    if (hayImpar && espera_impar>0) {
        up(&queue_impar);
        espera_impar--;
    }
    up(&mtx);

    tarea_planificada = 0;
}

/* Function invoked when timer expires (fires) */
static void fire_timer(struct timer_list *timer)
{
    int cpu_actual;
    char code[MAX_CODESIZE + 1];
    unsigned long flags; 

    spin_lock_irqsave(&sp, flags);
    
    generate_code(code);

    kfifo_in(&cbuffer, code, strlen(code) + 1);

    trace_code_in_buffer(code, kfifo_len(&cbuffer));

    if (!tarea_planificada && (kfifo_len(&cbuffer) * 100) / MAX_CBUFFER_LEN >= emergency_threshold) {
        cpu_actual = smp_processor_id();
        schedule_work_on(!(cpu_actual & 0x1), &my_work); // Toma el último bit de la cpu (paridad) y lo alterna
        tarea_planificada = 1;
    }

    spin_unlock_irqrestore(&sp, flags);
    
    /* Re-activate the timer*/
    mod_timer(timer, jiffies + msecs_to_jiffies(timer_period_ms)); 
}

static int codetimer_open(struct inode * inode, struct file * file) {
    unsigned long flags;

    if (atomic_read(&abierto) == 2) {
        printk(KERN_INFO "codetimer: codetimer cannot be opened more than two times\n");
        return -EPERM;
    }

    try_module_get(THIS_MODULE);

    atomic_inc(&abierto);

    // Si se abre por primera vez se inicializan todos los campos pero no se activa el timer
    if (atomic_read(&abierto) == 1) {
        INIT_LIST_HEAD(&mylist_even);
        INIT_LIST_HEAD(&mylist_odd);

        spin_lock_irqsave(&sp, flags);
        if ( kfifo_alloc(&cbuffer, MAX_CBUFFER_LEN, GFP_KERNEL) ) {
            printk(KERN_INFO "Can't allocate memory to fifo\n");
            return -ENOMEM;
        }
        spin_unlock_irqrestore(&sp, flags);

        /* Create work*/
        INIT_WORK(&my_work, copy_items_into_list);

        /* Create timer */
        timer_setup(&my_timer, fire_timer, 0);

        my_timer.expires=jiffies + msecs_to_jiffies(timer_period_ms);

        file->private_data = "par";
    }
    // Si se abre por segunda vez no hace falta inicializar nada (aparte del campo private_data), solo activar el timer
    else {
        file->private_data = "impar";

        add_timer(&my_timer);
    }

    return 0;
}

static ssize_t codetimer_read(struct file* filp, char __user* buf, size_t len, loff_t* off)  {
    char kbuf[KBUF_SIZE];
    int nBytes = 0;
    struct list_head* pos, * e;
    struct list_item* item = NULL;

    if (down_interruptible(&mtx)) {
            return -EINTR;
    }

    if (strcmp(filp->private_data, "impar") == 0) {
        while (list_empty(&mylist_odd)) {
            espera_impar++;
            up(&mtx);
            if (down_interruptible(&queue_impar)){
                down(&mtx);
                espera_impar--;
                up(&mtx);
                return -EINTR;
            }
            if (down_interruptible(&mtx))
                return -EINTR;
        }

        list_for_each_safe(pos, e, &mylist_odd) {
            item = list_entry(pos, struct list_item, links);
            nBytes += sprintf(&kbuf[nBytes], "%s\n", item->data);
            list_del(pos);
            trace_code_read(item->data);  
            vfree(item->data);
            vfree(item);
        }
    }    
    else {
        while (list_empty(&mylist_even)) {
            espera_par++;
            up(&mtx);
            if (down_interruptible(&queue_par)){
                down(&mtx);
                espera_par--;
                up(&mtx);
                return -EINTR;
            }
            if (down_interruptible(&mtx))
                return -EINTR;
        }

        list_for_each_safe(pos, e, &mylist_even) {
            item = list_entry(pos, struct list_item, links);
            nBytes += sprintf(&kbuf[nBytes], "%s\n", item->data);
            list_del(pos);
            trace_code_read(item->data);  
            vfree(item->data);
            vfree(item);
        }
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

    if (atomic_read(&abierto) == 2) { // Solo es necesario liberar las estructuras cuando se cierra el primer lector
        del_timer_sync(&my_timer); 
        spin_lock_irqsave(&sp, flags);
        kfifo_reset(&cbuffer);
        kfifo_free(&cbuffer);
        spin_unlock_irqrestore(&sp, flags);
    }

    atomic_dec(&abierto);
    module_put(THIS_MODULE);

    return 0;
}

static const struct proc_ops codetimer_entry_fops = {
    .proc_open = codetimer_open,
    .proc_release = codetimer_release,    
    .proc_read = codetimer_read,
};

static ssize_t codeconfig_write(struct file* filp, const char __user* buf, size_t len, loff_t* off) {

    int available_space = KBUF_SIZE - 1;
    char kbuf[KBUF_SIZE];
    int numero;

    if ((*off) > 0) /* The application can write in this entry just once !! */
        return 0;

    if (len > available_space) {
        printk(KERN_INFO "codeconfig: not enough space!!\n");
        return -ENOSPC;
    }

    if (copy_from_user(&kbuf[0], buf, len)) {
        return -EFAULT;
    }

    kbuf[len] = '\0'; /* Add the `\0' */
    *off += len;
   
    if (sscanf(kbuf, "timer_period_ms %d", &numero) == 1) {
        if (numero <= 0 ) {
            printk(KERN_INFO "codeconfig: timer_period_ms must be greater than 0\n");
            return -EINVAL;
        }
        timer_period_ms = numero;
    }
    else if (sscanf(kbuf, "emergency_threshold %d", &numero) == 1) {
        if (numero <= 0 || numero > 100) {
            printk(KERN_INFO "codeconfig: emergency_threshold must be between 1 and 100\n");
            return -EINVAL;
        }
        emergency_threshold = numero;
    }

    /* No tiene sentido que se pueda modificar code_format si el formato es aleatorio

    else if (sscanf(kbuf, "code_format %s", code) == 1) {
        size = strlen(code);
        if (size > MAX_CODESIZE) {
            printk(KERN_INFO "codeconfig: Code cannot be greater than 8\n");
            return -EINVAL;
        }
        for (i = 0; i < size; i++) {
            if (code[i] != 'a' && code[i] != 'A' && code[i] != '0') {
            printk(KERN_INFO "codeconfig: Code cannot contain a char different than a, A or 0\n");
            return -EINVAL;
            }
        }
        strncpy(code_format, code, 9);
        codesize = size;
    } */

    else {
        printk(KERN_INFO "modlist: Opcion no valida\n");
        return -EINVAL;
    }
    return len;
}

static ssize_t codeconfig_read(struct file* filp, char __user* buf, size_t len, loff_t* off) {
    char kbuf[KBUF_SIZE];
    int nBytes = 0;

    if ((*off) > 0) /* Tell the application that there is nothing left to read */
        return 0;

    nBytes += sprintf(&kbuf[nBytes], "timer_period_ms=%d\n", timer_period_ms);
    nBytes += sprintf(&kbuf[nBytes], "emergency_threshold=%d\n", emergency_threshold);
    // nBytes += sprintf(&kbuf[nBytes], "code_format=%s\n", code_format);

    if (len < nBytes)
        return -ENOSPC;

    /* Transfer data from the kernel to userspace */
    if (copy_to_user(buf, kbuf, nBytes))
        return -EINVAL;

    (*off) += len;  /* Update the file pointer */

    return nBytes;
}

static const struct proc_ops codeconfig_entry_fops = {
    .proc_write = codeconfig_write,   
    .proc_read = codeconfig_read,
};

int init_timer_module( void )
{
    sema_init(&queue_impar, 0);
    sema_init(&queue_par, 0);

    codetimer_entry = proc_create( "codetimer", 0666, NULL, &codetimer_entry_fops);
    if (codetimer_entry == NULL) {
        printk(KERN_INFO "codetimer: Can't create /proc entry\n");
        return -ENOMEM;
    }
    codeconfig_entry = proc_create( "codeconfig", 0666, NULL, &codeconfig_entry_fops);
    if (codeconfig_entry == NULL) {
        remove_proc_entry("codetimer", NULL);
        printk(KERN_INFO "codetimer: Can't create /proc entry\n");
        return -ENOMEM;
    }
    printk(KERN_INFO "codetimer: Module loaded\n");
    return 0;
}


void cleanup_timer_module( void ){
    remove_proc_entry("codetimer", NULL);
    remove_proc_entry("codeconfig", NULL);
    printk(KERN_INFO "codetimer: Module unloaded\n");
}

module_init( init_timer_module );
module_exit( cleanup_timer_module );

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("codetimer Module");