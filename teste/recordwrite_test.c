
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <t2fs.h>
#include <t2disk.h>

int new_record2(T_RECORD* rec);
MAP* blank_map();

extern T_MOUNTED* mounted;

int main() {
	if(format2(1, 4)) {
		printf("Error while formatting 1: NOT OK\n");
	}	
	
	if (mount(1)) {
		printf("Error while mounting 1: NOT OK\n");
	}
	
	printf("Opening dir: %s\n", opendir2() ? "NOT OK" : "OK");

	
	int dentries = 135;
	int offset = 0;
	
	T_RECORD rec[dentries];
	char names[dentries][51];
	MAP* dummymap = blank_map();
	
	int i;
	int err;
	for (i = 0; i < dentries; i++) {
		sprintf(names[i], "%d", i+1); 
		strcpy(rec[i].name, names[i]);
		rec[i].TypeVal = (i+1) % 256;
		rec[i].TypeVal = rec[i].TypeVal == 0 ? 1 : rec[i].TypeVal;
		
		err = new_record2(&rec[i]);
		
		printf("Creating rec name %s - id %d: %s\n", rec[i].name, rec[i].TypeVal, err ? "NOT OK" : "OK");
	}
	
	printf("Total entries - %d : %s\n", mounted->root->total_entries, mounted->root->total_entries == dentries ? "OK" : "NOT OK");
	
	T_RECORD* dummyrec = alloc_record(1);
	for (i = 0; i < dentries; i++) {
		printf("Looking for record %d: %s\n", i+offset, map_index_to_record(i+offset, &dummyrec, dummymap) <= 0 ? "NOT OK" : "OK");
		printf("\tComparing data\n\t\tName: \"%s\" - %s\n\t\tTypeVal: %d - %s\t\t\n", dummyrec->name, \
																				 strcmp(rec[i].name, dummyrec->name) == 0 ? "OK" : "NOT OK",\
																				 dummyrec->TypeVal,\
																				 rec[i].TypeVal == dummyrec->TypeVal ? "OK" : "NOT OK"); 
	}
	free(dummyrec);
	
	printf("Closing dir: %s\n", closedir2() ? "NOT OK" : "OK");

	printf("Opening dir: %s\n", opendir2() ? "NOT OK" : "OK");

	for (i = 0; i < dentries; i++) {
		printf("Deleting record %d: %s\n", i+offset, delete_entry(rec[i].name) ? "NOT OK" : "OK");
	}

	printf("Closing dir: %s\n", closedir2() ? "NOT OK" : "OK");


	if(umount()) {
		printf("Error while unmounting 1: NOT OK\n");
	}
	

	free(dummymap);
	
	return 0;
}
