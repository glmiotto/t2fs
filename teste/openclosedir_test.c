
#include <stdio.h>
#include <t2fs.h>

int main() {
	if(format2(1, 4)) {
		printf("Error while formatting 1: NOT OK\n");
	}
	if (mount(1)) {
		printf("Error while mounting 1: NOT OK\n");
		return -1;
	}
	
	printf("Opening dir: %s\n", opendir2() ? "NOT OK" : "OK");
	
	FILE2 fp1 = -1;
	FILE2 fp2 = -1;
	printf("Creating file: %s ", (fp1 = create2("target")) < 0 ? "NOT OK" : "OK");
	printf("- Handler %d\n", fp1);
	printf("Creating file: %s ", (fp2 = create2("target2")) < 0 ? "NOT OK" : "OK");
	printf("- Handler %d\n", fp2);
	
	printf("Closing file handler %d: %s\n", fp1, close2(fp1) ? "NOT OK" : "OK");
	
	printf("Failed open file %s\n", (fp1 = open2("nonexistent")) < 0 ? "OK" : "NOT OK");
	
	printf("Opening file \"target\": %s\n", (fp1 = open2("target")) < 0 ? "NOT OK" : "OK");
	printf("- Handler %d\n", fp1);

	printf("Closing file handler %d: %s\n", fp1, close2(fp1) ? "NOT OK" : "OK");
	printf("Closing file handler %d: %s\n", fp2, close2(fp2) ? "NOT OK" : "OK");
	
	printf("Creating symlink to \"target2\": %s\n", sln2("linkfile", "target2") ? "NOT OK" : "OK");

	printf("Creating hardlink to \"target2\": %s\n", hln2("hardfile", "target2") ? "NOT OK" : "OK");

	printf("Opening symlink \"linkfile\": %s\n", (fp1 = open2("linkfile")) < 0 ? "NOT OK" : "OK");
	printf("- Handler %d\n", fp1);

	printf("Closing dir: %s\n", closedir2() ? "NOT OK" : "OK");

	printf("Opening dir: %s\n", opendir2() ? "NOT OK" : "OK");
	

	printf("===========\n");
	DIRENT2 dirent;
	while(!readdir2(&dirent)) {
		if (dirent.fileType != 0x00) {
			printf("File name: %s\n", dirent.name);
			printf("File size: %d\n", dirent.fileSize);
			printf("File type: %s\n", dirent.fileType == 0x01 ? "regular" : "symlink");
		}
	}
	
	
	
	printf("Closing dir: %s\n", closedir2() ? "NOT OK" : "OK");
	
	if(umount()) {
		printf("Error while unmounting 1: NOT OK\n");
		return -1;
	}
	
	if (mount(1)) {
		printf("Error while mounting 2: NOT OK\n");
		return -1;
	}
	
	printf("Opening dir: %s\n", opendir2() ? "NOT OK" : "OK");
	printf("Opening file \"target\": %s\n", (fp1 = open2("target")) < 0 ? "NOT OK" : "OK");
	printf("Closing file handler %d: %s\n", fp1, close2(fp1) ? "NOT OK" : "OK");
	
	printf("Deleting file \"target2\": %s\n", delete2("target2") ? "NOT OK" : "OK");
	
	printf("Opening file \"hardlink\": %s\n", (fp1 = open2("hardfile")) < 0 ? "NOT OK" : "OK");
	
	printf("Closing file \"hardlink\": %s\n", close2(fp1) ? "NOT OK" : "OK");
	
	printf("Trying to open invalid softlink: %s\n", (fp1 = open2("softfile")) < 0 ? "OK" : "NOT OK");
	
	printf("Closing dir: %s\n", closedir2() ? "NOT OK" : "OK");

	printf("Opening dir: %s\n", opendir2() ? "NOT OK" : "OK");
	
	printf("===========\n");
	while(!readdir2(&dirent)) {
		if (dirent.fileType != 0x00) {
			printf("File name: %s\n", dirent.name);
			printf("File size: %d\n", dirent.fileSize);
			printf("File type: %s\n", dirent.fileType == 0x01 ? "regular" : "symlink");
		}
	}
	
	printf("Closing dir: %s\n", closedir2() ? "NOT OK" : "OK");
	
	if(umount()) {
		printf("Error while unmounting 2: NOT OK\n");
		return -1;
	}
	
	return 0;
}
