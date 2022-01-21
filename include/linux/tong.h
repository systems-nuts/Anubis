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
/*
static inline void clean_io_data(struct vhost_table *kv_table)
{
	table->net_io_read=0;
	table->net_io_write=0;
}
static inline void update_io_data(struct vhost_table *kv_table,unsigned long a, unsigned long b)
{
        table->net_io_read=a;
        table->net_io_write=b;
}

*/
#endif
