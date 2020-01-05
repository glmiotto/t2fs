/* ========================================================================= */
/* ========================================================================= */
#include "t2fs.h"
#include "../include/t2disk.h"
#include "../include/apidisk.h"
#include "../include/bitmap2.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
/* ========================================================================= */
// GLOBAL VARIABLES
MBR 					disk_mbr;
T_MOUNTED*		mounted;
boolean t2fs_initialized = false;
/* ========================================================================= */
// DEBUG
boolean debugging = false;
int failed(char* msg) {
	printf("%s\n", msg);
	return FAILED;}
void print(char* msg) {printf("%s\n", msg);}
void* null(char* msg) {printf("%s\n", msg);return (void*)NULL;}
/* ========================================================================= */
/* ========================================================================= */

DWORD to_int(BYTE* chars, int num_bytes) {
	// Bytes stored in little endian format
	// Least significant byte comes first on the lowest index, highest comes last
	DWORD value = 0;
	int i;
	for (i = 0; i < num_bytes ; ++i){
		value |= (DWORD)(chars[i] << 8*i) ;
	}
	return value;
}

BYTE* DWORD_to_BYTE(DWORD value, int num_bytes) {

	BYTE* chars = (BYTE*)malloc(num_bytes*sizeof(BYTE));
	strncpy((char*)chars, (char*)"\0", num_bytes );
	int i;
	for (i = 0; i < num_bytes; ++i) {
		chars[i] = (value >> (8*i))&0xFF;
	}
	return chars;
}
BYTE* WORD_to_BYTE(WORD value, int num_bytes) {

	BYTE* chars = (BYTE*)malloc(num_bytes*sizeof(BYTE));
	strncpy((char*)chars, (char*)"\0", num_bytes );
	int i;
	for (i = 0; i < num_bytes; ++i) {
		chars[i] = (value >> (8*i))&0xFF;
	}
	return chars;
}

boolean is_mounted(void){
	if (mounted == NULL)
		return false;
	else return true;
}

T_MOUNTED* get_mounted(void) {
	if(is_mounted())
		return mounted;
	else return NULL;
}

boolean is_root_loaded(){
	if(is_mounted()){
		if(mounted->root == NULL)
			return false;
		else return true;
	}
	return false;
}
boolean is_root_open(){
	if(is_mounted() && is_root_loaded()){
		if(!mounted->root->open)
			return false;
		else return true;
	}
	return false;
}

boolean is_valid_filename(char* filename){
	if(strlen(filename) > MAX_FILENAME_SIZE){
		return false;
	}
	int i;
	for(i=0; i < strlen(filename); i++){
		if( (filename[i] < 0x21) || (filename[i] > 0x7A) ){
			return false;
		}
	}
	return true;
}

BYTE* alloc_sector() {
	BYTE* buffer= (BYTE*)malloc(sizeof(BYTE) * SECTOR_SIZE);
	return buffer;
}
T_INODE* alloc_inode(DWORD quantity){
	T_INODE* inode = (T_INODE*)malloc(quantity*sizeof(T_INODE));
	return inode;
}
T_RECORD* alloc_record(DWORD quantity){
	T_RECORD* rec = (T_RECORD*) malloc(quantity * sizeof(T_RECORD));
	return rec;
}
DIRENT2* alloc_dentry(DWORD quantity){
	DIRENT2* dentry = (DIRENT2*)malloc(quantity*sizeof(DIRENT2));
	return dentry;
}

T_INODE* blank_inode(){
	T_INODE* inode = alloc_inode(1);
	inode->blocksFileSize = 0;
	inode->bytesFileSize 	= 0;
	inode->dataPtr[0] 		= INVALID;
	inode->dataPtr[1] 		= INVALID;
	inode->singleIndPtr 	= INVALID;
	inode->doubleIndPtr 	= INVALID;
	inode->RefCounter 		= 0;
	return inode;
}

T_RECORD* blank_record(){
	T_RECORD* rec = alloc_record(1);
	memset(rec->name, '\0', sizeof(rec->name));
	rec->TypeVal 		  = TYPEVAL_INVALIDO;
	rec->Nao_usado[0] = INVALID;
	rec->Nao_usado[1] = INVALID;
	rec->inodeNumber  = INVALID;
	return rec;
}

DIRENT2* blank_dentry(){
	DIRENT2* dentry = alloc_dentry(1);
	memset(dentry->name, '\0', sizeof(dentry->name));
	dentry->fileType = TYPEVAL_INVALIDO;
	dentry->fileSize = 0;
	return dentry;
}

int load_root(){
	if (init() != SUCCESS) return(failed("Uninitialized"));
	if (!is_mounted())  return(failed("No partition mounted."));
	if (is_root_loaded()) return SUCCESS;

	BYTE* buffer = alloc_sector();

	if(read_sector(mounted->fst_inode_sector,  buffer) != SUCCESS)
		return(failed("LoadRoot failed"));

	T_INODE* dir_node = (T_INODE*)malloc(sizeof(T_INODE));
	if(access_inode(ROOT_INODE, dir_node)!=SUCCESS){
		return(failed("LoadRoot: Failed to access directory node"));
	}

	mounted->root =  (T_DIRECTORY*)malloc(sizeof(T_DIRECTORY));
	T_DIRECTORY* rt = mounted->root;

	rt->open = false;
	rt->inode = dir_node ;
	rt->inode_index = ROOT_INODE;
	rt->entry_index = DEFAULT_ENTRY;
	rt->valid_entry_counter = 0;
	rt->total_entries = dir_node->bytesFileSize/sizeof(T_RECORD);

	// Maximum number of ENTRY BLOCKS the d-node can hold.
	rt->max_entries = 2  						// direct pointers to Entry Nodes
			+ mounted->pointers_per_block // 1 single indirect pointer to block of direct pointers
			+ mounted->pointers_per_block*mounted->pointers_per_block; // 1 double indirect
	// Multiply by number of entries in a data block
	// to get no. of ENTRIES per d-node.
	rt->max_entries *= mounted->entries_per_block;
	int i;
	for (i=0; i< MAX_FILES_OPEN; i++){
		rt->open_files[i].handle = i;
		rt->open_files[i].inode_index = INVALID;
		rt->open_files[i].current_pointer = 0;
		rt->open_files[i].inode = NULL;
		rt->open_files[i].record = NULL;
	}

	rt->num_open_files = 0;
	return SUCCESS;

}

int init(){
	if (t2fs_initialized) return SUCCESS;
	// Reads first disk sector with raw MBR data
	BYTE* master_sector = alloc_sector();
	if(read_sector(SECTOR_DISK_MBR, master_sector) != SUCCESS) {
		return failed("Failed to read MBR"); }
	// Read disk master boot record into application space
	if(load_mbr(master_sector, &disk_mbr) != SUCCESS) {
		return failed("F"); }
	mounted = NULL;
	t2fs_initialized = true;
	return SUCCESS;
}

void report_superblock(T_SUPERBLOCK sb, int partition){
	printf("===============================================\n");
	printf("Superblock info for partition %d\n", partition);
	printf("Id: %.*s\n",4,sb.id); // Only the first 4 chars (there is no /0)
	printf("Version: %d\n",sb.version);
	printf("Superblock size(1 block, first in partition): %d\n",sb.superblockSize);
	printf("Free Blocks Bitmap Size(in blocks): %d\n",sb.freeBlocksBitmapSize);
	printf("Free Inodes Bitmap Size(in blocks): %d\n",sb.freeInodeBitmapSize);
	printf("Inode area size (in blocks): %d\n",sb.inodeAreaSize);
	printf("Block size (in sectors): %d\n",sb.blockSize);
	printf("Disk size of partition (in blocks): %d\n",sb.diskSize);
	printf("Checksum: %u\n", sb.Checksum);
	printf("===============================================\n");
}

void report_open_files(){
	printf("===============================================\n");
	printf("Reporting open files: \n");
	int i;
	for(i=0; i< MAX_FILES_OPEN; i++){
		printf("Position: %d\n", i);
		printf("File inode: %p\n", mounted->root->open_files[i].inode);
		printf("File pointer: %d\n", mounted->root->open_files[i].current_pointer);
		printf("File handle: %d\n", mounted->root->open_files[i].handle);
		printf("---\n");
	}
	printf("===============================================\n");
}

int BYTE_to_SUPERBLOCK(BYTE* bytes, T_SUPERBLOCK* sb){
	memcpy(sb->id, bytes, 4);

	sb->version =	to_int(&(bytes[4]), 2);
	sb->superblockSize = to_int(&(bytes[6]), 2);
	sb->freeBlocksBitmapSize = to_int(&(bytes[8]), 2);
	sb->freeInodeBitmapSize = to_int(&(bytes[10]), 2);

	sb->inodeAreaSize = to_int(&(bytes[12]), 2);
	sb->blockSize = to_int(&(bytes[14]), 2);
	sb->diskSize = to_int(&(bytes[16]), 4);

	sb->Checksum = to_int(&(bytes[20]), 4);

	return SUCCESS;
}

int RECORD_to_DIRENTRY(T_RECORD* record, DIRENT2* dentry){
	if (record == NULL || dentry == NULL) return FAILED;

	memcpy(dentry->name, record->name, 51);
	dentry->fileType = (DWORD) record->TypeVal;
	T_INODE* inode = alloc_inode(1);
	if(access_inode(record->inodeNumber, inode) != SUCCESS) return FAILED;
	dentry->fileSize = inode->bytesFileSize;

	return SUCCESS;
}

void print_RECORD(T_RECORD* record){
	if(record==NULL) {
		printf("Record is nullptr.");
		return;}
	printf("===============================================\n");
	printf("File name: %s\n", record->name);
	printf("File type: %x\n", record->TypeVal);
	printf("inode index: %x\n", record->inodeNumber);
	printf("===============================================\n");
}

int teste_superblock(MBR* mbr, T_SUPERBLOCK* sb) {

	printf("Initializing...\n");
	init();
	printf("Initialized.\n");

	format2(0, 4);
	BYTE* buffer = alloc_sector();
	int j = 0;
	printf("Initial sector: %d\n", mbr->disk_partitions[j].initial_sector);
	printf("Final sector: %d\n", mbr->disk_partitions[j].final_sector);
	printf("Partition name: %s\n", mbr->disk_partitions[j].partition_name);
	printf("Reading sector from disk...\n");
	if(read_sector(mbr->disk_partitions[j].initial_sector, buffer) != SUCCESS)
			printf("Failed to read sector.\n");

	BYTE_to_SUPERBLOCK(buffer, sb);

	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Informa a identificação dos desenvolvedores do T2FS.
-----------------------------------------------------------------------------*/
int identify2 (char *name, int size) {
	char *nomes ="Artur Waquil Campana\t00287677\nGiovanna Lazzari Miotto\t00207758\nHenrique Chaves Pacheco\t00299902\n\0";
	if (size < strlen(nomes)) {
		printf("ERROR: identification requires size %zu or larger.\n", strlen(nomes));
		return FAILED;
	}
	else strncpy(name, nomes, size);
	return SUCCESS;
}

int initialize_superblock(T_SUPERBLOCK* sb, int partition, int sectors_per_block) {
	// Gets pointer to superblock for legibility

	int fst_sect = disk_mbr.disk_partitions[partition].initial_sector;
	int lst_sect  = disk_mbr.disk_partitions[partition].final_sector;
	int num_sectors = lst_sect - fst_sect + 1;

	memcpy((void*)sb->id, (void*)"T2FS", 4);
	sb->version = 0x7E32;
	sb->superblockSize = 1;
	sb->blockSize = (WORD)sectors_per_block;

	//Number of logical blocks in formatted disk.
	sb->diskSize = (WORD)ceil(num_sectors/(float)sectors_per_block);

	// 10% of partition blocks reserved to inodes (ROUND UP)
	sb->inodeAreaSize = (WORD)(ceil(0.10*sb->diskSize));

	// Total number of inodes is how many we fit into its area size.
	// inodeAreaSize in bytes divided by inode size in bytes.
	int total_inodes = sb->inodeAreaSize*sectors_per_block*disk_mbr.sector_size;
	total_inodes /= sizeof(T_INODE);
	printf("[FORMAT2] Total inodes: %d\n",total_inodes);
	printf("[FORMAT2] Inode Area Size (bytes) %d\n", sb->inodeAreaSize*sectors_per_block*disk_mbr.sector_size);

	// inode bitmap size: 1 bit per inode given "X" inodes per block
	float inode_bmap = (float)total_inodes;
	// 1 bit per inode, now converted to number of blocks rounding up.
	inode_bmap /= (float)(8*disk_mbr.sector_size*sectors_per_block);
	sb->freeInodeBitmapSize = (WORD) ceil(inode_bmap);

	printf("[FORMAT2] Inode bitmap size (blocks): %f\n", inode_bmap);
	printf("[FORMAT2] Inode bitmap size (blocks rounded): %d\n", sb->freeInodeBitmapSize);

	// Total number of data blocks is dependent on size of its own bitmap!
	float data_blocks = sb->diskSize - sb->inodeAreaSize - sb->superblockSize;
	data_blocks -= sb->freeInodeBitmapSize;

	// block bitmap size is dependent on how many blocks are left after the bitmap.
	// therefore it is equal to current surviving blocks div by (8*bytes per block)+1

	sb->freeBlocksBitmapSize = (WORD)
		ceil(data_blocks / (float)(1 + 8*disk_mbr.sector_size*sectors_per_block));

	sb->Checksum = calculate_checksum(*sb);

	BYTE sector[SECTOR_SIZE] = {0};
	int i;
	for (i = 0; i < lst_sect; i++) {
		if(write_sector(fst_sect+i, sector)) {
			//printf("Failed zeroing the partition\n");
			return -1;
		}
	}
	//report_superblock(*sb, partition);
	save_superblock(*sb, partition);
	return SUCCESS;
}

int save_superblock(T_SUPERBLOCK sb, int partition) {
	// Gets pointer to application-space superblock
	BYTE* output = alloc_sector();

	memcpy(output, sb.id, 4);
	strncpy((char*)&(output[4]), (char*)WORD_to_BYTE(sb.version, 2),2);
	strncpy((char*)&(output[6]), (char*)WORD_to_BYTE(sb.superblockSize, 2),2);
	strncpy((char*)&(output[8]), (char*)WORD_to_BYTE(sb.freeBlocksBitmapSize, 2),2);
	strncpy((char*)&(output[10]),(char*)WORD_to_BYTE(sb.freeInodeBitmapSize, 2),2);
	strncpy((char*)&(output[12]),(char*)WORD_to_BYTE(sb.inodeAreaSize, 2),2);
	strncpy((char*)&(output[14]),(char*)WORD_to_BYTE(sb.blockSize, 2),2);
	strncpy((char*)&(output[16]),(char*)DWORD_to_BYTE(sb.diskSize, 4),4);
	strncpy((char*)&(output[20]),(char*)DWORD_to_BYTE(sb.Checksum, 4),4);

	if (write_sector(disk_mbr.disk_partitions[partition].initial_sector, output) != SUCCESS) {
		return failed("Failed to write superblock to partition sector");
	}
	else return SUCCESS;
}

int load_superblock() {
	if (!is_mounted()) return(failed("Unmounted."));
	mounted->superblock = (T_SUPERBLOCK*) malloc(sizeof(T_SUPERBLOCK));
	BYTE* buffer = alloc_sector();

	if(read_sector(mounted->mbr_data->initial_sector, buffer)!= SUCCESS) return(failed("failed reading sb"));

	if(BYTE_to_SUPERBLOCK(buffer,mounted->superblock) !=SUCCESS) return FAILED;
	if (calculate_checksum(*mounted->superblock) != mounted->superblock->Checksum) {
		free(mounted->superblock);
		mounted->superblock = NULL;
		free(buffer);
		return FAILED;
	}
	//print("--Resulting structure:\n");
	report_superblock(*mounted->superblock, mounted->id);
	return SUCCESS;
}

int load_mbr(BYTE* master_sector, MBR* mbr) {
	// reads the logical master boot record sector into a special structure of type MBR
	mbr->version = to_int(&(master_sector[0]), 2);
	mbr->sector_size = to_int(&(master_sector[2]), 2);
	mbr->initial_byte = to_int(&(master_sector[4]), 2);
	mbr->num_partitions = to_int(&(master_sector[6]),2);
	mbr->disk_partitions = (PARTITION*)malloc(sizeof(PARTITION)*mbr->num_partitions);
	int i;
	for(i = 0; i < mbr->num_partitions; ++i){
		int j = 8 + i*sizeof(PARTITION); //32 bytes per partition in the boot record
		mbr->disk_partitions[i].initial_sector = to_int(&(master_sector[j]),4);
		mbr->disk_partitions[i].final_sector = to_int(&(master_sector[j+4]),4);
		strncpy((char*)mbr->disk_partitions[i].partition_name, (char*)&(master_sector[j+8]), 24);
	}
	return SUCCESS;
}

int calculate_checksum(T_SUPERBLOCK sb) {
	// A superblock's checksum is the 1-complement of a sum of 5 integers.
	// Each integer is 4 bytes unsigned little-endian.
	BYTE temp4[4];
	DWORD checksum = to_int((BYTE*)sb.id, 4);
	// Version + superblockSize (2 bytes each, both little endian)
	strncpy((char*)&(temp4[0]), (char*)WORD_to_BYTE(sb.version, 2), 2);
	strncpy((char*)&(temp4[2]), (char*)WORD_to_BYTE(sb.superblockSize,2), 2);
	checksum += to_int(temp4, 4);
	// freeBlocksBitmapSize + freeInodeBitmapSize (2B each)
	strncpy((char*)&(temp4[0]), (char*)WORD_to_BYTE(sb.freeBlocksBitmapSize, 2), 2);
	strncpy((char*)&(temp4[2]), (char*)WORD_to_BYTE(sb.freeInodeBitmapSize,2), 2);
	checksum += to_int(temp4, 4);
	// inodeAreaSize + blockSize (2B each)
	strncpy((char*)&(temp4[0]), (char*)WORD_to_BYTE(sb.inodeAreaSize, 2), 2);
	strncpy((char*)&(temp4[2]), (char*)WORD_to_BYTE(sb.blockSize,2), 2);
	checksum += to_int(temp4, 4);
	// diskSize (a DWORD of 4 Bytes)
	checksum += sb.diskSize;
	// One's complement
	return ~checksum;
}

int initialize_inode_area(T_SUPERBLOCK* sb, int partition){
	// first inode sector
	int first_sector = disk_mbr.disk_partitions[partition].initial_sector;
	first_sector += (sb->superblockSize+sb->freeInodeBitmapSize+sb->freeBlocksBitmapSize)*sb->blockSize;

	T_INODE* dummy = blank_inode();
	BYTE* blank_sector = (BYTE*)malloc(INODES_PER_SECTOR * sizeof(T_INODE));

	int i;
	for (i = 0 ; i < INODES_PER_SECTOR; i++){
		memcpy( &(blank_sector[i]), dummy, sizeof(T_INODE));
	}
	// Area de inodes em blocks * setores por block
	int total_inode_sectors = sb->inodeAreaSize * sb->blockSize;
	printf("total inode sectors: %d\n", total_inode_sectors);

	for (i=0; i < total_inode_sectors; i++){
		if( write_sector(first_sector+i, blank_sector) != SUCCESS){
			printf("init-fail write\n");
			free(blank_sector);
			return(failed("[INIT INODES] Failed to write inode sector in disk"));
		}
	}
	printf("init-success\n");
	free(blank_sector);
	return SUCCESS;
}

int initialize_bitmaps(T_SUPERBLOCK* sb, int partition, int sectors_per_block){

	// Allocates both Bitmaps for the currently mounted partition.
	// Output: success/error code
	// OpenBitmap receives the sector with the superblock.
	if(openBitmap2(disk_mbr.disk_partitions[partition].initial_sector) != SUCCESS){
		return failed("OpenBitmap: Failed to allocate bitmaps in disk");
	}

	// Set inode bits to FREE,
	// except for root (inode #0, currently no data blocks)
	if(setBitmap2(BITMAP_INODES, ROOT_INODE, BIT_OCCUPIED) != SUCCESS)
		return(failed("Init SetBitmaps fail 1."));

	int valid_nodes = sb->inodeAreaSize*sectors_per_block*disk_mbr.sector_size;
	valid_nodes /= sizeof(T_INODE);
	//int theoretical_nodes = sb->freeInodeBitmapSize*sb->blockSize*SECTOR_SIZE*8;

	int bit;
	//printf("Free inode bits range: %d-%d\n", ROOT_INODE+1, valid_nodes);
	for (bit = ROOT_INODE+1; bit < valid_nodes; bit++){
		// Each bit after root is set to FREE
		if(setBitmap2(BITMAP_INODES, bit, BIT_FREE) != SUCCESS) {
			//printf("bit ruim %d\n", bit);
			return(failed("Failed to set a bit as free in inode bitmap"));
		}
	}
	//
	// for (bit = valid_nodes; bit < theoretical_nodes; bit++){
	// 	// Each non-mappable bit is set to OCCUPIED forever
	// 	if(setBitmap2(BITMAP_INODES, bit, BIT_OCCUPIED) != SUCCESS) {
	// 		return(failed("Failed to set a theobit as occupied in inode bitmap"));
	// 	}
	// }

	// Data bitmap:
	// Since bitmap size is measured in blocks (rounding up),
	// we first initialize valid bits to FREE
	// and invalid ones to OCC (to forcibly limit access to non-existing disk area)

	float data_blocks = sb->diskSize - sb->inodeAreaSize - sb->superblockSize;
	data_blocks -= sb->freeInodeBitmapSize;
	int valid_blocks = data_blocks - sb->freeBlocksBitmapSize;

	//int theoretical_blocks = sb->freeBlocksBitmapSize*sb->blockSize*SECTOR_SIZE*8;
	int pre_data_blocks =
		sb->freeInodeBitmapSize + sb->freeBlocksBitmapSize + sb->inodeAreaSize+ sb->superblockSize;

	//printf("Occupied (reserved) data bits range: %d-%d\n", 0, pre_data_blocks);
	for (bit= 0; bit < pre_data_blocks; bit++) {
		// non addressable blocks are marked OCCUPIED forever
		if(setBitmap2(BITMAP_BLOCKS, bit, BIT_OCCUPIED) != SUCCESS) {
			//printf("bit ruim %d\n", bit);
			return(failed("Failed to set reserved bit as occupied in blocks bitmap"));
		}
	}
	int data_blocks_limit = pre_data_blocks+valid_blocks;
	//printf("Free data bits range: %d-%d\n", pre_data_blocks, data_blocks_limit);
	for (bit= pre_data_blocks; bit < data_blocks_limit; bit++) {
		// total VALID blocks (addressable by the API)
		if(setBitmap2(BITMAP_BLOCKS, bit, BIT_FREE) != SUCCESS) {
			//printf("bit ruim %d\n", bit);
			return(failed("Failed to set bit as free in blocks bitmap"));
		}
	}

	if(closeBitmap2() != SUCCESS) {
		printf("WARNING: Could not save bitmap info to disk.");
	}
	return SUCCESS;
}

DWORD map_inode_to_sector(int inode_index) {

	DWORD sector = mounted->fst_inode_sector;
	sector += floor(inode_index/INODES_PER_SECTOR);
	return sector;
}


int BYTE_to_INODE(BYTE* sector_buffer, int inode_index, T_INODE* inode) {

	// Reads a single inode from the sector it belongs,
	// as a pre-allocated inode structure of DWORD data for easy access.
	DWORD offset = (inode_index % INODES_PER_SECTOR)*INODE_SIZE_BYTES;
	sector_buffer += offset;
	memcpy((void*)&inode->blocksFileSize, (void*)&sector_buffer[0], 4);
	memcpy((void*)&inode->bytesFileSize, (void*)&sector_buffer[4], 4);
	memcpy((void*)&inode->dataPtr[0], (void*)&sector_buffer[8], 4);
	memcpy((void*)&inode->dataPtr[1], (void*)&sector_buffer[12], 4);
	memcpy((void*)&inode->singleIndPtr, (void*)&sector_buffer[16], 4);
	memcpy((void*)&inode->doubleIndPtr, (void*)&sector_buffer[20], 4);
	memcpy((void*)&inode->RefCounter, (void*)&sector_buffer[24], 4);
	memcpy((void*)&inode->reservado, (void*)&sector_buffer[28], 4);

	return SUCCESS;
}

int INODE_to_BYTE(T_INODE* inode, BYTE* bytes) {

	// Converts inode DWORDs to unsigned char (BYTE) then copies the data to a
	// BYTE array. Probably not very useful since it doesnt write to any sector
	// and another function will need to call this one for that but alas.
	strncpy((char*)&(bytes[0]),  (char*)DWORD_to_BYTE(inode->blocksFileSize, 4), 4);
	strncpy((char*)&(bytes[4]),  (char*)DWORD_to_BYTE(inode->bytesFileSize, 4), 4);
	strncpy((char*)&(bytes[8]),  (char*)DWORD_to_BYTE(inode->dataPtr[0], 4), 4);
	strncpy((char*)&(bytes[12]), (char*)DWORD_to_BYTE(inode->dataPtr[1], 4), 4);
	strncpy((char*)&(bytes[16]), (char*)DWORD_to_BYTE(inode->singleIndPtr, 4), 4);
	strncpy((char*)&(bytes[20]), (char*)DWORD_to_BYTE(inode->doubleIndPtr, 4), 4);
	strncpy((char*)&(bytes[24]), (char*)DWORD_to_BYTE(inode->RefCounter, 4), 4);
	strncpy((char*)&(bytes[28]), (char*)DWORD_to_BYTE(inode->reservado, 4), 4);

	return SUCCESS;
}

int save_inode(DWORD index, T_INODE* inode) {
  // Validation
	if(index < FIRST_VALID_BIT) return(failed("Invalid inode index"));
	if(inode == NULL) return(failed("Invalid inode pointer."));

	DWORD sector = map_inode_to_sector(index);
	BYTE* buffer = (BYTE*) alloc_inode(INODES_PER_SECTOR);

	if(read_sector(sector, buffer)!=SUCCESS)
		return(failed("SaveInode: Failed to read sector"));

	DWORD offset = (index % INODES_PER_SECTOR)*INODE_SIZE_BYTES;
	// printf("[SAVEINODE] Map inode to sector %d offset %d\n", sector, offset);
	memcpy(&(buffer[offset]), inode, INODE_SIZE_BYTES);

	if(write_sector(sector, buffer) != SUCCESS) {
		return(failed("SaveInode, WriteSector failed "));
	} else {
		if(set_bitmap_index(BITMAP_INODES, index, BIT_OCCUPIED) == SUCCESS)
			return SUCCESS;
		else return FAILED;
	}
}

int update_inode(DWORD index, T_INODE inode) {
  // Validation
	if(index < FIRST_VALID_BIT-1) return(failed("Invalid inode index"));

	DWORD sector = map_inode_to_sector(index);
	BYTE* buffer = (BYTE*) alloc_inode(INODES_PER_SECTOR);

	if(read_sector(sector, buffer)!=SUCCESS) {
		free(buffer);
		return(failed("SaveInode: Failed to read sector"));
	}

	DWORD offset = (index % INODES_PER_SECTOR)*INODE_SIZE_BYTES;
	memcpy((void*)&(buffer[offset]), (void*)&inode, INODE_SIZE_BYTES);

	if(write_sector(sector, buffer) != SUCCESS) {
		return(failed("SaveInode, WriteSector failed "));
	}

	return SUCCESS;
}

/*-----------------------------------------------------------------------------*/
// Next Bitmap Index:
// Output ZERO if none of that bit value found in bitmap.
// Output positive int index when found.
// Output negative (-1) if some operation failed.
int next_bitmap_index(int bitmap_handle, int bit_value){

	if(!is_mounted()) return failed("[NEXTBIT] Failed validation");
	if ((bitmap_handle != BITMAP_BLOCKS) && (bitmap_handle != BITMAP_INODES))
		return failed("[NEXTBIT] Invalid bitmap handle.");
	if((bit_value < 0) || (bit_value > 1) ) return failed("[NEXTBIT] Invalid bit value.");

	DWORD sb_sector = mounted->mbr_data->initial_sector;
	if(openBitmap2(sb_sector) != SUCCESS) return failed("[NEXTBIT] Failed OPEN");

	DWORD index = searchBitmap2(bitmap_handle, bit_value);
	//printf("[NextBitmap] h=%d, index: %d, value=%d\n" , bitmap_handle, index, bit_value);
	if(closeBitmap2() != SUCCESS) return failed("[NEXTBIT] Failed CLOSE");
	return index;
}

/*-----------------------------------------------------------------------------*/
int set_bitmap_index(int bitmap_handle, DWORD index, int bit_value){
	/* Validation */
	if(!is_mounted()) return failed("Failed validation");
	if ((bitmap_handle != BITMAP_BLOCKS) && (bitmap_handle != BITMAP_INODES))
		return failed("Invalid bitmap handle.");
	if((bit_value < 0) || (bit_value > 1) ) return failed("Invalid bit value.");
	if (index < FIRST_VALID_BIT)	return failed("Subvalid index");

	DWORD sb_sector = mounted->mbr_data->initial_sector;

	if(openBitmap2(sb_sector) != SUCCESS) return failed("[SETBIT] Failed OPEN");

	//printf("[SetBM] h=%d, index: %d, value=%d\n",bitmap_handle, index, bit_value);
	if(setBitmap2(bitmap_handle, (int)index, bit_value) != SUCCESS) return failed("[SETBIT] Failed SET.");

	if(closeBitmap2() != SUCCESS) return failed("[SETBIT] Failed CLOSE");
	return SUCCESS;
}

/*-----------------------------------------------------------------------------*/

int next_entry(int sequential_index, T_RECORD* out_record) {

	DWORD record_size = sizeof(T_RECORD);
	int records_per_block = mounted->superblock->blockSize*SECTOR_SIZE / record_size;
	int sequential_block, blockid, record_index_within_block;

	T_DIRECTORY* rt = mounted->root;
	T_INODE* dirnode = rt->inode;
	BYTE* buffer = (BYTE*)malloc(record_size);

	// =-=-=-=-=-=-=-=-= Get data block from inode =-=-=-=-=-=-=-=-= //

	sequential_block = sequential_index / records_per_block;
	blockid = get_data_block_index(dirnode, sequential_block);
	if (blockid < INVALID) return failed("[NEXT ENTRY] Failed to retrieve data block");
	if (blockid == INVALID) return NOT_FOUND;

	// =-=-=-=-=-=-=-=-= Map to record, read and return =-=-=-=-=-=-=-=-= //

	record_index_within_block = sequential_index % records_per_block;
	if(read_block(blockid, buffer, record_index_within_block*record_size,record_size) != SUCCESS){
		return failed("[NEXT ENTRY] Failed readblock.");}

	memcpy(out_record, buffer, record_size);
	return 1; // Success
}


int new_entry(T_RECORD* entry ){

	// =-=-=-=-=-=-=-=-= Validation + variables =-=-=-=-=-=-=-=-= //
	if (entry == NULL) return failed("[NEW ENTRY] Null record");

	DWORD record_size = sizeof(T_RECORD);
	int records_per_block = mounted->superblock->blockSize*SECTOR_SIZE / record_size;

	T_DIRECTORY* rt = mounted->root;
	T_INODE* dirnode = rt->inode;
	T_RECORD* buffer_record = alloc_record(1);
	BYTE* buffer = (BYTE*)malloc(record_size);

	int record_stored = 0;
	int sequential_index = 0;
	int sequential_block, blockid, record_index_within_block;

	// =-=-=-=-=-=-=-=-= Iterate all potential spots until vacancy =-=-=-=-=-=-=-=-= //

	while (sequential_index < rt->max_entries && !record_stored){

		sequential_block = sequential_index / records_per_block;
		blockid = get_data_block_index(dirnode, sequential_block);
		if (blockid < INVALID) return failed("[NEW ENTRY] Failed to retrieve data block");
		if (blockid == INVALID) {

			// =-=-=-=-=-=-=-=-= Allocate new data block for entries =-=-=-=-=-=-=-=-= //
			printf("[NEW ENTRY] Alloc new block for spot %d.\n", sequential_index);

			DWORD datablock = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
			if(datablock <= INVALID ) return failed("[NEW ENTRY] Could not allocate data block.");
			if(set_bitmap_index(BITMAP_BLOCKS, datablock, BIT_OCCUPIED) != SUCCESS) return failed("[NEW ENTRY] Failed set occupied.");
			if(insert_data_block_index(dirnode, 0, sequential_block, datablock) <= INVALID) return failed("[NEW ENTRY] Failed to insert data block in directory inode.");
			blockid = datablock;
		}
		if (blockid > INVALID) {

			// =-=-=-=-=-=-=-=-= Map to record spot + check availability =-=-=-=-=-=-=-=-= //

			record_index_within_block = sequential_index % records_per_block;
			if(read_block(blockid, buffer, record_index_within_block*record_size,record_size) != SUCCESS){
				return failed("[NEW ENTRY] Failed readblock.");
			}
			memcpy(buffer_record, buffer, record_size);
			//if( strcmp(buffer_record->name, blank->name)!=0){
// && access_inode(buffer_record->inodeNumber, buffer_inode) == SUCCESS) {
			if (buffer_record->TypeVal != 0x00) {

				// =-=-=-=-=-=-=-=-= Occupied spot: PASS =-=-=-=-=-=-=-=-= //
				printf("[NEW ENTRY] Spot %d already taken.\n", sequential_index);
				sequential_index++;
				continue;
			}

			else {
				// =-=-=-=-=-=-=-=-= Free spot: write record structure =-=-=-=-=-=-=-=-= //

				printf("[NEW ENTRY] Found free spot at %d.\n", sequential_index);
				memcpy(buffer, entry, record_size);
				if(write_block(blockid, buffer, record_index_within_block*record_size,record_size) != SUCCESS){
					return failed("[NEW ENTRY] Failed writeblock.");
				}

				// =-=-=-=-=-=-=-=-= Update directory inode =-=-=-=-=-=-=-=-= //

				rt->total_entries++;
				dirnode->bytesFileSize += record_size;
				if(update_inode(0, *dirnode) != SUCCESS) {print("[NEW ENTRY] Failed to update dirnode.");}
				record_stored = 1;
				printf("[NEW ENTRY] Success and serendipity at spot %d.\n", sequential_index);
				break;
			}
		}
		sequential_index++;
	}
	free(buffer);
	free(buffer_record);
	if(record_stored) return 1;
	printf("[NEW ENTRY] All record spots are occupied (current index: %d).\n", sequential_index);
	return NOT_FOUND;
}

int new_file(char* filename, T_INODE** inode){

	if(inode == NULL) return FAILED;
	if( !is_valid_filename(filename)) return(failed("[NEW FILE] Invalid Filename."));

	free(*inode);
	*inode = blank_inode();

	(*inode)->RefCounter = 1;
	DWORD inode_index = next_bitmap_index(BITMAP_INODES, BIT_FREE);
	// printf("[NEW_FILE] inode_index: %d\n", inode_index);

	if(inode_index == NOT_FOUND)
		return(failed("No inodes free"));
	else if(inode_index < FIRST_VALID_BIT)
		return(failed("Failed bitmap query."));

	if(save_inode(inode_index, *inode) != SUCCESS)
		return(failed("[NEW FILE] Failed to save inode."));

	// new file - record creation
	T_RECORD* rec = blank_record();
	rec->inodeNumber  = inode_index;
	rec->TypeVal 			= TYPEVAL_REGULAR;
	strncpy(rec->name, filename, strlen(filename));

	//adds record to root directory
	if(new_entry(rec) <= NOT_FOUND) return failed("[NEW FILE] Could not store entry.");
	//if(new_record2(rec) != SUCCESS) return(failed("NewFile: Failed to save record"));
	return SUCCESS;
}

int init_open_files(){
	int i;
	for(i=0; i < MAX_FILES_OPEN; i++){
		mounted->root->open_files[i].inode = NULL;
		mounted->root->open_files[i].current_pointer = 0;
		mounted->root->open_files[i].handle = i;
	}
	return SUCCESS;
}

int remove_pointer_from_bitmap(DWORD number, WORD handle){

	if (number == INVALID) return FAILED;

	T_SUPERBLOCK* sb = mounted->superblock;
	DWORD first_inode_block = sb->superblockSize + sb->freeBlocksBitmapSize + sb->freeInodeBitmapSize;
	DWORD first_data_block = first_inode_block + sb->inodeAreaSize;
	DWORD last_inode = sb->inodeAreaSize*sb->blockSize*INODES_PER_SECTOR -1;
	DWORD last_data_block = mounted->mbr_data->final_sector / sb->blockSize ;
	printf("[CLEAR] h=%d, block=%d - ", handle, number);

	if(handle == BITMAP_INODES){
		if(number >= 1 && number <= last_inode ){
			if(set_bitmap_index(handle, number, BIT_FREE) != SUCCESS){
				return failed("[RPBMap] Failed to set inode bit free");
			}
			else return SUCCESS;
		}
	}
	else if (handle == BITMAP_BLOCKS){
		if(number >= first_data_block && number <= last_data_block){
			if(set_bitmap_index(handle, number, BIT_FREE) != SUCCESS){
				return failed("[RPBMap] Failed to set block bit free");
			}
			else {
				if(wipe_block(number)!= SUCCESS)
					print("[RPBM] Failed block data wipe");

				printf("[CLEAR] block %d success.\n",number);
				return SUCCESS;
			}
		}
	}
	return FAILED;
}

int iterate_singlePtr(DWORD indirection_block){

	if(indirection_block == INVALID) return FAILED;

	// Bloco de indices contendo
	// ponteiros 4By para blocos de dados(representados no bitmap)
	// Um setor = 64 ponteiros
	BYTE* buffer = alloc_sector();
	int i, j;
	int sectors_per_block = mounted->superblock->blockSize;
	int pointers_per_sector = mounted->pointers_per_block/sectors_per_block;
	int sector_offset = mounted->mbr_data->initial_sector;
	sector_offset += (indirection_block*sectors_per_block);

	for(i=0; i < sectors_per_block; i++){
		if(read_sector(sector_offset + i, buffer) != SUCCESS) {
			return failed("Failed to read sector");
		}
		//iterate pointers in sector
		for(j=0; j < pointers_per_sector; j++){
			remove_pointer_from_bitmap(*((DWORD*)&buffer[j*DATA_PTR_SIZE_BYTES]), BITMAP_BLOCKS);
		}
	}
	remove_pointer_from_bitmap(indirection_block, BITMAP_BLOCKS);
	free(buffer);
	return SUCCESS;
}

int iterate_doublePtr(T_INODE* inode, DWORD double_indirection_block){

	if (double_indirection_block == INVALID) return FAILED;

	int sectors_per_block = mounted->superblock->blockSize;
	int pointers_per_sector = mounted->pointers_per_block/sectors_per_block;
	int sector_offset = mounted->mbr_data->initial_sector;
	sector_offset += (double_indirection_block*sectors_per_block);

	//realiza leitura de um setor do bloco de indices
	BYTE* sector = alloc_sector();
	//iterate through sectors in block1
	int i1, j1;
	for(i1=0; i1 < sectors_per_block; i1++){
		if(read_sector(sector_offset + i1, sector) != SUCCESS) {
			free(sector);
			return failed("[IterDouble] Failed to read sector");
		}
		//iterate through pointers in sector1
		for(j1=0; j1 < pointers_per_sector; j1++){
				// pointer to single indirection index-block.
				// apply to iterate_singlePtr to deal with deeper level pointers
				// and then itself.
			iterate_singlePtr(*((DWORD*)&sector[j1*DATA_PTR_SIZE_BYTES]));
		}
	}
	remove_pointer_from_bitmap(double_indirection_block, BITMAP_BLOCKS);
	free(sector);
	return SUCCESS;
}

int remove_file_content(T_INODE* inode){
	// percorre ponteiros diretos para blocos de dados
	printf("[REMOVE] direct blocks %d, %d\n", inode->dataPtr[0], inode->dataPtr[1]);
	printf("[REMOVE] single ind %d\n", inode->singleIndPtr);
	printf("[REMOVE] double ind %d\n", inode->doubleIndPtr);

	printf("=-=-=-=-= [REMOVE] Clearing first direct data block =-=-=-=-= \n");
	remove_pointer_from_bitmap(inode->dataPtr[0], BITMAP_BLOCKS);
	printf("=-=-=-=-= [REMOVE] Clearing second direct data block =-=-=-=-= \n");
	remove_pointer_from_bitmap(inode->dataPtr[1], BITMAP_BLOCKS);

	// percorre ponteiros indiretos de indirecao simples
	printf("=-=-=-=-= [REMOVE] Clearing single indirection block =-=-=-=-=\n");
	iterate_singlePtr(inode->singleIndPtr);
	printf("=-=-=-=-= [REMOVE] Clearing double indirection block =-=-=-=-=\n");
	// percorre ponteiros de indirecao dupla, traduz ponteiro para posicao no bitmap de dados, zera posicoes no bitmap de dados
	iterate_doublePtr(inode, inode->doubleIndPtr);
	printf("=-=-=-=-= [REMOVE:done] Removed all file content from disk =-=-=-=-=\n");
	return SUCCESS;
}

/*-----------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------
Função:	Formata logicamente uma partição do disco virtual t2fs_disk.dat para o sistema de
		arquivos T2FS definido usando blocos de dados de tamanho
		corresponde a um múltiplo de setores dados por sectors_per_block.
-----------------------------------------------------------------------------*/
int format2(int partition, int sectors_per_block) {

	if(init() != SUCCESS) return failed("Failed to initialize.");

	// Initialize the partition starting area.
	T_SUPERBLOCK sb;
	if(initialize_superblock(&sb, partition, sectors_per_block) != SUCCESS)
		return failed("Failed to read superblock.");

	if(initialize_inode_area(&sb, partition) != SUCCESS)
		return(failed("Format2: Failed to initialize inode area"));

	if(initialize_bitmaps(&sb, partition, sectors_per_block) != SUCCESS)
		return(failed("Format2: Failed to initialize bitmap area"));


	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Monta a partição indicada por "partition" no diretório raiz
-----------------------------------------------------------------------------*/
int mount(int partition) {
	init();
	if (mounted != NULL && mounted->id == partition){
		return failed("Partition already mounted.");}

	if(mounted != NULL && mounted->id != partition){
		return failed("Unmount current partition before mounting another.");}

	if ((partition < 0) || (partition >= disk_mbr.num_partitions))
		return failed("Partition invalid.");

	mounted = (T_MOUNTED*)malloc(sizeof(T_MOUNTED));
	mounted->id = partition;
	mounted->mbr_data = &(disk_mbr.disk_partitions[partition]);
	if (load_superblock()) {
		//printf("Not a valid partition\n");
		free(mounted);
		mounted = NULL;
		return FAILED;
	}

	mounted->root = NULL;

	// Calculate initial sectors.
	T_SUPERBLOCK* sb = mounted->superblock;
	DWORD inode_s0 = mounted->mbr_data->initial_sector;
	inode_s0 += sb->superblockSize * sb->blockSize;
	inode_s0 += sb->freeInodeBitmapSize * sb->blockSize;
	inode_s0 += sb->freeBlocksBitmapSize * sb->blockSize;

	mounted->fst_inode_sector = inode_s0;
	mounted->fst_data_sector = inode_s0 + sb->inodeAreaSize * sb->blockSize;
	// TODO: REVISAR

	mounted->pointers_per_block =
			mounted->superblock->blockSize * SECTOR_SIZE / DATA_PTR_SIZE_BYTES;
	mounted->entries_per_block =
			mounted->superblock->blockSize * SECTOR_SIZE / ENTRY_SIZE_BYTES;

	mounted->total_inodes = 0;
	mounted->max_inodes 	= sb->inodeAreaSize*sb->blockSize*SECTOR_SIZE/INODE_SIZE_BYTES;

	//opendir2();

	// Get mounted partition, load root directory, set entry to zero.
	if(!is_root_loaded())
		load_root();

	//  Initialize open files
	if(init_open_files() != SUCCESS) {
		return failed("Failed to initialize open files");	}

	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Desmonta a partição atualmente montada, liberando o ponto de montagem.
-----------------------------------------------------------------------------*/
int umount(void) {

	if(mounted == NULL){ return failed("No partition to unmount.");}
	if(closedir2() != SUCCESS){ return failed("Unmount failed: could not close root dir.");}

	mounted->id = -1;
	if (mounted->root != NULL) free(mounted->root);
	mounted->root = NULL;
	free(mounted->superblock);
	mounted->superblock = NULL;
	free(mounted);
	mounted = NULL;

	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para criar um novo arquivo no disco e abrí-lo,
		sendo, nesse último aspecto, equivalente a função open2.
		No entanto, diferentemente da open2, se filename referenciar um
		arquivo já existente, o mesmo terá seu conteúdo removido e
		assumirá um tamanho de zero bytes.
-----------------------------------------------------------------------------*/
FILE2 create2 (char *filename) {
	// Validation
	if(init() != SUCCESS) return failed("Failed INIT");
	if(!is_mounted()) return failed("No partition mounted.");
	if(opendir2() != SUCCESS) return failed("Directory must be opened.");
	if(!is_valid_filename(filename)) return failed("Invalid filename");

	DWORD inode_index = next_bitmap_index(BITMAP_INODES, BIT_FREE);
	if(inode_index == NOT_FOUND)
		return failed("[CREATE] Failed: inode space full.");
	else if(inode_index < FIRST_VALID_BIT)
		return failed("[CREATE] Failed bitmap query.");

	T_RECORD* record = alloc_record(1);
	T_INODE* inode = alloc_inode(1);

	if(find_entry(filename, &record) == SUCCESS){
		// FILENAME ALREADY EXISTS
		if(record->TypeVal == TYPEVAL_LINK) {
			printf("Filename exists and belongs to soft link. Cannot overwrite.\n");
			return FAILED;
		}
		if(access_inode(record->inodeNumber, inode) != SUCCESS) return FAILED;
		if(inode->RefCounter > 1) {
			printf("Filename exists and has multiple (hard) links. Cannot overwrite.\n");
			return FAILED;
		}

		if(remove_file_content(inode) != SUCCESS){
			return failed("[CREATE] Failed to erase existing file content.");
		}
		free(inode);
		inode = blank_inode();
		inode->RefCounter=1;
		if(update_inode(record->inodeNumber, *inode) != SUCCESS) {
			return failed("[CREATE] Failed to update inode after content erasure.");
		}
		printf("Filename exists. Contents erased.\n");
		return open2(filename);
	}

	if(new_file(filename, &inode) != SUCCESS)
		return failed("[CREATE] Failed to create new file");

	//printf("File created: %s\n", filename);
	FILE2 handle = open2(filename);
	return handle;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para remover (apagar) um arquivo do disco.
-----------------------------------------------------------------------------*/
int delete2 (char *filename) {
	if (init() != SUCCESS) return failed("Failed INIT");
	if(!is_mounted()) return failed("No partition mounted.");
	if(!is_root_loaded()) return failed("Failed directory load.");
	if(!is_valid_filename(filename)) return failed("[DEL]: Filename invalid");

	T_RECORD* record = alloc_record(1);
	if(find_entry(filename, &record) != SUCCESS){
		return failed("[DEL]: File does not exist.");
	}
	else {
		T_INODE* inode = alloc_inode(1);
		if( access_inode(record->inodeNumber, inode) != SUCCESS)
			return failed("[DEL] Failed access inode");

		int i;
		for (i = 0; i< MAX_FILES_OPEN; i++){
			if(mounted->root->open_files[i].record != NULL){
				if(strcmp(filename,mounted->root->open_files[i].record->name) == 0){
					printf("[DEL] Files that are opened cannot be deleted. Close and try again.\n");
					return FAILED;
				}
			}
		}

		if(record->TypeVal == TYPEVAL_REGULAR){
			delete_entry(filename);
			printf("[DEL] Regular refs before: %d\n",inode->RefCounter);
			inode->RefCounter--;
			printf("[DEL] Regular refs after: %d\n",inode->RefCounter);
			update_inode(record->inodeNumber, *inode);
		  if(inode->RefCounter == 0) {
			 printf("[DEL] Regular Removing file contents inode %d\n", record->inodeNumber);
			 remove_file_content(inode);
			 printf("[DEL] Regular Setting inode bitmap free.\n");
			 if(set_bitmap_index(BITMAP_INODES,record->inodeNumber, BIT_FREE)!=SUCCESS)
				 return failed("[DEL] Failed to set inode bitmap free");
			}
			return SUCCESS;
		}
		else {
			printf("[DEL] Link Removing file contents inode %d\n", record->inodeNumber);
			remove_file_content(inode);
			printf("[DEL] Link Setting inode bitmap free.\n");
			if(set_bitmap_index(BITMAP_INODES,record->inodeNumber, BIT_FREE)!=SUCCESS)
				return failed("[DEL] Failed to set inode bitmap free");
			delete_entry(filename);
			return SUCCESS;
		}
	}
}

/*-----------------------------------------------------------------------------
Função:	Função que abre um arquivo existente no disco.
-----------------------------------------------------------------------------*/
FILE2 open2 (char *filename) {
	if (init() != SUCCESS) return(failed("open2: failed to initialize"));
	if(!is_mounted()) return(failed("No partition mounted."));
	if(!is_root_loaded()) return(failed("Directory must be opened."));

	if(mounted->root->inode==NULL)
		return FAILED;

	T_RECORD* rec = alloc_record(1);
	if (find_entry(filename, &rec)) {
		//printf("Couldn't find a file with the given filename: %s\n", filename);
		return -1;
	}

	T_INODE* inode = alloc_inode(1);
	//printf("\ninopen %d\n", rec->inodeNumber);
	if(access_inode(rec->inodeNumber, inode)) {
		//printf("Error accessing the file's inode\n");
		return -1;
	}

	T_FOPEN* fopen = mounted->root->open_files;

	if (mounted->root->num_open_files >= MAX_FILES_OPEN){
		//printf("Maximum number of open files reached.");
		return FAILED;
	}

	char _filename[51] = {'\0'};

	int i;
	for(i=0; i < MAX_FILES_OPEN; i++){
		// check if position is not occupied by another file
		if(fopen[i].inode == NULL){
			//printf("Adicionei arquivo na posicao: %d\n", i);
			if(rec->TypeVal == TYPEVAL_LINK){

				BYTE* buffer = alloc_sector();
				if(read_block(inode->dataPtr[0], buffer, 0, 51)!= SUCCESS) return FAILED;
				memcpy((void*)_filename, (void*)buffer, 51);

				return open2(_filename);
				//if (find_entry(_filename, &rec) != SUCCESS) return FAILED;
				//if (access_inode(rec->inodeNumber, inode) != SUCCESS) return FAILED;
			}

			fopen[i].record = rec;
			memcpy((void*)(fopen[i].record->name), filename, 51);
			fopen[i].handle = i;
			fopen[i].inode 	= inode;
			fopen[i].current_pointer = 0;
			fopen[i].inode_index = rec->inodeNumber;

			mounted->root->num_open_files++;
			return i;
		}
	}
	return -1;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para fechar um arquivo.
-----------------------------------------------------------------------------*/
int close2 (FILE2 handle) {
	// Validation
	if (init() != SUCCESS) return failed("close2: failed to initialize");
	if(!is_mounted()) return failed("No partition mounted.");
	if(!is_root_loaded()) return failed("Directory must be opened.");

	if(handle >= MAX_FILES_OPEN || handle < 0){
		printf("[CLOSE] Handle out of bounds.");
		return FAILED;
	}
	T_FOPEN* fopen = mounted->root->open_files;

	if(fopen[handle].inode == NULL) {
		printf("[CLOSE] File handle does not correspond to an open file.");
		return -1;
	}
	else mounted->root->num_open_files--;

	// In either case, overwriting fopen data to make sure.
	fopen[handle].inode 		= NULL;
	fopen[handle].current_pointer = 0;
	fopen[handle].inode_index 	=	0;
	fopen[handle].record = NULL;
	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para realizar a leitura de uma certa quantidade
		de bytes (size) de um arquivo.
-----------------------------------------------------------------------------*/
int read2 (FILE2 handle, char *buffer, int size) {

	 //Validation
	 if (init() != SUCCESS) return failed("close2: failed to initialize");
	 if(!is_mounted()) return failed("No partition mounted.");
	 if(!is_root_loaded()) return failed("Directory must be opened.");
	 if(size <= 0) return failed("Invalid number of bytes.");

	 T_FOPEN* f = &(mounted->root->open_files[handle]);
	 if(f->inode == NULL) return FAILED;

	 DWORD bytes_per_block = mounted->superblock->blockSize * SECTOR_SIZE;
	 // Capacidade maxima do arquivo agora.
	 DWORD file_size_bytes = f->inode->bytesFileSize;

	 DWORD cur_block_number;
	 DWORD cur_block_index;
	 DWORD read_length;
	 DWORD cur_data_byte = 0;
	 DWORD byte_shift = f->current_pointer % bytes_per_block;
	 int total_read = 0;

	if (f->current_pointer + size > file_size_bytes){
		size = file_size_bytes - f->current_pointer;
	}
	if (size == 0) return 0;

	// printf("fcurr %d - fsizeb %d\n", f->current_pointer, file_size_bytes);
	 while (cur_data_byte < size && f->current_pointer < file_size_bytes) {

	 	cur_block_number = f->current_pointer / bytes_per_block;
	 	cur_block_index = get_data_block_index(f->inode, cur_block_number);
		// printf("=-=cur_block_index = %d\n", cur_block_index);
	 	if(cur_block_index <= 0) return FAILED;

	 	if ( (size - cur_data_byte) < (bytes_per_block - byte_shift))
	 		read_length = size - cur_data_byte;
	 	else read_length = bytes_per_block - byte_shift;

		if (f->current_pointer + read_length > file_size_bytes) read_length = file_size_bytes - f->current_pointer;

	 	read_block(cur_block_index, (BYTE*)&(buffer[cur_data_byte]), byte_shift, read_length);

	 	f->current_pointer += read_length;
		total_read += read_length;
	 	byte_shift = f->current_pointer % bytes_per_block;
	 	cur_data_byte += read_length;
	 }
	 //printf("String resultante lida: \n %s\n\n\n", buffer);
	 return total_read;
}

int write_block(DWORD block_index, BYTE* data_buffer, DWORD initial_byte, int data_size ){

	DWORD bytes_per_block = mounted->superblock->blockSize * SECTOR_SIZE;
	DWORD offset = mounted->mbr_data->initial_sector + block_index*mounted->superblock->blockSize;
	BYTE* sector_buffer = alloc_sector();
	DWORD starting_sector = offset + (initial_byte % bytes_per_block) / SECTOR_SIZE;
	DWORD starting_byte = (initial_byte % bytes_per_block) % SECTOR_SIZE;

	DWORD current_data_byte = 0;
	DWORD sector = starting_sector;
	DWORD sector_byte = starting_byte;
	DWORD bytes_to_copy;

	int max_sector = mounted->mbr_data->initial_sector + (mounted->mbr_data->final_sector - mounted->mbr_data->initial_sector);

	while(sector < max_sector && current_data_byte < data_size){
		if(read_sector(sector, sector_buffer)) {
			printf("[WRITEBLOCK] Failed to read sector.\n");
			return -1;
		}

		if( (SECTOR_SIZE-sector_byte) < (data_size - current_data_byte))
			bytes_to_copy = SECTOR_SIZE-sector_byte;
		else
			bytes_to_copy = data_size - current_data_byte;

		// if(data_size==4){
		// 	printf("[WRITE] Bytes to Copy %d, Buffer: %x|%x|%x|%x\n", bytes_to_copy,data_buffer[0],data_buffer[1],data_buffer[2],data_buffer[3]);
		// }

		memcpy(&(sector_buffer[sector_byte]), &(data_buffer[current_data_byte]), bytes_to_copy);
		current_data_byte += bytes_to_copy;
		sector_byte = 0;
		write_sector(sector, sector_buffer);
		sector++;
	}

	if(set_bitmap_index(BITMAP_BLOCKS, block_index, BIT_OCCUPIED) != SUCCESS){
		return failed("[WRITEBLOCK] Failed to set bitmap index as occupied");
	}
	return SUCCESS;
}

int wipe_block(DWORD block_index){
	int bytes_per_block = mounted->superblock->blockSize*SECTOR_SIZE;
	BYTE* blank_block = (BYTE*) malloc(bytes_per_block);
	int i;
	for (i=0;i<bytes_per_block;i++){
		blank_block[i]=0;
	}
	if(write_block(block_index, blank_block, 0, bytes_per_block) != SUCCESS){
		free(blank_block);
		set_bitmap_index(BITMAP_BLOCKS, block_index, BIT_FREE);
		return failed("[WIPEBLOCK] Failed to write blank block.");
	}
	else {
		set_bitmap_index(BITMAP_BLOCKS, block_index, BIT_FREE);
		free(blank_block);
		return SUCCESS;
	}
}

int read_block(DWORD block_index, BYTE* data_buffer, DWORD initial_byte, int data_size ){

	DWORD bytes_per_block = mounted->superblock->blockSize * SECTOR_SIZE;
	DWORD offset = mounted->mbr_data->initial_sector + block_index*mounted->superblock->blockSize;
	BYTE* sector_buffer = alloc_sector();
	DWORD starting_sector = offset + (initial_byte % bytes_per_block) / SECTOR_SIZE;
	DWORD starting_byte = (initial_byte % bytes_per_block) % SECTOR_SIZE;

	DWORD current_data_byte = 0;
	DWORD sector = starting_sector;
	DWORD sector_byte = starting_byte;
	DWORD bytes_to_copy;

	int max_sector = mounted->mbr_data->initial_sector + (mounted->mbr_data->final_sector - mounted->mbr_data->initial_sector);

	while(sector < max_sector && current_data_byte < data_size){
		// printf("[READBLOCK] Sector = %d\n", sector);
		if(read_sector(sector, sector_buffer)) {
		  printf("[READBLOCK] Failed to read a sector.\n");
			return -1;
		}

		if( (SECTOR_SIZE-sector_byte) < (data_size - current_data_byte))
			bytes_to_copy = SECTOR_SIZE-sector_byte;
		else
			bytes_to_copy = data_size - current_data_byte;

		memcpy(&(data_buffer[current_data_byte]), &(sector_buffer[sector_byte]), bytes_to_copy);
		current_data_byte += bytes_to_copy;
		sector_byte = 0;
		sector++;
	}
	return SUCCESS;
}

int insert_data_block_index(T_INODE* inode, DWORD inode_index, DWORD cur_block_number, DWORD block_index) {

	printf("[INSERT BLOCKID] Alloc %d\n", block_index);
 	if (block_index == INVALID) return failed("[INSERT BLOCKID] Invalid bit index");
	// printf("Indice achado para o bloco de dados %d\n", index);
	int limit_direct = 2;
	int limit_single_indirection = limit_direct + mounted->pointers_per_block;
	int limit_double_indirection = limit_single_indirection + mounted->pointers_per_block*mounted->pointers_per_block;
	int pointers_per_sector = mounted->pointers_per_block / mounted->superblock->blockSize;
	// Direct data blocks
 	if (cur_block_number < limit_direct){
 		inode->dataPtr[cur_block_number] = block_index;
		inode->blocksFileSize++;
 		if(update_inode(inode_index, *inode) != SUCCESS) return failed("fail");
 		return 1;
 	}


 	else if (cur_block_number < limit_single_indirection){
 		int indirection_index = inode->singleIndPtr;

 		if (indirection_index == INVALID) {
			// =-=-=-= Allocate single indirection block =-=-=-= //
 			DWORD indirection = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
			printf("[INSERT BLOCKID] Next indirection block bit: %d\n", indirection);
 			if(indirection < FIRST_VALID_BIT){ return failed("Write2: Failed to find enough free data blocks.");}
 			if(set_bitmap_index(BITMAP_BLOCKS, indirection, BIT_OCCUPIED) != SUCCESS){return failed("[INSERTDATABLOCK] Failed set bit occ");}

			// =-=-=-= Store indirection + update inode =-=-=-= //
			inode->singleIndPtr = indirection;
			if(update_inode(inode_index, *inode) != SUCCESS) return failed("[INSERTDATABLOCK] Failed update inode single indi");
 		}

		// =-=-=-= Store pointer to block in indirection block =-=-=-= //

 		indirection_index = inode->singleIndPtr;
 		int pointers_per_sector = mounted->pointers_per_block / mounted->superblock->blockSize;
 		DWORD sector_in_block = (cur_block_number-2)/pointers_per_sector;
 		DWORD shift_in_sector = (cur_block_number-2)%pointers_per_sector;

		if(write_block(indirection_index, (BYTE*)&block_index,
 			sector_in_block*SECTOR_SIZE+shift_in_sector*DATA_PTR_SIZE_BYTES, DATA_PTR_SIZE_BYTES) != SUCCESS)
				return failed("[INSERT BLOCKID] failed to write block inode indir");
 		else {
			inode->blocksFileSize++;
 			if(update_inode(inode_index, *inode) != SUCCESS) return failed("fail");
			return 1;
		}
 	}

 	else if (cur_block_number < limit_double_indirection){

		int double_indirection_block = inode->doubleIndPtr;

		if (double_indirection_block == INVALID) {

			// =-=-=-= Allocate double indirection block =-=-=-= //
			DWORD new_double_index = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
			printf("[INSERT BLOCKID] 0Next double indirection block bit: %d\n", new_double_index);
			if(new_double_index < FIRST_VALID_BIT){return failed("Write2: Failed to find enough free data blocks.");}
			if(set_bitmap_index(BITMAP_BLOCKS, new_double_index, BIT_OCCUPIED) != SUCCESS){ return failed("[INSERTDATABLOCK] Failed set bit occ");}

			// =-=-=-= Allocate single indirection block =-=-=-= //
			DWORD new_single_index = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
			printf("[INSERT BLOCKID] 0Next single indirection block bit: %d\n", new_single_index);
			if(new_single_index < FIRST_VALID_BIT){return failed("Write2: Failed to find enough free data blocks.");}
			if(set_bitmap_index(BITMAP_BLOCKS, new_single_index, BIT_OCCUPIED) != SUCCESS){ return failed("[INSERTDATABLOCK] Failed set bit occ");}

			// =-=-=-= Store pointer for single block in double block =-=-=-= //
			if(write_block(new_double_index, (BYTE*)&new_single_index, 0, DATA_PTR_SIZE_BYTES) != SUCCESS)
					return failed("[INSERT BLOCKID] 0failed to write block inode indir");

			// =-=-=-= Store indirection + update inode =-=-=-= //
			inode->doubleIndPtr = new_double_index;
			// printf("[IDB-Alloc]Alocou bloco de indices da double ind: %d\n", indirection);
			if(update_inode(inode_index, *inode) != SUCCESS) return failed("[INSERTDATABLOCK] Failed update inode double indi");
		}

		double_indirection_block = inode->doubleIndPtr;

		// =-=-=-= Read pointer to single indirection block =-=-=-= //
		BYTE* sector_buffer = alloc_sector();
		DWORD sector_offset = mounted->mbr_data->initial_sector + double_indirection_block * mounted->superblock->blockSize;
		DWORD pointer_index_in_double = (cur_block_number-limit_single_indirection)/mounted->pointers_per_block;
		DWORD sector_in_block = pointer_index_in_double / pointers_per_sector;
		if (read_sector(sector_offset + sector_in_block, sector_buffer) != SUCCESS) {return failed("[GetDBI] Failed to read sector 1");}
		DWORD single_indirection_block = *((DWORD*)(&sector_buffer[pointer_index_in_double*DATA_PTR_SIZE_BYTES]));
		// printf("cur_block_number: %d\n",cur_block_number);
		// printf("[] Ptr in sector: %d | Address in buffer %d\n", pointer_index_in_double, pointer_index_in_double*DATA_PTR_SIZE_BYTES);
		// printf("[] DOUBLE BLOCK sector buffer: \n");
		// int iii;
		// for (iii=0;iii<256;iii=iii+4) {
		// 	printf("%u ", *((DWORD*)&sector_buffer[iii]));
		// }
		// printf("\n");

		if (single_indirection_block == INVALID) {

			// =-=-=-= Allocate single indirection block =-=-=-= //
			DWORD new_single_index = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
			printf("[INSERT BLOCKID] 1Next single indirection block bit: %d\n", new_single_index);
			if(new_single_index < FIRST_VALID_BIT){return failed("Write2: Failed to find enough free data blocks.");}
			if(set_bitmap_index(BITMAP_BLOCKS, new_single_index, BIT_OCCUPIED) != SUCCESS){ return failed("[INSERTDATABLOCK] Failed set bit occ");}

			// =-=-=-= Store pointer for single block in double block =-=-=-= //
			if(write_block(double_indirection_block, (BYTE*)&new_single_index, sector_in_block*SECTOR_SIZE+pointer_index_in_double*DATA_PTR_SIZE_BYTES, DATA_PTR_SIZE_BYTES) != SUCCESS)
					return failed("[INSERT BLOCKID] 1failed to write block inode indir");

			single_indirection_block = new_single_index;
		}

		// =-=-=-= Store POINTER to DATA BLOCK in single indirection block =-=-=-= //

		DWORD pointer_index_in_single = (cur_block_number-limit_single_indirection) % mounted->pointers_per_block;
		sector_in_block = pointer_index_in_single / pointers_per_sector;
		printf("[DOUBLE] Bloco single: %d\n", single_indirection_block);
		printf("[DOUBLE] Ponteiro no bloco single: %d\n", pointer_index_in_single);
		printf("[DOUBLE] Setor no bloco single: %d\n", sector_in_block);
		printf("[DOUBLE] Block index: %d\n", block_index);

		// BYTE bytes[4];
		// int g;
		// DWORD value = index;
		// for(g=0;g<4;g++){
		// 	bytes[g] = value % 256;
		// 	value = value / 256;
		// }
		// //printf("hexa (index %u): %x %x %x %x\n", index, bytes[0],bytes[1], bytes[2], bytes[3]);

		if(write_block(single_indirection_block, (BYTE*)&block_index, pointer_index_in_single*DATA_PTR_SIZE_BYTES, DATA_PTR_SIZE_BYTES) != SUCCESS)
				return failed("[INSERT BLOCKID] failed to write block inode indir");

			// BYTE* buf = alloc_sector();
			// read_block(single_indirection_block, buf, 0, 256);
			// int jj;
			// for (jj=0;jj<256;jj=jj+4){
			// 	printf("[%d:%u] ",jj/4, *((DWORD*)&buf[jj]));
			// }
			// printf("\n");

		// =-=-=-= Block added to file and metadata stored. Update inode =-=-=-= //

		inode->blocksFileSize++;
		if(update_inode(inode_index, *inode) != SUCCESS) return failed("Failed.");
		return 1;
	}
	// Else, block sequential index out of bounds.
	return INVALID;
}

int get_data_block_index(T_INODE* inode, DWORD cur_block_number) {

	 int limit_direct = 2;
	 int limit_single_indirection = limit_direct + mounted->pointers_per_block;
	 int limit_double_indirection = limit_single_indirection + mounted->pointers_per_block*mounted->pointers_per_block;
	 int pointers_per_sector = mounted->pointers_per_block / mounted->superblock->blockSize;

	 // Direct data blocks
	 if (cur_block_number < limit_direct)
	 	return inode->dataPtr[cur_block_number];

	 // Single Indirection
	 else if (cur_block_number < limit_single_indirection){

	 	DWORD indirection_index = inode->singleIndPtr;
	 	if (indirection_index == INVALID) return INVALID;

	 	BYTE* sector_buffer = alloc_sector();
	 	DWORD sector_in_block = (cur_block_number-2)/pointers_per_sector;
	 	DWORD shift_in_sector = (cur_block_number-2)%pointers_per_sector;
	 	DWORD offset = mounted->mbr_data->initial_sector + indirection_index * mounted->superblock->blockSize;

	 	if (read_sector(offset + sector_in_block, sector_buffer) != SUCCESS) {return failed("[GetDBI] Failed read sector");}
		DWORD blockid = *((DWORD*)(&sector_buffer[shift_in_sector*DATA_PTR_SIZE_BYTES]));
		return blockid;
	 }
	 // Double indirection
	 else if (cur_block_number < limit_double_indirection){

		 DWORD double_indirection_index = inode->doubleIndPtr;
		 if (double_indirection_index == INVALID) return INVALID;

		 BYTE* sector_buffer = alloc_sector();
		 DWORD sector_offset = mounted->mbr_data->initial_sector + double_indirection_index * mounted->superblock->blockSize;
		 //each pointer in double connects to "pointers_per_block" datablocks
		 //each sector in double controls pointers_per_block*pointers_per_sector
		 DWORD sector_in_block = (cur_block_number-limit_single_indirection)/(mounted->pointers_per_block*pointers_per_sector);
		 DWORD pointer_index_in_double = (cur_block_number-limit_single_indirection)/mounted->pointers_per_block;
		 sector_in_block = pointer_index_in_double / pointers_per_sector;
		 if (read_sector(sector_offset + sector_in_block, sector_buffer) != SUCCESS) {return failed("[GetDBI] Failed to read sector 1");}

		 DWORD single_indirection_block = *((DWORD*)(&sector_buffer[pointer_index_in_double*DATA_PTR_SIZE_BYTES]));
		 if (single_indirection_block == INVALID) return INVALID;

		 // ponteiro no bloco:
		 DWORD pointer_index_in_single = (cur_block_number-limit_single_indirection) % mounted->pointers_per_block;
		 sector_in_block = pointer_index_in_single / pointers_per_sector;
		 sector_offset = mounted->mbr_data->initial_sector + single_indirection_block * mounted->superblock->blockSize;
		 if (read_sector(sector_offset + sector_in_block, sector_buffer) != SUCCESS) return failed("[GetDBI] Failed to read sector 2");

		 DWORD blockid = *((DWORD*)(&sector_buffer[(pointer_index_in_single%pointers_per_sector)*DATA_PTR_SIZE_BYTES]));

			 // int ii;
			 // printf("Sector buffer (single block)\n");
			 // for(ii=0;ii<256; ii=ii+4)
				// printf("%u ", *((DWORD*)&sector_buffer[ii]));
			 // printf("\n");

		 if (blockid == INVALID) {
			 printf("Double indirect block number : %d\n", double_indirection_index);
			 printf("Single indirect block number : %d\n", single_indirection_block);
			 printf("Cur block number:%d | sector in block %d | pointer in block %d | pointer in sector %d |\n",
		 cur_block_number, sector_in_block,pointer_index_in_single,(pointer_index_in_single%pointers_per_sector) );
			 printf("[GET BLOCKID] blockid value invalid\n");
			 return INVALID;
		 }
		 return blockid;
	 }
	 return INVALID;
}


int write2 (FILE2 handle, char *buffer, int size) {
 	//Validation
 	if (init() != SUCCESS) return failed("Failed INIT");
 	if(!is_mounted()) return failed("No partition mounted.");
 	if(!is_root_loaded()) return failed("Directory must be loaded.");
 	if(size <= 0) return failed("Invalid number of bytes.");

 	T_FOPEN* f = &(mounted->root->open_files[handle]);
 	if(f->inode == NULL) return FAILED;

 	DWORD bytes_per_block = mounted->superblock->blockSize * SECTOR_SIZE;
 	// Capacidade maxima do arquivo agora.
 	DWORD current_max_capacity = f->inode->blocksFileSize * bytes_per_block;

 	//printf("Current max capac at %d\n",(int)current_max_capacity );
 	//printf("Cur pointer at %d\n",(int)f->current_pointer);
 	//printf("size to write %d\n",(int)size );
 	DWORD cur_block_number;
 	DWORD cur_block_index;
 	DWORD write_length;
 	DWORD cur_data_byte = 0;
	int total_written =0;
 	DWORD byte_shift = f->current_pointer % bytes_per_block;
	// for(int i=0; i<size; i++){
	// 	printf("1buf[%d]=%c\n",i,buffer[i]);
	// }

 	if (f->current_pointer + size > current_max_capacity) {
 		// alloc more blocks + update inode
		float new_bytes = f->current_pointer + size - current_max_capacity;
		DWORD number_new_blocks = ceil(new_bytes/bytes_per_block);
		//printf("[WRITE2] NEW BYTES: %.2f NEW BLOCKS: %d\n",new_bytes,number_new_blocks );
 		int i;
 		//unsigned int* indexes = (unsigned int*)malloc(sizeof(unsigned int)*number_new_blocks);
 		int indice;

		int previous_block_file_size = f->inode->blocksFileSize;
		// printf("Total new blocks to be allocated = %d\n", number_new_blocks);
 		for(i=0; i< number_new_blocks; i++){
 			indice = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
			//printf("[Write2] Encontrou block bitmap index: %d\n", indice);
 			if(indice < FIRST_VALID_BIT){
 				// printf("Needs %u new data blocks, but partition only has %u free.\n",number_new_blocks,i);
 				return failed("[WRITE]: Failed to find enough free data blocks.");
 			}
 			else {
				//printf("[Write2] Ocupando block bitmap index: %d\n", indice);
 				if (set_bitmap_index(BITMAP_BLOCKS, indice, BIT_OCCUPIED) !=SUCCESS) return failed("[WRITE2]ALLOC-Failed set bit occ");

 				if(insert_data_block_index(f->inode, f->inode_index, previous_block_file_size + i, indice) <= 0)
					return failed("[WRITE] failed to insert new block in inode");
 			}
		}
		// printf("+++Blocks File size after all allocs: %d\n", f->inode->blocksFileSize);
 		current_max_capacity = f->inode->blocksFileSize * bytes_per_block;
 	}
	cur_data_byte = 0;

 	if (f->current_pointer + size <= current_max_capacity) {
 		// no need to allocate anything new.
 		while (cur_data_byte < size) {
			// printf("-0--cur_data_byte> %d\n", cur_data_byte);

 			cur_block_number = f->current_pointer / bytes_per_block;
			//printf("[WRITE] Get data block- current block number: %d\n", cur_block_number);
 			cur_block_index = get_data_block_index(f->inode, cur_block_number);

 			if(cur_block_index <= INVALID)
				return failed("[WRITE] GetDataBlock retornou indice invalido");

 			if ( (size - cur_data_byte) < (bytes_per_block - byte_shift))
 				write_length = size - cur_data_byte;
 			else write_length = bytes_per_block - byte_shift;

			// printf("-3--WRITING BLOCK- cur data byte %d, byte_shift: %d, write length %d\n", cur_data_byte,byte_shift,write_length);

			if(write_block(cur_block_index, (BYTE*)&(buffer[cur_data_byte]), byte_shift, write_length) != SUCCESS){
				return failed("[WRITE] Failed writeblock.");}

 			f->current_pointer += write_length;

 			byte_shift = f->current_pointer % bytes_per_block;
 			cur_data_byte += write_length;
			total_written += write_length;
 		}
		// printf("1CURRENT: %d\n", f->current_pointer);
		// printf("1FILESIZE: %d\n", f->inode->bytesFileSize);
		if(f->current_pointer >= f->inode->bytesFileSize) {
			f->inode->bytesFileSize += (f->current_pointer - f->inode->bytesFileSize);
			if(update_inode(f->inode_index, *(f->inode)) != SUCCESS) return failed("fail");
		}
 	}
	else {
		printf("WRITE failed: Insufficient space (even after alloc)\n");
		return -1;
	}
 	return total_written;
 }

// /*-----------------------------------------------------------------------------
// Função:	Função que abre um diretório existente no disco.
// -----------------------------------------------------------------------------*/
 int opendir2 (void) {
 	if (init() != SUCCESS) return failed("Failed INIT");
 	if(!is_mounted()) return failed("No partition mounted yet.");

 	mounted->root->open = true;
 	mounted->root->entry_index = 0;
	mounted->root->valid_entry_counter = 0;
 	return SUCCESS;
}

int BYTE_to_DIRENTRY(BYTE* data, DIRENT2* dentry){

	memcpy((char*)&(dentry->name), &(data[0]), (DIRENT_MAX_NAME_SIZE+1)*sizeof(char));
	dentry->fileType = data[DIRENT_MAX_NAME_SIZE+1];
	dentry->fileSize = to_int(&(data[DIRENT_MAX_NAME_SIZE+2]), sizeof(DWORD));
	return SUCCESS;
}

int DIRENTRY_to_BYTE(DIRENT2* dentry, BYTE* bytes){
	strncpy((char*)&(bytes[0]), (char*)&(dentry->name), (DIRENT_MAX_NAME_SIZE+1)*sizeof(char));
	bytes[DIRENT_MAX_NAME_SIZE+1] = dentry->fileType;
	strncpy((char*)&(bytes[DIRENT_MAX_NAME_SIZE+2]), (char*)DWORD_to_BYTE(dentry->fileSize, sizeof(DWORD)), sizeof(DWORD));
	return SUCCESS;
}

int access_inode(int inode_index, T_INODE* return_inode) {

	// Root directory is first inode in first inode-sector
	DWORD sector = mounted->fst_inode_sector + (inode_index/INODES_PER_SECTOR);
	BYTE* buffer = alloc_sector();

	if(read_sector(sector, buffer) != SUCCESS){
		free(buffer);
		return(failed("[ACCESS INODE]: Read sector failed."));
	}
	if(BYTE_to_INODE(buffer, inode_index , return_inode) != SUCCESS) {
		free(buffer);
		return(failed("Failed BYTE to INODE translation"));
	}
	// printf("[ACCESS_INODE] sector: %d - inode_idx: %d\n", sector, inode_index);
	free(buffer);
	return SUCCESS;
}

int find_entry_in_block(DWORD entry_block, char* filename, T_RECORD* record) {
	if(entry_block <= INVALID) return FAILED;
	T_SUPERBLOCK* sb = mounted->superblock;
	BYTE* buffer = alloc_sector();

	char* entry_name = (char*)malloc(sizeof(char)*(MAX_FILENAME_SIZE+1));

	int 	entry_size = sizeof(T_RECORD);
	int 	entries_per_sector = mounted->entries_per_block / sb->blockSize;
	int 	total_sects = sb->blockSize;
	DWORD 	offset = mounted->mbr_data->initial_sector+entry_block * sb->blockSize;
	int 	sector, e;
	// We were given a block, now read sector by sector.
	for (sector = 0; sector < total_sects; sector++){
		if(read_sector(offset + sector, buffer) != SUCCESS){
			//printf("FindEIB: failed to read sector %d of block %d", sector, entry_block);
			return(failed("[FIND ENTRY-B]: Read sector failed."));
		}
		// Buffer (sector of an entry block) now holds about 4 entries.
		for (e = 0; e < entries_per_sector; e++) {
			// EACH ENTRY has a byte for Type then 51 bytes for name.
			BYTE* buffer_name = &(buffer[e * entry_size + 1]);
			strncpy((char*) entry_name,(char*) buffer_name, (MAX_FILENAME_SIZE+1));

			if (strcmp(entry_name, filename) == 0){
				memcpy(record, &(buffer[e*entry_size]), sizeof(T_RECORD));
				//printf("ACHOU A ENTRADA\nFilename: %s\nInode number: %d\n", record->name, record->inodeNumber);
				return record->inodeNumber;
			}
		}
	}
	return NOT_FOUND;
}

int find_indirect_entry(DWORD index_block, char* filename, T_RECORD* record){

	if(index_block < 1) return FAILED;
	T_SUPERBLOCK* sb = mounted->superblock;

	int total_sects = sb->blockSize;
	int total_ptrs = SECTOR_SIZE / DATA_PTR_SIZE_BYTES;
	DWORD offset = mounted->mbr_data->initial_sector+index_block * sb->blockSize;
	BYTE* buffer = alloc_sector();
	DWORD entry_block;
	int i, s;
	for(s=0; s < total_sects; s++ ){
		if(read_sector(offset + s, buffer) != SUCCESS){
			//printf("Sweep: failed to read sector %d of block %d", s, index_block);
			return(failed("[FIND ENTRY-I]: Read sector failed."));
		}

		for(i=0; i<total_ptrs; i++){
			// Each pointer points to an ENTRY BLOCK.
			entry_block = to_int(&(buffer[i*DATA_PTR_SIZE_BYTES]), DATA_PTR_SIZE_BYTES);
			if(find_entry_in_block(entry_block, filename, record) > 0){
				//printf("ACHOU A ENTRADA INDIRETAMENTE\nFilename: %s\nInode number: %d\n", record->name, record->inodeNumber);
				return 1;
			}
		}
	}
	return 0;
}

// input: filename and empty record structure
// output: success code. if found, dentry holds the dir entry
int find_entry(char* filename, T_RECORD** record) {
	// Validation
	if(!is_mounted()) return FAILED;
	if(!is_root_loaded()) return FAILED;
	if(mounted->root->total_entries == 0) return FAILED;
	if(mounted->root->inode == NULL) return failed("bad inode ptr");
	if(!is_valid_filename(filename)) return failed("Filename invalid.");

	T_SUPERBLOCK* sb = mounted->superblock;
	T_DIRECTORY* 	rt = mounted->root;

	free(*record);
	*record = alloc_record(1);
	BYTE* buffer = alloc_sector();
	int total_sects = sb->blockSize;
	int total_ptrs = SECTOR_SIZE / DATA_PTR_SIZE_BYTES;

	DWORD entry_block, offset, index_block, inner_index_block;
	int i, s;
	// DIRECT POINTERS TO ENTRY BLOCKS
	for (i = 0; i < 2; i++) {
		entry_block = rt->inode->dataPtr[i];
		if(find_entry_in_block(entry_block, filename, *record) > NOT_FOUND){
			//printf("Found entry successfully");
			return SUCCESS;
		}
	}

	// INDIRECT POINTER TO INDEX BLOCKS
	index_block = rt->inode->singleIndPtr;
	if(index_block > INVALID) {
		// Valid index
		if(find_indirect_entry(index_block, filename, *record) > NOT_FOUND){
			//printf("Found entry successfully");
			return SUCCESS;
		}
	}

	// DOUBLE INDIRECT POINTER
	index_block = rt->inode->doubleIndPtr;
	offset = mounted->mbr_data->initial_sector + index_block*sb->blockSize;
	//printf("[FIND ENTRY] Looking for %s in double\n", filename);
	//printf("[FIND ENTRY] Bloco de indices double: %d\n", index_block);

	if(index_block > INVALID) {
		// Valid index
		for(s=0; s < total_sects; s++){
			if(read_sector(offset + s, buffer) != SUCCESS){
				//printf("Sweep: failed to read sector %d of block %d", s, index_block);
				return(failed("[FIND ENTRY]: failed to read a sector"));}

			for(i=0; i<total_ptrs; i++){
				// Each pointer --> a block of indexes to more blocks.
				inner_index_block = *((DWORD*)&buffer[i*DATA_PTR_SIZE_BYTES]);

				if(find_indirect_entry(inner_index_block, filename, *record) > NOT_FOUND){
					//printf("Found entry successfully");
					return SUCCESS;
				}
			}
		}
	}

	return FAILED;
}

// input: filename
// output: success code. if success, deleted entry corresponding to name
int delete_entry(char* filename) {
	// Validation
	if(!is_mounted()) return FAILED;
	if(!is_root_loaded()) return FAILED;
	if(mounted->root->total_entries == 0) return FAILED;
	if(mounted->root->inode == NULL) return failed("Null inode");
	if(!is_valid_filename(filename)) return failed("Filename invalid.");

	T_SUPERBLOCK* sb = mounted->superblock;
	T_DIRECTORY* 	rt = mounted->root;
	BYTE* buffer = alloc_sector();
	int total_sects = sb->blockSize;
	int total_ptrs = SECTOR_SIZE / DATA_PTR_SIZE_BYTES;

	DWORD entry_block, offset;
	int i, s;
	// DIRECT POINTERS TO ENTRY BLOCKS
	for (i = 0; i < 2; i++) {
		entry_block = rt->inode->dataPtr[i];
		if(delete_entry_in_block(entry_block, filename) > NOT_FOUND){
			//printf("Deleted entry successfully");
			return SUCCESS;
		}
	}

	// INDIRECT POINTER TO INDEX BLOCKS
	DWORD index_block = rt->inode->singleIndPtr;
	if(index_block > INVALID) {
		// Valid index
		if(delete_indirect_entry(index_block, filename) > NOT_FOUND){
			//printf("Deleted entry successfully");
			return SUCCESS;
		}
	}
	// DOUBLE INDIRECT POINTER
	index_block = rt->inode->doubleIndPtr;
	offset = mounted->mbr_data->initial_sector + index_block*sb->blockSize;
	DWORD inner_index_block;
	printf("[DEL ENTRY] Looking for %s in double\n", filename);
	printf("[DEL ENTRY] Bloco de indices double: %d\n", index_block);
	if(index_block > INVALID) {
		// Valid index
		for(s=0; s < total_sects; s++){
			if(read_sector(offset + s, buffer) != SUCCESS){
				//printf("Del: failed to read sector %d of block %d", s, index_block);
				return(failed("Del: failed to read a sector"));}
			for(i=0; i<total_ptrs; i++){
				// Each pointer --> a block of indexes to more blocks.
				inner_index_block = *((DWORD*)&buffer[i*DATA_PTR_SIZE_BYTES]);

				if(delete_indirect_entry(inner_index_block, filename) > NOT_FOUND){
					//printf("Deleted entry successfully");
					return SUCCESS;
				}
			}
		}
	}
	return FAILED;
}

int delete_indirect_entry(DWORD index_block, char* filename){

	if(index_block < 1) return FAILED;
	T_SUPERBLOCK* sb = mounted->superblock;

	printf("[DEL ENTRY] Bloco de indices single: %d\n", index_block);
	int i, s;
	int total_sects = sb->blockSize;
	DWORD offset = mounted->mbr_data->initial_sector+index_block * sb->blockSize;
	BYTE* buffer = alloc_sector();
	int total_ptrs = SECTOR_SIZE / DATA_PTR_SIZE_BYTES;
	DWORD entry_block;

	for(s=0; s < total_sects; s++ ){
		if(read_sector(offset + s, buffer) != SUCCESS){
			//printf("deleteentry: failed to read sector %d of block %d", s, index_block);
			return(failed("deleteentry: failed to read a sector"));
		}

		for(i=0; i<total_ptrs; i++){
			// Each pointer points to an ENTRY BLOCK.
			entry_block = *((DWORD*)&(buffer[i*DATA_PTR_SIZE_BYTES]));
			if(delete_entry_in_block(entry_block, filename) > 0){
				return 1;
			}
		}
	}
	return NOT_FOUND;
}

int delete_entry_in_block(DWORD entry_block, char* filename) {
	if(entry_block <= INVALID) return FAILED;
	T_SUPERBLOCK* sb = mounted->superblock;
	BYTE* buffer = alloc_sector();

	T_RECORD* record = alloc_record(1);
	T_RECORD* blank = blank_record();

	int 	entry_size = sizeof(T_RECORD);
	int 	entries_per_sector = mounted->entries_per_block / sb->blockSize;
	int 	total_sects = sb->blockSize;
	DWORD 	offset = mounted->mbr_data->initial_sector+entry_block * sb->blockSize;
	int 	sector, e;
	// We were given a block, now read sector by sector.
	for (sector = 0; sector < total_sects; sector++){
		if(read_sector(offset + sector, buffer) != SUCCESS){
			//printf("FindEIB: failed to read sector %d of block %d", sector, entry_block);
			return(failed("FindEIB: failed to read a sector"));
		}
		// Buffer (sector of an entry block) now holds about 4 entries.
		for (e = 0; e < entries_per_sector; e++) {
			memcpy(record, &(buffer[e*entry_size]), entry_size);

			if(strcmp(record->name, filename) == 0) {
				// achou
				memcpy(&(buffer[e*entry_size]), blank , entry_size);
				if(write_sector(offset + sector, buffer) != SUCCESS) return(failed("Failed a write sector"));
				mounted->root->inode->bytesFileSize -= entry_size;
				mounted->root->total_entries--;
				if(update_inode(0, *(mounted->root->inode)) != SUCCESS) print("[DEL ENTRY] Could not update dirnode.") ;
				return 1;
			}
		}
	}
	return NOT_FOUND;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para ler as entradas de um diretório.
-----------------------------------------------------------------------------*/
int readdir2 (DIRENT2 *dentry) {
	if (init() != SUCCESS) return(failed("ReadDir: failed to initialize"));
	if (!is_mounted()) return(failed("ReadDir failed: no partition mounted yet."));
	// if mounted, check if directory open. if not, open and read the first index.
	// otherwise read current entry IF VALID or the next valid one.
	if(!is_root_open()) { opendir2(); }

	T_RECORD* rec = alloc_record(1);
	T_INODE* inode = alloc_inode(1);
	T_DIRECTORY* rt = mounted->root;
	int return_code;

	rec->TypeVal = 0x00;
	dentry->fileType = 0x00;
	strcpy(dentry->name, "\0");
	dentry->fileSize = 0;

	while(rec->TypeVal == 0x00) {

		if (rt->valid_entry_counter >= rt->total_entries || rt->entry_index >= rt->max_entries) {
			//printf("1Valid entries so far: %d | Total valid: %d | Cur index: %d | Maximum: %d\n",
			//rt->valid_entry_counter,rt->total_entries, rt->entry_index, rt->max_entries);
			printf("Reached end of dir\n");
			free(rec);
			free(inode);
			return -1;
		}

		return_code = next_entry(rt->entry_index, rec);
		if(return_code < INVALID) return FAILED;
		if(return_code > INVALID) {
			if (rec->TypeVal != 0x00 && access_inode(rec->inodeNumber, inode)) {
				printf("[READ DIR] Corrupted entry: couldn't load inode\n");
				free(rec);
				free(inode);
				return -1;
			}
		}
		// if return INVALID, then mapped to unallocated block
		// but may have more entries in some other block or indirection level
		// (continue iterating).
		rt->entry_index++;
	}

	// Found a valid entry.
	rt->valid_entry_counter++;

	dentry->fileType = rec->TypeVal;
	dentry->fileSize = inode->bytesFileSize;
	strcpy(dentry->name, rec->name);

	free(rec);
	free(inode);
	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para fechar um diretório.
-----------------------------------------------------------------------------*/
int closedir2 (void) {
	if (init() != SUCCESS) return(failed("CloseDir: failed to initialize"));
	if (!is_mounted()) return(failed("CloseDir: no partition mounted."));
	update_inode(0, *(mounted->root->inode));
	mounted->root->open = false;
	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para criar um caminho alternativo (softlink)
-----------------------------------------------------------------------------*/
int sln2 (char *linkname, char *filename) {

	if (init() != SUCCESS) return failed("sln2: failed to initialize");
	if (!is_mounted()) return failed("No partition mounted.");
	if (!is_root_loaded()) return failed("Directory must be opened.");
	if (!is_valid_filename(filename)) return(failed("Filename not valid."));
	if (!is_valid_filename(linkname)) return(failed("Linkname not valid."));

	T_RECORD* record = alloc_record(1);
	// if file 'linkname' already exists
	if (find_entry(linkname, &record) == SUCCESS){
		//printf("File %s already exists.\n", linkname);
		return FAILED;
	}

	// if file 'filename' doesnt exist
	if (find_entry(filename, &record) != SUCCESS){
		//printf("File %s doesn't exist.\n", filename);
		return FAILED;
	}

	int indice_inode = next_bitmap_index(BITMAP_INODES, BIT_FREE);
	if (indice_inode == NOT_FOUND) return failed("Inode Bitmap full.");
	else if (indice_inode < FIRST_VALID_BIT) return failed("Inode Bitmap op1 failed.");
	if(set_bitmap_index(BITMAP_INODES, indice_inode, BIT_OCCUPIED) != SUCCESS)
		return failed("Failed to set bitmap");

	int indice_bloco = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
	if (indice_bloco == NOT_FOUND) return failed("Failed to read block.");
	else if (indice_bloco < FIRST_VALID_BIT) return failed("Block bitmap op1 failed.");
	if(set_bitmap_index(BITMAP_BLOCKS, indice_bloco, BIT_OCCUPIED) != SUCCESS)
		return failed("Failed to set blocks bitmap");

	int setor_inicial = indice_bloco * mounted->superblock->blockSize;
	int base_particao = mounted->mbr_data->initial_sector;

	// // TODO: apagar conteudo do bloco?
	// copia do nome do arquivo para o buffer
	BYTE* buffer = alloc_sector();
	// TODO: garantir que filename termina em \0 (strlen nao inclui \0)
	memcpy(buffer, filename, sizeof(char)*(strlen(filename)+1));

	// escreve o bloco no disco
	if (write_sector(base_particao + setor_inicial, buffer) != SUCCESS) {
		if(set_bitmap_index(BITMAP_INODES, indice_inode, BIT_FREE) != SUCCESS)
			return failed("Failed to set bitmap");
		if (set_bitmap_index(BITMAP_BLOCKS, indice_bloco, BIT_FREE))
			return failed("Failed to write sector + to unset bitmap");
		else
			return failed("Failed to write sector");
	}

	// inicializacao do inode
	T_INODE* inode = blank_inode();
	inode->blocksFileSize = 1;
	inode->bytesFileSize = sizeof(char)*(strlen(filename)+1); // +1 inclui \0
	inode->dataPtr[0] = indice_bloco;
	inode->dataPtr[1] = INVALID; // obs: a principio o blank_inode ja vem com isso assim
	inode->singleIndPtr = INVALID;
	inode->doubleIndPtr = INVALID;
	inode->RefCounter = 1;

	// inicializacao da entrada no dir
	T_RECORD* registro = blank_record();
	registro->TypeVal = TYPEVAL_LINK;
	if (strlen(linkname) > MAX_FILENAME_SIZE) return failed("Linkname is too big.");
	// 51 contando o /0 da string
	strncpy(registro->name, linkname, strlen(linkname)+1);
	registro->inodeNumber = indice_inode;


	if( save_inode(indice_inode, inode) != SUCCESS) {
		if(set_bitmap_index(BITMAP_INODES, indice_inode, BIT_FREE) != SUCCESS)
			return failed("Failed to savenode + UNset bitmap node");
		if (set_bitmap_index(BITMAP_BLOCKS, indice_bloco, BIT_FREE))
			return failed("Failed to save node+ unset bitmap block");
	}

	if(new_entry(registro) <= NOT_FOUND){

		if(set_bitmap_index(BITMAP_INODES, indice_inode, BIT_FREE) != SUCCESS)
			return failed("Failed to save rec + UNset bitmap node");
		if (set_bitmap_index(BITMAP_BLOCKS, indice_bloco, BIT_FREE))
			return failed("Failed to save rec + unset bitmap block");
		return FAILED;
	}

	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para criar um caminho alternativo (hardlink)
-----------------------------------------------------------------------------*/
int hln2(char *linkname, char *filename) {
	if (init() != SUCCESS) return failed("hln2: failed to initialize");
	if (!is_mounted()) return failed("No partition mounted.");
	if (!is_root_loaded()) return failed("Directory must be opened.");
	if (!is_valid_filename(filename)) return(failed("Filename not valid."));
	if (!is_valid_filename(linkname)) return(failed("Linkname not valid."));
	T_RECORD* record = alloc_record(1);
	// if file 'linkname' already exists
	if (find_entry(linkname, &record) == SUCCESS){
		//printf("File %s already exists.\n", linkname);
		return FAILED;
	}
	// if file 'filename' doesnt exist
	if (find_entry(filename, &record) != SUCCESS){
		//printf("File %s doesn't exist.\n", filename);
		return FAILED;
	}

	if( find_entry(filename, &record) != SUCCESS) return FAILED;
	int indice_inode = record->inodeNumber;
	T_INODE* inode = alloc_inode(1);
	// abre inode do arquivo 'filename'
	if(access_inode(indice_inode, inode) != SUCCESS) return FAILED;
	if(record->TypeVal != TYPEVAL_REGULAR){
		printf("Cannot establish hard link to symbolic file.\n");
		return FAILED;
	}
	// apenas incrementa o contador de referencias
	inode->RefCounter += 1;
	update_inode(indice_inode, *inode);

	// inicializacao da entrada no dir
	T_RECORD* registro = blank_record();
	registro->TypeVal = TYPEVAL_REGULAR;
	strncpy(registro->name, linkname, strlen(linkname)+1);
	registro->inodeNumber = indice_inode;

	if(new_entry(registro) <= NOT_FOUND) return FAILED;

	return SUCCESS;
}
