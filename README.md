# Anubis
### General
Please check fs/proc/kvm_new.c for debug and test

Most of the file change are in kernel/sched/fair.c 

and arch/x86/kvm 

The code is still prototype, Not fully clean and perfect coded. 

the guest require the 4.14 guest 

We will update more test case soon 
### Test
##### Preparation 
---
1. Before start the VMs, in the host we need `sudo echo 0xd300 > /proc/ct_offset`.
2. Start the VMs(with correct kernel version).
3. While starting the VMs, do `cat /proc/burrito_flag` to open for 1 sec and `cat /proc/burrito_flag` again.
4. Use `cat /proc/vcpu_list_show` to check if success (all vCPUs value not zero)
5. Use `virsh vcpupin` to bind running VM's vCPU.
6. Create the CPU backgroud pressure `sysbench cpu --time=99999999 --threads={Nr of vCPUs} run &`

##### Example 
---
```
sysbench fileio --file-total-size=10G --file-test-mode=seqrd --time=40 --max-requests=0 --file-extra-flags=direct --report-interval=5 --percentile=99 --threads=1 run
```

More details please refer to the paper Maximizing VMs' IO Performance on Overcommitted CPUs with Fairness

@inproceedings{10.1145/3620678.3624649,
author = {Xing, Tong and Xiong, Cong and Ye, Chuan and Wei, Qi and Picorel, Javier and Barbalace, Antonio},
title = {Maximizing VMs' IO Performance on Overcommitted CPUs with Fairness},
year = {2023},
isbn = {9798400703874},
publisher = {Association for Computing Machinery},
address = {New York, NY, USA},
url = {https://doi.org/10.1145/3620678.3624649},
doi = {10.1145/3620678.3624649},
pages = {93–108},
numpages = {16},
keywords = {IO performance, KVM, Linux, Overcommit, compute resources, fair scheduling, low-latency, virtualization},
location = {<conf-loc>, <city>Santa Cruz</city>, <state>CA</state>, <country>USA</country>, </conf-loc>},
series = {SoCC '23}
}


