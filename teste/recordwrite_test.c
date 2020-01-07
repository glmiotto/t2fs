
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <t2fs.h>
#include <t2disk.h>

extern T_MOUNTED* mounted;

int main() {
	if(format2(0, 1)) {
		printf("Error while formatting: NOT OK\n");
	}
	if (mount(0)) {
		printf("Error while mounting: NOT OK\n");
	}

	printf("Max entries: %d\n", mounted->root->max_entries);
	printf("Opening dir: %s\n", opendir2() ? "NOT OK" : "OK");
// 264 entries before double
// max entries 264 + 16384
	int dentries = 300;
	int offset = 0;

	T_RECORD rec[dentries];
	char names[dentries][51];

	int code = create2("original");

	int i;
	int err;
	for (i = 1; i < dentries; i++) {
		sprintf(names[i], "%d", i);
		memcpy(rec[i].name, &(names[i]),5);
		err = hln2(&(names[i]), "original");
		// rec[i].TypeVal = (i+1) % 256;
		// rec[i].TypeVal = rec[i].TypeVal == 0 ? 1 : rec[i].TypeVal;
		// err = new_record2(&rec[i]);
		// printf("Creating rec name %s - id %d: %s\n", rec[i].name, rec[i].TypeVal, err ? "NOT OK" : "OK");
		if(err < 0)
		printf("Creating rec name %s: return %s\n", rec[i].name, err ? "NOT OK" : "OK");

	}

	printf("Total entries - %d : %s\n", mounted->root->total_entries, mounted->root->total_entries == dentries ? "OK" : "NOT OK");

	printf("Closing dir: %s\n", closedir2() ? "NOT OK" : "OK");
	printf("LISTING DIRECTORY:\n");
	// Abre o diret�rio pedido
	int d;
	d = opendir2();
	if (d<0) {
			printf("Open dir NOT OK: %d\n", d);
	}
	// Coloca diretorio na tela
	DIRENT2 dentry;
	int ii=0;
	while ( readdir2(&dentry) == 0 ) {
			printf ("%4d|%c %8u %s\n", ii,(dentry.fileType==0x02?'d':'-'), dentry.fileSize, dentry.name);
			ii++;
	}
	closedir2();

	int entry_counter = dentries;

	printf("Opening dir: %s\n", opendir2() ? "NOT OK" : "OK");
	for (i = 1; i < dentries; i=i+2) {
		printf("Deleting record %d: %s\n", i+offset, delete_entry(rec[i].name) ? "NOT OK" : "OK");
		entry_counter--;
	}
	printf("LISTING DIRECTORY:\n");
	ii=0;
	while ( readdir2(&dentry) == 0 ) {
			printf ("%4d|%c %8u %s\n", ii,(dentry.fileType==0x02?'d':'-'), dentry.fileSize, dentry.name);
	 		ii++;
	}
	printf("Closing dir: %s\n", closedir2() ? "NOT OK" : "OK");
	printf("Total entries - %d : %s\n", mounted->root->total_entries, mounted->root->total_entries == entry_counter ? "OK" : "NOT OK");

//-----------------//
	printf("Opening dir: %s\n", opendir2() ? "NOT OK" : "OK");
	for (i = 2; i < dentries; i=i+2) {
		printf("Deleting record %d: %s\n", i+offset, delete_entry(rec[i].name) ? "NOT OK" : "OK");
		entry_counter--;
	}
	printf("LISTING DIRECTORY:\n");
	ii=0;
	while ( readdir2(&dentry) == 0 ) {
			printf ("%4d|%c %8u %s\n", ii,(dentry.fileType==0x02?'d':'-'), dentry.fileSize, dentry.name);
			ii++;
	}
	printf("Closing dir: %s\n", closedir2() ? "NOT OK" : "OK");
	printf("Total entries - %d : %s\n", mounted->root->total_entries, mounted->root->total_entries == entry_counter ? "OK" : "NOT OK");

	if(umount()) {
		printf("Error while unmounting 0: NOT OK\n");
	}
	return 0;
}
