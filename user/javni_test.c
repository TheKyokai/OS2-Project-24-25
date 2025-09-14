#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void check_data(uint blocks, uchar *blk, uint block_size);

int
main(int argc, char *argv[])
{

    init_raid(RAID5);

    uint disk_num, block_num, block_size;
    info_raid(&block_num, &block_size, &disk_num);

    printf("Block num: %d\n", block_num);
    printf("Block size: %d\n", block_size);
    printf("Disk num: %d\n", disk_num);


    uint blocks = (512 > block_num ? block_num : 512);

//    disk_fail_raid(2);

    uchar* blk = malloc(block_size);
    int pid = fork();
    if (!pid){
        for (uint i = 0; i < blocks/2; i++) {
            for (uint j = 0; j < block_size; j++) {
                blk[j] = (j + i);
            }
            write_raid(i, blk);
        }
        printf("Child DONE\n");
        exit(0);
    } else {
        for (uint i = blocks/2; i < blocks; i++) {
            for (uint j = 0; j < block_size; j++) {
                blk[j] = (j + i);
            }
            write_raid(i, blk);
        }
        printf("Parent DONE\n");
        int status;
        wait(&status);
    }


//    for (uint i = 0; i < blocks; i++) {
//        for (uint j = 0; j < block_size; j++) {
//            blk[j] = (j + i);
//        }
//        write_raid(i, blk);
//    }

    printf("DATA WRITTEN TO DISKS\n");

    check_data(blocks, blk, block_size);


    disk_fail_raid(2);

    check_data(blocks, blk, block_size);

    disk_repaired_raid(2);

    check_data(blocks, blk, block_size);

    free(blk);




    exit(0);
}

void check_data(uint blocks, uchar *blk, uint block_size)
{
      for (uint i = 0; i < blocks; i++)
      {
        read_raid(i, blk);
        for (uint j = 0; j < block_size; j++)
        {
//          printf("\nexpected=%d got=%d", j + i, blk[j]);
          if ((uchar)(j + i) != blk[j])
          {
            printf("expected=%d got=%d", j + i, blk[j]);
            printf(" Data in the block %d faulty\n", i);
            break;
          }
        }
      }
}
