#ifndef XV6_RISCV_OS2_RSICV_RAID_RAID_H
#define XV6_RISCV_OS2_RSICV_RAID_RAID_H

#include "types.h"
// Disk size in bytes
#define DISK_SIZE 1 << 27
// Blocks per disk
// Limited to 512 for testing purposes
// Real value = ((DISK_SIZE) / (BSIZE))
#define BLKN 1024


int init_raid(int raid_type);
int read_raid(int blkn, uchar* data);
int write_raid(int blkn, uchar* data);
int disk_fail_raid(int diskn);
int disk_repaired_raid(int diskn);
int info_raid(uint *blkn, uint *blks, uint *diskn);
int destroy_raid();

void init_raid_lock();

#endif //XV6_RISCV_OS2_RSICV_RAID_RAID_H
