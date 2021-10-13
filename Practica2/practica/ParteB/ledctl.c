#include <linux/syscalls.h> /* For SYSCALL_DEFINEi() */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/tty.h>      /* For fg_console */
#include <linux/kd.h>       /* For KDSETLED */
#include <linux/vt_kern.h>
#include <linux/errno.h>
#include <asm-generic/errno.h>
#include <asm-generic/errno-base.h>

struct tty_driver* kbd_driver= NULL;

struct tty_driver* get_kbd_driver_handler(void){
   return vc_cons[fg_console].d->port.tty->driver;
}

static inline int set_leds(struct tty_driver* handler, unsigned int mask){
    return (handler->ops->ioctl) (vc_cons[fg_console].d->port.tty, KDSETLED,mask);
}

SYSCALL_DEFINE1(ledctl, unsigned int, mask)
{
   
   unsigned int leds = 0;

   if (mask < 0x0) {
      return -EINVAL;
   }

   kbd_driver= get_kbd_driver_handler();

   if (kbd_driver == NULL) {
      return -ENODEV;
   }

   if (mask & 0x1) {
      leds |= 0x1;
   }
   if (mask & 0x2) {
      leds |= 0x4;
   }
   if (mask & 0x4) {
      leds |= 0x2;
   }

   return set_leds(kbd_driver,leds);  
}
