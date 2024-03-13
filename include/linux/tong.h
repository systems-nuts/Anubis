/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TONG_H
#define _LINUX_TONG_H

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/poison.h>
#include <linux/const.h>
#include <linux/kernel.h>
#include <linux/hashtable.h>
#include <linux/list.h>
#include <linux/list_sort.h>
#include <linux/kvm_host.h>
#include <linux/fdtable.h>
#include <linux/spinlock.h>
#include <trace/events/sched.h>
//AUG 15 2023 TONG
//@Huawei
//add rdtsc for measuring the overhead
#include<asm/msr.h>
#include<asm/tsc.h>
extern void kvm_get_kvm(struct kvm *kvm);
static int check_debts(struct task_struct *task);
static unsigned long long get_debts(struct task_struct *task);
static unsigned long long update_debts(struct task_struct *task, unsigned long long debts, unsigned long long money);
static void rearrange_vcpu(void);
/*
 * Hash table implementation for scheduler boosting
 *
 */
struct kvm_vhost{
	int kvm_pid;
	int vhost_pid;
	struct hlist_node node;
};
struct vhost_table{
	int vhost_pid;
	unsigned long net_io_read;
	unsigned long net_io_write;
	struct hlist_node node;

};
struct kvm_io{
	int kvm_pid;
	int vhost_pid;
	unsigned long net_io;
	struct list_head node;
};
struct vcpu_io{
	int kvm_pid;
	int vcpu_id;
	int vcpu_pid;
	int vcpu_running_at;
	int IRQ_vcpu_pid;
	unsigned long eventfd_time;
	struct hlist_node hnode;
	struct list_head lnode;
};
struct kvm_irq_vcpu{
	int kvm_pid;
	int IRQ_vcpu_pid;
	int *IRQ_time;
	struct kvm *kvm_structure;
	struct list_head lnode;
};

static int get_kvm_by_vpid(pid_t nr, struct kvm** kvmp)
{
	__u64 anubis_cycle;
    struct pid *pid;
    struct task_struct *task;
    struct files_struct *files;
    int fd, max_fds;
    struct kvm *kvm = NULL;
    rcu_read_lock();
    pid = find_vpid(nr);
    if(!pid)
    {
        rcu_read_unlock();
        return 1;
    }
    task = pid_task(pid, PIDTYPE_PID);
    if(!task)
    {
        rcu_read_unlock();
        return 1;
    }
    files = task->files;
	if(!files)
    {
        rcu_read_unlock();
        return 1;
    }
    max_fds = files_fdtable(files)->max_fds;
    if(!max_fds)
    {
        rcu_read_unlock();
        return 1;
    }
    for(fd = 0; fd < max_fds; fd++)
    {
        struct file* file;
        char buffer[32];
        char* fname;
        if(!(file = fcheck_files(files, fd)))
            continue;
        fname = d_path(&(file->f_path), buffer, sizeof(buffer));
        if(fname < buffer || fname >= buffer + sizeof(buffer))
            continue;
        if(strcmp(fname, "anon_inode:kvm-vm") == 0)
        {
            kvm = file->private_data;
            break;
        }
    }
    rcu_read_unlock();
    if(!kvm)
    {
        return 1;
    }
    (*kvmp) = kvm;
	return 0; 
}
//int check_debts(struct task_struct *task);
//int update_debts(struct task_struct *task, unsigned long debts, unsigned long money);
//unsigned long get_debts(struct task_struct *task);

//RETURN 1:  needs to pay debts
extern unsigned long long yield_time;
extern unsigned long long yield_level;
extern spinlock_t debts_lock;
/*
static int check_debts(struct task_struct *task)
{
	struct kvm *my_kvm;
	int ret;
	signed long long debts;
	ret = get_kvm_by_vpid(task->pid,&my_kvm);
	if(ret)
		return 0;
	rcu_read_lock();
	//spin_lock_irq(&debts_lock);
	debts = my_kvm->debts;
	//spin_unlock_irq(&debts_lock);
	rcu_read_unlock();
	if(debts > 5000000000)
		debts = 5000000000; 
	if(debts > 12000000 && debts <= 1500000000)
		ret = 1; 
	else if (debts > 1500000000)
		ret = 100; //in the range, 1 to 2sec, the mismatch should start yield
	else 
		ret = 0;
	return ret;
}
*/
//RETURN 1: Update successfully
static unsigned long long update_debts(struct task_struct *task, unsigned long long debts, unsigned long long money)
{
	struct kvm *my_kvm;
    int ret;
	unsigned long long value;
    ret = get_kvm_by_vpid(task->pid,&my_kvm);
    if(ret)
        return 0;
	//rcu_read_lock();
	//spin_lock_irq(&debts_lock);
	if(debts)
	{
		trace_sched_vcpu_runtime2(task, my_kvm->debts, 999);
		my_kvm->debts += debts;
		value = debts;
	}
	else
	{
		trace_sched_vcpu_runtime2(task, my_kvm->debts, 111);
		if(my_kvm->debts > 1000000000000000)
			my_kvm->debts = 1200000;
		if(my_kvm->debts > money)
		{
			my_kvm->debts -= money;
			value = money;
		}
		else
		{
			value = 0;
			my_kvm->debts =0;
		}
	}
	//spin_unlock_irq(&debts_lock);
	//rcu_read_unlock();

	return value;
}

static unsigned long long get_debts(struct task_struct *task)
{
	struct kvm *my_kvm;
    unsigned long long debts;
	int ret;
    ret = get_kvm_by_vpid(task->pid,&my_kvm);
	if(ret)
        return 0;
	//rcu_read_lock();
	//spin_lock_irq(&debts_lock);
    debts = my_kvm->debts;
	//spin_unlock_irq(&debts_lock);
	//rcu_read_unlock();
	return debts;
}
static void _rearrange_vcpu(struct task_struct *task1, struct task_struct *task2)
{
	cpumask_t mask1, mask2;
	cpumask_copy(&mask1, &task1->cpus_mask);
    cpumask_copy(&mask2, &task2->cpus_mask);

    /* Set the CPU affinity of task1 to mask2 */
    sched_setaffinity(task1, &mask2);

    /* Set the CPU affinity of task2 to mask1 */
    sched_setaffinity(task2, &mask1);
}
static void rearrange_vcpu(void)
{
    struct kvm *my_kvm;
	struct task_struct *task;
	struct kvm_vcpu *vcpu;
    int ret, i;
    ret = get_kvm_by_vpid(current->pid,&my_kvm);
    if(ret)
        return;
	kvm_for_each_vcpu(i, vcpu, my_kvm) {
		task = pid_task(vcpu->pid, PIDTYPE_PID);
		if(task==current)
			continue;
		if(task->conflict==0 && task->boost_heap==0)
		{
			printk("switch %d %d\n",current->pid,task->pid);
			_rearrange_vcpu(current, task);
			return;
		}
	}
}

#endif
