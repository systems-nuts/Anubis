#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/init.h>      // included for __init and __exit macros
#include <asm/msr.h>
typedef unsigned long long ticks;


void MSR_vs_MMIO(int way)
{
	unsigned long long benchmark_record=1;
	unsigned long long a;
	unsigned long long b;
	int i = 0;
	unsigned long *msrReg;
	unsigned long *mmio;
	printk("address msr %llx mmio %llx\n",phys_to_virt(0x00000800),phys_to_virt(0xfee00000));
	msrReg = ioremap(0x00000800+0x280,100);
	mmio = ioremap(0xfee00000+0x280,1000);
	printk("ioremap %lx <- 0xfee00000 -> %lx\n ",mmio,*mmio);

    		for (i = 0; i < 10000; i++)
        	{
		        a =  rdtsc();   
			native_apic_mem_write(APIC_ESR, 0);
		        b =  rdtsc();   
		        if ( b > a )
		        	benchmark_record += b - a ;
        	}
	printk("%lld %lld  done \n", benchmark_record/10000, benchmark_record);
}
static int __init hello_init(void)
{
    printk(KERN_INFO "Hello world!\n");
    MSR_vs_MMIO(1);
    return 0;    // Non-zero return means that the module couldn't be loaded.
}

static void __exit hello_cleanup(void)
{
    printk(KERN_INFO "Cleaning up module.\n");
}
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mazi");
module_init(hello_init);
module_exit(hello_cleanup);
