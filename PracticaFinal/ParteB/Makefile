obj-m += multififo_android.o 

## Parte común módulos
ANDROIDSRC=$(HOME)/oreo-src
KERNEL_OUT=$(ANDROIDSRC)/out/target/product/x86_64/obj/kernel

all:
	make -C $(KERNEL_OUT)  O=$(KERNEL_OUT)  M=$(PWD)  modules

clean:
	make -C $(KERNEL_OUT) O=$(KERNEL_OUT) M=$(PWD)  clean




