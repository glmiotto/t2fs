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

  format2(0,2);
  mount(0);
  opendir2();

  T_RECORD* rec = (T_RECORD*)malloc(sizeof(T_RECORD));
  int i;
  //mounted->root->max_entries
  for (i=0; i < 30; i++){
    map_index_to_record(i, rec);
  }

  closedir2();

  return 0;
}
