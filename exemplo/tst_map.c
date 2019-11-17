#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/t2fs.h"
#include "../include/apidisk.h"
#include "../include/bitmap2.h"
#include "../include/t2disk.h"

#define SUCCESS 0

int main(int argc, char *argv[]) {

  printf("Hello World\n");

  BYTE* mbr_sector = (BYTE*) malloc(SECTOR_SIZE);
  read_sector(0, mbr_sector);
	int j;
  for(j=0; j<SECTOR_SIZE; j++){
    printf("%c",mbr_sector[j]);
  }
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

  mount(3);
  BOLA_DA_VEZ* mounted = (BOLA_DA_VEZ*)malloc(sizeof(BOLA_DA_VEZ));
  mounted = get_mounted();
  T_SUPERBLOCK* sb = mounted->superblock;
  printf("Id: %s\n",sb->id);
  printf("Version: %d\n",sb->version);
  printf("Superblock size(1 block, first in partition): %d\n",sb->superblockSize);
  printf("Free Blocks Bitmap Size(in blocks): %d\n",sb->freeBlocksBitmapSize);
  printf("Free Inodes Bitmap Size(in blocks): %d\n",sb->freeInodeBitmapSize);
  printf("Inode area size (in blocks): %d\n",sb->inodeAreaSize);
  printf("Block size (in sectors): %d\n",sb->blockSize);
  printf("Disk size of partition (in blocks): %d\n",sb->diskSize);
  printf("Checksum: %d", sb->Checksum);

  opendir2();
  T_RECORD* rec = (T_RECORD*)malloc(sizeof(T_RECORD));
  int i=0;

  while( i < mounted->root->max_entries){
    map_index_to_record(i, rec);
    print_RECORD(rec);

  }



  return 0;
}
