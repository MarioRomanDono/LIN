#define MAX_KBUF 1024
#define MAX_CBUFFER_LEN 1024


int prod_count=0,cons_count=0;
struct kfifo cbuffer;
struct semaphore mtx;
struct semaphore sem_prod;
struct semaphore sem_cons;
int nr_prod_waiting=0;
int nr_cons_waiting=0;

int fifoproc_open(struct inode * inode, struct file * file) {
	if (down_interruptible(&mtx)) {
		return -EINTR;
	}

	if (file->f_mode & FMODE_READ) { // Consumidor
		cons_cont++;

		if (nr_prod_waiting > 0) {
			up(&sem_prod);
			nr_prod_waiting--;
		}

		/* Esperar hasta que el productor abra su extremo de escritura */
		while (prod_cont == 0) {
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
	}
	else { //Productor
		prod_cont++;

		if (nr_cons_waiting > 0) {
			up(&sem_cons);
			nr_cons_waiting--;
		}

		/* Esperar hasta que el consumidor abra su extremo de escritura */
		while (cons_cont == 0) {
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
	}

	up(&mtx);
	return 0;
}

int fifoproc_write(struct file * file, const char * buf, size_t len, loff_t * off) {
	char kbuffer[MAX_KBUF];

	if ((*off) > 0) /* The application can write in this entry just once !! */
        return 0;

	if (len> MAX_CBUFFER_LEN || len> MAX_KBUF) {
		printk(KERN_INFO "fifoproc: not enough space!!\n");
       return -ENOSPC;
	}

	if (copy_from_user( &kbuf[0], buf, len ))  {
            return -EFAULT;	
    }
    
    kbuf[len] = '\0'; /* Add the `\0' */  
    *off+=len;


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

	kfifo_in(&cbuffer,kbuf,len);

	/* Despertar a posible consumidor bloqueado */
	if (nr_cons_waiting > 0) {
		up(&sem_cons);
		nr_cons_waiting--;
	}

	up(&mtx);
	return len;
}

int fifoproc_read(const char* buff, int len) {
	char kbuffer[MAX_KBUF];
	if (len> MAX_CBUFFER_LEN || len> MAX_KBUF) { return Error;}
	
	lock(mtx);
	/* Esperar hasta que el buffer contenga más bytes que los solicitados mediante read (debe haber productores) */
	while (kfifo_len(&cbuffer)<len && prod_count>0 ){
		cond_wait(cons,mtx);
	}
	/* Detectar fin de comunicación correcto (el extremo de escritura ha sido cerrado) */
	if (kfifo_is_empty(&cbuffer) && prod_count==0) {
		unlock(mtx);
		return 0;
	}

	kfifo_out(&cbuffer,kbuffer,len);

	/* Despertar a posible productor bloqueado */
	cond_signal(prod);

	if (copy_to_user(kbuffer,buff,len)) {unlock(mtx); return Error;}

	unlock(mtx);
	return len;
}



void fifoproc_release(bool lectura) {
	lock(mtx);

	if (lectura) {				
		cons_cont--;
		cond_signal(prod);
	}
	else {
		prod_cont--;	
		cond_signal(cons);	
	}

	if (prod_cons == 0 && cons_cont == 0) {
			kfifo_reset(&cbuffer);
	}

	unlock(mtx);
}

int init_fifoproc_module( void )
{
  int ret = 0;
  if ( kfifo_alloc(&cbuffer, MAX_CBUFFER_LEN, GFP_KERNEL) ) {
  		printk(KERN_INFO "fifoproc: Can't allocate memory to fifo\n");
  		return -ENOMEM;
  }
  proc_entry = proc_create( "fifoproc", 0666, NULL, &proc_entry_fops);
  if (proc_entry == NULL) {
        ret = -ENOMEM;
        printk(KERN_INFO "fifoproc: Can't create /proc entry\n");
  } else {
        printk(KERN_INFO "fifoproc: Module loaded\n");
  }
  return ret;

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