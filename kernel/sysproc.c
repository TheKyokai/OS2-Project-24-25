#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

#include "fs.h"
#include "raid.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}


// RAID SYSCALLS (Wrappers)
// Using a temporary page in kernel space to store the data that is later copied to user space using copyout
uint64
sys_init_raid(void)
{
    int raid_type;
    argint(0,&raid_type);
    int status = init_raid(raid_type);
    return status;
}

uint64
sys_read_raid(void)
{
    int blkn;
    uint64 data_address;
    argint(0,&blkn);
    argaddr(1,&data_address);
    uchar* temp_page_address = kalloc();
    int status = read_raid(blkn, temp_page_address);
    copyout(myproc()->pagetable, data_address, (char *) temp_page_address, BSIZE);
    kfree(temp_page_address);
    return status;
}

uint64
sys_write_raid(void)
{
    int blkn;
    uint64 data_address;
    argint(0,&blkn);
    argaddr(1,&data_address);
    uchar* temp_page_address = kalloc();
    copyin(myproc()->pagetable, (char *) temp_page_address, data_address, BSIZE);
    int status = write_raid(blkn, temp_page_address);
    kfree(temp_page_address);
    return status;
}

uint64
sys_disk_fail_raid(void)
{
    int diskn;
    argint(0,&diskn);
    int status = disk_fail_raid(diskn);
    return status;
}

uint64
sys_disk_repaired_raid(void)
{
    int diskn;
    argint(0,&diskn);
    int status = disk_repaired_raid(diskn);
    return status;
}

uint64
sys_info_raid(void)
{
    uint64 blkn;
    uint64 blks;
    uint64 diskn;
    argaddr(0,&blkn);
    argaddr(1,&blks);
    argaddr(2,&diskn);
    uchar* temp_page_address = kalloc();
    uint* blkn_addr = (uint*) temp_page_address;
    uint* blks_addr = (uint*) temp_page_address + 1;
    uint* diskn_addr = (uint*) temp_page_address + 2;
    int status = info_raid(blkn_addr, blks_addr, diskn_addr);
    copyout(myproc()->pagetable, blkn, (char*) blkn_addr, sizeof(uint));
    copyout(myproc()->pagetable, blks, (char*) blks_addr, sizeof(uint));
    copyout(myproc()->pagetable, diskn, (char*) diskn_addr, sizeof(uint));
    kfree(temp_page_address);
    return status;
}

uint64
sys_destroy_raid(void)
{
    int status = destroy_raid();
    return status;
}
