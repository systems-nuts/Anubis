#define _GNU_SOURCE

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <sched.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>
#include <sys/syscall.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/resource.h>
#define MAX_VCPU 12
static int iopid;
static pthread_t *p;
static int flag;
static struct vcpu *my_vcpu;
struct vcpu {
    u_int64_t vcpu_num;
    int64_t left_time;
	pthread_mutex_t lock;
	int up;
	int pid;
};
u_int64_t get_pid_affinity(int pid) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    int s = sched_getaffinity(pid, sizeof(cpu_set_t), &cpuset);
    if (s != 0) return -1;
    u_int64_t j = 0;
    u_int64_t _j = 0;
    for (j = 0; j < CPU_SETSIZE; j++)
        if (CPU_ISSET(j, &cpuset)) {
            _j = j;
        }
    return _j;
}
void set_pid_affinity(u_int64_t vcpu_num, int pid) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(vcpu_num, &cpuset);

    if (sched_setaffinity(pid, sizeof(cpu_set_t), &cpuset) < 0) {
        printf(stderr, "Set thread to VCPU error!\n");
    }
}

void set_nice_priority(int priority, int pid) {
    int which = PRIO_PROCESS;

    int ret = setpriority(which, pid, priority);
}

u_int64_t get_affinity(void) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    int s = pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (s != 0) return -1;
    u_int64_t j = 0;
    u_int64_t _j = 0;
    for (j = 0; j < CPU_SETSIZE; j++)
        if (CPU_ISSET(j, &cpuset)) {
            _j = j;
        }
    return _j;
}
void set_affinity(u_int64_t vcpu_num) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(vcpu_num, &cpuset);

    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) < 0) {
        fprintf(stderr, "Set thread to VCPU error!\n");
    }
}
unsigned long time_check()
{
    struct timespec t1, t2;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    usleep(100);    
    clock_gettime(CLOCK_MONOTONIC, &t2);
    return (t2.tv_nsec - t1.tv_nsec) + (t2.tv_sec - t1.tv_sec) * 1000000000;
}
static struct timespec tt1,tt2;
void escape(struct vcpu *vcpu)
{
	//
	clock_gettime(CLOCK_MONOTONIC, &tt2);
	int p=0;;
	if((tt2.tv_nsec - tt1.tv_nsec) + (tt2.tv_sec - tt1.tv_sec) * 1000000000 > 9000000 )
	{
		p=1;
//		printf("%ld\n",(tt2.tv_nsec - tt1.tv_nsec) + (tt2.tv_sec - tt1.tv_sec) * 1000000000);
	}
	tt1=tt2;
	int64_t max_left_time = 0;
	struct vcpu *ptr,*tmp;
	int max_vpu_num = 100;
	pthread_mutex_t lock;
	int i ,cc=0, bb=0 ;
	ptr = my_vcpu;
	for(i=0; i<MAX_VCPU ; i++ )
	{
		tmp = ptr+i;
		//if(tmp->vcpu_num == 0)
		//	continue;
//		if(tmp->vcpu_num == vcpu->vcpu_num)
//			continue;
		//pthread_mutex_lock(&lock);
	//	if(!tmp->up) 
	//	{
//			cc++;
			//printf("%d is die \n", tmp->vcpu_num);
			//pthread_mutex_unlock(&lock);
//			continue;
//		}
//		if(p)
//			printf("%d has %ld\n",tmp->vcpu_num,tmp->left_time);
		if(tmp->left_time >= max_left_time)
		{
			bb++;
			max_left_time = tmp->left_time;
			max_vpu_num = tmp->vcpu_num;
			usleep(10);
			//printf("left %ld in %d \n", max_left_time, max_vpu_num);
		}
	//	pthread_mutex_unlock(&lock);
	}
	if(max_vpu_num==vcpu->vcpu_num)
	{
		flag=1;
		return;
	}
	if(max_vpu_num==100)
	{
		printf("\neverybody is faling, go die %d %d\n",cc,bb);
		
		flag=1;
		return;
	}
	tmp = ptr +max_vpu_num;
	tmp->pid = vcpu->pid;
	vcpu->pid = 0;
	set_pid_affinity(tmp->vcpu_num,tmp->pid);
	//printf("move %d  to %d with %ld left\n",vcpu->vcpu_num,tmp->vcpu_num, tmp->left_time);
	flag=1;
}
void *thread_func(void *arg) 
{	
	struct vcpu *vcpu = ((struct vcpu*) arg);
	u_int64_t vn = vcpu->vcpu_num;
	pthread_mutex_t lock = vcpu->lock;
	set_affinity(vn);
	vn = get_affinity();
	printf("Timeslice worker is on %lu with IO %d\n", vn, vcpu->pid);
	int pid = syscall(SYS_gettid);
	printf("Time slice thread PID number is %d\n", pid);
	int i=0;
    signed long time;
    signed long acc_time, maybe_time;
	//acc_time, so far how many time has run, 
	//maybe_time, last time slice we use as left time 
	maybe_time =acc_time= 0;
    while(1)
    {
        time = time_check();
		//we sleep and get time
		if(time > 50000000)
		{
			continue;
		}
		//invalid if it larger than 20ms
        if(time<200000)
        {
			vcpu->up = 1;
            acc_time+=time;
        }
		//time less than 500us, since we sleep 300us, give the fact that might have overhead
		//but should not larger than 200us, so we assume it is still alive
        else
        {
			//pthread_mutex_lock(&lock);
			vcpu->up = 1;
			//pthread_mutex_unlock(&lock);
			maybe_time = acc_time;
//			printf("acc %ld\n",acc_time);
            acc_time=0;
        }
		//well, it sleep more than 500us, which possible the fact that it descheduled
		//use the current accumulate time as the maybe timeslice. 
		//reset accumulate time. 
		if(maybe_time-acc_time < 700000 ) //less than 800us, escape, lefttime set to 0. up set to 0.
		{
			//pthread_mutex_lock(&lock);
			vcpu->up = 0;
			if(maybe_time>acc_time)
				vcpu->left_time = maybe_time-acc_time;
			else
				vcpu->left_time =0;
			//pthread_mutex_unlock(&lock);
			if(vcpu->pid && flag)
			{
				flag=0;
		//		pthread_mutex_unlock(&lock);
		//		printf("wtf?\n");
				escape(vcpu);
			}
		//	else
		//		pthread_mutex_unlock(&lock);
		}
		else //more time, update left time
		{
			if(maybe_time>acc_time)
			//pthread_mutex_lock(&lock);
				vcpu->left_time = maybe_time-acc_time;

			//pthread_mutex_unlock(&lock);
		}
		//if accumulate time is less than possible timeslice 600us as their paper said,
		//600us might too small, we change it to 1ms. 
		//it should schedule the IO thread.
		//the IO thread migration in their code, first it try migrate back, if it is not aviable
		//it migrate to anyone has larger than 600us life vCPU.
    }	
}

void init_cpu_thread(int iopid) {
	int ret = 0;
	flag=0;
    u_int64_t i;
	struct vcpu *ptr;
	my_vcpu = (struct vcpu*)malloc(sizeof(struct vcpu) *MAX_VCPU);
	p = (pthread_t *) malloc(sizeof(pthread_t) *MAX_VCPU); //4 vcpu
	int cpu = get_pid_affinity(iopid);
	printf("cpu %d\n",cpu);
	ptr = my_vcpu;
	for (i = 0; i < MAX_VCPU; i++) {
		ptr->vcpu_num = i;
		if(cpu == i)// IO running on this vCPU
			ptr->pid = iopid;
		else
			ptr->pid = 0;
		printf("my_vcpu->pid %d\n",ptr->pid);
		pthread_mutex_init(&ptr->lock, NULL);
		ret = pthread_create(&(p[i]), NULL, thread_func, ptr);
		usleep(100000);
		ptr += 1;
        if (ret != 0) {
            exit(EXIT_SUCCESS);
		}
	}
	flag=1;

}



int main(int argc, char **argv)
{
	int i=0;
	if(argv[1] == NULL)
	{
		printf("need pid\n");
		return 1;
	}
	iopid = atoi(argv[1]);
	printf("pid %d\n",iopid);
    unsigned long time, t1, t2;
	clock_gettime(CLOCK_MONOTONIC, &tt1);
	init_cpu_thread(iopid);
	for (i = 0; i < MAX_VCPU; i++) 
	{
		pthread_join(p[i], NULL);
	}
    return 0;
}

