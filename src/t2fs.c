
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

boolean t2fs_initialized = false;
boolean debugging = true;

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

	printf("FILENAME: %s\n", filename);
	printf("Filename size: %lu\n", sizeof(filename)/sizeof(char));
	printf("Filename length: %lu\n", strlen(filename));

	if(strlen(filename) > MAX_FILENAME_SIZE){
		return false;
	}
	int i;
	for(i=0; i < strlen(filename); i++){
		printf("Filename at i=%d: %x aka %c", i, filename[i], filename[i]);
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
	printf("LDRT 1\n");

	if (init() != SUCCESS) return(failed("Uninitialized"));
	if (!is_mounted())  return(failed("No partition mounted."));
	if (is_root_loaded()) return SUCCESS;

	printf("LDRT 2\n");


	BYTE* buffer = alloc_sector();
	printf("LDRT 3\n");

	if(read_sector(mounted->fst_inode_sector,  buffer) != SUCCESS)
		return(failed("LoadRoot failed"));

	T_INODE* dir_node = (T_INODE*)malloc(sizeof(T_INODE));
	if(access_inode(ROOT_INODE, dir_node)!=SUCCESS){
		return(failed("LoadRoot: Failed to access directory node"));
	}
	printf("LDRT 4\n");

	mounted->root =  (T_DIRECTORY*)malloc(sizeof(T_DIRECTORY));
	T_DIRECTORY* rt = mounted->root;

	printf("LDRT 5\n");

	rt->open = true; // TODO: nao acho que ja posso assumir isso
	rt->inode = dir_node ;
	rt->inode_index = ROOT_INODE;
	rt->entry_index = DEFAULT_ENTRY;
	rt->total_entries = 0; // TODO: errado, percorrer pelo node todos validos

	printf("LDRT 6\n");

	// Maximum number of ENTRY BLOCKS the d-node can hold.
	rt->max_entries = 2  						// direct pointers to Entry Nodes
			+ mounted->pointers_per_block // 1 single indirect pointer to block of direct pointers
			+ mounted->pointers_per_block*mounted->pointers_per_block; // 1 double indirect
	// Multiply by number of entries in a data block
	// to get no. of ENTRIES per d-node.
	printf("LDRT 7\n");

	rt->max_entries *= mounted->entries_per_block;
	printf("LDRT 8\n");

	rt->open_files = NULL; // TODO errado??
	rt->num_open_files = 0; // TODO errado??
	printf("LDRT 9\n");

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
		return failed("Fu"); }

	mounted = NULL;
	t2fs_initialized = true;
	return SUCCESS;
}

void report_superblock(){
	printf("********************************\n");
	printf("Superblock info for partition %d\n", mounted->id);
	T_SUPERBLOCK* sb = mounted->superblock;
	printf("Id: %.*s\n",4,sb->id); // Only the first 4 chars (there is no /0)
	printf("Version: %d\n",sb->version);
	printf("Superblock size(1 block, first in partition): %d\n",sb->superblockSize);
	printf("Free Blocks Bitmap Size(in blocks): %d\n",sb->freeBlocksBitmapSize);
	printf("Free Inodes Bitmap Size(in blocks): %d\n",sb->freeInodeBitmapSize);
	printf("Inode area size (in blocks): %d\n",sb->inodeAreaSize);
	printf("Block size (in sectors): %d\n",sb->blockSize);
	printf("Disk size of partition (in blocks): %d\n",sb->diskSize);
	printf("Checksum: %u\n", sb->Checksum);
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
	//sb->id[3] = bytes[0];
	//sb->id[2] = bytes[1];
	//sb->id[1] = bytes[2];
	//sb->id[0] = bytes[3];
	printf("BY2SPBK 1\n");

	sb->version =	to_int(&(bytes[4]), 2);
	sb->superblockSize = to_int(&(bytes[6]), 2);
	sb->freeBlocksBitmapSize = to_int(&(bytes[8]), 2);
	sb->freeInodeBitmapSize = to_int(&(bytes[10]), 2);
	printf("BY2SPBK 3\n");

	sb->inodeAreaSize = to_int(&(bytes[12]), 2);
	sb->blockSize = to_int(&(bytes[14]), 2);
	sb->diskSize = to_int(&(bytes[16]), 4);
	printf("BY2SPBK 7\n");

	sb->Checksum = to_int(&(bytes[20]), 4);
	printf("BY2SPBK 8\n");

	return SUCCESS;
}

int RECORD_to_DIRENTRY(T_RECORD* record, DIRENT2* dentry){
	if (record == NULL || dentry == NULL) return FAILED;

	memcpy(dentry->name, record->name, 51);
	dentry->fileType = (DWORD) record->TypeVal;

	// dentry->inodeNumber =

	// TODO: tamanho do arquivo, usando o inode number do RECORD_to_DIRENT
	//dentry->fileSize = (DWORD) ...
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

	printf("Byteman vs Superblock\n");
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

int initialize_superblock(int sectors_per_block) {
	// Gets pointer to superblock for legibility
	T_SUPERBLOCK* sb = (T_SUPERBLOCK*)malloc(sizeof(T_SUPERBLOCK));

	sb = mounted->superblock;

	printf("INIT_SB1\n");

	int fst_sect = mounted->mbr_data->initial_sector;
	int lst_sect  = mounted->mbr_data->final_sector;
	int num_sectors = lst_sect - fst_sect + 1;
	printf("INIT_SB2\n");


	//strncpy(sb->id, "T2FS", 4);
	memcpy(sb->id, (char*)"T2FS", 4);

	printf("INIT_SB3\n");

	sb->version = 0x7E32;
	printf("INIT_SB4\n");

	sb->superblockSize = 1;
	sb->blockSize = (WORD)sectors_per_block;
	printf("INIT_SB5\n");

	//Number of logical blocks in formatted disk.
	sb->diskSize = (WORD)ceil(num_sectors/(float)sectors_per_block);
	printf("INIT_SB6\n");

	printf("num sectors %d\n", num_sectors);
	printf("num sectors per block %d\n", sectors_per_block);
	printf("disksize %d\n", sb->diskSize);

	// 10% of partition blocks reserved to inodes (ROUND UP)
	sb->inodeAreaSize = (WORD)(ceil(0.10*sb->diskSize));
	printf("inode Area Size %d\n", sb->inodeAreaSize);

	printf("INIT_SB7\n");


	/* ************* BITMAPS ************* */

	// Total number of inodes is how many we fit into its area size.
	// inodeAreaSize in bytes divided by inode size in bytes.
	mounted->total_inodes = sb->inodeAreaSize*sectors_per_block*disk_mbr.sector_size;
	mounted->total_inodes /= sizeof(T_INODE);

	printf("INIT_SB8\n");

	// inode bitmap size: 1 bit per inode given "X" inodes per block
	float inode_bmap = (float)mounted->total_inodes;
	// 1 bit per inode, now converted to number of blocks rounding up.
	inode_bmap /= (float)(8*disk_mbr.sector_size*sectors_per_block);
	sb->freeInodeBitmapSize = (WORD) ceil(inode_bmap);

	printf("INIT_SB9\n");

	// Total number of data blocks is dependent on size of its own bitmap!
	float data_blocks = sb->diskSize - sb->inodeAreaSize - sb->superblockSize;
	data_blocks -= sb->freeInodeBitmapSize;

	printf("INIT_SB10\n");

	// block bitmap size is dependent on how many blocks are left after the bitmap.
	// therefore it is equal to current surviving blocks div by (8*bytes per block)+1
	sb->freeBlocksBitmapSize = (WORD)
		ceil(data_blocks / (float)(1 + 8*disk_mbr.sector_size*sectors_per_block));

	mounted->total_data_blocks = data_blocks - sb->freeBlocksBitmapSize;

	// TODO: revisar tudo isso
	printf("diskSize (of partition) in blocks %d\n", sb->diskSize);
	printf("freeBlocksBitmapSize %d\n", sb->freeBlocksBitmapSize);
	printf("freeInodeBitmapSize %d\n", sb->freeInodeBitmapSize);
	printf("inodeAreaSize %d\n", sb->inodeAreaSize);
	calculate_checksum(sb);
	printf("CHECKSUM %u\n", sb->Checksum);

	printf("Total VALID inodes: %d\n", mounted->total_inodes);
	printf("Total VALID data blocks: %d\n", mounted->total_data_blocks);
	printf("Total theoretical data blocks: %d\n", sb->freeBlocksBitmapSize*sb->blockSize*SECTOR_SIZE*8);

	print("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	report_superblock();
	print("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");

	save_superblock();
	return SUCCESS;
}

int save_superblock() {
	// Gets pointer to application-space superblock
	T_SUPERBLOCK* sb = mounted->superblock;
	BYTE* output = alloc_sector();

	memcpy(output, sb->id, 4);
	//output[0] = sb->id[3];
	//output[1] = sb->id[2];
	//output[2] = sb->id[1];
	//output[3] = sb->id[0];
	strncpy((char*)&(output[4]), (char*)WORD_to_BYTE(sb->version, 2),2);
	strncpy((char*)&(output[6]), (char*)WORD_to_BYTE(sb->superblockSize, 2),2);
	strncpy((char*)&(output[8]), (char*)WORD_to_BYTE(sb->freeBlocksBitmapSize, 2),2);
	strncpy((char*)&(output[10]),(char*)WORD_to_BYTE(sb->freeInodeBitmapSize, 2),2);
	strncpy((char*)&(output[12]),(char*)WORD_to_BYTE(sb->inodeAreaSize, 2),2);
	strncpy((char*)&(output[14]),(char*)WORD_to_BYTE(sb->blockSize, 2),2);
	strncpy((char*)&(output[16]),(char*)DWORD_to_BYTE(sb->diskSize, 4),4);
	strncpy((char*)&(output[20]),(char*)DWORD_to_BYTE(sb->Checksum, 4),4);

	if (write_sector(mounted->mbr_data->initial_sector, output) != SUCCESS) {
		return failed("Failed to write superblock to partition sector");
	}
	else return SUCCESS;
}

int load_superblock() {
	if (!is_mounted()) return(failed("Unmounted."));

	mounted->superblock = (T_SUPERBLOCK*) malloc(sizeof(T_SUPERBLOCK));
	BYTE* buffer = alloc_sector();
	printf("LSB1\n");

	printf("->Reading sector %d (first in partition)\n", mounted->mbr_data->initial_sector);

	if(read_sector(mounted->mbr_data->initial_sector, buffer)!= SUCCESS) return(failed("failed reading sb"));
	printf("LSB2\n");

	printf("->Sector in hex:\n");
	int i;
	for (i = 0; i < sizeof(T_SUPERBLOCK); i++){
		printf("%x ", buffer[i]);
	}
	printf("\n");
	print("->Conversion Sector to Superblock:\n");
	print("-->Conversion using BYTE_to_SUPERBLOCK:\n");
	if(BYTE_to_SUPERBLOCK(buffer,mounted->superblock) !=SUCCESS) return FAILED;
	print("--Resulting structure:\n");
	report_superblock();

	//print("-->Conversion using memcpy:\n");
	//memcpy(mounted->superblock, buffer, sizeof(T_SUPERBLOCK));
	//print("--Resulting structure:\n");
	//report_superblock();

	//print("--->Conclusão, disco bugado sei la\n");
	printf("LoadSB-Final\n");


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

void calculate_checksum(T_SUPERBLOCK* sb) {
	// A superblock's checksum is the 1-complement of a sum of 5 integers.
	// Each integer is 4 bytes unsigned little-endian.
	BYTE temp4[4];
	DWORD checksum = to_int((BYTE*)sb->id, 4);
	// Version + superblockSize (2 bytes each, both little endian)
	strncpy((char*)&(temp4[0]), (char*)WORD_to_BYTE(sb->version, 2), 2);
	strncpy((char*)&(temp4[2]), (char*)WORD_to_BYTE(sb->superblockSize,2), 2);
	checksum += to_int(temp4, 4);
	// freeBlocksBitmapSize + freeInodeBitmapSize (2B each)
	strncpy((char*)&(temp4[0]), (char*)WORD_to_BYTE(sb->freeBlocksBitmapSize, 2), 2);
	strncpy((char*)&(temp4[2]), (char*)WORD_to_BYTE(sb->freeInodeBitmapSize,2), 2);
	checksum += to_int(temp4, 4);
	// inodeAreaSize + blockSize (2B each)
	strncpy((char*)&(temp4[0]), (char*)WORD_to_BYTE(sb->inodeAreaSize, 2), 2);
	strncpy((char*)&(temp4[2]), (char*)WORD_to_BYTE(sb->blockSize,2), 2);
	checksum += to_int(temp4, 4);
	// diskSize (a DWORD of 4 Bytes)
	checksum += sb->diskSize;
	// One's complement
	sb->Checksum = ~checksum;
}

int initialize_inode_area(){

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

int initialize_bitmaps(){
	T_SUPERBLOCK* sb = mounted->superblock;

	// Allocates both Bitmaps for the currently mounted partition.
	// Output: success/error code
	// OpenBitmap receives the sector with the superblock.
	if(openBitmap2(mounted->mbr_data->initial_sector) != SUCCESS){
		return failed("OpenBitmap: Failed to allocate bitmaps in disk");
	}

	// Set inode bits to FREE,
	// except for root (inode #0, currently no data blocks)
	if(setBitmap2(BITMAP_INODES, ROOT_INODE, BIT_OCCUPIED) != SUCCESS)
		return(failed("Init SetBitmaps fail 1."));

	int valid_nodes = mounted->total_inodes;
	//int theoretical_nodes = sb->freeInodeBitmapSize*sb->blockSize*SECTOR_SIZE*8;

	int bit;
	printf("Free inode bits range: %d-%d\n", ROOT_INODE+1, valid_nodes);
	for (bit = ROOT_INODE+1; bit < valid_nodes; bit++){
		// Each bit after root is set to FREE
		if(setBitmap2(BITMAP_INODES, bit, BIT_FREE) != SUCCESS) {
			printf("bit ruim %d\n", bit);
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
	int valid_blocks = mounted->total_data_blocks;
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
	printf("Free data bits range: %d-%d\n", pre_data_blocks, data_blocks_limit);
	for (bit= pre_data_blocks; bit < data_blocks_limit; bit++) {
		// total VALID blocks (addressable by the API)
		if(setBitmap2(BITMAP_BLOCKS, bit, BIT_FREE) != SUCCESS) {
			printf("bit ruim %d\n", bit);
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
	inode->blocksFileSize = to_int(&(sector_buffer[0]), 4);
	inode->bytesFileSize = 	to_int(&(sector_buffer[4]), 4);
	inode->dataPtr[0] = 		to_int(&(sector_buffer[8]), 4);
	inode->dataPtr[1] = 		to_int(&(sector_buffer[12]), 4);
	inode->singleIndPtr = 	to_int(&(sector_buffer[16]), 4);
	inode->doubleIndPtr = 	to_int(&(sector_buffer[20]), 4);
	inode->RefCounter = 		to_int(&(sector_buffer[24]), 4);
	inode->reservado = 			to_int(&(sector_buffer[28]), 4);

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

int save_inode(DWORD index, T_INODE* inode){
  // Validation
	if(index < FIRST_VALID_BIT) return(failed("Invalid inode index"));
	if(inode == NULL) return(failed("Invalid inode pointer."));

	DWORD sector = map_inode_to_sector(index);
	BYTE* buffer = (BYTE*) alloc_inode(INODES_PER_SECTOR);

	if(read_sector(sector, buffer)!=SUCCESS)
		return(failed("SaveInode: Failed to read sector"));

	DWORD offset = (index % INODES_PER_SECTOR)*INODE_SIZE_BYTES;
	memcpy(&(buffer[offset+sector]), inode, INODE_SIZE_BYTES);

	if(write_sector(sector, buffer) != SUCCESS) {
		return(failed("SaveInode, WriteSector failed "));}

	else {
		if(set_bitmap_index(BITMAP_INODES, index, BIT_OCCUPIED) == SUCCESS)
			return SUCCESS;
		else return FAILED;
	}
}
		// used to be part of save new node
		// int* block_indexes = (int*) malloc((2+inode->blocksFileSize)*sizeof(int));
		//
		// int count_free =  0 ;
		// while(count_free < inode->blocksFileSize) {
		//
		// 	int code_blocks = searchBitmap2(BITMAP_INODES, BIT_FREE);
		// 	if (code_blocks < 0 ) {return(failed("Failed blocks bitmap search."));}
		// 	if (code_blocks == 0) {return(failed("Data bitmap full. Cannot write inode.")); }
		//
		// 	// Collect all indexes in a list
		// 	block_indexes[count_free] = code_blocks;
		// 	count_free++;
		// }

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

//writes new record to the MFD

// TODO: averiguar isso,
// write2 tem que ter um comportamento diferenciado para diretorio
// ou para arquivo ou para softlink etc  .
int save_record(T_RECORD* rec){
	//char* buffer = (char*) malloc(sizeof(T_RECORD));
	//memcpy(buffer, rec, sizeof(T_RECORD));
	return write2(ROOT_INODE, (char*)rec, sizeof(T_RECORD));
}


int new_file(char* filename, T_INODE* inode){

	if( !is_valid_filename(filename)) return(failed("New_file: Invalid Filename."));

	free(inode);
	inode = blank_inode();

	DWORD inode_index = next_bitmap_index(BITMAP_INODES, BIT_FREE);
	if(inode_index == NOT_FOUND)
		return(failed("No inodes free"));
	else if(inode_index < FIRST_VALID_BIT)
		return(failed("Failed bitmap query."));

	if(save_inode(inode_index, inode) != SUCCESS)
		return(failed("[NewFile]Failed to save inode."));

	// new file - record creation
	T_RECORD* rec = blank_record();
	rec->inodeNumber  = inode_index;
	rec->TypeVal 			= TYPEVAL_REGULAR;
	strncpy(rec->name, filename, strlen(filename));

	//adds record to root directory
	if(save_record(rec) != SUCCESS) return(failed("NewFile: Failed to save record"));

	return SUCCESS;
}

int set_file_open(T_INODE* file_inode){

	if (!is_mounted())   return(failed("SetFileOpen failed 1"));
	if (!is_root_open()) return(failed("SetFileOpen failed 2"));
	if(mounted->root->open_files == NULL){
		return(failed("SetFileOpen failed 3"));
	}

	T_FOPEN* fopen = mounted->root->open_files;

	if (mounted->root->num_open_files >= MAX_FILES_OPEN){
		print("Maximum number of open files reached.");
		return FAILED;
	}
	int i;
	for(i=0; i < MAX_FILES_OPEN; i++){
		// check if position is not occupied by another file
		if(fopen[i].inode == NULL){

			printf("Adicionei arquivo na posicao: %d\n", i);
			fopen[i].handle = i;
			fopen[i].inode 	= file_inode;
			fopen[i].current_pointer = 0;
			fopen[i].inode_index = -1;
			// TODO!!! tem que achar o inode index junto com o inode
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
	if (!is_root_open()) return(failed("SetFileClose failed 2"));
	if(mounted->root->open_files == NULL){return(failed("SetFileClose failed 3"));}

	T_FOPEN* fopen = mounted->root->open_files;

	if(fopen[handle].inode == NULL) {
		print("File handle does not correspond to an open file.");
	}
	else mounted->root->num_open_files--;

	// In either case, overwriting fopen data to make sure.
	fopen[handle].inode 		= NULL;
	fopen[handle].current_pointer = 0;
	fopen[handle].inode_index 	=	-1;
	// TODO nao sei se botamos o handle -1 tbm só pra garantir

	return SUCCESS;
}

int init_open_files(){

	// if(!is_mounted()) return FAILED;
	// if(!is_root_loaded()) return(failed("Root not loaded prev. to init fopen"));

	printf("Openfiles 0\n");

	if(mounted->root->open_files == NULL){
		mounted->root->open_files =
			(T_FOPEN*) malloc(MAX_FILES_OPEN*sizeof(T_FOPEN));
	}

	printf("Openfiles 1\n");
	// T_FOPEN* fopen = mounted->root->open_files;
	int i;
	for(i=0; i < MAX_FILES_OPEN; i++){
		mounted->root->open_files[i].inode = NULL;
		mounted->root->open_files[i].current_pointer = 0;
		mounted->root->open_files[i].handle = i; // TODO: ou entao botar um handle invalido ate ser usado
	}
	printf("Openfiles 2\n");

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
	printf("F1\n");
	// Mount partition first.
	mount(partition);
	// Initialize the partition starting area.
	if(initialize_superblock(sectors_per_block) != SUCCESS)
		return failed("Failed to read superblock.");
	printf("F2\n");
	if(initialize_inode_area() != SUCCESS)
		return(failed("Format2: Failed to initialize inode area"));
	printf("F3\n");

	if(initialize_bitmaps() != SUCCESS)
		return(failed("Format2: Failed to initialize bitmap area"));
	printf("F4\n");

	umount(); // TODO: verificar se mantem montado apos formatacao
	// mas acho que deve desmontar.

	// Afterwards: the rest is data blocks.
	// first block onwards after inodes is reserved to
	// file data, directory files, and index blocks for big files.
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
	load_superblock();
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

	opendir2();

	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Desmonta a partição atualmente montada, liberando o ponto de montagem.
-----------------------------------------------------------------------------*/
int umount(void) {

	if(mounted == NULL){
		return(failed("No partition to unmount."));
	}
	printf("Unm1\n");

	if(closedir2() != SUCCESS){
		return(failed("Unmount failed: could not close root dir."));
	}
	printf("Unm2\n");

	mounted->id = -1;
	free(mounted->root);
	free(mounted->superblock);
	mounted->root = NULL;
	mounted->superblock = NULL;

	printf("Unm3\n");

	free(mounted);
	mounted = NULL;

	printf("Unmnt. Last\n");
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

	printf("Create 1\n");

	DWORD inode_index = next_bitmap_index(BITMAP_INODES, BIT_FREE);
	if(inode_index == NOT_FOUND)
		return(failed("No inodes free"));
	else if(inode_index < FIRST_VALID_BIT)
		return(failed("Failed bitmap query."));

	T_RECORD* record = alloc_record(1);

	printf("Create 2\n");

	if(find_entry(filename, record) == SUCCESS){
		return failed("Filename already exists.");
	}

	printf("Create 3\n");

	T_INODE* inode = alloc_inode(1);
	if(new_file(filename, inode) != SUCCESS)
		return failed("Create: Failed to create new file");
	// atualizar bitmap
	printf("Create 4\n");

	free(record);
	record = blank_record();
	int size = strlen(filename)+1;
	memcpy(record->name, filename, size);
	record->TypeVal = TYPEVAL_REGULAR;
	record->inodeNumber = inode_index;
	// revisar.

	printf("Create Last\n");

	FILE2 handle = set_file_open(inode);
	return handle;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para remover (apagar) um arquivo do disco.
-----------------------------------------------------------------------------*/
int delete2 (char *filename) {
	if (init() != SUCCESS) return(failed("delete2: failed to initialize"));
	if(!is_mounted()) return(failed("No partition mounted."));
	if(is_root_open() != SUCCESS) return(failed("Could not open MFD."));
	if(!is_valid_filename(filename)) return(failed("Delete: Filename invalid"));

	T_RECORD* record = alloc_record(1);
	if(find_entry(filename, record) != SUCCESS){
		// TODO: talvez retornar SUCCESS,
		// pois o delete foi trivialmente feito e nada "falhou"
		return(failed("Delete: File does not exist."));
	}

	//remove_file_content(record->inodeNumber);
	remove_record(filename);
	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Função que abre um arquivo existente no disco.
-----------------------------------------------------------------------------*/
FILE2 open2 (char *filename) {
	if (init() != SUCCESS) return(failed("open2: failed to initialize"));
	if(!is_mounted()) return(failed("No partition mounted."));
	if(!is_root_open()) return(failed("Directory must be opened."));

	if(mounted->root->inode==NULL)
		return FAILED;

	// TODO: modify sweep_root to return a RECORD instead of a DIRENT
	// which has inodeNumber, to be used with access_inode to get the node.
	//T_INODE* f = search_root_for_filename(filename);
	//if(f==NULL)
		//return FAILED;

	// TODO: get inode from filename then open
	//FILE2 handle = set_file_open(f);
	FILE2 handle = 0;
	return handle;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para fechar um arquivo.
-----------------------------------------------------------------------------*/
int close2 (FILE2 handle) {
	if (init() != SUCCESS) return(failed("close2: failed to initialize"));
	if(!is_mounted()) return(failed("No partition mounted."));
	if(!is_root_open()) return(failed("Directory must be opened."));

	return set_file_close(handle);
}

/*-----------------------------------------------------------------------------
Função:	Função usada para realizar a leitura de uma certa quantidade
		de bytes (size) de um arquivo.
-----------------------------------------------------------------------------*/
int read2 (FILE2 handle, char *buffer, int size) {

	// read:
	// given open file handle and number of bytes,
	// find file inode, read bytes from data block at the
	// file`s CURRENT POINTER, and save into the buffer.
	// update current pointer


	// alternate behaviors:
	// SOFTLINK:
	// read its block with source file name.
	// find file name and inode in directory.
	// load correct block with current pointer (SOFT or ORIGINAL??)
	// read bytes into buffer
	// HARDLINK:
	// get pointer to original file inode.
	// treat as a regular file.




	return -1;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para realizar a escrita de uma certa quantidade
		de bytes (size) de  um arquivo.
-----------------------------------------------------------------------------*/
int write2 (FILE2 handle, char *buffer, int size) {

	//write:
	// open file handle and number of bytes.
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
	// therefore: always check whether current pointer under the "size in bytes" in the inode of file.

	// alternate behavior:
	// softlink: finds original file by name then do as above.
	// hardlink: get original file by inode then same.
	// update hardlink with size etc too.

	
	return -1;
}

/*-----------------------------------------------------------------------------
Função:	Função que abre um diretório existente no disco.
-----------------------------------------------------------------------------*/
int opendir2 (void) {
	if (init() != SUCCESS) return(failed("OpenDir: failed to initialize"));

	if(!is_mounted()) return(failed("No partition mounted yet."));

	printf("OpD 1\n");
	// Get mounted partition, load root directory, set entry to zero.
	if(!is_root_loaded())
		load_root();

	printf("OpD 2\n");

	mounted->root->open = true;
	mounted->root->entry_index = 0;
	printf("OpD 3\n");

	//  Initialize open files
	if(init_open_files() != SUCCESS) {
		return failed("Failed to initialize open files");	}

  printf("OpDir final\n");

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
		return(failed("AccessDirectory: Failed to read dir sector."));
	}
	if(BYTE_to_INODE(buffer, inode_index , return_inode) != SUCCESS)
		return(failed("Failed BYTE to INODE translation"));
	return SUCCESS;
}

int find_entry_in_block(DWORD entry_block, char* filename, T_RECORD* record) {
	if(entry_block < 1) return FAILED;
	// Helpful pointer
	T_SUPERBLOCK* sb = mounted->superblock;

	BYTE* buffer = alloc_sector();
	char* entry_name = (char*)malloc(sizeof(char)*(MAX_FILENAME_SIZE+1));

	int 	entry_size = sizeof(T_RECORD);
	int 	entries_per_sector = mounted->entries_per_block / sb->blockSize;
	int 	total_sects = sb->blockSize;
	DWORD 	offset = entry_block * sb->blockSize;
	int 	sector;
	int 	e;

	// We were given a block, now read sector by sector.
	for (sector = 0; sector < total_sects; sector++){
		if(read_sector(offset + sector, buffer) != SUCCESS){
			printf("Sweep: failed to read sector %d of block %d", sector, entry_block);
			return(failed("Sweep: failed to read a sector"));
		}
		// Buffer (sector of an entry block) now holds about 4 entries.
		for (e = 0; e < entries_per_sector; e++) {
			// EACH ENTRY has a byte for Type then 51 bytes for name.
			BYTE* buffer_name = &(buffer[e * entry_size + 1]);
			strncpy((char*) entry_name,(char*) buffer_name, (MAX_FILENAME_SIZE+1));

			if (strcmp(entry_name, filename) == 0){
				memcpy(record, &(buffer[e*entry_size]), sizeof(T_RECORD));
				printf("ACHOU A ENTRADA\nFilename: %s\nInode number: %d\n", record->name, record->inodeNumber);
				return 1;
			}
		}
	}
	return 0;//NOT FOUND
}

int find_indirect_entry(DWORD index_block, char* filename, T_RECORD* record){

	if(index_block < 1) return FAILED;
	T_SUPERBLOCK* sb = mounted->superblock;

	int i, s;
	int total_sects = sb->blockSize;
	DWORD offset = index_block * sb->blockSize;
	BYTE* buffer = alloc_sector();
	int total_ptrs = SECTOR_SIZE / DATA_PTR_SIZE_BYTES;
	DWORD entry_block;

	for(s=0; s < total_sects; s++ ){
		if(read_sector(offset + s, buffer) != SUCCESS){
			printf("Sweep: failed to read sector %d of block %d", s, index_block);
			return(failed("Sweep: failed to read a sector"));
		}

		for(i=0; i<total_ptrs; i++){
			// Each pointer points to an ENTRY BLOCK.
			entry_block = to_int(&(buffer[i*DATA_PTR_SIZE_BYTES]), DATA_PTR_SIZE_BYTES);
			if(find_entry_in_block(entry_block, filename, record) > 0){
				printf("ACHOU A ENTRADA INDIRETAMENTE\nFilename: %s\nInode number: %d\n", record->name, record->inodeNumber);
				return 1;
			}
		}
	}
	return 0;
}

//TODO

// input: filename and empty record structure
// output: success code. if found, dentry holds the dir entry
int find_entry(char* filename, T_RECORD* record) {
	// Validation
	if(!is_mounted()) return FAILED;
	if(!is_root_open()) return FAILED;
	if(mounted->root->total_entries == 0) return FAILED;
	if(mounted->root->inode == NULL) return failed("bad inode ptr");
	if(!is_valid_filename(filename)) return failed("Filename invalid.");
	// Helpful pointers
	T_SUPERBLOCK* sb = mounted->superblock;
	T_DIRECTORY* 	rt = mounted->root;

	free(record);
	record = alloc_record(1);
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
		if(find_entry_in_block(entry_block, filename, record) > NOT_FOUND){
			print("Found entry successfully");
			return SUCCESS;
		}
	}

	// INDIRECT POINTER TO INDEX BLOCKS
	DWORD index_block = rt->inode->singleIndPtr;
	if(index_block > INVALID) {
		// Valid index
		if(find_indirect_entry(index_block, filename, record) > NOT_FOUND){
			print("Found entry successfully");
			return SUCCESS;
		}
	}

	// DOUBLE INDIRECT POINTER
	index_block = rt->inode->doubleIndPtr;
	int total_ptrs = SECTOR_SIZE / DATA_PTR_SIZE_BYTES;
	offset = index_block * sb->blockSize;
	DWORD inner_index_block;

	if(index_block > INVALID) {
		// Valid index
		for(s=0; s < total_sects; s++){
			if(read_sector(offset + s, buffer) != SUCCESS){
				printf("Sweep: failed to read sector %d of block %d", s, index_block);
				return(failed("Sweep: failed to read a sector"));
			}
			for(i=0; i<total_ptrs; i++){
				// Each pointer --> a block of indexes to more blocks.
				inner_index_block = (DWORD)buffer[i*DATA_PTR_SIZE_BYTES];

				if(find_indirect_entry(inner_index_block, filename, record) > NOT_FOUND){
					print("Found entry successfully");
					return SUCCESS;
				}
			}
		}
	}
	printf("Filename %s not found in root\n", filename);
	return FAILED;
}


int map_index_to_record(DWORD index, T_RECORD* record) {

	if(!is_mounted()) 	return(failed("MITR no partition mounted yet."));
	if(!is_root_open()) return(failed("MITR root not even loaded"));

	T_DIRECTORY*  rt = mounted->root;
	T_SUPERBLOCK* sb = mounted->superblock;

	if(index < 0) 							 		 return(failed("MITR bad negative index"));
	if(index >= rt->max_entries) 		 return(failed("MITR bad positive index"));
	if(rt->inode->dataPtr<(DWORD*)1) return(failed("MITR boo"));

	DWORD epb = mounted->entries_per_block;
	DWORD eps = epb / sb->blockSize; // TODO :isso ta certo?
	DWORD ppb = mounted->pointers_per_block;
	DWORD pps = ppb / sb->blockSize;

	DWORD block_key 	 = index / epb;
	DWORD block_shift  = index % epb;
	DWORD sector_key 	 = block_shift / eps;
	DWORD sector_shift = block_shift % eps;

	DWORD entry_block;
	DWORD index_block;
	DWORD offset;
	BYTE* buffer = alloc_sector();
	// Boundaries:
	// 1 -> dataPtr 1
	// 2 -> dataPtr 2
	// 3 to (numPointersPerBlock*numEntriesPerBlock-1)
	// numPointersPerBlock*numEntriesPerBlock to max_entries.
	if(block_key < 2){
		entry_block = rt->inode->dataPtr[block_key];
		offset = mounted->mbr_data->initial_sector + entry_block * sb->blockSize;

		if(debugging){
			printf("DIRECT| Entry: %u \n", index);
			printf("Bloco: %u | ", block_key);
			printf("Setor: %u | ", sector_key);
			printf("Item: %u \n", sector_shift);
			printf("Setor absoluto = %u\n", offset+sector_key);
		}

		if(read_sector(offset+sector_key, buffer) != SUCCESS) return(failed("Womp womp"));

		memcpy(record, &(buffer[sector_shift*ENTRY_SIZE_BYTES]), sizeof(T_RECORD));
		return SUCCESS;
	}
	else if (block_key < 2+ppb){
		index_block = rt->inode->singleIndPtr;
		offset = mounted->mbr_data->initial_sector + index_block * sb->blockSize;
		DWORD pointer_index = block_key - 2;
		DWORD pointer_sector = pointer_index / pps;
		if(read_sector(offset+pointer_sector, buffer) != SUCCESS) return(failed("Womp2 womp"));


		entry_block = buffer[(pointer_index % pps)*DATA_PTR_SIZE_BYTES];
		//printf("ENTRY BLOCK after indirection: %x\n", entry_block);
		offset = mounted->mbr_data->initial_sector + entry_block * sb->blockSize;

		if(read_sector(offset+sector_key, buffer) != SUCCESS) return(failed("Womp40 womp"));
		if(debugging){
			printf("SINGLE|| Entry: %u \n", index);
			printf("NroPtr: %u | ", pointer_index);
			printf("SetorPtr %u | ItemPtr %u | ",pointer_sector,pointer_index%pps);
			printf("Bloco %u | ", block_key);
			printf("Setor: %u | ", sector_key);
			printf("Item: %u\n", sector_shift);
			printf("Setor absoluto = %u\n", offset+sector_key);
		}

		memcpy(record, &(buffer[sector_shift*ENTRY_SIZE_BYTES]), sizeof(T_RECORD));
		return SUCCESS;

	}

	else {
		DWORD double_index_block = rt->inode->doubleIndPtr;
		offset = mounted->mbr_data->initial_sector + double_index_block * sb->blockSize;
		DWORD pointer_index  = block_key - 2 - ppb;

		//DWORD pointer_sector_dind = pointer_index/(ppb*ppb) / pps;
		DWORD pointer_sector_dind = pointer_index/ppb/pps;
		if(debugging){
			printf("DOUBLE||| Entry: %u \n", index);
			printf("NroPtrD: %u | ", pointer_index/ppb);
			printf("SetorPtrD %u | ItemPtrD %u \n",pointer_sector_dind,pointer_sector_dind % pps);
		}
		printf("Vou ler offset: %u e setor %u\n", offset, pointer_sector_dind);

		if(read_sector(offset+pointer_sector_dind, buffer) != SUCCESS) return(failed("Womp3 womp"));
		index_block = (DWORD)buffer[((pointer_index/ppb) % pps)*DATA_PTR_SIZE_BYTES];

		offset = mounted->mbr_data->initial_sector+ index_block * sb->blockSize;
		pointer_index = block_key - 2 - ppb - pointer_sector_dind*pps; //*ppb*epb
		DWORD pointer_sector = pointer_index / pps;

		if(debugging){
			printf("NroPtrS: %u | ", pointer_index);
			printf("SetorPtrS %u | ItemPtrS %u \n",pointer_sector, pointer_index% pps);
		}
		//if(read_sector(offset+pointer_sector%ppb, buffer) != SUCCESS) return(failed("Womp4 womp"));
		printf("Vou ler offset: %u e setor %u\n", offset, ((pointer_index%ppb) / pps));
		if(read_sector(offset+((pointer_index%ppb) / pps), buffer) != SUCCESS) return(failed("Womp4 womp"));

		// TODO: Bug here?
		entry_block = (DWORD)buffer[(pointer_index% pps)*DATA_PTR_SIZE_BYTES];
		//memcpy entryblock dword <-buffer uchars
		offset = mounted->mbr_data->initial_sector+ entry_block * sb->blockSize;
		if(debugging){
			printf("Bloco %u | ", block_key);
			printf("Setor: %u | ", sector_key);
			printf("Item: %u\n", sector_shift);
			printf("Setor absoluto = %u\n", offset+sector_key);
		}

		printf("Vou ler offset: %u e setor %u\n", offset, sector_key);

		if(read_sector(offset+sector_key, buffer) != SUCCESS) return(failed("Womp Womp 9000"));


		memcpy(record, &(buffer[sector_shift*ENTRY_SIZE_BYTES]), sizeof(T_RECORD));

		return SUCCESS;
	}
	return FAILED;
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
	//T_DIRECTORY* rt = mounted->root;

	// map index entry_index
	// get entry as a DIRENT2
	// etc

	mounted->root->entry_index++;
	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para fechar um diretório.
-----------------------------------------------------------------------------*/
int closedir2 (void) {
	if (init() != SUCCESS) return(failed("CloseDir: failed to initialize"));

	if (!is_mounted()) return(failed("CloseDir: no partition mounted."));

	if (!is_root_open()) {
		print("CloseDir: root directory was already unopened.");
	}

	/*
	for (int i ; i < MAX_FILES_OPEN; i++){
		if (mounted->root->open_files[i] != invalido/null){
			//close file still open, saving back modifications etc.
		}
	}
	mounted->root->open = false;
	// DESALOCAR O ROOT na real (unload root or something)
	depois
	mounted->root = NULL ;
	*/
	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para criar um caminho alternativo (softlink)
-----------------------------------------------------------------------------*/
int sln2 (char *linkname, char *filename) {

	if (init() != SUCCESS) return failed("sln2: failed to initialize");
	if (!is_mounted()) return failed("No partition mounted.");
	if (!is_root_open()) return failed("Directory must be opened.");
	if (!is_valid_filename(filename)) return(failed("Filename not valid."));
	if (!is_valid_filename(linkname)) return(failed("Linkname not valid."));

	int indice_inode = next_bitmap_index(BITMAP_INODES, BIT_FREE);
	if (indice_inode == NOT_FOUND) return failed("Inode Bitmap full.");
	else if (indice_inode < FIRST_VALID_BIT) return failed("Inode Bitmap op1 failed.");

	int indice_bloco = next_bitmap_index(BITMAP_BLOCKS, BIT_FREE);
	if (indice_bloco == NOT_FOUND) return failed("Failed to read block.");
	else if (indice_bloco < FIRST_VALID_BIT) return failed("Block bitmap op1 failed.");

	// IMPORTANTE, testar disponibilidade de inode e bloco antes de qqr escrita
	// Para fins de sync, acho bom tbm setar o bit occupied primeiro,
	// depois tentar escrever em disco, e se a escrita falhar des-setar o bit.

	T_RECORD* record = alloc_record(1);

	// if file 'linkname' already exists
	if (find_entry(linkname, record) == SUCCESS){
		printf("File %s already exists.\n", linkname);
		return FAILED;
	}

	// if file 'filename' doesnt exist
	if (find_entry(filename, record) != SUCCESS){
		printf("File %s doesn't exist.\n", filename);
		return FAILED;
	}

	// reserva bloco para o arquivo
	// TODO: como essa funcao ja abre e salva o bitmap, estou
	// repensando se vale a pena (em vez de so salvar no fim de tudo quando deu certo)
	if(set_bitmap_index(BITMAP_BLOCKS, indice_bloco, BIT_OCCUPIED) != SUCCESS)
		return failed("Failed to set blocks bitmap");
	if(set_bitmap_index(BITMAP_INODES, indice_inode, BIT_OCCUPIED) != SUCCESS)
		return failed("Failed to set bitmap");


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
	memcpy(buffer, filename, sizeof(char)*strlen(filename));

	// escreve o bloco no disco
	// TODO: ta escrevendo apenas um setor
	// -> nao é necessariamente ruim se for garantidamente o primeiro setor,
	// pois o setor tem 256 bytes e o filename tem 51 no max.
	if (write_sector(base_particao + setor_inicial, buffer) != SUCCESS) {

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
	// TODO: acho que RefCounter != 0 é só para arquivos originais
	// que sao apontados por hardlinks.

	// inicializacao da entrada no dir
	T_RECORD* registro = blank_record();
	registro->TypeVal = TYPEVAL_LINK;
	if (strlen(linkname) > MAX_FILENAME_SIZE) return failed("Linkname is too big.");
	// 51 contando o /0 da string
	// ops acho que strlen nao conta o \0. MAX_FILENAME_SIZE é 50.
	strncpy(registro->name, linkname, strlen(linkname)+1);
	registro->inodeNumber = indice_inode;

	// TODO: escrever inode no disco e entrada no dir

	// ...

	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para criar um caminho alternativo (hardlink)
-----------------------------------------------------------------------------*/
int hln2(char *linkname, char *filename) {

	if (init() != SUCCESS) return failed("hln2: failed to initialize");
	if (!is_mounted()) return failed("No partition mounted.");
	if (!is_root_open()) return failed("Directory must be opened.");
	if (!is_valid_filename(filename)) return(failed("Filename not valid."));
	if (!is_valid_filename(linkname)) return(failed("Linkname not valid."));

	T_RECORD* record = alloc_record(1);

	// if file 'linkname' already exists
	if (find_entry(linkname, record) == SUCCESS){
		printf("File %s already exists.\n", linkname);
		return FAILED;
	}

	// if file 'filename' doesnt exist
	if (find_entry(filename, record) != SUCCESS){
		printf("File %s doesn't exist.\n", filename);
		return FAILED;
	}

	int indice_inode = search_inode_by_filename(filename);

	// abre inode do arquivo 'filename'
	T_INODE* inode = alloc_inode(1);
	//inode = open_inode_by_index(indice_inode);
	if(access_inode(indice_inode, inode) != SUCCESS) return FAILED;
	// apenas incrementa o contador de referencias
	inode->RefCounter += 1;

	// inicializacao da entrada no dir
	T_RECORD* registro = blank_record();
	registro->TypeVal = TYPEVAL_LINK;
	if (strlen(linkname) > MAX_FILENAME_SIZE) return failed("Linkname is too big.");
	// 51 contando o /0 da string
	// ops acho que strlen nao conta o \0. MAX_FILENAME_SIZE é 50.
	strncpy(registro->name, linkname, strlen(filename)+1);
	registro->inodeNumber = indice_inode;

	// TODO: escrever entrada no dir

	// ...

	return SUCCESS;
}
