// SPDX-License-Identifier: GPL-2.0
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/delay.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/kthread.h>

int kvm_pids[1024]={0};
int kvm_pids_read_bytes[1024]={0};
int kvm_pids_write_bytes[1024]={0};
static int control =0;
EXPORT_SYMBOL(kvm_pids);
static int pids(struct seq_file *m, void *v)
{
	int i;
	seq_printf(m,"kvm pid is \n");
	for(i=0;i<1024;i++)
	{
		if(kvm_pids[i]==0)
			continue;
		else
		{
			if(pid_task(find_vpid(kvm_pids[i]), PIDTYPE_PID))
				seq_printf(m,"%d\n",kvm_pids[i]);
			else
				kvm_pids[i]=0;
		}

	}
        return 0;
}
static int pids_easy(struct seq_file *m, void *v)
{
        int i;
        seq_printf(m,"kvm io is %d \n",control);
	if(control)
		control=0;
	else
		control=1;
        for(i=0;i<1024;i++)
        {
                if(kvm_pids[i]==0)
                        continue;
                else
                        seq_printf(m,"%d\n",kvm_pids[i]);
        }
        return 0;
}

static void do_io_accounting(struct task_struct *task,int idx, int record, int nice)
{
	struct task_io_accounting acct = task->ioac;
	unsigned long flags;
	struct task_struct *t = task;

	if (lock_task_sighand(task, &flags)) {
	

		task_io_accounting_add(&acct, &task->signal->ioac);
		while_each_thread(task, t)
			task_io_accounting_add(&acct, &t->ioac);

		unlock_task_sighand(task, &flags);
	}
	if(record)
	{
		kvm_pids_read_bytes[idx]=acct.read_bytes-kvm_pids_read_bytes[idx];
		kvm_pids_write_bytes[idx]=acct.write_bytes-kvm_pids_write_bytes[idx];
		if(nice)
		{
			if(kvm_pids_read_bytes[idx]>5000|| kvm_pids_write_bytes[idx]>5000)
			{	
				set_user_nice(task,-20);
				while_each_thread(task, t)
					set_user_nice(t,-20);
			}
			else
			{
				set_user_nice(task,0);
                                while_each_thread(task, t)
                                        set_user_nice(t,0);
			}
		}

	}
	else
	{
		kvm_pids_read_bytes[idx]=acct.read_bytes;
		kvm_pids_write_bytes[idx]=acct.write_bytes;
	}
}
static int kvm_io(struct seq_file *m, void *v)
{
	int i;
	struct task_struct *kvm_task;
        seq_printf(m,"kvm io\n");
        for(i=0;i<1024;i++)
        {
                if(kvm_pids[i]==0)
                        continue;
                else
                {
			kvm_task=pid_task(find_vpid(kvm_pids[i]), PIDTYPE_PID);
                        if(kvm_task)
			{
				do_io_accounting(kvm_task,i,0,0);
			}
                        else
			{
                                kvm_pids[i]=0;
			}
                }

        }
	msleep(1000);
	for(i=0;i<1024;i++)
        {
                if(kvm_pids[i]==0)
                        continue;
                else
                {
                        kvm_task=pid_task(find_vpid(kvm_pids[i]), PIDTYPE_PID);
                        if(kvm_task)
			{
                                do_io_accounting(kvm_task,i,1,0);
				seq_printf(m,"kvm pid %d read %d bytes/s write %d bytes/s\n",kvm_pids[i],kvm_pids_read_bytes[i],kvm_pids_write_bytes[i]);
			}	
                        else
                        {
                                kvm_pids[i]=0;
                        }
                }

        }


        return 0;
}
static int io_detector(void *arg0)
{
        int i;
        struct task_struct *kvm_task;
	while(!kthread_should_stop())
	{
	if(control)
	{
		msleep(1000);
		continue;
	}
        for(i=0;i<1024;i++)
        {
                if(kvm_pids[i]==0)
                        continue;
                else
                {
                        kvm_task=pid_task(find_vpid(kvm_pids[i]), PIDTYPE_PID);
                        if(kvm_task)
                        {
                                do_io_accounting(kvm_task,i,0,0);
                        }
                        else
                        {
                                kvm_pids[i]=0;
                        }
                }

        }
        msleep(100);
        for(i=0;i<1024;i++)
        {
                if(kvm_pids[i]==0)
                        continue;
                else
                {
                        kvm_task=pid_task(find_vpid(kvm_pids[i]), PIDTYPE_PID);
                        if(kvm_task)
                        {
                                do_io_accounting(kvm_task,i,1,1);
                        }
                        else
                        {
                                kvm_pids[i]=0;
                        }
                }

        }
	}
	return 0;
}
static int __init proc_cmdline_init(void)
{
	struct task_struct *tsk;
	tsk = kthread_run(io_detector, NULL, "KVM_io");
	if (IS_ERR(tsk)) {
		printk(KERN_ERR "Cannot create KVM_IO, %ld\n", PTR_ERR(tsk));
	}
	proc_create_single("kvm_pids", 0, NULL, pids);
	proc_create_single("kvm_io", 0, NULL, kvm_io);
	proc_create_single("kvm_pids_easy", 0, NULL, pids_easy);
	return 0;
}
fs_initcall(proc_cmdline_init);

