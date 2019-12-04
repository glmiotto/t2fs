
#include <stdio.h>
#include <t2fs.h>

int main() {
	printf("Mounting invalid: %s\n", mount(3) ? "OK" : "NOT OK");
	
	int i;	
	for (i = 1; i < 32; i = i*2){

	if(format2(1, i)) {
		printf("Error while formatting 1 with %d spb: NOT OK\n", i);
	}
	if (mount(1)) {
		printf("Error while mounting 1: NOT OK\n");
	}
	
	if(umount()) {
		printf("Error while unmounting 1: NOT OK\n");
	}
}
	
	return 0;
}
