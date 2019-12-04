
/**
*/
#include "t2fs.h"
#include "../include/t2disk.h"
#include "../include/apidisk.h"
#include "../include/bitmap2.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* **************************************************************** */
// GLOBAL VARIABLES
MBR 					disk_mbr;
T_MOUNTED*		mounted;

//T_FOPEN dir;

boolean t2fs_initialized = false;
boolean debugging = false;
//boolean writeMFD = true;
// Debugging
int failed(char* msg) {printf("%s\n", msg);return FAILED;}
void print(char* msg) {printf("%s\n", msg);}
void* null(char* msg) {printf("%s\n", msg);return (void*)NULL;}

/* **************************************************************** */

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

	rt->open = false; // TODO: nao acho que ja posso assumir isso
	rt->inode = dir_node ;
	rt->inode_index = ROOT_INODE;
	rt->entry_index = DEFAULT_ENTRY;
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
	printf("********************************\n");
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
}

void report_open_files(){
	printf("********************************\n");
	printf("Reporting open files: \n");
	int i;
	for(i=0; i< MAX_FILES_OPEN; i++){
		printf("Position: %d\n", i);
		printf("File inode: %p\n", mounted->root->open_files[i].inode);
		printf("File pointer: %d\n", mounted->root->open_files[i].current_pointer);
		printf("File handle: %d\n", mounted->root->open_files[i].handle);
		printf("\n");
	}
	printf("********************************\n");
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
	if(record==NULL) {print("Record is nullptr."); return;}
	printf("--------------\n");
	printf("File name: %s\n", record->name);
	printf("File type: %x\n", record->TypeVal);
	printf("inode index: %x\n", record->inodeNumber);

}

int teste_superblock(MBR* mbr, T_SUPERBLOCK* sb) {

	printf("Initializing...\n");
	init();
	printf("Initialized.\n");

	format2(0, 4);
	BYTE* buffer = alloc_sector();
	printf("Reading sector from disk...\n");
	int j = 0;
	printf("Initial sector: %d\n", mbr->disk_partitions[j].initial_sector);
	printf("Final sector: %d\n", mbr->disk_partitions[j].final_sector);
	printf("Partition name: %s\n", mbr->disk_partitions[j].partition_name);
	if(read_sector(mbr->disk_partitions[j].initial_sector, buffer) <0)
			printf("Nao leu sector certo\n");

	BYTE_to_SUPERBLOCK(buffer, sb);
	printf("BvS done\n");

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

	//strncpy(sb->id, "T2FS", 4);
	memcpy((void*)sb->id, (void*)"T2FS", 4);

	sb->version = 0x7E32;

	sb->superblockSize = 1;
	sb->blockSize = (WORD)sectors_per_block;

	//Number of logical blocks in formatted disk.
	sb->diskSize = (WORD)ceil(num_sectors/(float)sectors_per_block);

	// 10% of partition blocks reserved to inodes (ROUND UP)
	sb->inodeAreaSize = (WORD)(ceil(0.10*sb->diskSize));

	/* ************* BITMAPS ************* */

	// Total number of inodes is how many we fit into its area size.
	// inodeAreaSize in bytes divided by inode size in bytes.
	int total_inodes = sb->inodeAreaSize*sectors_per_block*disk_mbr.sector_size;
	total_inodes /= sizeof(T_INODE);

	// inode bitmap size: 1 bit per inode given "X" inodes per block
	float inode_bmap = (float)total_inodes;
	// 1 bit per inode, now converted to number of blocks rounding up.
	inode_bmap /= (float)(8*disk_mbr.sector_size*sectors_per_block);
	sb->freeInodeBitmapSize = (WORD) ceil(inode_bmap);

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

	report_superblock(*sb, partition);

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

int initialize_inode_area(T_SUPERBLOCK* sb){

	// TODO: init format blank inodes
	// Helpful pointer

	// first inode sector
	// mounted->fst_inode_sector;

	//T_INODE* dummy = blank_inode();

	// T_INODE* inodes = (T_INODE*)malloc(INODES_PER_SECTOR * sizeof(T_INODE));
	// for (int i = 0 ; i < INODES_PER_SECTOR; i++){
	// 	memcpy( &(inodes[i]), &dummy_inode, sizeof(T_INODE));
	// }
	// // Area de inodes em blks * setores por blk
	// for (int isector=0; isector < sb->inodeAreaSize * sb->blockSize; isector++){
	// 	if( write_sector(start_sector+isector, (unsigned char*)inodes) != SUCCESS){
	// 		free(inodes);
	// 		return(failed("Failed to write inode sector in disk"));
	// 	}
	// }
	// free(inodes);
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


	printf("Occupied (reserved) data bits range: %d-%d\n", 0, pre_data_blocks);
	for (bit= 0; bit < pre_data_blocks; bit++) {
		// non addressable blocks are marked OCCUPIED forever
		if(setBitmap2(BITMAP_BLOCKS, bit, BIT_OCCUPIED) != SUCCESS) {
			printf("bit ruim %d\n", bit);
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
		print("------> WARNING: Could not save bitmap info to disk.");
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
BYTE* get_block(int sector, int offset, int n){
	if(offset > SECTOR_SIZE)
		return NULL;

	BYTE* buffer = alloc_sector();
	if(!read_sector(sector, buffer))
		return NULL;

	BYTE* block = (BYTE*)malloc(n);
	memcpy( &block, &buffer, n);

	return block;
}

// Next Bitmap Index:
// Output ZERO if none of that bit value found in bitmap.
// Output positive int index when found.
// Output negative (-1) if some operation failed.
int next_bitmap_index(int bitmap_handle, int bit_value){
	/* Validation */
	if ((bitmap_handle != BITMAP_BLOCKS) && (bitmap_handle != BITMAP_INODES)){
		return(failed("Invalid bitmap handle."));}
	if((bit_value < 0) || (bit_value > 1) ){
		return(failed("Invalid bit value."));}
	if(!is_mounted()) return FAILED;

	DWORD sb_sector = mounted->mbr_data->initial_sector;
	/* Bitmap handling */
	if(openBitmap2(sb_sector) != SUCCESS){return FAILED;}

	DWORD index = searchBitmap2(bitmap_handle, bit_value);

	if(closeBitmap2() != SUCCESS) return FAILED;
	return index;
}

int set_bitmap_index(int bitmap_handle, DWORD index, int bit_value){
	/* Validation */
	if ((bitmap_handle != BITMAP_BLOCKS) && (bitmap_handle != BITMAP_INODES)){
		return(failed("Invalid bitmap handle."));}
	if((bit_value < 0) || (bit_value > 1) ){
		return(failed("Invalid bit value."));}
	if(!is_mounted()) return FAILED;
	if (index < FIRST_VALID_BIT)
		return(failed("Subvalid index"));

	DWORD sb_sector = mounted->mbr_data->initial_sector;
	/* Bitmap handling */
	if(openBitmap2(sb_sector) != SUCCESS){return FAILED;}

	if(setBitmap2(bitmap_handle, (int)index, bit_value) != SUCCESS)
	 	return(failed("Failed to set bitmap bit."));

	if(closeBitmap2() != SUCCESS) return FAILED;

	return SUCCESS;
}


MAP* blank_map() {
	MAP* map = (MAP*)malloc(sizeof(MAP));
	map->indirection_level = 0;
	map->block_key = 0;
	map->sector_key = 0;
  map->sector_shift = 0;
  map->data_block = 0;
  map->sector_address = 0;
  map->buffer_index = 0;
  map->single_pointer_to_block = 0;
  map->single_pointer_sector = 0;
  map->single_pointer_index = 0;
  map->single_sector_address = 0;
  map->single_buffer_index = 0;
  map->double_pointer_to_block = 0;
  map->double_pointer_sector = 0;
  map->double_pointer_index = 0;
  map->double_sector_address = 0;
  map->double_buffer_index = 0;

	return map;
}

int new_record2(T_RECORD* rec){
	if (init() != SUCCESS) return failed("Failed to initialize");
	if (!is_mounted()) return failed("No partition mounted.");
	if (!is_root_loaded()) return failed("Directory must be opened.");
	if (rec == NULL) return failed("Bad record");

	T_DIRECTORY* rt = mounted->root;
	T_INODE* dirnode = rt->inode;
	DWORD dentry_size = sizeof(T_RECORD);

	DWORD current_blocks = dirnode->blocksFileSize;

	int my_entry_index = 0;
	MAP* map = blank_map();
	T_RECORD* dummy = alloc_record(1);
	BYTE* buf =(BYTE*)malloc(sizeof(BYTE)*dentry_size);
	memcpy( buf, rec, dentry_size);

	boolean allocated = false;

	while (my_entry_index < rt->max_entries && allocated == false) {
		int ret = map_index_to_record(my_entry_index, &dummy, map );
		//printf("index %d dummytype %d\n", my_entry_index, dummy->TypeVal);
		//printf("return %d %s\n", ret, ret == NOT_FOUND ? "NOT FOUND" : "FOUND");
		//printf("map sectorshift %d map sectorkey %d map indir %d\n\n", map->sector_shift, map->sector_key, map->indirection_level);
		if (ret == NOT_FOUND) {

			if(map->indirection_level == 0) {
				if(map->data_block > NOT_FOUND){
					// TINHA BLOCO ALOCADO COM ESPACO LIVRE
					write_block(
						 map->data_block,
						 buf,
						 map->sector_key*SECTOR_SIZE+map->sector_shift*sizeof(T_RECORD),
						 dentry_size);

					dirnode->bytesFileSize += dentry_size;
					rt->total_entries++;

					if (update_inode(0, *dirnode)) {
						//printf("Couldn't save the directory inode\n");
						free(buf);
						return -1;
					}

					allocated = true;

				} else {
					// Precisa registrar o ponteiro pra bloco direto
					int new_data_block = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
					if(new_data_block == NOT_FOUND) return failed("No space in disk for another entry block.");
					else if (new_data_block < FIRST_VALID_BIT) return failed("Failed bitmap op.");

					write_block(
						 new_data_block,
						 buf,
						 map->sector_key*SECTOR_SIZE,
						 dentry_size);

					set_bitmap_index(BITMAP_BLOCKS, new_data_block, BIT_OCCUPIED);
					dirnode->bytesFileSize += dentry_size;
					dirnode->blocksFileSize += 1;
					dirnode->dataPtr[current_blocks] = new_data_block;
					rt->total_entries++;

					if (update_inode(0, *dirnode)) {
						//printf("Couldn't save the directory inode\n");
						free(buf);
						return -1;
					}

					allocated = true;

				}

			} else if (map->indirection_level == 1) {
				if(map->data_block > INVALID){
					// TINHA BLOCO ALOCADO COM ESPACO LIVRE
					write_block(
						 map->data_block,
						 buf,
						 map->sector_key*SECTOR_SIZE+map->sector_shift*sizeof(T_RECORD),
						 dentry_size);

					dirnode->bytesFileSize += dentry_size;
					rt->total_entries++;

					if (update_inode(0, *dirnode)) {
						//printf("Couldn't save the directory inode\n");
						free(buf);
						return -1;
					}
					allocated = true;

				} else {
					// BLOCK NOT FOUND.
					DWORD new_data_block = (DWORD)next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);

					if(new_data_block == NOT_FOUND) return failed("No space in disk for another entry block.");
					else if (new_data_block < FIRST_VALID_BIT) return failed("Failed bitmap op.");
					set_bitmap_index(BITMAP_BLOCKS, new_data_block, BIT_OCCUPIED);


					if (dirnode->singleIndPtr == INVALID) {
						// alloc new index block with pointers to entry blocks.
						int bloco_indices = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
						if(bloco_indices == NOT_FOUND) return failed("No space in disk for another index block.");
						else if (bloco_indices < FIRST_VALID_BIT) return failed("Failed bitmap op.");

						if(write_block(bloco_indices, (BYTE*)&new_data_block, 0, sizeof(DWORD))) {
								//printf("Map function failed to allocate block\n");
								free(buf);
								return -1;
						}
						//printf("block indexes %d\n", bloco_indices);
						set_bitmap_index(BITMAP_BLOCKS, bloco_indices, BIT_OCCUPIED);
						dirnode->singleIndPtr = bloco_indices;

						write_block(
							new_data_block,
							buf,
							0,
							dentry_size
						);

						dirnode->bytesFileSize += dentry_size;
						dirnode->blocksFileSize += 1;
						rt->total_entries++;

						allocated = true;

						if (update_inode(0, *dirnode)) {
							//printf("Couldn't save the directory inode\n");
							free(buf);
							return -1;
						}


					} else if (dirnode->singleIndPtr > INVALID) {
						write_block(
							 new_data_block,
							 buf,
							 map->sector_key*SECTOR_SIZE+map->sector_shift*sizeof(T_RECORD),
							 dentry_size
						);

						set_bitmap_index(BITMAP_BLOCKS, new_data_block, BIT_OCCUPIED);
						dirnode->bytesFileSize += dentry_size;
						dirnode->blocksFileSize += 1;
						write_block(
							dirnode->singleIndPtr,
							(BYTE*)&new_data_block,
							((map->single_pointer_index % mounted->pointers_per_block)*sizeof(DWORD)),
							sizeof(DWORD));
						rt->total_entries++;

						if (update_inode(0, *dirnode)) {
							//printf("Couldn't save the directory inode\n");
							free(buf);
							return -1;
						}

						allocated = true;

					}
				}

		} else if (map->indirection_level == 2){return -1;
			if(map->data_block > INVALID) {
				// TINHA BLOCO ALOCADO COM ESPACO LIVRE
				write_block(
					 map->data_block,
					 buf,
					 map->sector_key*SECTOR_SIZE+map->sector_shift,
					 dentry_size);

				dirnode->bytesFileSize += dentry_size;
				rt->total_entries++;
				allocated = true;

			} else {
				// BLOCK NOT FOUND.
				int new_data_block = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
				if(new_data_block == NOT_FOUND) return failed("No space in disk for another entry block.");
				else if (new_data_block < FIRST_VALID_BIT) return failed("Failed bitmap op.");


				if(dirnode->doubleIndPtr == INVALID) {

					int bloco_2_indices = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
					if(bloco_2_indices == NOT_FOUND) return failed("No space in disk for another index block.");
					else if (bloco_2_indices < FIRST_VALID_BIT) return failed("Failed bitmap op.");
					int bloco_1_indices = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
					if(bloco_1_indices == NOT_FOUND) return failed("No space in disk for another index block.");
					else if (bloco_1_indices < FIRST_VALID_BIT) return failed("Failed bitmap op.");

					// write tudo isso
					write_block(new_data_block,buf,0,dentry_size);

					set_bitmap_index(BITMAP_BLOCKS, new_data_block, BIT_OCCUPIED);
					dirnode->bytesFileSize += dentry_size;
					dirnode->blocksFileSize += 1;
					rt->total_entries++;

					allocated = true;

					 // ESCREVE UM BLOCO DE IND DUPLA com um unico ponteiro para o simples

					write_block(bloco_2_indices,(BYTE*)&bloco_1_indices,0,sizeof(DWORD));

					set_bitmap_index(BITMAP_BLOCKS, bloco_2_indices, BIT_OCCUPIED);
					dirnode->doubleIndPtr = (DWORD)bloco_2_indices;

					 // Agora o BLOCO IND SIMPLES do DUPLO recebe ponteiro para o novo bloco de entradas.
					write_block(bloco_1_indices, (BYTE*)&new_data_block,0,sizeof(DWORD));
					set_bitmap_index(BITMAP_BLOCKS, bloco_1_indices, BIT_OCCUPIED);

				} else if(map->single_pointer_to_block == INVALID) {
					int bloco_1_indices = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
					if(bloco_1_indices == NOT_FOUND) return failed("No space in disk for another index block.");
					else if (bloco_1_indices < FIRST_VALID_BIT) return failed("Failed bitmap op.");

					write_block(new_data_block,buf, 0,dentry_size);
					set_bitmap_index(BITMAP_BLOCKS, new_data_block, BIT_OCCUPIED);
					dirnode->bytesFileSize += dentry_size;
					dirnode->blocksFileSize += 1;
					rt->total_entries++;
					allocated = true;
					write_block(bloco_1_indices,(BYTE*)&new_data_block,0,sizeof(DWORD));
					set_bitmap_index(BITMAP_BLOCKS, bloco_1_indices, BIT_OCCUPIED);

					write_block(dirnode->doubleIndPtr,(BYTE*)&bloco_1_indices,0,sizeof(DWORD));

				} else if (map->single_pointer_to_block != INVALID) {

					write_block(new_data_block, buf, 0,dentry_size);

				 	set_bitmap_index(BITMAP_BLOCKS, new_data_block, BIT_OCCUPIED);
		 		 	dirnode->bytesFileSize += dentry_size;
		 		 	dirnode->blocksFileSize += 1;
					rt->total_entries++;
					allocated = true;
					write_block(map->single_pointer_to_block,(BYTE*)((map->single_pointer_index % mounted->pointers_per_block)*sizeof(DWORD)),
							 map->sector_key*SECTOR_SIZE+map->sector_shift,sizeof(DWORD));
					}
				}
			}
		}
		my_entry_index++;
	}

	free(map);
	free(buf);
	free(dummy);

	if (allocated) return SUCCESS;

	return FAILED;
}

int new_record(T_RECORD* rec){
	if (init() != SUCCESS) return failed("Failed to initialize");
	if (!is_mounted()) return failed("No partition mounted.");
	if (!is_root_loaded()) return failed("Directory must be opened.");
	if (rec == NULL) return failed("Bad record");

	T_DIRECTORY* rt = mounted->root;
	T_INODE* dirnode = rt->inode;
	DWORD dentry_size = sizeof(T_RECORD);
	DWORD block_size = mounted->superblock->blockSize * SECTOR_SIZE;

	DWORD current_blocks = dirnode->blocksFileSize;
	DWORD current_bytes = dirnode->bytesFileSize;

	//se terminar o loop sem not found achar o primeiro INVALID e criar index block ali

	BYTE* buf = (BYTE*) malloc(sizeof(BYTE)*dentry_size);
	memcpy((void*)buf, (void*)rec, dentry_size);

// versao original semi abortada
	if (current_bytes % block_size == 0) {
		// New block of entries required.
		int data_block = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
		if(data_block == NOT_FOUND) return failed("No space in disk for another entry block.");
		else if (data_block < FIRST_VALID_BIT) return failed("Failed bitmap op.");

		if (current_blocks+1 <= 2 ) {
			// allocate new block at dataPtr[current_blocks]
			write_block(data_block, buf, 0, dentry_size);
			set_bitmap_index(BITMAP_BLOCKS, data_block, BIT_OCCUPIED);
			dirnode->bytesFileSize += dentry_size;
			dirnode->blocksFileSize += 1;
			dirnode->dataPtr[current_blocks] = data_block;
			rt->total_entries++;

			if (update_inode(0, *dirnode)) {
				//printf("Couldn't save the directory inode\n");
				free(buf);
				return -1;
			}

		}
		else if (current_blocks+1 <= 2 + mounted->pointers_per_block) {

			// check whether single indirect has been allocated.
			if (dirnode->singleIndPtr == INVALID) {
				// alloc new index block with pointers to entry blocks.
				int bloco_indices = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
				if(bloco_indices == NOT_FOUND) return failed("No space in disk for another index block.");
				else if (bloco_indices < FIRST_VALID_BIT) return failed("Failed bitmap op.");

				BYTE* pointer = DWORD_to_BYTE(data_block, sizeof(DWORD));
				write_block(bloco_indices, pointer, 0, sizeof(DWORD));
				set_bitmap_index(BITMAP_BLOCKS, bloco_indices, BIT_OCCUPIED);
				dirnode->singleIndPtr = bloco_indices;

				write_block(data_block, buf, 0, dentry_size);
				set_bitmap_index(BITMAP_BLOCKS, data_block, BIT_OCCUPIED);
				dirnode->bytesFileSize += dentry_size;
				dirnode->blocksFileSize += 1;
				rt->total_entries++;
				if (update_inode(0, *dirnode)) {
					//printf("Couldn't save the directory inode\n");
					free(buf);
					return -1;
				}

			}
			// check whether there is room in the single indirection pointers for a new block.
			else if (current_blocks+1 <= 2 + mounted->pointers_per_block*(1 + mounted->pointers_per_block)) {
				// if valid index block, there must be some pointer available.
				//DWORD offset = parara find first pointer
				//write_block(dirnode->singleIndPtr, data_block, )
				;
			} else {
				return -1; // No space
			}

		}
	}

	free(buf);
	return 0;

}


int new_file(char* filename, T_INODE** inode){

	if(inode == NULL) return FAILED;
	if( !is_valid_filename(filename)) return(failed("New_file: Invalid Filename."));

	free(*inode);
	*inode = blank_inode();


	DWORD inode_index = next_bitmap_index(BITMAP_INODES, BIT_FREE);
	if(inode_index == NOT_FOUND)
		return(failed("No inodes free"));
	else if(inode_index < FIRST_VALID_BIT)
		return(failed("Failed bitmap query."));

	if(save_inode(inode_index, *inode) != SUCCESS)
		return(failed("[NewFile]Failed to save inode."));

	// new file - record creation
	T_RECORD* rec = blank_record();
	rec->inodeNumber  = inode_index;
	rec->TypeVal 			= TYPEVAL_REGULAR;
	strncpy(rec->name, filename, strlen(filename));

	//adds record to root directory
	if(new_record2(rec) != SUCCESS) return(failed("NewFile: Failed to save record"));

	return SUCCESS;
}

int set_file_open(T_INODE* file_inode){

	if (!is_mounted())   return(failed("SetFileOpen failed 1"));
	if (!is_root_loaded()) return(failed("SetFileOpen failed 2"));

	T_FOPEN* fopen = mounted->root->open_files;

	if (mounted->root->num_open_files >= MAX_FILES_OPEN){
		print("Maximum number of open files reached.");
		return FAILED;
	}
	int i;
	for(i=0; i < MAX_FILES_OPEN; i++){
		// check if position is not occupied by another file
		if(fopen[i].inode == NULL){

			//printf("Adicionei arquivo na posicao: %d\n", i);
			fopen[i].handle = i;
			fopen[i].inode 	= file_inode;
			fopen[i].current_pointer = 0;
			fopen[i].inode_index = INVALID;
			// TODO tem que achar o inode index junto com o inode
			// para salvar no arquivo aberto.

			mounted->root->num_open_files++;
			return i;
		}
	}
	return FAILED;
}

int set_file_close(FILE2 handle){
	if(handle >= MAX_FILES_OPEN || handle < 0){
		print("Handle out of bounds.");
		return FAILED;
	}
	if (!is_mounted())   return(failed("SetFileClose failed 1"));
	if (!is_root_loaded()) return(failed("SetFileClose failed 2"));

	T_FOPEN* fopen = mounted->root->open_files;

	if(fopen[handle].inode == NULL) {
		print("File handle does not correspond to an open file.");
		return -1;
	}
	else mounted->root->num_open_files--;

	// In either case, overwriting fopen data to make sure.
	fopen[handle].inode 		= NULL;
	fopen[handle].current_pointer = 0;
	fopen[handle].inode_index 	=	-1;

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


int remove_pointer_from_bitmap(DWORD pointer, DWORD sector_start, DWORD block_size, WORD handle){
	int bit = floor(pointer - sector_start)/block_size;

	if(setBitmap2(handle, bit, 0)==SUCCESS){
		return SUCCESS;
	}
	return FAILED;
}

int iterate_singlePtr(T_INODE* inode, DWORD start_data_sector, DWORD block_size){

	//realiza leitura de um setor do bloco de indices
	BYTE* buffer = alloc_sector();
	int i, j;
	for(i=0; i < block_size; i++){
		if(read_sector(start_data_sector, buffer) != SUCCESS) {
			return failed("Failed to read MBR"); }
		//iterate pointers in sector
		for(j=0; j < 64; j++){
			if(buffer[j]!=0x00)
				remove_pointer_from_bitmap(buffer[j], start_data_sector, block_size, 1);
		}
	}
	free(buffer);
	return SUCCESS;
}

int iterate_doublePtr(T_INODE* inode, DWORD start_inode_sector, DWORD start_data_sector, DWORD block_size){

	//realiza leitura de um setor do bloco de indices
	BYTE* sector1 = alloc_sector();
	BYTE* sector2 = alloc_sector();

	//iterate through sectors in block1
	int i1, j1;
	for(i1=0; i1 < block_size; i1++)
	{
		if(read_sector(start_inode_sector, sector1) != SUCCESS) {
			return failed("Failed to read MBR"); }

		//iterate through pointers in sector1
		for(j1=0; j1 < 64; j1++)
		{
			if(sector1[j1]!=0x00){

				//iterate through sectors in block2
				int i2, j2;
				for(i2=0; i2 < block_size; i2++)
				{
					if(read_sector(start_inode_sector, sector2) != SUCCESS) {
						return failed("Failed to read MBR");
					}

					//iterate through pointers in sector2
					for(j2=0; j2 < 64; j2++)
					{
						if(sector2[j2]!=0x00)
							remove_pointer_from_bitmap(sector2[j2], start_data_sector, block_size, 1);
					}
				}
			}

			remove_pointer_from_bitmap(sector1[j1], start_inode_sector, block_size, 0);

		}
	}

	free(sector1);
	free(sector2);

	return SUCCESS;
}

int remove_file_content(T_INODE* inode){
	int superbloco_sector = mounted->mbr_data->initial_sector;

	if(openBitmap2(superbloco_sector) != SUCCESS)
		return FAILED;


	T_SUPERBLOCK* sb = mounted->superblock;

	DWORD start_block = sb->superblockSize + sb->freeBlocksBitmapSize + sb->freeInodeBitmapSize;
	// Add offset to where the partition starts in the disk
	int start_inode_sector = mounted->mbr_data->initial_sector;
	// Add offset to where in the partition the inodes start
	start_inode_sector += start_block * sb->blockSize;

	int start_data_sector = start_inode_sector + (sb->inodeAreaSize * sb->blockSize);

	DWORD block_size = sb->blockSize;

	// percorre ponteiros diretos para blocos de dados
	remove_pointer_from_bitmap(inode->dataPtr[0], start_data_sector, block_size, 1);
	remove_pointer_from_bitmap(inode->dataPtr[1], start_data_sector, block_size, 1);

	// percorre ponteiros indiretos de indirecao simples
	iterate_singlePtr(inode, start_data_sector, block_size);

	// percorre ponteiros de indirecao dupla, traduz ponteiro para posicao no bitmap de dados, zera posicoes no bitmap de dados
	iterate_doublePtr(inode, start_inode_sector, start_data_sector, block_size);

	return SUCCESS;
}

int remove_record(char* filename){

	int superbloco_sector = mounted->mbr_data->initial_sector;

	if(openBitmap2(superbloco_sector) != SUCCESS)
		return FAILED;

	// percorre blocos de dados do diretorio raiz buscando registro

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

	if(initialize_inode_area(&sb) != SUCCESS)
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
		return(failed("Partition already mounted."));
	}
	if(mounted != NULL && mounted->id != partition){
		return(failed("Unmount current partition before mounting another."));
	}
	if ((partition < 0) || (partition >= disk_mbr.num_partitions))
		return(failed("Partition invalid."));

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

	if(mounted == NULL){
		return(failed("No partition to unmount."));
	}

	if(closedir2() != SUCCESS){
		return(failed("Unmount failed: could not close root dir."));
	}

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
	if(init() != SUCCESS) return failed("create2: failed to initialize");
	if(!is_mounted()) return failed("No partition mounted.");
	if(opendir2() != SUCCESS) return failed("Directory must be opened.");
	if(!is_valid_filename(filename)) return failed("Invalid filename");

	DWORD inode_index = next_bitmap_index(BITMAP_INODES, BIT_FREE);
	if(inode_index == NOT_FOUND)
		return(failed("No inodes free"));
	else if(inode_index < FIRST_VALID_BIT)
		return(failed("Failed bitmap query."));

	T_RECORD* record = alloc_record(1);

	if(find_entry(filename, &record) == SUCCESS){
		return failed("Filename already exists.");
	}

	T_INODE* inode = alloc_inode(1);
	if(new_file(filename, &inode) != SUCCESS)
		return failed("Create: Failed to create new file");

	//printf("File created: %s\n", filename);

	FILE2 handle = set_file_open(inode);
	return handle;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para remover (apagar) um arquivo do disco.
-----------------------------------------------------------------------------*/
int delete2 (char *filename) {
	if (init() != SUCCESS) return(failed("delete2: failed to initialize"));
	if(!is_mounted()) return(failed("No partition mounted."));
	if(!is_root_loaded()) return(failed("Could not open MFD."));
	if(!is_valid_filename(filename)) return(failed("Delete: Filename invalid"));

	T_RECORD* record = alloc_record(1);
	if(find_entry(filename, &record) != SUCCESS){
		return(failed("Delete: File does not exist."));
	}
	else {
		T_INODE* inode = alloc_inode(1);
		if( access_inode(record->inodeNumber, inode) != SUCCESS) return FAILED ;

		if(record->TypeVal == TYPEVAL_REGULAR){
			delete_entry(filename);
			inode->RefCounter--;
		  if(inode->RefCounter == 0) {
			 remove_file_content(inode);
			}

			return SUCCESS;
		}

		else {
			remove_file_content(inode);
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
		print("Maximum number of open files reached.");
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
				if(read_sector(mounted->mbr_data->initial_sector
							   + (mounted->superblock->blockSize)*inode->dataPtr[0], buffer) != SUCCESS) return FAILED;
				memcpy((void*)_filename, (void*)buffer, 51);

				if (find_entry(_filename, &rec) != SUCCESS) return FAILED;
				if (access_inode(rec->inodeNumber, inode) != SUCCESS) return FAILED;
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
	//if(!is_valid_handle(handle)) return failed("Invalid file handle."); // cant compile without a function
	return set_file_close(handle);
}

/*-----------------------------------------------------------------------------
Função:	Função usada para realizar a leitura de uma certa quantidade
		de bytes (size) de um arquivo.
-----------------------------------------------------------------------------*/
int read2 (FILE2 handle, char *buffer, int size) {
return -1;
	// Validation
	if (init() != SUCCESS) return failed("close2: failed to initialize");
	if(!is_mounted()) return failed("No partition mounted.");
	if(!is_root_loaded()) return failed("Directory must be opened.");
	//f(!is_valid_handle(handle)) return failed("Invalid Fopen handle."); // cant compile without a function
	if(size <= 0) return failed("Invalid number of bytes.");
	// TODO: verificar se pode numero negativo de bytes
	// (ler os x bytes anteriores ao current pointer e atualizar o current)
	T_FOPEN f = mounted->root->open_files[handle];
	if(f.inode == NULL) return FAILED;

	DWORD bytes_per_block = mounted->superblock->blockSize * SECTOR_SIZE;
	// Capacidade maxima do arquivo agora.
	DWORD max_capacity = f.inode->blocksFileSize * bytes_per_block;

	DWORD cur_block_number;
	DWORD cur_block_index;
	DWORD read_length;
	DWORD cur_data_byte = 0;
	DWORD byte_shift = f.current_pointer % bytes_per_block;


	while (cur_data_byte < size && cur_data_byte < max_capacity) {

		cur_block_number = f.current_pointer / bytes_per_block;
		cur_block_index = get_data_block_index(f, cur_block_number);
		if(cur_block_index == INVALID) return FAILED;


		if ( (size - cur_data_byte) < (bytes_per_block - byte_shift))
			read_length = size - cur_data_byte;
		else read_length = bytes_per_block - byte_shift;

		write_block(cur_block_index, (BYTE*)&(buffer[cur_data_byte]), byte_shift, read_length);

		f.current_pointer += read_length;
		byte_shift = f.current_pointer % bytes_per_block;
		cur_data_byte += read_length;
	}

	printf("String resultante lida: \n %s", buffer);
	return SUCCESS;
}

// map current_pointer (Nth byte) to a specific block and sector

// if map to N+size is a different block/sector:
// map para cada setor ou block subsequente
// cada map retorna o setor
// while (size > 0 )
// memcopy do sector para o BUFFER recebido, min(size, SECTORSIZE) bytes
// decrementa size
// update current pointer para o byte SEGUINTE ao ultimo lido.

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
			//printf("error at writeblock\n");
			return -1;
		}


		if( (SECTOR_SIZE-sector_byte) < (data_size - current_data_byte))
			bytes_to_copy = SECTOR_SIZE-sector_byte;
		else
			bytes_to_copy = data_size - current_data_byte;

		memcpy(&(sector_buffer[sector_byte]), &(data_buffer[current_data_byte]), bytes_to_copy);
		current_data_byte += bytes_to_copy;
		sector_byte = 0;

		write_sector(sector, sector_buffer);


	}

	set_bitmap_index(BITMAP_BLOCKS, block_index, BIT_OCCUPIED);

	return SUCCESS;

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
		if(read_sector(sector, sector_buffer)) {
			//printf("error at writeblock\n");
			return -1;
		}


		if( (SECTOR_SIZE-sector_byte) < (data_size - current_data_byte))
			bytes_to_copy = SECTOR_SIZE-sector_byte;
		else
			bytes_to_copy = data_size - current_data_byte;

		memcpy(&(data_buffer[current_data_byte]), &(sector_buffer[sector_byte]), bytes_to_copy);
		current_data_byte += bytes_to_copy;
		sector_byte = 0;
	}
	return SUCCESS;
}

int write_data(T_INODE* inode, int position, char *buffer, int size) {

	// DWORD bytes_per_block = mounted->superblock->blockSize * SECTOR_SIZE;

	// int offset = position % bytes_per_block;

	// int sector = find_sector_by_position(inode, position);

	// // write x bytes from buffer at position given by pointer
	// printf("printando buffer para escrever nas devidas posicoes\n");
	// for(int i =0; i < size; i++){
	// 	printf("%x ", buffer[i]);
	// }
	// printf("posicao %d\n", sector);

	// write_block(position, buffer, position, size);
	return FAILED;

}


int insert_data_block_index(T_FOPEN file, DWORD cur_block_number, DWORD index) {

return -1;

	if (index == INVALID) return FAILED;
	if (cur_block_number < 2){

		file.inode->dataPtr[cur_block_number] = index;


	if(update_inode(file.inode_index, *(file.inode)) != SUCCESS) return failed("fail");

		return SUCCESS;
	}
	else if (cur_block_number < mounted->pointers_per_block){
		int indirection_index = file.inode->singleIndPtr;
		if (indirection_index == INVALID) {
			DWORD indirection = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
			if(indirection < FIRST_VALID_BIT){
				return failed("Write2: Failed to find enough free data blocks.");
			}
			else {
				set_bitmap_index(BITMAP_BLOCKS, indirection, BIT_OCCUPIED);
				file.inode->singleIndPtr = indirection;
	if(update_inode(file.inode_index, *(file.inode)) != SUCCESS) return failed("fail");
			}
		}

		indirection_index = file.inode->singleIndPtr;

		int pointers_per_sector = mounted->pointers_per_block / mounted->superblock->blockSize;
		DWORD sector_in_block = (cur_block_number-2)/pointers_per_sector;
		DWORD shift_in_sector = (cur_block_number-2)%pointers_per_sector;

		if(write_block(indirection_index, (BYTE*)&index,
			sector_in_block*SECTOR_SIZE+shift_in_sector, DATA_PTR_SIZE_BYTES) != SUCCESS) return FAILED;

		else {
	if(update_inode(file.inode_index, *(file.inode)) != SUCCESS) return failed("fail");
return SUCCESS;}
	}
	else return INVALID;
}



int get_data_block_index(T_FOPEN file, DWORD cur_block_number) {

return -1;

	if (cur_block_number < 2)
		return file.inode->dataPtr[cur_block_number];
	else if (cur_block_number < mounted->pointers_per_block){

		DWORD indirection_index = file.inode->singleIndPtr;
		if (indirection_index == INVALID) return INVALID;
		BYTE* sector_buffer = alloc_sector();

		int pointers_per_sector = mounted->pointers_per_block / mounted->superblock->blockSize;
		DWORD sector_in_block = (cur_block_number-2)/pointers_per_sector;
		DWORD shift_in_sector = (cur_block_number-2)%pointers_per_sector;

		DWORD offset = mounted->mbr_data->initial_sector + indirection_index * mounted->superblock->blockSize;

		if (read_sector(offset + sector_in_block, sector_buffer) != SUCCESS) return FAILED;

		return (DWORD)sector_buffer[shift_in_sector*DATA_PTR_SIZE_BYTES];

	}
	else return INVALID;
}


int write2 (FILE2 handle, char *buffer, int size) {
return -1;
	// Validation
	if (init() != SUCCESS) return failed("close2: failed to initialize");
	if(!is_mounted()) return failed("No partition mounted.");
	if(!is_root_loaded()) return failed("Directory must be loaded.");
	//if(!is_valid_handle(handle)) return failed("Invalid Fopen handle.");
	if(size <= 0) return failed("Invalid number of bytes.");

	T_FOPEN f = mounted->root->open_files[handle];
	if(f.inode == NULL) return FAILED;

	DWORD bytes_per_block = mounted->superblock->blockSize * SECTOR_SIZE;
	// Capacidade maxima do arquivo agora.
	DWORD current_max_capacity = f.inode->blocksFileSize * bytes_per_block;

	printf("Current max capac at %d\n",(int)current_max_capacity );
	printf("Cur pointer at %d\n",(int)f.current_pointer);
	printf("size to write %d\n",(int)size );
	DWORD cur_block_number;
	DWORD cur_block_index;
	DWORD write_length;
	DWORD cur_data_byte = 0;
	DWORD byte_shift = f.current_pointer % bytes_per_block;

	if (f.current_pointer + size > current_max_capacity) {
		// alloc more blocks + update inode
		DWORD number_new_blocks = 1+(f.current_pointer + size - current_max_capacity)/bytes_per_block;
		printf("Number of new blocks %d\n",(int)number_new_blocks );
		int i;
		//unsigned int* indexes = (unsigned int*)malloc(sizeof(unsigned int)*number_new_blocks);
int indice;

		for(i=0; i< number_new_blocks; i++){

			indice = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
			if(indice < FIRST_VALID_BIT){
				printf("Needs %u new data blocks, but partition only has %u free.\n",number_new_blocks,i);
				return failed("Write2: Failed to find enough free data blocks.");
			}
			else {

if (set_bitmap_index(BITMAP_BLOCKS, indice, BIT_OCCUPIED) !=SUCCESS) return FAILED;

if(insert_data_block_index(f, f.inode->blocksFileSize + i, indice) <= 0) return FAILED;
}
		}



		f.inode->blocksFileSize += number_new_blocks;

		current_max_capacity = f.inode->blocksFileSize * bytes_per_block;
	if(update_inode(f.inode_index, *(f.inode)) != SUCCESS) return failed("fail");

	}

	if (f.current_pointer + size <= current_max_capacity) {

		// no need to allocate anything new.
		while (cur_data_byte < size) {

			cur_block_number = f.current_pointer / bytes_per_block;
			cur_block_index = get_data_block_index(f, cur_block_number);
			if(cur_block_index == INVALID) return FAILED;


			if ( (size - cur_data_byte) < (bytes_per_block - byte_shift))
				write_length = size - cur_data_byte;
			else write_length = bytes_per_block - byte_shift;

			write_block(cur_block_index, (BYTE*)&(buffer[cur_data_byte]), byte_shift, write_length);

			f.current_pointer += write_length;
			if(f.current_pointer >= f.inode->bytesFileSize) {
				f.inode->bytesFileSize = f.current_pointer+1;
			}

			byte_shift = f.current_pointer % bytes_per_block;
			cur_data_byte += write_length;
		}
	}
	printf("Inode number: %d \n", (int)f.inode_index);
	if(update_inode(f.inode_index, *(f.inode)) != SUCCESS) return failed("fail");
	return SUCCESS;
}
// find inode, find data block that includess the current pointer
// if no data block allocated, alloc one or more according to bytes.
// start writing bytes
// if bytes exceed the block, jump to next block
// OR allocate another block
// therefore: calculate whether
// current pointer + number of new bytes > free space available until end of file blocks.
// if it is, check if you can allocate more blocks in disk before starting
// write bytes, update pointer, update size in bytes and size in blocks.
// if bytes ends at the very last byte,
// the pointer points to a byte position that does not exist yet.
// therefore: always check whether current pointer <= the "size in bytes" in the inode of file.

// alternate behavior:
// softlink: finds original file by name then do as above.
// hardlink: get original file by inode then same.
// update hardlink with size etc too.


/*-----------------------------------------------------------------------------
Função:	Função usada para realizar a escrita de uma certa quantidade
		de bytes (size) de  um arquivo.
-----------------------------------------------------------------------------*/
int write3 (FILE2 handle, char *buffer, int size) {
	return -1;
	// Validation
// 	if (init() != SUCCESS) return failed("close2: failed to initialize");
// 	if(!is_mounted()) return failed("No partition mounted.");
// 	if(!is_root_loaded()) return failed("Directory must be opened.");
// 	// if(!is_valid_handle(handle)) return failed("Invalid Fopen handle.");
// 	if(size <= 0) return failed("Invalid number of bytes.");
//
//
// 	T_FOPEN f = mounted->root->open_files[handle];
// 	T_INODE* file_inode;
//
//
// 	printf("Writing 0\n");
//
// 	if(f.inode==NULL){}
//
// 	if(writeMFD){
// 		file_inode = mounted->root->inode;
// 	}
// 	// checking if handle is valid
// 	else if(f.inode==NULL){
// 		return failed("File must be opened.");
// 	}
// 	else{
// 		file_inode = f.inode;
// 	}
//
//
// 	//DWORD offset;
// 	//BYTE* sector_buffer = alloc_sector();
//
// // Vou assumir so caso normal por enquanto
// 	//write:
// 	// openfilehandle and number of bytes.
// 	// map current pointer to block and sector
// 	DWORD bytes_per_block = mounted->superblock->blockSize * SECTOR_SIZE;
//
// 	// Capacidade maxima do arquivo agora.
// 	// DWORD max_bytes_currently = f.inode->blocksFileSize * bytes_per_block;
// 	DWORD max_bytes_currently = file_inode->blocksFileSize * bytes_per_block;
//
// 	// Qtos bytes pode expandir ainda sem alocar nada novo.
// 	// DWORD max_bytes_left = max_bytes_currently - f.inode->bytesFileSize;
// 	DWORD max_bytes_left = max_bytes_currently - file_inode->bytesFileSize;
//
//
// 	// calculando quanto expandir se porventura necessario
// 	// int extra_size = f.current_pointer + size - f.inode->bytesFileSize;
// 	int extra_size;
// 	if(!writeMFD){
// 		extra_size = f.current_pointer + size - file_inode->bytesFileSize;
// 	}
// 	else{
// 		extra_size = size;
// 	}
//
// 	// printf("blocksize =%d\n", mounted->superblock->blockSize);
// 	// printf("bytes/block =%d\n", bytes_per_block);
// 	// printf("max_bytes_currently =%d\n", max_bytes_currently);
// 	// printf("max_bytes_left =%d\n", max_bytes_left);
// 	// printf("extra_size =%d\n", extra_size);
//
// 	if( extra_size > 0) {
// 		// vai ter x bytes novos para escrever alem do size atual do arquivo.
//
// 		if( max_bytes_left < extra_size) {
//
// 			// vai ter que alocar mais blocos!!
// 			// checar se bytes_per_block é zero?
// 			DWORD num_new_blocks = 1 + (extra_size - max_bytes_left) / bytes_per_block;
// 			int i;
//
// 			DWORD* indexes = (DWORD*)malloc(sizeof(DWORD)*num_new_blocks);
// 			for(i=0; i<num_new_blocks; i++){
//
//
// 				indexes[i] = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
//
//
// 				if(indexes[i] < FIRST_VALID_BIT){
// 					printf("Needs %u new data blocks, but partition only has %u free.\n",num_new_blocks,i);
// 					return failed("Write2: Failed to find enough free data blocks.");
// 				}
//
// 			}
// 			printf("Writing 33\n");
//
// 			// ok, tem blocos suficientes para dados.
// 			// mas e se agora mudou de ponteiro ou nivel de indirecao no inode?
// 			// int future_size_blocks = file_inode->blocksFileSize + num_new_blocks;
// 			printf("Writing 4\n");
//
// 			allocate_new_indexes(file_inode, indexes, num_new_blocks);
//
// 			if(writeMFD){
// 				// write_data(file_inode, file_inode->blocksFileSize, buffer, size);
// 				for(int i=0; i<num_new_blocks;i++) {
// 					printf("Escrevendo registro\n");
// 					write_block(indexes[i], (BYTE*)buffer, file_inode->bytesFileSize, size);
// 				}
// 			}
// 			else {
// 				printf("Escrevendo arquivo\n");
// 				write_data(file_inode, f.current_pointer, buffer, size);
// 				f.current_pointer = f.current_pointer + size + 1;
//
// 			}
//
// 				// map to pointers etc
//
// 			// se arq tinha 0 blocks, os primeiros dois novos sao sem overhead (dataPtr 0 e 1)
// 			// a partir do 3 bloco do arquivo, precisa um bloco de indices.
// 			// cada ponteiro nesse bloco aponta para um dos blocos de dados
// 			// a partir do bloco Y , precisa um bloco de indices (double) e outros Z blocos de indices (single)
//
// 		}
// 	}
//
// 	// find inode, find data block that includess the current pointer
// 	// if no data block allocated, alloc one or more according to bytes.
// 	// start writing bytes
// 	// if bytes exceed the block, jump to next block
// 	// OR allocate another block
// 	// therefore: calculate whether
// 	// current pointer + number of new bytes > free space available until end of file blocks.
// 	// if it is, check if you can allocate more blocks in disk before starting
// 	// write bytes, update pointer, update size in bytes and size in blocks.
// 	// if bytes ends at the very last byte,
// 	// the pointer points to a byte position that does not exist yet.
// 	// therefore: always check whether current pointer <= the "size in bytes" in the inode of file.
//
// 	// alternate behavior:
// 	// softlink: finds original file by name then do as above.
// 	// hardlink: get original file by inode then same.
// 	// update hardlink with size etc too.
//
//
// 	return SUCCESS;
	// return -1;
}

int allocate_new_indexes(T_INODE* file_inode, DWORD* indexes, DWORD num_new_blocks){

	// maximum number of pointers per index_block
	int pointers_per_block = (mounted->superblock->blockSize * SECTOR_SIZE) / sizeof(DWORD*);

	// maximum number of pointers per inode dataPtr+singleIndPtr+doubleIndPtr
	int maximum_indexes = 2 + pointers_per_block + pointers_per_block*pointers_per_block;

	int new_num_blocks = sizeof(indexes);

	// current number of blocks in a file
	int current_blocks = file_inode->blocksFileSize;
	// number of blocks that will be in block after allocate_new_indexes
	int future_blocks = file_inode->blocksFileSize + new_num_blocks;
	if(future_blocks>maximum_indexes)
		return failed("Allocate Indexes: Inode cannot allocate enough indexes for this buffer.");

	int offset=0;
	int written_blocks=0;

	// allocate at dataPtr, 2 maximum indexes
	// int end = max(new_num_blocks, 2);
	// int end = new_num_blocks;
	if(future_blocks <= 2){
		for(int i=current_blocks; i<new_num_blocks; i++){
			// save value of indexes to dataPtr
			printf("allocating block %d\n", i);

			file_inode->dataPtr[i] = indexes[written_blocks];

			written_blocks++;
		}
	}

	// allocate at singleIndPtr, from 2 to 2+pointers_per_block maximum indexes
	offset  = current_blocks - 2;
	// end = new_num_blocks - written_blocks;

	// end = max(end, pointers_per_block);
	if(offset>0 && future_blocks <= pointers_per_block+2){
		// index block has not been initialized yet
		int index;
		// if(file_inode->singleIndPtr==NULL){
		if(file_inode->singleIndPtr==0){
			// create single ind block to data block

			// search data bitmap for free block
			index = next_bitmap_index(1, 0);
			// set occupied
			if(set_bitmap_index(0, index, 1)!=SUCCESS){
				return failed("Could not set index");
			}

			file_inode->singleIndPtr = index;
		}
		else {
			index = file_inode->singleIndPtr;
		}


		int buffer_size = num_new_blocks - written_blocks;

		// write indexes to indPtr
		// write_block(index, (char*)indexes, indexes[written_blocks], buffer_size);
		write_block(index, (BYTE*)indexes, written_blocks, buffer_size);

	}

	// allocate at doubleIndPts, from 2+pointers_per_block to maximum_indexes
	// offset = current_blocks - (2+pointers_per_block);
	// end = new_num_blocks - written_blocks;
	// if(offset>0 && future_blocks <= maximum_indexes){

	// 	if(file_inode->doubleIndPtr==NULL){
	// 		// create ind block to ind block
	// 	}

	// 	for(int i=offset; i<end; i++){
	// 		int double_to_single = offset % pointers_per_block;

	// 		// read index block pointed by first
	// 		BYTE* buffer = alloc_sector();
	// 		read_sector(file_inode->doubleIndPtr, buffer);
	// 		// get value of single indirect
	// 		if((DWORD)buffer[0]==NULL)
	// 		{
	// 			// create ind block to data block
	// 		}

	// 		end = new_num_blocks - written_blocks;
	// 		for(int j=0; j<end; end++){
	// 			printf("Write index to index block\n");
	// 		}
	// 	}
	// }
	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Função que abre um diretório existente no disco.
-----------------------------------------------------------------------------*/
int opendir2 (void) {
	if (init() != SUCCESS) return(failed("OpenDir: failed to initialize"));

	if(!is_mounted()) return(failed("No partition mounted yet."));

	//printf("OpD 1\n");

	mounted->root->open = true;
	mounted->root->entry_index = 0;

  //printf("OpDir final\n");

	// Caso contrário usar o valor na variável global, acessar o seu root,
	// e guardar seu ponteiro ou inicializar algum estrutura tipo "T_DIR"
	// que guarde globalmente tudo que precisamos de um diretório.
	// Se um diretório ja aberto, dizer que já tem aberto/mandar fechar o atual
	// (embora seja o mesmo).
	// Setar atual entrada de leitura para 0 para o readdir.
	// mounted->root->current_entry = 0;
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
	DWORD sector = mounted->fst_inode_sector;
	BYTE* buffer = alloc_sector();

	if(read_sector(sector, buffer) != SUCCESS){
		free(buffer);
		return(failed("AccessDirectory: Failed to read dir sector."));
	}
	if(BYTE_to_INODE(buffer, inode_index , return_inode) != SUCCESS) {
		free(buffer);
		return(failed("Failed BYTE to INODE translation"));
	}
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
			return(failed("FindEIB: failed to read a sector"));
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

	int i, s;
	int total_sects = sb->blockSize;
	DWORD offset = mounted->mbr_data->initial_sector+index_block * sb->blockSize;
	BYTE* buffer = alloc_sector();
	int total_ptrs = SECTOR_SIZE / DATA_PTR_SIZE_BYTES;
	DWORD entry_block;

	for(s=0; s < total_sects; s++ ){
		if(read_sector(offset + s, buffer) != SUCCESS){
			//printf("Sweep: failed to read sector %d of block %d", s, index_block);
			return(failed("Sweep: failed to read a sector"));
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
	// Helpful pointers
	T_SUPERBLOCK* sb = mounted->superblock;
	T_DIRECTORY* 	rt = mounted->root;

	free(*record);
	*record = alloc_record(1);
	BYTE* buffer = alloc_sector();
	int total_sects = sb->blockSize;

	// TODO: verificar se ponteiro dentro do inode vai para índice de bloco absoluto
	// ou relativo a particao

	// Directory structure:
	// dataPtr[0] --> Entry block (4 entries per sector).
	// dataPtr[1] --> 4 more entries per sector .
	// singleIndPtr --> Index block --> Entry blocks.
	// doubleIndPtr --> Index block --> Index blocks --> Entry blocks.

	DWORD entry_block;
	DWORD offset;
	int i, s;
	// DIRECT POINTERS TO ENTRY BLOCKS
	for (i = 0; i < 2; i++) {
		entry_block = rt->inode->dataPtr[i];
		if(find_entry_in_block(entry_block, filename, *record) > NOT_FOUND){
			print("Found entry successfully");
			return SUCCESS;
		}
	}

	// INDIRECT POINTER TO INDEX BLOCKS
	DWORD index_block = rt->inode->singleIndPtr;
	if(index_block > INVALID) {
		// Valid index
		if(find_indirect_entry(index_block, filename, *record) > NOT_FOUND){
			print("Found entry successfully");
			return SUCCESS;
		}
	}

	// DOUBLE INDIRECT POINTER
	index_block = rt->inode->doubleIndPtr;
	int total_ptrs = SECTOR_SIZE / DATA_PTR_SIZE_BYTES;
	offset = mounted->mbr_data->initial_sector + index_block*sb->blockSize;
	DWORD inner_index_block;

	if(index_block > INVALID) {
		// Valid index
		for(s=0; s < total_sects; s++){
			if(read_sector(offset + s, buffer) != SUCCESS){
				//printf("Sweep: failed to read sector %d of block %d", s, index_block);
				return(failed("Sweep: failed to read a sector"));
			}
			for(i=0; i<total_ptrs; i++){
				// Each pointer --> a block of indexes to more blocks.
				inner_index_block = (DWORD)buffer[i*DATA_PTR_SIZE_BYTES];

				if(find_indirect_entry(inner_index_block, filename, *record) > NOT_FOUND){
					print("Found entry successfully");
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
	if(mounted->root->inode == NULL) return failed("bad inode ptr");
	if(!is_valid_filename(filename)) return failed("Filename invalid.");

	// Helpful pointers
	T_SUPERBLOCK* sb = mounted->superblock;
	T_DIRECTORY* 	rt = mounted->root;

	BYTE* buffer = alloc_sector();
	int total_sects = sb->blockSize;

	// Directory structure:
	// dataPtr[0] --> Entry block (4 entries per sector).
	// dataPtr[1] --> 4 more entries per sector .
	// singleIndPtr --> Index block --> Entry blocks.
	// doubleIndPtr --> Index block --> Index blocks --> Entry blocks.

	DWORD entry_block;
	DWORD offset;
	int i, s;
	// DIRECT POINTERS TO ENTRY BLOCKS
	for (i = 0; i < 2; i++) {
		entry_block = rt->inode->dataPtr[i];
		if(delete_entry_in_block(entry_block, filename) > NOT_FOUND){
			print("Deleted entry successfully");
			return SUCCESS;
		}
	}

	// INDIRECT POINTER TO INDEX BLOCKS
	DWORD index_block = rt->inode->singleIndPtr;
	if(index_block > INVALID) {
		// Valid index
		if(delete_indirect_entry(index_block, filename) > NOT_FOUND){
			print("Deleted entry successfully");
			return SUCCESS;
		}
	}

	// DOUBLE INDIRECT POINTER
	index_block = rt->inode->doubleIndPtr;
	int total_ptrs = SECTOR_SIZE / DATA_PTR_SIZE_BYTES;
	offset = mounted->mbr_data->initial_sector + index_block*sb->blockSize;
	DWORD inner_index_block;

	if(index_block > INVALID) {
		// Valid index
		for(s=0; s < total_sects; s++){
			if(read_sector(offset + s, buffer) != SUCCESS){
				//printf("Del: failed to read sector %d of block %d", s, index_block);
				return(failed("Del: failed to read a sector"));
			}
			for(i=0; i<total_ptrs; i++){
				// Each pointer --> a block of indexes to more blocks.
				inner_index_block = (DWORD)buffer[i*DATA_PTR_SIZE_BYTES];

				if(delete_indirect_entry(inner_index_block, filename) > NOT_FOUND){
					print("Deleted entry successfully");
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
			entry_block = (DWORD)(buffer[i*DATA_PTR_SIZE_BYTES]);
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
			memcpy(record, &(buffer[e*entry_size]), sizeof(T_RECORD));

			if(strcmp(record->name, filename) == 0) {
				// achou
				memcpy(&(buffer[e*entry_size]), blank , sizeof(T_RECORD));
				if(write_sector(offset + sector, buffer) != SUCCESS) return(failed("Failed a write sector"));
				return 1;
			}
		}
	}
	return NOT_FOUND;
}


int map_index_to_record(DWORD index, T_RECORD** record, MAP* map){
	if(!is_mounted()) 	return(failed("MITR no partition mounted yet."));
	if(!is_root_loaded()) return(failed("MITR root not loaded"));
	T_DIRECTORY*  rt = mounted->root;
	T_SUPERBLOCK* sb = mounted->superblock;
	if(index < 0) 							 		 return(failed("MITB bad negative index"));
	if(index >= rt->max_entries) 		 return(failed("MITB bad positive index"));

	DWORD eps = mounted->entries_per_block / sb->blockSize;
	DWORD sector_shift = (index % mounted->entries_per_block) % eps;

	BYTE* buffer = alloc_sector();
	int block = map_index_to_sector(index, mounted->entries_per_block, &buffer, map);
	if(block <= NOT_FOUND) return block; // failed ou inexistente

	free(*record); *record = alloc_record(1);

	map->buffer_index = map->sector_shift*ENTRY_SIZE_BYTES;
	memcpy((void*)*record, (void*)&(buffer[sector_shift*ENTRY_SIZE_BYTES]), sizeof(T_RECORD));

	free(buffer);

	return (*record)->TypeVal;
}

// This mapping function uses
// the the number of the entry or the position byte within a file
// to map straight to the --sector? blockid?-- where the content is, from the inode. TODO: answer
int map_index_to_sector(DWORD index, DWORD units_per_block, BYTE** buffer, MAP* map ) {

	if(!is_mounted()) 	return(failed("MITB no partition mounted yet."));
	if(!is_root_loaded()) return(failed("MITB root not loaded"));
	T_DIRECTORY*  rt = mounted->root;
	T_SUPERBLOCK* sb = mounted->superblock;

	DWORD epb = units_per_block;
	DWORD eps = epb / sb->blockSize;
	DWORD ppb = mounted->pointers_per_block;
	DWORD pps = ppb / sb->blockSize;

	DWORD block_key 	 = index / epb;
	DWORD sector_key 	 = (index % epb) / eps;
	DWORD sector_shift = (index % epb) % eps;
	map->block_key = block_key;
	map->sector_key= sector_key;
	map->sector_shift = sector_shift;

	DWORD entry_block, index_block, offset;
	free(*buffer); *buffer = alloc_sector();

	// DIRECT
	if(block_key < 2){
		entry_block = rt->inode->dataPtr[block_key];
		map->data_block = entry_block;
		map->indirection_level = 0;
	}

	// SINGLE INDIRECTION
	else if (block_key < 2+ppb){
		index_block = rt->inode->singleIndPtr;
		map->single_pointer_to_block = index_block;
		map->indirection_level = 1;
		map->data_block = 0;

		if(index_block <= INVALID) return NOT_FOUND;

		offset = mounted->mbr_data->initial_sector + index_block * sb->blockSize;

		DWORD pointer_index = block_key - 2;
		DWORD pointer_sector = pointer_index / pps;
		map->single_pointer_index  = pointer_index;
		map->single_pointer_sector = pointer_sector;
		if(debugging) {
			printf("SINGLE|| Entry: %u \n", index);
			printf("NroPtr: %u | ", pointer_index);
			printf("SetorPtr %u | ItemPtr %u | ",pointer_sector,pointer_index%pps);
			printf("Vou ler offset: %u e setor %u\n", offset,pointer_sector);
		}
		map->single_sector_address = offset+pointer_sector;

		if(read_sector(offset+pointer_sector, *buffer) != SUCCESS)
			return(failed("Womp2 womp"));

		entry_block = (DWORD)(*buffer)[(pointer_index % pps)*DATA_PTR_SIZE_BYTES];
		map->single_buffer_index = (pointer_index % pps)*DATA_PTR_SIZE_BYTES;

		map->data_block = entry_block;

	}
	// DOUBLE INDIRECTION
	else {
		DWORD double_index_block = rt->inode->doubleIndPtr;
		map->double_pointer_to_block = double_index_block;
		map->indirection_level = 2;

		if(double_index_block <= INVALID) return NOT_FOUND;

		offset = mounted->mbr_data->initial_sector + double_index_block * sb->blockSize;
		DWORD pointer_index  = block_key - 2 - ppb;
		DWORD pointer_sector_dind = pointer_index/ppb/pps;

		map->double_pointer_index = pointer_index;
		map->double_pointer_sector = pointer_sector_dind;

		if(debugging){
			printf("DOUBLE||| Entry: %u \n", index);
			printf("NroPtrD: %u | ", pointer_index/ppb);
			printf("SetorPtrD %u | ItemPtrD %u \n",pointer_sector_dind,pointer_sector_dind % pps);
			printf("Vou ler offset: %u e setor %u\n", offset, pointer_sector_dind);
		}

		if(read_sector(offset+pointer_sector_dind, *buffer) != SUCCESS)
			return(failed("Womp3 womp"));

		map->double_sector_address = offset+pointer_sector_dind;
		index_block = (DWORD)(*buffer)[((pointer_index/ppb) % pps)*DATA_PTR_SIZE_BYTES];
		map->double_buffer_index = ((pointer_index/ppb) % pps)*DATA_PTR_SIZE_BYTES;
		map->single_pointer_to_block = index_block;

		if(index_block <= INVALID) return NOT_FOUND;

		offset = mounted->mbr_data->initial_sector + index_block * sb->blockSize;
		pointer_index = block_key - 2 - ppb - pointer_sector_dind*pps; //*ppb*epb
		DWORD pointer_sector = pointer_index / pps;
		map->single_pointer_index  = pointer_index;
		map->single_pointer_sector = pointer_sector;

		if(debugging){
			printf("NroPtrS: %u | ", pointer_index);
			printf("SetorPtrS %u | ItemPtrS %u \n",pointer_sector, pointer_index% pps);
			printf("Vou ler offset: %u e setor %u\n", offset, ((pointer_index%ppb) / pps));
		}
		if(read_sector(offset+((pointer_index%ppb) / pps), *buffer) != SUCCESS) return(failed("Womp4 womp"));

		map->single_sector_address = offset+((pointer_index%ppb) / pps);
		entry_block = (DWORD)(*buffer)[(pointer_index% pps)*DATA_PTR_SIZE_BYTES];
		map->single_buffer_index = (pointer_index% pps)*DATA_PTR_SIZE_BYTES;
		map->data_block = entry_block;
	}

	// READING SECTOR FROM FINAL BLOCK

	if(entry_block <= INVALID) return NOT_FOUND;

	// Offset in sectors
	offset = mounted->mbr_data->initial_sector;
	offset += (map->data_block*sb->blockSize);

	if(debugging && map->indirection_level == 0){
		printf("DIRECT| Entry: %u \n", index);
		printf("Bloco: %u | ", block_key);
		printf("Setor: %u | ", sector_key);
		printf("Item: %u \n", sector_shift);
		printf("Setor absoluto = %u\n", offset+sector_key);
		printf("Vou ler offset: %u e setor %u\n", offset, sector_key);
	}

	map->sector_address = offset+sector_key;
	//printf("\t--%d %d--\n", map->data_block, map->single_buffer_index);
	if(map->data_block <= INVALID) return NOT_FOUND;
	if(read_sector(offset+sector_key, *buffer) != SUCCESS) return(failed("WompLast womp"));

	return map->data_block;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para ler as entradas de um diretório.
-----------------------------------------------------------------------------*/
int readdir2 (DIRENT2 *dentry) {
	if (init() != SUCCESS) return(failed("ReadDir: failed to initialize"));

	if (!is_mounted()) return(failed("ReadDir failed: no partition mounted yet."));

	// if mounted, check if directory open. if not, open and read the first index.
	// otherwise read current entry IF VALID or the next valid one.
	// if end of directory reached, return a code.

	// each call to readdir returns a single entry.
	// then, ups the internal counter to the next entry for the next call.
	// return error code if:
	// can`t read it for some reason
	// no more valid entries in the open dir.
	if(!is_root_open()) {
		opendir2();
	}

	T_RECORD* rec = alloc_record(1);

	T_INODE* inode = alloc_inode(1);
	rec->TypeVal = 0x00;

	dentry->fileType = 0x00;
	strcpy(dentry->name, "\0");
	dentry->fileSize = 0;

	MAP* dummymap = blank_map();
	while(rec->TypeVal == 0x00) {
		if (mounted->root->entry_index >= mounted->root->total_entries) {
			//printf("Reached end of dir\n");
			free(rec);
			free(inode);
			free(dummymap);
			return -1;
		}

		map_index_to_record(mounted->root->entry_index, &rec, dummymap);

		if (rec->TypeVal != 0x00 && access_inode(rec->inodeNumber, inode)) {
			//printf("Couldn't load inode\n");
			free(rec);
			free(inode);
			free(dummymap);
			return -1;
		}

		mounted->root->entry_index++;
	}

	dentry->fileType = rec->TypeVal;
	dentry->fileSize = inode->bytesFileSize;
	strcpy(dentry->name, rec->name);

	free(rec);
	free(inode);
	free(dummymap);

	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para fechar um diretório.
-----------------------------------------------------------------------------*/
int closedir2 (void) {
	if (init() != SUCCESS) return(failed("CloseDir: failed to initialize"));

	if (!is_mounted()) return(failed("CloseDir: no partition mounted."));

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
	// BYTE* buffer = (BYTE*)malloc(sizeof(BYTE)*SECTOR_SIZE)
	// int i;
	// for (i = 0; i < SECTOR_SIZE; i++)
	// 	buffer[i] = 0;
	// for (i = 0; i < mounted->superblock->blockSize; i++)
	// 	write_sector(base_particao + setor_inicial + i, buffer);

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
	// ops acho que strlen nao conta o \0. MAX_FILENAME_SIZE é 50.
	strncpy(registro->name, linkname, strlen(linkname)+1);
	registro->inodeNumber = indice_inode;


	if( save_inode(indice_inode, inode) != SUCCESS) {
		if(set_bitmap_index(BITMAP_INODES, indice_inode, BIT_FREE) != SUCCESS)
			return failed("Failed to savenode + UNset bitmap node");
		if (set_bitmap_index(BITMAP_BLOCKS, indice_bloco, BIT_FREE))
			return failed("Failed to save node+ unset bitmap block");
	}

	if(new_record2(registro) != SUCCESS ){

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
	// apenas incrementa o contador de referencias
	inode->RefCounter += 1;
	update_inode(indice_inode, *inode);

	// inicializacao da entrada no dir
	T_RECORD* registro = blank_record();
	registro->TypeVal = TYPEVAL_REGULAR;
	strncpy(registro->name, linkname, strlen(filename)+1);
	registro->inodeNumber = indice_inode;

	if(new_record2(registro) != SUCCESS ){
		return FAILED;
	}
	return SUCCESS;
}
