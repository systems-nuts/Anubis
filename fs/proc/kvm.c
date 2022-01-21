#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/kthread.h>
#include <linux/list_sort.h>
#include <linux/tong.h>
#include <linux/sort.h>
DECLARE_HASHTABLE(tbl,2);
DECLARE_HASHTABLE(vtbl,10);

static struct kvm_io *kvm_list;
int kvm_vhost_add(int pid1, int pid2)
{
	struct kvm_vhost *entry;
	entry=(struct kvm_vhost*)kmalloc(sizeof(struct kvm_vhost),GFP_KERNEL);
	entry->kvm_pid=pid1;
	entry->vhost_pid=pid2;
	hash_add(tbl,&entry->node,entry->kvm_pid);
	return 0;
}
EXPORT_SYMBOL(kvm_vhost_add);

int list_kvm_vhost_add(int pid1, int pid2)
{
        struct kvm_io *list;
        list=(struct kvm_io*)kmalloc(sizeof(struct kvm_io),GFP_KERNEL);
        list->kvm_pid=pid1;
        list->vhost_pid=pid2;
	list->net_io=0;
        list_add(&list->node,&kvm_list->node);
	printk("list->kvm %d vhost %d\n",list->kvm_pid,list->vhost_pid);
        return 0;
}
EXPORT_SYMBOL(list_kvm_vhost_add);


int vhost_table_add(int pid)
{
	struct vhost_table *entry;
	printk("vhost table add %d \n",pid);
        entry=(struct vhost_table*)kmalloc(sizeof(struct vhost_table),GFP_KERNEL);
        entry->vhost_pid=pid;
	entry->net_io_read=0;
	entry->net_io_write=0;
        hash_add(vtbl,&entry->node,entry->vhost_pid);
        return 0;
}
EXPORT_SYMBOL(vhost_table_add);
int vhost_table_update_read(int pid,unsigned long len)
{
	struct vhost_table *cur;
	hash_for_each_possible(vtbl,cur,node,pid)
	{
		cur->net_io_read+=len;
	}
	return 0;
}
EXPORT_SYMBOL(vhost_table_update_read);
int vhost_table_update_write(int pid,unsigned long len)
{
        struct vhost_table *cur;
        hash_for_each_possible(vtbl,cur,node,pid)
        {
                cur->net_io_write+=len;
        }
        return 0;
}
EXPORT_SYMBOL(vhost_table_update_write);

int vhost_table_clean_io(int pid)
{
        struct vhost_table *cur;
        hash_for_each_possible(vtbl,cur,node,pid)
        {
                cur->net_io_write=0;
		cur->net_io_read=0;
        }
        return 0;
}

int vhost_table_clean_io_all(void)
{
        struct vhost_table *cur;
	unsigned bkt;
        hash_for_each(vtbl,bkt,cur,node)
        {
                cur->net_io_write=0;
                cur->net_io_read=0;
        }
        return 0;
}


struct vhost_table* vhost_table_get_io(int pid)
{
        struct vhost_table *cur, *ret;
	int vhost_pid;
	hash_for_each_possible(tbl,cur,node,pid)
        {
        	vhost_pid=cur->vhost_pid;
        }

        hash_for_each_possible(vtbl,cur,node,vhost_pid)
        {
		ret=cur;
        }
        return ret;
}

//int kvm_hash_remove(int pid)

int kvm_io_comp(void *priv, const struct list_head *_a,const struct list_head *_b)
{
	struct kvm_io *a=list_entry(_a, struct kvm_io, node);
	struct kvm_io *b=list_entry(_b, struct kvm_io, node);
	if(a->net_io > b->net_io)
		return -1;
	else if(a->net_io < b->net_io)
		return 1;
	else return 0;
}
static int kvm_sort(struct seq_file *m, void *v)
{
	struct list_head *pos;
	struct kvm_io *entry;
	struct vhost_table *cur;
	int vhost_pid;
	seq_printf(m,"update kvm_list\n");
	list_for_each(pos,&kvm_list->node)
	{
		//each entry in kvm list 
		entry=list_entry(pos,struct kvm_io, node);
		//find the vhost -> io in hash table
		vhost_pid=entry->vhost_pid;
		hash_for_each_possible(vtbl,cur,node,vhost_pid)
	        {
		//	printk("vhost %d, read %lu, write %lu, total %lu\n",cur->net_io_read,cur->net_io_write);
                	entry->net_io = cur->net_io_read+cur->net_io_write;
        	}

		seq_printf(m,"kvm %d vhost %d net_io %lu\n",entry->kvm_pid,entry->vhost_pid,entry->net_io);
	}
	list_sort(NULL,&kvm_list->node,kvm_io_comp);
	seq_printf(m,"after sort\n");

	list_for_each(pos,&kvm_list->node)
        {
                entry=list_entry(pos,struct kvm_io, node);
                seq_printf(m,"kvm %d vhost %d net_io %lu\n",entry->kvm_pid,entry->vhost_pid,entry->net_io);
        }
	//vhost_table_clean_io_all();
	return 0;

}

static int read_all(struct seq_file *m, void *v)
{
	unsigned bkt;
	struct vhost_table *cur2;
	struct list_head *pos;
        struct kvm_io *entry;
	printk("fuck\n");
	
	list_for_each(pos,&kvm_list->node)
        {
                entry=list_entry(pos,struct kvm_io, node);
                //seq_printf(m,"kvm %d vhost %d net_io %lu\n",entry->kvm_pid,entry->vhost_pid,entry->net_io);
		
		printk("%llx kvm %d vhost %d net_io %lu\n",entry,entry->kvm_pid,entry->vhost_pid,entry->net_io);
        }
	

	hash_for_each(vtbl,bkt,cur2,node)
        {
		printk("vhost %d read %lu write %lu\n",cur2->vhost_pid,cur2->net_io_read,cur2->net_io_write);
        //        seq_printf(m,"vhost %d read %lu write %lu\n",cur2->vhost_pid,cur2->net_io_read,cur2->net_io_write);
        }
        return 0;
}
static int fake1=100;
static int fake2=200;
static int fake_add(struct seq_file *m, void *v)
{
	vhost_table_add(fake2);

	list_kvm_vhost_add(fake1,fake2);

	vhost_table_update_read(fake2,1000*fake1+fake2);


	fake1+=42;
	fake2+=79;
	printk("done\n");
        return 0;
}
static int __init proc_cmdline_init(void)
{
	printk("start fuck \n");
	//hash_init(tbl);
	hash_init(vtbl);
	kvm_list=(struct kvm_io*)kmalloc(sizeof(struct kvm_io),GFP_KERNEL);
	INIT_LIST_HEAD(&kvm_list->node);
	proc_create_single("list_sort",0,NULL,kvm_sort);
	proc_create_single("hash_all",0,NULL,read_all);
	proc_create_single("fake_add",0,NULL,fake_add);
        return 0;
}
fs_initcall(proc_cmdline_init);


