# Anubis
### General
Please check fs/proc/kvm_new.c for debug and test

Most of the file change are in kernel/sched/fair.c 

and arch/x86/kvm 

The code is still prototype, Not fully clean and perfect coded. 

the guest require the 4.14 guest 

We will update more test case soon 
### Test
Preparation 

Use `virsh vcpupin` to bind running VM's vCPU.

To create the CPU backgroud pressure `sysbench cpu --time=99999999 --threads={Nr of vCPUs} run &`

Example 
```
sysbench fileio --file-total-size=10G --file-test-mode=seqrd --time=40 --max-requests=0 --file-extra-flags=direct --report-interval=5 --percentile=99 --threads=1 run

```
