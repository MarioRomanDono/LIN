#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x7a9aeab2, "module_layout" },
	{ 0xbf1aa5c8, "remove_proc_entry" },
	{ 0xa4bf3758, "proc_create" },
	{ 0xc5850110, "printk" },
	{ 0xbcab6ee6, "sscanf" },
	{ 0x13c49cc2, "_copy_from_user" },
	{ 0xe2d5255a, "strcmp" },
	{ 0x68f31cbd, "__list_add_valid" },
	{ 0xe914e41e, "strcpy" },
	{ 0x754d539c, "strlen" },
	{ 0xd6ee688f, "vmalloc" },
	{ 0xe1537255, "__list_del_entry_valid" },
	{ 0x999e8297, "vfree" },
	{ 0xc959d152, "__stack_chk_fail" },
	{ 0x56470118, "__warn_printk" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x3c3ff9fd, "sprintf" },
	{ 0xbdfb6dbb, "__fentry__" },
};

MODULE_INFO(depends, "");

