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

  unsigned char b[2] = {0x0F, 0x01};

  FILE* disco = fopen("../t2fs_disk.dat", "rw");

  BYTE* mbr_sector = (BYTE*) malloc(SECTOR_SIZE);
  read_sector(0, mbr_sector);

  for(j=0; j<SECTOR_SIZE; j++){
    printf("%c",mbr_sector[j]);
  }



  return 0;
}
