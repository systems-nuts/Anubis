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
#include <linux/trace.h>
#include <linux/log2.h>
#include <linux/kvm_host.h>
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <trace/events/sched.h>

DEFINE_SPINLOCK(debts_lock);
EXPORT_SYMBOL(debts_lock);
DECLARE_HASHTABLE(vvtbl,10);
int vcfs_timer=0;
int vcfs_timer2=0;
static int _counter, _counter2;
static struct vcpu_io *vcpu_list;
static struct kvm_irq_vcpu *irq_list;
static int booster_pid;
static int control;
unsigned long long yield_level=0;
unsigned long long yield_time=40000000000; //max yield 50sec by default
EXPORT_SYMBOL(yield_time);
EXPORT_SYMBOL(yield_level);
int list_table_vcpu_add(int pid1, int pid2, int id, int cpu)
{
	struct vcpu_io *entry;
	entry=(struct vcpu_io*)kmalloc(sizeof(struct vcpu_io),GFP_KERNEL);
	_counter++;
        printk("counter ++ %d\n",_counter);
	entry->kvm_pid=pid1;
	entry->vcpu_id=id;
	entry->vcpu_pid=pid2;
	entry->eventfd_time=0;
	entry->vcpu_running_at=cpu;
	entry->IRQ_vcpu_pid=0;
	hash_add(vvtbl,&entry->hnode,entry->vcpu_pid);
	list_add(&entry->lnode,&vcpu_list->lnode);

	return 0;

}
EXPORT_SYMBOL(list_table_vcpu_add);

int list_kvm_irq_list_add(int kvm_pid)
{
	struct kvm_irq_vcpu *irq;
	int *IRQ_record;
	int i;
	IRQ_record = (int*)kmalloc(sizeof(int)*100,GFP_KERNEL);//change 100 -> max vcpu
        irq=(struct kvm_irq_vcpu*)kmalloc(sizeof(struct kvm_irq_vcpu),GFP_KERNEL);
	_counter2++;
        printk("kvm_pid %d\n",kvm_pid);
        irq->kvm_pid=kvm_pid;
        irq->IRQ_vcpu_pid=0;
	irq->IRQ_time=IRQ_record;
	for(i=0; i<100; i++)
		irq->IRQ_time[i]=0;
        list_add(&irq->lnode,&irq_list->lnode);
	printk("wtf? %d",kvm_pid);
	return 0; 
}
EXPORT_SYMBOL(list_kvm_irq_list_add);
extern int sched_check_task_is_running(struct task_struct *tsk);
extern void sched_force_schedule(struct task_struct *tsk, int clear_flag);
extern void sched_extend_life(struct task_struct *tsk);

//CLEARLY THERE IS A BUG, BEFORE IT GOT CHECK, THE IRQ_VCPU_PID MIGHT GOT 
//REFREASH, BECAUSE THE IPI CAN HAPPENED WAY MUCH LATE THAN THE TIME IRQ CAME
//SO, INSTEAD OF EACH KVM <-> IRQ_VCPU 
//WE DO EACH KVM -> VCPU[N], EACH VCPU KEEP THE TIME IT HAS BEEN INTERRUPTED
//THEN WHENEVER WE CHECK ONE, WE JUST MINUS 1 UNTIL IT IS 0 -> NO IRQ 
//SO IT WOULD BE FINE. 
//2022-08-03 TONG AT UoE, TRY FINISH THIS PATCH BY NEXT DAY.
//2023-05-02 TONG AT UoE, UPDATE this, we only boost the IPI after a certain time it received irq
//Each vCPU received a IRQ, will has a timestamp, within this time slice, we boost all the IPI.
//return 1 indicate recently received IRQ
int check_is_IRQ_vcpu(int vcpu_pid)
{
	struct task_struct *IRQ_vcpu;
	ktime_t time_interval;
	u64 ns;
	IRQ_vcpu = find_get_task_by_vpid(vcpu_pid);
	time_interval = ktime_sub(ktime_get(),IRQ_vcpu->event_fd_time);
	ns = ktime_to_ns(time_interval);
	if(ns < 4800000) //about 2 tick 2.4ms
	{
		trace_sched_check_IRQ(1,ns);
		return 1; //recently have received IRQ
	}
	else
	{
		trace_sched_check_IRQ(0,ns);
		return 0; //nope
	}
}
int check_irq_vcpu(int vcpu_vpid, int curr_kvm)
{
    struct kvm_irq_vcpu *irq;
	int i,vcpu_id=0;
    struct list_head *pos,*next;
	struct kvm_vcpu *vcpu;
	list_for_each_safe(pos,next,&irq_list->lnode) 
    {
		irq=list_entry(pos,struct kvm_irq_vcpu, lnode);
		//irq: the vCPU has received the IRQ
		if(irq) //it it exist
		{
			if(irq->kvm_pid == curr_kvm) //this IRQ vCPU is the sender of IPI
			{
				kvm_for_each_vcpu(i, vcpu, irq->kvm_structure)
				{
					if(vcpu->pid->numbers[0].nr == vcpu_vpid) 
					{
						vcpu_id=vcpu->vcpu_id;
						if(irq->IRQ_time[vcpu_id]>0)
						{
							irq->IRQ_time[vcpu_id]-=1;
		        	       	        	return 1;
						}
						else
							return 0;
					}
				}
			}
		}
    }
	return 0;

}
//TODO find the longest life one intsead of just check if it is running

int kvm_vcpu_young(struct kvm *kvm, int dest_id)
{
	unsigned int i;
	struct kvm_vcpu *vcpu;
	struct list_head *pos;
	struct kvm_irq_vcpu *irq;
    struct vcpu_io *entry;
    struct task_struct *IO_vcpu;
	int ret = 0, irq_vcpu=0, vcpuID=0;
	if(dest_id > (1 << (kvm->created_vcpus-1)))
		dest_id = (1 << (kvm->created_vcpus-1)); 
	vcpu=kvm->vcpus[order_base_2(dest_id)];
	IO_vcpu = find_get_task_by_vpid(vcpu->pid->numbers[0].nr);
	if(sched_check_task_is_running(IO_vcpu))
	{
		ret = dest_id;
		irq_vcpu=vcpu->pid->numbers[0].nr;
	}
	else
	{
		kvm_for_each_vcpu(i, vcpu, kvm) {
			IO_vcpu = find_get_task_by_vpid(vcpu->pid->numbers[0].nr);
			if(sched_check_task_is_running(IO_vcpu))
			{
				ret = 1 << vcpu->vcpu_id;
				vcpuID=vcpu->vcpu_id;
				irq_vcpu=vcpu->pid->numbers[0].nr;
				break;
			}
		}
	}
    /*
	list_for_each(pos,&irq_list->lnode)
        {
                irq=list_entry(pos,struct kvm_irq_vcpu, lnode);
                if(irq)
                {
			//list of KVM, each has a content point to IRQ vcpu
                        if(irq->kvm_pid == (int)kvm->userspace_pid)
			{
				//set the one who receive the IRQ as the irq_vcpu
				//for later boosting in IPI
                                //irq->IRQ_vcpu_pid = irq_vcpu;
				if(irq->IRQ_time[vcpuID]<100)
					irq->IRQ_time[vcpuID]+=1;
				irq->kvm_structure=kvm;
			//	printk("IRQ_VCPU ID %d\n",vcpuID);
			}
                }
        }
*/
	return ret;
	
}
EXPORT_SYMBOL(kvm_vcpu_young);
void boost_IO_vcpu(struct kvm *kvm, int vcpu_pid, int dest_id)
{
	dest_id=order_base_2(dest_id);
	if(dest_id > kvm->created_vcpus-1) //incase it goes to all node
        dest_id = kvm->created_vcpus-1; 
	//default goes to 1 -> vcpu0
	//CLEARLY WE NEED SOME CHANGE TO UPDATE THIS, 
	//BECAUSE CURRENTLY IT JUST BOOST ALL IPI, 
	//LET IT ONLY BOOST THE IPI THAT WAKE UP THE 
	//IO THAT WE WANT INSTEAD OF THE RESCHEDULER OR TIMER, ETC
	struct list_head *pos;
    struct vcpu_io *entry;
    struct task_struct *IO_vcpu;
	int curr_kvm;

    list_for_each(pos,&vcpu_list->lnode)
    {
		entry=list_entry(pos,struct vcpu_io, lnode);
		if(!entry)
			return;
		if(entry->vcpu_pid == vcpu_pid)
		{
			curr_kvm = entry->kvm_pid;
        }	
    }
	//if(!check_is_IRQ_vcpu(vcpu_pid)) //check if sender recently has IRQ
	//	return;

	list_for_each(pos,&vcpu_list->lnode)
    {
		entry=list_entry(pos,struct vcpu_io, lnode);
        if(!entry)
			return;
		if(entry->kvm_pid == curr_kvm && entry->vcpu_id == dest_id)
        {
			IO_vcpu = find_get_task_by_vpid(entry->vcpu_pid);
        }
	}

	if(!IO_vcpu)
		return;
	if(!sched_check_task_is_running(IO_vcpu))
		sched_force_schedule(IO_vcpu,1);
    else
	{
        IO_vcpu->lucky_guy+=1; //if I have a IPI, I will get more time
	}

}
EXPORT_SYMBOL(boost_IO_vcpu);

void boost_IRQ_vcpu(int vcpu_pid)
{
	booster_pid = vcpu_pid;
	struct list_head *pos, *pos2;
	struct kvm_irq_vcpu *irq;
    struct vcpu_io *entry;
	struct task_struct *IRQ_vcpu;
    list_for_each(pos,&vcpu_list->lnode)
    {
		entry=list_entry(pos,struct vcpu_io, lnode); 
		if(entry->vcpu_pid == vcpu_pid) //get the vCPU struture from our list
        {	
			IRQ_vcpu = find_get_task_by_vpid(entry->vcpu_pid); //get task strut of vCPU
	//		IRQ_vcpu->event_fd_time= ktime_get();
			/*
			list_for_each(pos2,&irq_list->lnode)
		    {
        		irq=list_entry(pos2,struct kvm_irq_vcpu, lnode); 
				if(irq)
				{
					if(irq->kvm_pid == entry->kvm_pid) //mark the vCPU as irq
						irq->IRQ_vcpu_pid = entry->vcpu_pid;
				}
    		}
			*/
		}
    }
	//current running task
	if(!IRQ_vcpu)
		return;
		
	if(!sched_check_task_is_running(IRQ_vcpu)) //if current running is not IRQ_vcpu
	{
        sched_force_schedule(IRQ_vcpu,0);
	}
    else
	{
		if(vcfs_timer2)
		{
	        IRQ_vcpu->lucky_guy+=1;
//			IRQ_vcpu->boost_heap+=1;
		}
	}
	
}
EXPORT_SYMBOL(boost_IRQ_vcpu);
//add vhost pid to each vcpu structure 
/*
int list_table_update_vhost_pid(int vhost_pid, int kvm_pid)
{
        struct list_head *pos;
        struct vcpu_io *entry;
        int vcpu_pid;

        list_for_each(pos,&vcpu_list->lnode)
        {
                entry=list_entry(pos,struct vcpu_io, lnode);
		if(entry->kvm_pid == kvm_pid)
			entry->vhost_pid = vhost_pid;
        }
        return 0;

}
EXPORT_SYMBOL(list_table_update_vhost_pid);
*/
int list_table_vcpu_have(int pid)
{
	struct vcpu_io *cur;
        hash_for_each_possible(vvtbl,cur,hnode,pid)
        {
		if(cur->vcpu_pid == pid)
			return 1;
        }
        return 0;
}
EXPORT_SYMBOL(list_table_vcpu_have);
int vcpu_table_update_io(int pid,int cpu)
{
	struct vcpu_io *cur;
	hash_for_each_possible(vvtbl,cur,hnode,pid)
	{
		cur->eventfd_time++;
		cur->vcpu_running_at=cpu;
	}
	return 0;
}
EXPORT_SYMBOL(vcpu_table_update_io);
/*
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

*/
int vcpu_table_clean_io(int pid)
{
        struct vcpu_io *cur;
        hash_for_each_possible(vvtbl,cur,hnode,pid)
        {
                cur->eventfd_time=0;
        }
        return 0;
}

int vcpu_table_clean_io_all(void)
{
        struct vcpu_io *cur;
	unsigned bkt;
        hash_for_each(vvtbl,bkt,cur,hnode)
        {
		cur->eventfd_time=0;
        }
        return 0;
}



int vcpu_io_comp(void *priv, const struct list_head *_a,const struct list_head *_b)
{
	struct vcpu_io *a=list_entry(_a, struct vcpu_io, lnode);
	struct vcpu_io *b=list_entry(_b, struct vcpu_io, lnode);
	if(a->eventfd_time > b->eventfd_time)
		return -1;
	else if(a->eventfd_time < b->eventfd_time)
		return 1;
	else return 0;
}
static int vcpu_sort(struct seq_file *m, void *v)
{
	struct list_head *pos;
	struct vcpu_io *entry;
	int vcpu_pid;
	list_sort(NULL,&vcpu_list->lnode,vcpu_io_comp);

	list_for_each(pos,&vcpu_list->lnode)
        {
                entry=list_entry(pos,struct vcpu_io, lnode);
                seq_printf(m,"kvm %d vcpu %d vcpu_pid %d pcpu %d io %lu \n",entry->kvm_pid,entry->vcpu_id,entry->vcpu_pid,entry->vcpu_running_at,entry->eventfd_time);
        }
	return 0;

}

static int vcpu_io_sort(struct seq_file *m, void *v)
{
        struct list_head *pos;
        struct vcpu_io *entry;
        int vcpu_pid;
        list_sort(NULL,&vcpu_list->lnode,vcpu_io_comp);

        list_for_each(pos,&vcpu_list->lnode)
        {
                entry=list_entry(pos,struct vcpu_io, lnode);
		if(entry->eventfd_time!=0)
                seq_printf(m,"vcpu %d io %lu \n",entry->vcpu_pid,entry->eventfd_time);
        }
        return 0;

}
static int vcpu_list_show(struct seq_file *m, void *v)
{
	struct list_head *pos;
	struct vcpu_io *entry;
	struct task_struct *tmp;
	list_for_each(pos,&vcpu_list->lnode)
        {
                entry=list_entry(pos,struct vcpu_io, lnode);
		//seq_printf(m,"kvm %d vcpu %d vcpu_pid %d pcpu %d io %lu \n",entry->kvm_pid,entry->vcpu_id,entry->vcpu_pid,entry->vcpu_running_at,entry->eventfd_time);
		tmp = find_get_task_by_vpid(entry->vcpu_pid);
		seq_printf(m,"kvm %d vcpu %d current %llx gpa %llx\n", entry->kvm_pid, entry->vcpu_pid, tmp->gcurrent_ptr, tmp->mygpa);
        }
        return 0;
}

static int fake1=100;
static int fake2=200;
static int fake3=1;
static int vcpu_fake_add(struct seq_file *m, void *v)
{
	list_table_vcpu_add(fake1, fake2, fake3,4);
	if(fake3)
		vcpu_table_update_io(fake2,4);


	fake1+=42;
	fake2+=79;
	fake3+=1;
	fake3%=4;
	printk("done\n");
        return 0;
}
static int sleep_time=500;
static int vcpu_boosting_worker(void *arg0)

{
	while(!kthread_should_stop())
	{
		if(control)
			vcpu_table_clean_io_all();
		msleep(sleep_time);
	}
	return 0;
}
static int vcpu_boosting(struct seq_file *m, void *v)
{
        if(control) control=0;
        else control =1;
        seq_printf(m,"vcpu boosting %d\n",control);
        return 0;
}

int cfs_boost_flag =0;
int cfs_print_flag =0;

EXPORT_SYMBOL(cfs_boost_flag);
EXPORT_SYMBOL(cfs_print_flag);

static int cfs_boost(struct seq_file *m, void *v)
{
        if(cfs_boost_flag)
                cfs_boost_flag=0;
        else
                cfs_boost_flag=1;
        seq_printf(m, "cfs_boost_flag %d\n",cfs_boost_flag);
        return 0;
}
static int cfs_print(struct seq_file *m, void *v)
{
        if(cfs_print_flag)
                cfs_print_flag=0;
        else
                cfs_print_flag=1;
        seq_printf(m, "cfs_print_flag %d\n",cfs_print_flag);
        return 0;
}
int IRQ_redirect=0;
int IRQ_redirect_log=0;
int IRQ_redirect_noboost =0;
int IRQ_redirect_onlyredirect=0;
int fake_yield_flag=0;
int vcfs_timer3=0;
int vcfs_timer4=0;
int burrito_flag =0;
int burrito_flag2 =0;
int burrito_flag3 =0;
EXPORT_SYMBOL(burrito_flag3);

EXPORT_SYMBOL(burrito_flag2);

EXPORT_SYMBOL(burrito_flag);
EXPORT_SYMBOL(vcfs_timer3);

EXPORT_SYMBOL(vcfs_timer);
EXPORT_SYMBOL(vcfs_timer2);
EXPORT_SYMBOL(vcfs_timer4);

EXPORT_SYMBOL(fake_yield_flag);
EXPORT_SYMBOL(IRQ_redirect);
EXPORT_SYMBOL(IRQ_redirect_noboost);
EXPORT_SYMBOL(IRQ_redirect_onlyredirect);
EXPORT_SYMBOL(IRQ_redirect_log);

static int time_check(struct seq_file *m, void *v)
{
        if(vcfs_timer)
            vcfs_timer=0;
        else
            vcfs_timer=1;
        seq_printf(m, "vcfs_timer %d\n",vcfs_timer);
        return 0;
}

static int time_check2(struct seq_file *m, void *v)
{
        if(vcfs_timer2)
            vcfs_timer2=0;
        else
            vcfs_timer2=1;
        seq_printf(m, "vcfs_timer2 %d\n",vcfs_timer2);
        return 0;
}
static int time_check3(struct seq_file *m, void *v)
{
        if(vcfs_timer3)
            vcfs_timer3=0;
        else
            vcfs_timer3=1;
        seq_printf(m, "vcfs_timer3 %d\n",vcfs_timer3);
        return 0;
}
static int time_check44(struct seq_file *m, void *v)
{
        if(vcfs_timer4)
            vcfs_timer4=0;
        else
            vcfs_timer4=1;
        seq_printf(m, "vcfs_timer4 %d\n",vcfs_timer4);
        return 0;
}

static int time_check4(struct seq_file *m, void *v)
{
        if(burrito_flag)
            burrito_flag=0;
        else
            burrito_flag=1;
        seq_printf(m, "burrito_flag %d\n",burrito_flag);
        return 0;
}
static int time_check5(struct seq_file *m, void *v)
{
        if(burrito_flag2)
            burrito_flag2=0;
        else
            burrito_flag2=1;
        seq_printf(m, "burrito_flag2 %d\n",burrito_flag2);
        return 0;
}

static int time_check6(struct seq_file *m, void *v)
{
        if(burrito_flag3)
            burrito_flag3=0;
        else
            burrito_flag3=1;
        seq_printf(m, "burrito_flag3 %d\n",burrito_flag3);
        return 0;
}

static int fake_yield(struct seq_file *m, void *v)
{
        if(fake_yield_flag)
                fake_yield_flag=0;
        else
		fake_yield_flag=1;
        seq_printf(m, "fake_yield_flag %d\n",fake_yield_flag);
        return 0;
}

static int irq_check2(struct seq_file *m, void *v)
{
        if(IRQ_redirect_log)
                IRQ_redirect_log=0;
        else
                IRQ_redirect_log=1;
        seq_printf(m, "IRQ_redirect_log %d\n",IRQ_redirect_log);
        return 0;
}


static int irq_check(struct seq_file *m, void *v)
{
        if(IRQ_redirect)
                IRQ_redirect=0;
        else
                IRQ_redirect=1;
        seq_printf(m, "IRQ_redirect %d\n",IRQ_redirect);
        return 0;
}
static int irq_noboost(struct seq_file *m, void *v)
{
        if(IRQ_redirect_noboost)
                IRQ_redirect_noboost=0;
        else
                IRQ_redirect_noboost=1;
        seq_printf(m, "IRQ_redirect_noboost %d\n",IRQ_redirect_noboost);
        return 0;
}
static int irq_redirect_only(struct seq_file *m, void *v)
{
        if(IRQ_redirect_onlyredirect)
                IRQ_redirect_onlyredirect=0;
        else
                IRQ_redirect_onlyredirect=1;
        seq_printf(m, "IRQ_redirect_onlyredirect %d\n",IRQ_redirect_onlyredirect);
        return 0;
}

int check_cpu_boosting(int cpu)
{
	struct list_head *pos;
        struct vcpu_io *entry;
        list_for_each(pos,&vcpu_list->lnode)
        {
                entry=list_entry(pos,struct vcpu_io, lnode);
		//if there is IO in 1 sec 
                if(entry->eventfd_time!=0)
			if(entry->vcpu_running_at == cpu)
				return 1;
			
        }
        return 0;

}
EXPORT_SYMBOL(check_cpu_boosting);


int ctx_sw_flag =0;
static int cfs_ctx_sw_flag(struct seq_file *m, void *v)
{
        if(ctx_sw_flag)
                ctx_sw_flag=0;
        else
                ctx_sw_flag=1;
        seq_printf(m, "ctx_sw_flag %d\n",ctx_sw_flag);
        return 0;
}
static unsigned long long record_min =-1; 
static unsigned long long record_max =0;
static unsigned long long record_exit =-1;
static long counter=0;
static unsigned long long total_ctx=0;
static unsigned long long record;
void cfs_record_run(void)
{
	
	if(rdtsc() < record_exit)
		return;
	record = rdtsc() - record_exit;
	if (record_min > record)
		record_min=record;
	if (record_max < record)
		record_max=record;
	total_ctx+=record;
	counter++;
}

static int cfs_ctx_sw_show(struct seq_file *m, void *v)
{
	int i;
        struct kvm_irq_vcpu *irq;
        struct list_head *pos;
        list_for_each(pos,&irq_list->lnode)
        {
                irq=list_entry(pos,struct kvm_irq_vcpu, lnode);
                if(irq)
                {
			seq_printf(m, "kvm pid %d\n",irq->kvm_pid);
			for(i=0; i<5; i++)
			{
				seq_printf(m, "vcpu %d irq %d\n", i, irq->IRQ_time[i]);
			}
                }
        }
	return 0;
}
static int cfs_ctx_sw_refresh(struct seq_file *m, void *v)
{
	record_min=-1;
	record_max=0;
	record_exit=-1;
	total_ctx=0;
	counter=0;
	return 0;
}


void cfs_record_exit(void)
{
	record_exit=rdtsc();
}
EXPORT_SYMBOL(ctx_sw_flag);
EXPORT_SYMBOL(cfs_record_exit);
EXPORT_SYMBOL(cfs_record_run);

unsigned long ct_offset;
EXPORT_SYMBOL(ct_offset);

static int yield_level_show(struct seq_file *m, void *v)
{
	seq_printf(m, "yield_level %d \n", yield_level);
	return 0;
}
static int yield_level_open(struct inode *inode, struct file *filp)
{
        return single_open(filp, yield_level_show, NULL);
}


static ssize_t
yield_level_write(struct file *filp, const char __user *ubuf,size_t cnt, loff_t *ppos)
{
	int ret;
	unsigned long long res;
	ret = kstrtoull_from_user(ubuf, cnt, 10, &res);
	if (ret) {
        	/* Negative error code. */
		return ret;
	} 
	else 
	{
		yield_level=res;	
		return cnt;
	}
}

static int yield_level_show2(struct seq_file *m, void *v)
{
        seq_printf(m, "yield_time %llu \n", yield_time);
        return 0;
}
static int yield_level_open2(struct inode *inode, struct file *filp)
{
        return single_open(filp, yield_level_show2, NULL);
}


static ssize_t
yield_level_write2(struct file *filp, const char __user *ubuf,size_t cnt, loff_t *ppos)
{
        unsigned long long ret;
        unsigned long long res;
        ret = kstrtoull_from_user(ubuf, cnt, 14, &res);
        if (ret) {
                /* Negative error code. */
                return ret;
        }
        else
        {
                yield_time=res;
                return cnt;
        }
}
static int yield_level_show3(struct seq_file *m, void *v)
{
        seq_printf(m, "ct_offset 0x%llx \n", ct_offset);
        return 0;
}
static int yield_level_open3(struct inode *inode, struct file *filp)
{
        return single_open(filp, yield_level_show3, NULL);
}

static ssize_t
yield_level_write3(struct file *filp, const char __user *ubuf,size_t cnt, loff_t *ppos)
{
        int ret;
        unsigned long long res;
        ret = kstrtoull_from_user(ubuf, cnt, 16, &res);
        if (ret) {
                /* Negative error code. */
                return ret;
        }
        else
        {
                ct_offset=res;
                return cnt;
        }
}

unsigned long  kvm_phys_base;
EXPORT_SYMBOL(kvm_phys_base);
static int yield_level_show4(struct seq_file *m, void *v)
{
        seq_printf(m, "kvm_phys_base 0x%llx \n", kvm_phys_base);
        return 0;
}
static int yield_level_open4(struct inode *inode, struct file *filp)
{
        return single_open(filp, yield_level_show4, NULL);
}

static ssize_t
yield_level_write4(struct file *filp, const char __user *ubuf,size_t cnt, loff_t *ppos)
{
        int ret;
        unsigned long long res;
        ret = kstrtoull_from_user(ubuf, cnt, 16, &res);
        if (ret) {
                /* Negative error code. */
                return ret;
        }
        else
        {
                kvm_phys_base=res;
                return cnt;
        }
}


static const struct proc_ops kvm_file_ops = 
{
	.proc_open = yield_level_open,
	.proc_read = seq_read,
	.proc_write = yield_level_write,
};

static const struct proc_ops kvm_file_ops2 =
{
        .proc_open = yield_level_open2,
        .proc_read = seq_read,
        .proc_write = yield_level_write2,
};

static const struct proc_ops kvm_file_ops3 =
{
        .proc_open = yield_level_open3,
        .proc_read = seq_read,
        .proc_write = yield_level_write3,
};

static const struct proc_ops kvm_file_ops4 =
{
        .proc_open = yield_level_open4,
        .proc_read = seq_read,
        .proc_write = yield_level_write4,
};


static int __init proc_cmdline_init(void)
{
	//struct task_struct *tsk;
	hash_init(vvtbl);
	vcpu_list=(struct vcpu_io*)kmalloc(sizeof(struct vcpu_io),GFP_KERNEL);
	irq_list=(struct kvm_irq_vcpu*)kmalloc(sizeof(struct kvm_irq_vcpu),GFP_KERNEL);
	INIT_LIST_HEAD(&irq_list->lnode);
	INIT_LIST_HEAD(&vcpu_list->lnode);
	_counter=0;
	_counter2=0;
	ct_offset=0;
	proc_create_single("vcpu_list_show",0,NULL,vcpu_list_show);
	proc_create_single("IPI_boost", 0, NULL, cfs_print);
	proc_create_single("IRQ_redirect",0 , NULL, irq_check);
	proc_create_single("IRQ_redirect_log",0 , NULL, irq_check2);
	proc_create_single("REDIRECT_ONLY",0 , NULL, irq_redirect_only);
	proc_create_single("NOBOOST_IRQ",0 , NULL, irq_noboost);
	proc_create_single("Check_IRQ_record",0 , NULL, cfs_ctx_sw_show);
	proc_create_single("Fake_yield_flag",0 , NULL, fake_yield);
    proc_create_single("Vcfs_timer",0 , NULL, time_check);
    proc_create_single("Vcfs_timer2",0 , NULL, time_check2);
    proc_create_single("Vcfs_timer3",0 , NULL, time_check3);
	proc_create_single("Vcfs_timer4",0 , NULL, time_check44);

	proc_create("yield_level",0660,NULL,&kvm_file_ops);
	proc_create("yield_time",0660,NULL,&kvm_file_ops2);

	proc_create_single("burrito_flag",0 , NULL, time_check4);
	proc_create_single("burrito_flag2",0 , NULL, time_check5);
	proc_create_single("burrito_flag3",0 , NULL, time_check6);
	proc_create("ct_offset",0777,NULL,&kvm_file_ops3);
	proc_create("phys_offset",0777,NULL,&kvm_file_ops4);

        return 0;
}
fs_initcall(proc_cmdline_init);


