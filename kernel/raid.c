#include "raid.h"
#include "types.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"

void printf(char*, ...); // Debugging purposes
void* kalloc(void); // Allocating memory for RCB

void read_block(int diskn, int blockno, uchar* data);
void write_block(int diskn, int blockno, uchar* data);

void acquiresleep(struct sleeplock*);
void releasesleep(struct sleeplock*);
void initsleeplock(struct sleeplock*, char*);


typedef struct raid_control_block {
    uint initialized;
    // RAID type - used for function pointer reloading
    uint type;
    // Pointers to RAID specific functions
    int (*read)(int, uchar*);
    int (*write)(int, uchar*);
    void (*disk_repair)(int);
    uint (*blkn)();
    uint (*diskn)();
    // Disk health: 0 - Faulty, 1 - Healthy
    uint disk_health_status[8];
    // Using leftover memory from page allocation for buffering data in blocks
    uchar block_buffer[BSIZE];
    uchar repaired_block_buffer[BSIZE];
} RCB;

RCB * rcb = 0;
struct sleeplock raid_lock;

// RAID 0
int read_raid_0(int blkn, uchar* data);
int write_raid_0(int blkn, uchar* data);
void repair_raid_0(int diskn);
uint blkn_raid_0();
uint diskn_raid_0();

// RAID 1
int read_raid_1(int blkn, uchar* data);
int write_raid_1(int blkn, uchar* data);
void repair_raid_1(int diskn);
uint blkn_raid_1();
uint diskn_raid_1();

// RAID 0 + 1
int read_raid_01(int blkn, uchar* data);
int write_raid_01(int blkn, uchar* data);
void repair_raid_01(int diskn);
uint blkn_raid_01();
uint diskn_raid_01();

// RAID 4
int read_raid_4(int blkn, uchar* data);
int write_raid_4(int blkn, uchar* data);
void repair_raid_4(int diskn);
uint blkn_raid_4();
uint diskn_raid_4();

// RAID 5
int read_raid_5(int blkn, uchar* data);
int write_raid_5(int blkn, uchar* data);
void repair_raid_5(int diskn);
uint blkn_raid_5();
uint diskn_raid_5();


int
reload_RAID_functions()
{
    switch (rcb->type) {
        case 0:
//            printf("Reloading RAID 0 functions\n");
            rcb->read = read_raid_0;
            rcb->write = write_raid_0;
            rcb->disk_repair = repair_raid_0;
            rcb->blkn = blkn_raid_0;
            rcb->diskn = diskn_raid_0;
            return 0;
        case 1:
//            printf("Reloading RAID 1 functions\n");
            rcb->read = read_raid_1;
            rcb->write = write_raid_1;
            rcb->disk_repair = repair_raid_1;
            rcb->blkn = blkn_raid_1;
            rcb->diskn = diskn_raid_1;
            return 0;
        case 2:
//            printf("Reloading RAID 0+1 functions\n");
            rcb->read = read_raid_01;
            rcb->write = write_raid_01;
            rcb->disk_repair = repair_raid_01;
            rcb->blkn = blkn_raid_01;
            rcb->diskn = diskn_raid_01;
            return 0;
        case 3:
//            printf("Reloading RAID 4 functions\n");
            rcb->read = read_raid_4;
            rcb->write = write_raid_4;
            rcb->disk_repair = repair_raid_4;
            rcb->blkn = blkn_raid_4;
            rcb->diskn = diskn_raid_4;
            return 0;
        case 4:
//            printf("Reloading RAID 5 functions\n");
            rcb->read = read_raid_5;
            rcb->write = write_raid_5;
            rcb->disk_repair = repair_raid_5;
            rcb->blkn = blkn_raid_5;
            rcb->diskn = diskn_raid_5;
            return 0;
        default:
            return -4; // RAID type unknown
    }
}

int
load_RCB()
{
    rcb = (RCB*) kalloc();
    if (rcb <= 0)
        return -10; // Allocation failed
//    printf("RCB allocated\n");
    int raid_initialized = 0;
    for (int i=1; i<=DISKS; i++){
        read_block(i,0,(uchar*)rcb);
//        printf("Checking disk %d for RCB\n", i);
//        printf("Loaded RCB: Init - %d, Type - %d, Disk %d Healthy - %d\n", rcb->initialized, rcb->type, i, rcb->disk_health_status[i]);
        if (rcb->initialized){
            raid_initialized = 1;
            if (rcb->disk_health_status[i]) {
//                printf("Disk %d healthy, valid RCB found\n", i);
                // Reloading function pointers - Loaded pointers may be invalid due to recompiling
                return reload_RAID_functions();
            }
        }
    }
//    printf("RAID Not initialized on any disks\n");
    return raid_initialized? -6: -5;
    // -5 - RAID initialized, no healthy disks to trust
    // -6 - RAID uninitialized
}

int
save_RCB()
{
    if (!rcb)
        return -1; // RCB not loaded into op mem
    for (int i=1; i<=DISKS; i++){
        write_block(i, 0, (uchar*) rcb);
    }
//    printf("RCB saving complete\n");
    return 0;
}

void
clear_block(uchar* block)
{
    for (int i=0; i<BSIZE; i++){
        block[i] = 0;
    }
}

void
clear_disks()
{
    clear_block(rcb->block_buffer);
    for (int disk=1; disk<=DISKS; disk++){
        for (int block=1; block < BLKN; block++){
            write_block(disk, block, rcb->block_buffer);
        }
    }
}

void
init_raid_lock()
{
    initsleeplock(&raid_lock, "raid_lock");
}


int
init_raid(int raid_type)
{
    if (DISKS <= 0)
        return -1;
    acquiresleep(&raid_lock);
    if (!rcb){
        int load_status = load_RCB();
        if (load_status <= -10) // Errors not handled by RAID or not interpreted as errors during init
        {
            releasesleep(&raid_lock);
            return load_status;
        }
    }
    switch (raid_type) {
        case 0:
//            printf("Initializing RAID 0\n");
            rcb->initialized = 1;
            rcb->type = 0;
            rcb->read = read_raid_0;
            rcb->write = write_raid_0;
            rcb->disk_repair = repair_raid_0;
            rcb->blkn = blkn_raid_0;
            rcb->diskn = diskn_raid_0;
            for (int i=0; i<8; i++) // Assuming that init is called when all disks are healthy
                rcb->disk_health_status[i] = 1;
            clear_disks();
            save_RCB();
            releasesleep(&raid_lock);
            return 0;
        case 1:
            if (DISKS < 2) // Not enough disks for RAID 1
            {
                releasesleep(&raid_lock);
                return -3;
            }
//            printf("Initializing RAID 1\n");
            rcb->initialized = 1;
            rcb->type = 1;
            rcb->read = read_raid_1;
            rcb->write = write_raid_1;
            rcb->disk_repair = repair_raid_1;
            rcb->blkn = blkn_raid_1;
            rcb->diskn = diskn_raid_1;
            for (int i=0; i<8; i++) // Assuming that init is called when all disks are healthy
                rcb->disk_health_status[i] = 1;
            clear_disks();
            save_RCB();
            releasesleep(&raid_lock);
            return 0;
        case 2:
            if (DISKS < 2) // Not enough disks for RAID 0 + 1
            {
                releasesleep(&raid_lock);
                return -3;
            }
//            printf("Initializing RAID 0+1\n");
            rcb->initialized = 1;
            rcb->type = 2;
            rcb->read = read_raid_01;
            rcb->write = write_raid_01;
            rcb->disk_repair = repair_raid_01;
            rcb->blkn = blkn_raid_01;
            rcb->diskn = diskn_raid_01;
            for (int i=0; i<8; i++) // Assuming that init is called when all disks are healthy
                rcb->disk_health_status[i] = 1;
            clear_disks();
            save_RCB();
            releasesleep(&raid_lock);
            return 0;
        case 3:
            if (DISKS < 2) // Not enough disks for RAID 4
            {
                releasesleep(&raid_lock);
                return -3;
            }
//            printf("Initializing RAID 4\n");
            rcb->initialized = 1;
            rcb->type = 3;
            rcb->read = read_raid_4;
            rcb->write = write_raid_4;
            rcb->disk_repair = repair_raid_4;
            rcb->blkn = blkn_raid_4;
            rcb->diskn = diskn_raid_4;
            for (int i=0; i<8; i++) // Assuming that init is called when all disks are healthy
                rcb->disk_health_status[i] = 1;
            clear_disks();
            save_RCB();
            releasesleep(&raid_lock);
            return 0;
        case 4:
            if (DISKS < 2) // Not enough disks for RAID 5
            {
                releasesleep(&raid_lock);
                return -3;
            }
//            printf("Initializing RAID 5\n");
            rcb->initialized = 1;
            rcb->type = 4;
            rcb->read = read_raid_5;
            rcb->write = write_raid_5;
            rcb->disk_repair = repair_raid_5;
            rcb->blkn = blkn_raid_5;
            rcb->diskn = diskn_raid_5;
            for (int i=0; i<8; i++) // Assuming that init is called when all disks are healthy
                rcb->disk_health_status[i] = 1;
            clear_disks();
            save_RCB();
            releasesleep(&raid_lock);
            return 0;
        default:
            releasesleep(&raid_lock);
            return -2; // Not supported
    }
}

int
read_raid(int blkn, uchar* data)
{
    if (DISKS <= 0)
        return -1;
    acquiresleep(&raid_lock);
    if (!rcb){
        int load_status = load_RCB();
        if (load_status < 0) {
            releasesleep(&raid_lock);
            return load_status;
        }
    }
    int status = rcb->read(blkn, data);
    releasesleep(&raid_lock);
    return status;
}

int
write_raid(int blkn, uchar* data)
{
    if (DISKS <= 0)
        return -1;
    acquiresleep(&raid_lock);
    if (!rcb){
        int load_status = load_RCB();
        if (load_status < 0) {
            releasesleep(&raid_lock);
            return load_status;
        }
    }
    int status = rcb->write(blkn, data);
    releasesleep(&raid_lock);
    return status;
}

int
disk_fail_raid(int diskn)
{
    if (DISKS <= 0 || diskn <= 0)
        return -1;
    acquiresleep(&raid_lock);
    if (!rcb){
        int load_status = load_RCB();
        if (load_status < 0) {
            releasesleep(&raid_lock);
            return load_status;
        }
    }
    if (diskn > rcb->diskn()){
        releasesleep(&raid_lock);
        return -1;
    }
    printf("Disk %d Fail\n", diskn);
    rcb->disk_health_status[diskn] = 0;
    save_RCB();
    releasesleep(&raid_lock);
    return 0;
}

int
disk_repaired_raid(int diskn)
{
    if (DISKS <= 0 || diskn <= 0)
        return -1;
    acquiresleep(&raid_lock);
    if (!rcb){
        int load_status = load_RCB();
        if (load_status < 0) {
            releasesleep(&raid_lock);
            return load_status;
        }
    }
    if (diskn > rcb->diskn()){
        releasesleep(&raid_lock);
        return -1;
    }
    rcb->disk_repair(diskn);
    save_RCB();
    releasesleep(&raid_lock);
    return 0;
}

int
info_raid(uint *blkn, uint *blks, uint *diskn)
{
    if (DISKS <= 0)
        return -1;
    acquiresleep(&raid_lock);
    if (!rcb){
        int load_status = load_RCB();
        if (load_status < 0) {
            releasesleep(&raid_lock);
            return load_status;
        }
    }
    *blkn = rcb->blkn();
    *blks = BSIZE;
    *diskn = rcb->diskn();
    releasesleep(&raid_lock);
    return 0;
}

int
destroy_raid()
{
    if (DISKS <= 0)
        return -1;
    acquiresleep(&raid_lock);
    if (!rcb){
        int load_status = load_RCB();
        if (load_status <= -10){
            releasesleep(&raid_lock);
            return load_status;
        }
        if (load_status < 0) {
            releasesleep(&raid_lock);
            return 0;
        }
    }
    rcb->initialized = 0;
    save_RCB();
    releasesleep(&raid_lock);
    return 0;
}


// RAID 0
int
read_raid_0(int blkn, uchar* data)
{
    if (blkn >= rcb->blkn())
        return -2; // Logical block out of range

    int physical_blockn, diskn;
    physical_blockn = blkn/DISKS + 1; // To account for bookkeeping block 0
    diskn = blkn%DISKS + 1; // To account for swap disk
//    printf("Logical block %d read: pblock - %d, diskn - %d\n", blkn, physical_blockn, diskn);

    if (!rcb->disk_health_status[diskn])
        return -3; // Disk not healthy
    read_block(diskn, physical_blockn, data);
    return 0;
}

int
write_raid_0(int blkn, uchar* data)
{
    if (blkn >= rcb->blkn())
        return -2; // Logical block out of range

    int physical_blockn, diskn;
    physical_blockn = blkn/DISKS + 1; // To account for bookkeeping block 0
    diskn = blkn%DISKS + 1; // To account for swap disk
//    printf("Logical block %d write: pblock - %d, diskn - %d\n", blkn, physical_blockn, diskn);

    if (!rcb->disk_health_status[diskn])
        return -3; // Disk not healthy
    write_block(diskn, physical_blockn, data);
    return 0;
}

void
repair_raid_0(int diskn)
{
//    printf("RAID 0 repair of disk %d\n", diskn);
    rcb->disk_health_status[diskn] = 1;
}

uint
blkn_raid_0()
{
    return DISKS * (BLKN - 1); // Assuming all disks are healthy
}

uint
diskn_raid_0()
{
    return DISKS;
}


// RAID 1
int
read_raid_1(int blkn, uchar* data)
{
    if (blkn >= rcb->blkn())
        return -2; // Logical block out of range

    int physical_blkn, diskn;
    physical_blkn = (blkn%(BLKN - 1)) + 1; // To account for bookkeeping block 0
    diskn = blkn / (BLKN - 1) + 1; // To account for swap disk

//    printf("Initial Disk %d\n", diskn);
    diskn = rcb->disk_health_status[diskn] ? diskn : diskn + DISKS/2;

//    printf("Logical block %d read: pblock - %d, diskn - %d\n", blkn, physical_blkn, diskn);

    if (!rcb->disk_health_status[diskn])
        return -3; // Disk not healthy
    read_block(diskn, physical_blkn, data);
    return 0;
}

int
write_raid_1(int blkn, uchar* data)
{
    if (blkn >= rcb->blkn())
        return -2; // Logical block out of range

    int physical_blkn, diskn, diskn_mirror;
    physical_blkn = (blkn%(BLKN - 1)) + 1; // To account for bookkeeping block 0
    diskn = blkn / (BLKN - 1) + 1; // To account for swap disk
    diskn_mirror = diskn + DISKS/2;

//    printf("Initial Disk %d\n", diskn);
//    printf("Mirror Disk %d\n", diskn_mirror);

    if (!rcb->disk_health_status[diskn] && !rcb->disk_health_status[diskn_mirror])
        return -3; // Disks not healthy

    // Writing to original
    if (rcb->disk_health_status[diskn]){
//        printf("Logical block %d write: pblock - %d, diskn - %d\n", blkn, physical_blkn, diskn);
        write_block(diskn, physical_blkn, data);

    }

    // Writing to mirror
    if (rcb->disk_health_status[diskn_mirror]){
//        printf("Logical block %d write: pblock - %d, diskn - %d\n", blkn, physical_blkn, diskn_mirror);
        write_block(diskn_mirror, physical_blkn, data);
    }

    return 0;
}

void
repair_raid_1(int diskn)
{
//    printf("RAID 1 repair of disk %d\n", diskn);
    if (rcb->disk_health_status[diskn])
        return; // Disk was not faulty - No repair needed
    rcb->disk_health_status[diskn] = 1;
    int mirror_breakpoint = rcb->diskn()/2, diskn_copy;
    diskn_copy = diskn <= mirror_breakpoint ? diskn + mirror_breakpoint : diskn - mirror_breakpoint;
//    printf("Disk to repair %d\n",diskn);
//    printf("Disk copy %d\n",diskn_copy);
    if (!rcb->disk_health_status[diskn_copy])
        return; // Cannot recover data, copy faulty
    for (int i=1; i<BLKN; i++){
        read_block(diskn_copy, i, rcb->block_buffer); // Read one block from valid copy to buffer
        write_block(diskn, i, rcb->block_buffer); // Write one block from buffer to repaired disk
    }

}

uint
blkn_raid_1()
{
    return ((DISKS) / 2) * (BLKN - 1); // Assuming all disks are healthy
}

uint
diskn_raid_1()
{
    return (DISKS/2) * 2;
}


// RAID 0 + 1
int
read_raid_01(int blkn, uchar* data)
{
    if (blkn >= rcb->blkn())
        return -2; // Logical block out of range

    int physical_blkn, diskn;
    physical_blkn = blkn/(DISKS/2) + 1; // To account for bookkeeping block 0
    diskn = blkn % (DISKS/2) + 1; // To account for swap disk

//    printf("Initial Disk %d\n", diskn);
    diskn = rcb->disk_health_status[diskn] ? diskn : diskn + DISKS/2;

//    printf("Logical block %d read: pblock - %d, diskn - %d\n", blkn, physical_blkn, diskn);

    if (!rcb->disk_health_status[diskn])
        return -3; // Disk not healthy
    read_block(diskn, physical_blkn, data);
    return 0;
}

int
write_raid_01(int blkn, uchar* data)
{
    if (blkn >= rcb->blkn())
        return -2; // Logical block out of range

    int physical_blkn, diskn, diskn_mirror;
    physical_blkn = blkn/(DISKS/2) + 1; // To account for bookkeeping block 0
    diskn = blkn % (DISKS/2) + 1; // To account for swap disk
    diskn_mirror = diskn + DISKS/2;

//    printf("Initial Disk %d\n", diskn);
//    printf("Mirror Disk %d\n", diskn_mirror);

    if (!rcb->disk_health_status[diskn] && !rcb->disk_health_status[diskn_mirror])
        return -3; // Disks not healthy

    // Writing to original
    if (rcb->disk_health_status[diskn]){
//        printf("Logical block %d write: pblock - %d, diskn - %d\n", blkn, physical_blkn, diskn);
        write_block(diskn, physical_blkn, data);

    }

    // Writing to mirror
    if (rcb->disk_health_status[diskn_mirror]){
//        printf("Logical block %d write: pblock - %d, diskn - %d\n", blkn, physical_blkn, diskn_mirror);
        write_block(diskn_mirror, physical_blkn, data);
    }

    return 0;
}

void
repair_raid_01(int diskn)
{
//    printf("RAID 0+1 repair of disk %d\n", diskn);
    if (rcb->disk_health_status[diskn])
        return; // Disk was not faulty - No repair needed
    rcb->disk_health_status[diskn] = 1;
    int mirror_breakpoint = rcb->diskn()/2, diskn_copy;
    diskn_copy = diskn <= mirror_breakpoint ? diskn + mirror_breakpoint : diskn - mirror_breakpoint;
//    printf("Disk to repair %d\n",diskn);
//    printf("Disk copy %d\n",diskn_copy);
    if (!rcb->disk_health_status[diskn_copy])
        return; // Cannot recover data, copy faulty
    for (int i=1; i<BLKN; i++){
        read_block(diskn_copy, i, rcb->block_buffer); // Read one block from valid copy to buffer
        write_block(diskn, i, rcb->block_buffer); // Write one block from buffer to repaired disk
    }

}

uint
blkn_raid_01()
{
    return ((DISKS) / 2) * (BLKN - 1); // Assuming all disks are healthy
}

uint
diskn_raid_01()
{
    return (DISKS/2) * 2;
}

// RAID 4 and 5 utility

void
xor_blocks(uchar* dst, uchar* src)
{
    // dst = dst ^ src
    for (int i=0; i<BSIZE; i++){
        dst[i] ^= src[i];
    }
}

int
update_parity_block(int physical_blkn, int diskn_parity, uchar* data)
{
    if (!rcb->disk_health_status[diskn_parity])
        return -4; // Parity disk faulty

    read_block(diskn_parity, physical_blkn, rcb->block_buffer);
    xor_blocks(rcb->block_buffer, data);
    write_block(diskn_parity, physical_blkn, rcb->block_buffer);
    return 0;
}

int
reconstruct_block(int physical_blkn, int diskn, uchar* data)
{
//    printf("Reconstructing Block %d on Disk %d\n",physical_blkn,diskn);
    clear_block(data); // Clear destination block buffer
    for (int disk = 1; disk <= DISKS; disk++){
        if (disk == diskn) // Skip faulty disk
            continue;
        if (!rcb->disk_health_status[disk])
            return -3; // More than one disk faulty
        read_block(disk, physical_blkn, rcb->block_buffer);
        xor_blocks(data, rcb->block_buffer);
    }
    return 0;
}



// TODO: RAID 4 - TEST
int
read_raid_4(int blkn, uchar* data)
{
    if (blkn >= rcb->blkn())
        return -2; // Logical block out of range

    int physical_blkn, diskn;
    physical_blkn = blkn/(DISKS - 1) + 1; // To account for bookkeeping block 0
    diskn = blkn % (DISKS - 1) + 1; // To account for swap disk

//    printf("Initial Disk %d\n", diskn);
//    printf("Logical block %d read: pblock - %d, diskn - %d\n", blkn, physical_blkn, diskn);

    if (!rcb->disk_health_status[diskn]){
        return reconstruct_block(physical_blkn, diskn, data); // Cannot read directly from disk - Try reconstructing using others
    }

    read_block(diskn, physical_blkn, data);
    return 0;
}

int
write_raid_4(int blkn, uchar* data)
{
    if (blkn >= rcb->blkn())
        return -2; // Logical block out of range

    int physical_blkn, diskn, diskn_parity;
    physical_blkn = blkn/(DISKS - 1) + 1; // To account for bookkeeping block 0
    diskn = blkn % (DISKS - 1) + 1; // To account for swap disk
    diskn_parity = DISKS;

//    printf("Initial Disk %d\n", diskn);
//    printf("Parity Disk %d\n", diskn_parity);

    // Writing to targeted disk
    if (rcb->disk_health_status[diskn]){
//        printf("Logical block %d write: pblock - %d, diskn - %d\n", blkn, physical_blkn, diskn);
        write_block(diskn, physical_blkn, data);
    }

    int status = update_parity_block(physical_blkn, diskn_parity, data);
    if (status < 0 && !rcb->disk_health_status[diskn])
        return -3; // Targeted disk and parity disk faulty

    return 0;
}

void
repair_raid_4(int diskn)
{
//    printf("Repair RAID 4 - Disk %d\n", diskn);
    for (int i=1; i < BLKN; i++){
        if (!reconstruct_block(i,diskn,rcb->repaired_block_buffer))
            write_block(diskn,i,rcb->repaired_block_buffer);
    }
    rcb->disk_health_status[diskn] = 1;
//    printf("Repair RAID 4 - Disk %d - DONE\n", diskn);
}

uint
blkn_raid_4()
{
    return ((DISKS) - 1) * (BLKN - 1); // Assuming all disks are healthy
}

uint
diskn_raid_4()
{
    return DISKS;
}


// TODO: RAID 5 - TEST

int
read_raid_5(int blkn, uchar* data)
{
    if (blkn >= rcb->blkn())
        return -2; // Logical block out of range

    int physical_blkn, diskn, diskn_parity;
    physical_blkn = blkn/(DISKS - 1) + 1; // To account for bookkeeping block 0
    diskn = blkn % (DISKS - 1) + 1; // To account for swap disk; // To account for swap disk
    diskn_parity = (physical_blkn - 1) % DISKS + 1;
    if (diskn >= diskn_parity)
        diskn++;

//    printf("Disk %d\n", diskn);
//    printf("Parity Disk %d\n", diskn_parity);
//    printf("Logical block %d read: pblock - %d, diskn - %d\n", blkn, physical_blkn, diskn);

    if (!rcb->disk_health_status[diskn]){
        return reconstruct_block(physical_blkn, diskn, data); // Cannot read directly from disk - Try reconstructing using others
    }

    read_block(diskn, physical_blkn, data);
    return 0;
}

int
write_raid_5(int blkn, uchar* data)
{
    if (blkn >= rcb->blkn())
        return -2; // Logical block out of range

    int physical_blkn, diskn, diskn_parity;
    physical_blkn = blkn/(DISKS - 1) + 1; // To account for bookkeeping block 0
    diskn = blkn % (DISKS - 1) + 1; // To account for swap disk; // To account for swap disk
    diskn_parity = (physical_blkn - 1) % DISKS + 1;
    if (diskn >= diskn_parity)
        diskn++;

//    printf("Initial Disk %d\n", diskn);
//    printf("Parity Disk %d\n", diskn_parity);

    // Writing to targeted disk
    if (rcb->disk_health_status[diskn]){
//        printf("Logical block %d write: pblock - %d, diskn - %d\n", blkn, physical_blkn, diskn);
        write_block(diskn, physical_blkn, data);
    }

    int status = update_parity_block(physical_blkn, diskn_parity, data);
    if (status < 0 && !rcb->disk_health_status[diskn])
        return -3; // Targeted disk and parity disk faulty

    return 0;
}

void
repair_raid_5(int diskn)
{
//    printf("Repair RAID 5 - Disk %d\n", diskn);
    for (int i=1; i < BLKN; i++){
        if (!reconstruct_block(i,diskn,rcb->repaired_block_buffer))
            write_block(diskn,i,rcb->repaired_block_buffer);
    }
    rcb->disk_health_status[diskn] = 1;
//    printf("Repair RAID 5 - Disk %d - DONE\n", diskn);
}

uint
blkn_raid_5()
{
    return ((DISKS) - 1) * (BLKN - 1); // Assuming all disks are healthy
}

uint
diskn_raid_5()
{
    return DISKS;
}
