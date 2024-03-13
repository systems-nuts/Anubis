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
#include <linux/sched.h>
#include <linux/types.h>
DECLARE_HASHTABLE(tbl,2);
DECLARE_HASHTABLE(vtbl,10);
struct sched_param {
	int sched_priority;
};
static int fifo;
static int open;
static struct kvm_io *kvm_list;
static int control;
static struct sched_param *param;
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
	if(open==1)
		printk("%d read len %lu\n",pid,len);
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
	if(open==1)
                printk("%d write len %lu\n",pid,len);
        hash_for_each_possible(vtbl,cur,node,pid)
        {
                cur->net_io_write+=len;
        }
        return 0;
}
EXPORT_SYMBOL(vhost_table_update_write);
int kvm_exit_clean_up(int pid)
{
	struct kvm_io *entry, *node_to_del;
	struct list_head *pos;
	struct vhost_table *cur2, *table_to_del;
	printk("kvm %d del\n",pid);

	list_for_each(pos,&kvm_list->node)
        {
                entry=list_entry(pos,struct kvm_io, node);
		if(entry->kvm_pid == pid)
		{
                	printk("kvm %d vhost %d net_io %lu\n",entry->kvm_pid,entry->vhost_pid,entry->net_io);
			node_to_del=entry;
			break;
        	}
	}

	hash_for_each_possible(vtbl,cur2,node,node_to_del->vhost_pid)
        {
                table_to_del=cur2;
                printk("vhost %d read %lu write %lu\n",cur2->vhost_pid,cur2->net_io_read,cur2->net_io_write);
	}
	list_del(&node_to_del->node);
	hlist_del(&table_to_del->node);
	return 0;
}
EXPORT_SYMBOL(kvm_exit_clean_up);

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
	return 0;

}

static int kvm_list_for_cgroup(struct seq_file *m, void *v)
{
        struct list_head *pos;
        struct kvm_io *entry;
        struct vhost_table *cur;
        int vhost_pid;
        list_for_each(pos,&kvm_list->node)
        {
                entry=list_entry(pos,struct kvm_io, node);
                vhost_pid=entry->vhost_pid;
                hash_for_each_possible(vtbl,cur,node,vhost_pid)
                {
                        entry->net_io = cur->net_io_read+cur->net_io_write;
                }
        }
        list_sort(NULL,&kvm_list->node,kvm_io_comp);

        list_for_each(pos,&kvm_list->node)
        {
                entry=list_entry(pos,struct kvm_io, node);
		seq_printf(m,"%d\n",entry->kvm_pid);
        }
        return 0;

}

static int read_all(struct seq_file *m, void *v)
{
	unsigned bkt;
	struct vhost_table *cur2;
	struct list_head *pos;
        struct kvm_io *entry;
	
	list_for_each(pos,&kvm_list->node)
        {
                entry=list_entry(pos,struct kvm_io, node);
                seq_printf(m,"kvm %d vhost %d net_io %lu\n",entry->kvm_pid,entry->vhost_pid,entry->net_io);
		
		//printk("%llx kvm %d vhost %d net_io %lu\n",entry,entry->kvm_pid,entry->vhost_pid,entry->net_io);
        }
	

	hash_for_each(vtbl,bkt,cur2,node)
        {
		//printk("vhost %d read %lu write %lu\n",cur2->vhost_pid,cur2->net_io_read,cur2->net_io_write);
                seq_printf(m,"vhost %d read %lu write %lu\n",cur2->vhost_pid,cur2->net_io_read,cur2->net_io_write);
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
static int io_boosting(void *arg0)

{
	int nice;
	struct task_struct *kvm_task, *t;
	struct list_head *pos;
        struct kvm_io *entry;
        struct vhost_table *cur;
        int vhost_pid;
	while(!kthread_should_stop())
	{
		nice=-20;
		list_for_each(pos,&kvm_list->node)
        	{
                	//each entry in kvm list 
	                entry=list_entry(pos,struct kvm_io, node);
        	        //find the vhost -> io in hash table
                	vhost_pid=entry->vhost_pid;
	                hash_for_each_possible(vtbl,cur,node,vhost_pid)
        	        {
                        	entry->net_io = cur->net_io_read+cur->net_io_write;
	                }

        	}
        	list_sort(NULL,&kvm_list->node,kvm_io_comp);

	        list_for_each(pos,&kvm_list->node)
        	{
                	entry=list_entry(pos,struct kvm_io, node);
			kvm_task=pid_task(find_vpid(entry->kvm_pid), PIDTYPE_PID);
			
			t=kvm_task;
			//printk("kvm_task %d kvm %d net_io %lu\n",kvm_task->pid,entry->kvm_pid,entry->net_io);
			if(kvm_task && control==0 &&fifo!=0)
			{
				if(entry->net_io>0 && nice<0)
				{
					set_user_nice(kvm_task,nice);
					while_each_thread(kvm_task,t)
					{
						if(t)
							set_user_nice(t,nice);
					}
					nice++;
				}
				else
				{
					set_user_nice(kvm_task,0);
					while_each_thread(kvm_task,t)
					{
						if(t)
                                       	 		set_user_nice(t,0);
					}
				}
			}
			else if(kvm_task && fifo==0 && control!=0)
			{
				if(entry->net_io>0)
                                {
					param->sched_priority=nice+119;
					sched_setscheduler(kvm_task, SCHED_FIFO, param);
                                        while_each_thread(kvm_task,t)
                                        {
                                                if(t)
							sched_setscheduler(t, SCHED_FIFO, param);
                                        }
                                        nice++;
                                }
                                else
                                {
					param->sched_priority=0;
					sched_setscheduler(kvm_task, SCHED_NORMAL, param);
                                        while_each_thread(kvm_task,t)
                                        {
                                                if(t)
                                                        sched_setscheduler(t, SCHED_NORMAL, param);
                                        }
                                }
			}
			else
			{}
		}
        	vhost_table_clean_io_all();
		msleep(400);
	}
	return 0;
}

static int hash_print(struct seq_file *m, void *v)
{
	if(open==0) open=1;
	else open =0;
	seq_printf(m,"open %d\n",open);
	return 0;
}
static int boosting(struct seq_file *m, void *v)
{
	
	if(control==0)
	{
		control=1;
		seq_printf(m,"control is %d, boosting disable\n",control);
	}
	else
	{
		control=0;
		seq_printf(m,"control is %d, boosting enable\n",control);
	}	
	return 0;
}
static int fifoosting(struct seq_file *m, void *v)
{
	if(fifo==0)
        {
                fifo=1;
                seq_printf(m,"fifo is %d, fifo disable\n",control);
        }
        else
        {
                fifo=0;
                seq_printf(m,"fifo is %d, fifo enable\n",control);
        }
        return 0;
}
int print_print_flag=0,record_flag=0,print_flag=0,the_choosen_pid=0;
unsigned long total_process_time=0,total_process_number=0;
EXPORT_SYMBOL(record_flag);
EXPORT_SYMBOL(print_flag);
EXPORT_SYMBOL(total_process_time);
EXPORT_SYMBOL(total_process_number);
EXPORT_SYMBOL(print_print_flag);
static int kvm_print_flag(struct seq_file *m, void *v)
{
        if(print_print_flag==0)
        {
                print_print_flag=1;
                seq_printf(m,"eventfd %d, enable\n",print_print_flag);
        }
        else
        {
		print_print_flag=0;
                seq_printf(m,"eventfd %d, disable\n",print_print_flag);
        }
        return 0;
}
static int my_print_flag(struct seq_file *m, void *v)
{
        if(print_flag==0)
        {
                print_flag=1;
                seq_printf(m,"printflag %d, enable\n",print_flag);
        }
        else
        {
                print_flag=0;
                seq_printf(m,"printflag is %d, disable\n",print_flag);
        }
        return 0;
}
static int my_record_flag(struct seq_file *m, void *v)
{
	if(record_flag==0)
        {
                record_flag=1;
                seq_printf(m,"recordflag %d, enable\n",record_flag);
        }
        else
        {
                record_flag=0;
                seq_printf(m,"recordflag is %d, disable\n",record_flag);
        }
        return 0;
}
static int choosen_pid(struct seq_file *m, void *v)
{
	return 0;
}
static int vhost_result(struct seq_file *m, void *v)
{
	seq_printf(m,"result %lu/%lu=%lu\n",total_process_time,total_process_number,total_process_time/total_process_number);
	return 0;
}
static int __init proc_cmdline_init(void)
{
	struct task_struct *tsk;
	control=1;
	fifo =1;
	open=0;
	hash_init(vtbl);
	param=(struct sched_param*)kmalloc(sizeof(struct sched_param),GFP_KERNEL);
	kvm_list=(struct kvm_io*)kmalloc(sizeof(struct kvm_io),GFP_KERNEL);
	INIT_LIST_HEAD(&kvm_list->node);
	tsk=kthread_run(io_boosting, NULL, "Huawei_Boosting");
	if (IS_ERR(tsk)) {
                printk(KERN_ERR "Cannot create KVM_IO, %ld\n", PTR_ERR(tsk));
        }
	proc_create_single("kvm_list",0,NULL,kvm_list_for_cgroup);
	proc_create_single("list_sort",0,NULL,kvm_sort);
	proc_create_single("hash_all",0,NULL,read_all);
	proc_create_single("fake_add",0,NULL,fake_add);
	proc_create_single("hash_print",0,NULL,hash_print);
	proc_create_single("kvm_boosting",0,NULL,boosting);
	proc_create_single("kvm_fifo",0,NULL,fifoosting);
	proc_create_single("vhost_print",0,NULL,my_print_flag);
	proc_create_single("kvm_eventfd_print",0,NULL,kvm_print_flag);
	proc_create_single("vhost_record",0,NULL,my_record_flag);
	proc_create_single("vhost_choosen_pid",0,NULL,choosen_pid);
	proc_create_single("vhost_result",0,NULL,vhost_result);
        return 0;
}
fs_initcall(proc_cmdline_init);


