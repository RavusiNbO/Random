#include <linux/module.h>
#include <linux/export-internal.h>
#include <linux/compiler.h>

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



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0xb1f02544, "input_close_device" },
	{ 0x1bde2cc, "input_unregister_handle" },
	{ 0x37a0cba, "kfree" },
	{ 0xdcb764ad, "memset" },
	{ 0x38e1ba81, "crypto_shash_digest" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x444391f3, "input_unregister_handler" },
	{ 0x406790b9, "init_net" },
	{ 0xa5235143, "nf_unregister_net_hook" },
	{ 0x3c12dfe, "cancel_work_sync" },
	{ 0xe2964344, "__wake_up" },
	{ 0x3f981128, "misc_deregister" },
	{ 0x69e1e207, "crypto_destroy_tfm" },
	{ 0x92997ed8, "_printk" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0xd9a5ea54, "__init_waitqueue_head" },
	{ 0x80a37921, "crypto_alloc_shash" },
	{ 0x52c5c991, "__kmalloc_noprof" },
	{ 0x460e7978, "misc_register" },
	{ 0x23c31418, "nf_register_net_hook" },
	{ 0x4382b160, "input_register_handler" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0x800473f, "__cond_resched" },
	{ 0xfe487975, "init_wait_entry" },
	{ 0x1000e51, "schedule" },
	{ 0x8c26d495, "prepare_to_wait_event" },
	{ 0x92540fbf, "finish_wait" },
	{ 0x6cbbfc54, "__arch_copy_to_user" },
	{ 0xf1bf7658, "kmalloc_caches" },
	{ 0x121cea39, "__kmalloc_cache_noprof" },
	{ 0xc6f824c9, "input_register_handle" },
	{ 0x4a6f93a0, "input_open_device" },
	{ 0xa65c6def, "alt_cb_patch_nops" },
	{ 0x9c1e5bf5, "queued_spin_lock_slowpath" },
	{ 0x2f2c95c4, "flush_work" },
	{ 0xb43f9365, "ktime_get" },
	{ 0x2d3385d3, "system_wq" },
	{ 0xc5b6f236, "queue_work_on" },
	{ 0xc913f0a6, "noop_llseek" },
	{ 0x8f213c29, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("input:b*v*p*e*-e*1,*k*r*a*m*l*s*f*w*");
