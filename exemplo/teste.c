#include "../include/t2fs.h"
#include "../include/cthread.h"

#include <stdio.h>
#include <stdlib.h>

#define SUCCESS 0




int main(int argc, char *argv[]) {


void print(char a){
  int i;
  for (i = 0; i < 8; i++) {
      printf("%d", !!((a << i) & 0x80));
  }
  printf("\n");
}

unsigned int to_int(unsigned char* bytes, int num_bytes) {
	// Bytes stored in little endian format
	unsigned int value = 0;
	for (int i = 0; i < num_bytes ; ++i){
    printf("Byte numero %d: \n",i);
    print(bytes[i]);
		value |= (unsigned int)(bytes[i] << 8*i) ;
	}

  return value;
}


int main(void) {
  printf("Hello World\n");

  unsigned char b[2] = {0x0F, 0x01};

  print(b[0]);
  print(b[1]);

  printf("Bytes:\n%u, %u\n", b[0], b[1]);
  unsigned int r = to_int(b, 2);

  printf("%u", r);


  return 0;
}

	return 0;
}
