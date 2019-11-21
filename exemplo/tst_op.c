#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/t2fs.h"
#include "../include/apidisk.h"
#include "../include/bitmap2.h"
#include "../include/t2disk.h"

#define SUCCESS 0

int main() {

  BYTE* mbr_sector = (BYTE*) malloc(SECTOR_SIZE);
  read_sector(0, mbr_sector);
	int j;
  for(j=0; j<SECTOR_SIZE; j++){
    printf("%x ",mbr_sector[j]);
  }
  printf("\n-------------------\n");



  MBR* mbr = (MBR*)malloc(sizeof(MBR));
  load_mbr(mbr_sector, mbr);
  printf("\nVersion: %d\n", mbr->version);
  printf("Sector size in bytes: %d\n", mbr->sector_size);
  printf("Initial Byte: %d\n", mbr->initial_byte);
  printf("# Partitions: %d\n", mbr->num_partitions);

  for (j=0; j < mbr->num_partitions; j++){
    printf("Initial sector: %d\n", mbr->disk_partitions[j].initial_sector);
    printf("Final sector: %d\n", mbr->disk_partitions[j].final_sector);
    printf("Partition name: %s\n", mbr->disk_partitions[j].partition_name);
  }

// mount(0);
// umount();
// report_superblock();


//   BOLA_DA_VEZ* mounted = (BOLA_DA_VEZ*)malloc(sizeof(BOLA_DA_VEZ));
//   mounted = get_mounted();
//   T_SUPERBLOCK* sb = mounted->superblock;
//   printf("---Id: %s\n",sb->id);
//   printf("Version: %d\n",sb->version);
//   printf("Superblock size(1 block, first in partition): %d\n",sb->superblockSize);
//   printf("Free Blocks Bitmap Size(in blocks): %d\n",sb->freeBlocksBitmapSize);
//   printf("Free Inodes Bitmap Size(in blocks): %d\n",sb->freeInodeBitmapSize);
//   printf("Inode area size (in blocks): %d\n",sb->inodeAreaSize);
//   printf("Block size (in sectors): %d\n",sb->blockSize);
//   printf("Disk size of partition (in blocks): %d\n",sb->diskSize);
//   printf("Checksum: %u\n", sb->Checksum);

//   opendir2();
//   T_RECORD* rec = (T_RECORD*)malloc(sizeof(T_RECORD));
//   int i=14;
//   printf("Max number of entries in root: %d\n", mounted->root->max_entries);
//   while( i < 22){
//     printf("\n REGISTRO #%d\n", i);
//     map_index_to_record(i, rec);
//     print_RECORD(rec);
//     i++;
//   }

//   closedir2();

mount(0);

report_open_files();

create2("a.txt");


  return 0;
}
