
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
// Debugging
int failed(char* msg) {printf("%s\n", msg);return FAILED;}
void print(char* msg) {printf("%s\n", msg);}
void* null(char* msg) {printf("%s\n", msg);return (void*)NULL;}

// GLOBAL VARIABLES
MBR 					disk_mbr;
BOLA_DA_VEZ*	mounted;
T_INODE 			dummy_inode;

// old variables to be removed when compiling errors stop
T_FOPEN 			open_files[MAX_FILES_OPEN];

// Maximum of 10 open file handles at once
//(***can be same file multiple times!!!)
int fopen_count;
boolean t2fs_initialized = false;

/* **************************************************************** */

DWORD to_int(BYTE* chars, int num_bytes) {
	// Bytes stored in little endian format
	// Least significant byte comes first on the lowest index, highest comes last
	DWORD value = 0;
	for (int i = 0; i < num_bytes ; ++i){
		value |= (DWORD)(chars[i] << 8*i) ;
	}
	return value;
}

DWORD new_to_int(BYTE* chars, int num_bytes) {
	// doesnt flip the endianness because C do dat
	DWORD value = 0;
	for (int i =0; i<num_bytes; i++) {
		value =0 ;
		value |= (DWORD)(chars[i] << 8*(num_bytes-1-i));
	}
	return value;
}

BYTE* DWORD_to_BYTE(DWORD value, int num_bytes) {

	BYTE* chars = (BYTE*)malloc(num_bytes*sizeof(BYTE));
	strncpy((char*)chars, (char*)"\0", num_bytes );
	for (int i = 0; i < num_bytes; ++i) {
		chars[i] = (value >> (8*i))&0xFF;
	}
	return chars;
}
BYTE* WORD_to_BYTE(WORD value, int num_bytes) {

	BYTE* chars = (BYTE*)malloc(num_bytes*sizeof(BYTE));
	strncpy((char*)chars, (char*)"\0", num_bytes );
	for (int i = 0; i < num_bytes; ++i) {
		chars[i] = (value >> (8*i))&0xFF;
	}
	return chars;
}

boolean is_mounted(void){
	if (mounted == NULL)
		return false;
	else return true;
}

int get_mounted(void) {
	if(is_mounted())
		return mounted->id;
	else return FAILED;
}

boolean is_root_open(){
	if(is_mounted()){
		if(mounted->root == NULL || !mounted->root->open)
			return false;
		else return true;
	}
	return false;
}

BYTE* alloc_sector() {
	BYTE* buffer= (BYTE*)malloc(sizeof(BYTE) * SECTOR_SIZE);
	return buffer;
}

int load_root(){
	if (init() != SUCCESS) return(failed("Uninitialized"));
	if (!is_mounted())  return(failed("No partition mounted."));
	if (is_root_open()) return SUCCESS;

	BYTE* buffer = alloc_sector();
	if(read_sector(mounted->fst_inode_sector,  buffer) != SUCCESS)
		return(failed("LoadRoot failed"));

	T_INODE* dir_node = (T_INODE*)malloc(sizeof(T_INODE));
	if(access_inode(ROOT_INODE, dir_node)!=SUCCESS){
		return(failed("LoadRoot: Failed to access directory node"));
	}

	T_DIRECTORY* rt = mounted->root;
	rt->open = true; // TODO: nao acho que ja posso assumir isso
	rt->inode = dir_node ;
	rt->inode_index = ROOT_INODE;
	rt->entry_index = DEFAULT_ENTRY;
	rt->total_entries = 0; // TODO: errado, percorrer pelo node todos validos

	// Maximum number of ENTRY BLOCKS the d-node can hold.
	rt->max_entries = 2  						// direct pointers to Entry Nodes
			+ mounted->pointers_per_block // 1 single indirect pointer to block of direct pointers
			+ mounted->pointers_per_block*mounted->pointers_per_block; // 1 double indirect
	// Multiply by number of entries in a data block
	// to get no. of ENTRIES per d-node.
	rt->max_entries *= mounted->entries_per_block;

	rt->open_files = NULL; // TODO errado??
	rt->num_open_files = 0; // TODO errado??
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

	//  Initialize open files
	if(init_open_files() != SUCCESS) {
		return failed("Failed to initialize open files");	}

	mounted = NULL;
	t2fs_initialized = true;
	return SUCCESS;
}

void report_superblock(){
	printf("********************************\n");
	printf("Superblock info for partition %d\n", mounted->id);
	T_SUPERBLOCK* sb = mounted->superblock;
	printf("Id: %s\n",sb->id);
	printf("Version: %d\n",sb->version);
	printf("Superblock size(1 block, first in partition): %d\n",sb->superblockSize);
	printf("Free Blocks Bitmap Size(in blocks): %d\n",sb->freeBlocksBitmapSize);
	printf("Free Inodes Bitmap Size(in blocks): %d\n",sb->freeInodeBitmapSize);
	printf("Inode area size (in blocks): %d\n",sb->inodeAreaSize);
	printf("Block size (in sectors): %d\n",sb->blockSize);
	printf("Disk size of partition (in blocks): %d\n",sb->diskSize);
	printf("Checksum: %d", sb->Checksum);
}

int BYTE_to_SUPERBLOCK(BYTE* bytes, T_SUPERBLOCK* sb){

	sb->id[3] = bytes[0];
	sb->id[2] = bytes[1];
	sb->id[1] = bytes[2];
	sb->id[0] = bytes[3];
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

int teste_superblock(MBR* mbr, T_SUPERBLOCK* sb) {

	printf("Initializing...\n");
	init();
	printf("Initialized.\n");

	format2(0, 4);
	BYTE* buffer = (BYTE*)malloc(sizeof(BYTE)*SECTOR_SIZE);
	printf("Reading sector from disk...\n");
	int j = get_mounted();
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
	T_SUPERBLOCK* sb = mounted->superblock;

	int fst_sect = mounted->mbr_data->initial_sector;
	int lst_sect  = mounted->mbr_data->final_sector;
	int num_sectors = lst_sect - fst_sect + 1;

	strncpy(sb->id, "T2FS", 4);
	sb->version = 0x7E32;
	sb->superblockSize = 1;
	sb->blockSize = (WORD)sectors_per_block;
	//Number of logical blocks in formatted disk.
	sb->diskSize = (WORD)ceil(num_sectors/(float)sectors_per_block);
	printf("num sectors %d\n", num_sectors);
	printf("num sectors per block %d\n", sectors_per_block);
	printf("disksize %d\n", sb->diskSize);

	// 10% of partition blocks reserved to inodes (ROUND UP)
	sb->inodeAreaSize = (WORD)(ceil(0.10*sb->diskSize));
	printf("inode Area Size %d\n", sb->inodeAreaSize);
	// inode bitmap size: 1 bit per inode given "X" inodes per block
	// size in bits then converted to bytes -> sectors -> blocks.
	// First: inode Area in bytes
	float inode_bmap = (float)sb->inodeAreaSize*sectors_per_block*disk_mbr.sector_size;
	// Divide by size of 1 inode in bytes, thus the number of inodes in the inode area.
	inode_bmap /= (float)sizeof(T_INODE);
	// 1 bit per inode, now converted to number of blocks rounding up.
	inode_bmap /= (float)(8*disk_mbr.sector_size*sectors_per_block);
	sb->freeInodeBitmapSize = (WORD) ceil(inode_bmap);

	float data_blocks = sb->diskSize - sb->inodeAreaSize - sb->superblockSize;
	data_blocks -= sb->freeInodeBitmapSize;
	// block bitmap size is dependent on how many blocks are left after the bitmap.
	// therefore it is equal to current surviving blocks div by (8*bytes per block)+1
	sb->freeBlocksBitmapSize = (WORD)
		ceil(data_blocks / (float)(1 + 8*disk_mbr.sector_size*sectors_per_block));

	// TODO: revisar isso
	printf("diskSize in blocks %d\n", sb->diskSize);
	printf("freeBlocksBitmapSize %d\n", sb->freeBlocksBitmapSize);
	printf("freeInodeBitmapSize %d\n", sb->freeInodeBitmapSize);
	printf("inodeAreaSize %d\n", sb->inodeAreaSize);
	calculate_checksum(sb);
	printf("CHECKSUM %u\n", sb->Checksum);

	save_superblock();
	return SUCCESS;
}

int save_superblock() {
	// Gets pointer to application-space superblock
	T_SUPERBLOCK* sb = mounted->superblock;
	BYTE* output = alloc_sector();
	output[0] = sb->id[3];
	output[1] = sb->id[2];
	output[2] = sb->id[1];
	output[3] = sb->id[0];
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
	if(read_sector(mounted->mbr_data->initial_sector, buffer)!= SUCCESS) return(failed("failed reading sb"));
	if(BYTE_to_SUPERBLOCK(buffer,mounted->superblock) !=SUCCESS) return FAILED;
	return SUCCESS;
}

int load_mbr(BYTE* master_sector, MBR* mbr) {
	// reads the logical master boot record sector into a special structure of type MBR
	mbr->version = to_int(&(master_sector[0]), 2);
	mbr->sector_size = to_int(&(master_sector[2]), 2);
	mbr->initial_byte = to_int(&(master_sector[4]), 2);
	mbr->num_partitions = to_int(&(master_sector[6]),2);
	mbr->disk_partitions = (PARTITION*)malloc(sizeof(PARTITION)*mbr->num_partitions);
	for(int i = 0; i < mbr->num_partitions; ++i){
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
	// Helpful pointer
	T_SUPERBLOCK* sb = mounted->superblock;
	// Starting block:
	// First partition block is superblock
	// Then come bitmaps (size given in block)
	// Then the inodes, occupying 10% of the disk.
	// ROOT DIRECTORY is inode number 0
	// Each inode holds 32 bytes.
	// inode area given in BLOCKS
	// Calculate first inodes block within the partition
	DWORD start_block = sb->superblockSize + sb->freeBlocksBitmapSize + sb->freeInodeBitmapSize;
	// Add offset to where the partition starts in the disk
	int start_sector = mounted->mbr_data->initial_sector;
	// Add offset to where in the partition the inodes start
	start_sector += start_block * sb->blockSize;

	dummy_inode.blocksFileSize = -1;
	dummy_inode.bytesFileSize = -1;
	dummy_inode.dataPtr[0] = -1;
	dummy_inode.dataPtr[1] = -1;
	dummy_inode.singleIndPtr = -1;
	dummy_inode.doubleIndPtr = -1;
	dummy_inode.RefCounter = 0;

	printf("Tamanho de um inode: %d", (int) sizeof(T_INODE));
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

	// Allocates both Bitmaps (block status and inode status)
	// Input: disk sector where the partition's superblock is
	// (aka, the first sector in a formatted partition)
	// Output: success/error code
	if(openBitmap2(mounted->mbr_data->initial_sector) != SUCCESS){
		// provavelmente desalocar alguma coisa
		return failed("OpenBitmap: Failed to allocate bitmaps in disk");
	}
	// Bitmaps open and allocated, needs to be initialized as FREE
	// inode bitmap: handle 0
	// blocks bitmap: handle not 0
	for (int bit_idx=0;  // TODO: TALVEZ TENHA QUE SER POSITIVO (ROOT DIRECTORY É IDX 0)
				bit_idx < sb->freeInodeBitmapSize * sb->blockSize *SECTOR_SIZE*8;
					bit_idx++){
		if(setBitmap2(BITMAP_INODES, bit_idx, BIT_FREE) != SUCCESS) {
			return(failed("Failed to set bit as free in inode bitmap"));
		}
	}
	for (int bit_idx=0;
		bit_idx < sb->freeBlocksBitmapSize * sb->blockSize *SECTOR_SIZE*8;
		bit_idx++) {

		if(setBitmap2(BITMAP_BLOCKS, bit_idx, BIT_FREE) != SUCCESS) {
			return(failed("Failed to set bit as free in blocks bitmap"));
		}
	}
	return SUCCESS;
}

DWORD map_inode_to_sector(int inode_index) {

	DWORD sector = mounted->fst_inode_sector;
	sector += floor(inode_index/INODES_PER_SECTOR);
	return sector;
}
DWORD map_block_to_sector(int block_index) {

	DWORD sector = mounted->fst_data_sector;
	sector += block_index * mounted->superblock->blockSize;
	sector += block_index % mounted->superblock->blockSize;
	// TODO verificar.
	return sector;
}

// Input: inode index in disk, pointer to an allocated inode structure
int read_inode(int inode_index, T_INODE* inode){

	return SUCCESS;
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

int save_new_inode(T_INODE* inode){

	int code_node = searchBitmap2(BITMAP_INODES, BIT_FREE);

	if(code_node<0) {return(failed("Failed to search inode bitmap."));}
	if(code_node==0){return(failed("Inode bitmap full. Cannot write inode."));}

	if(code_node > 0) { // inode index
		// TODO: teste de limite do numero de blocos contando a necessidade de
		// bloco de indices single apos 2 blocos e double após {2+x} blocos.
		int inode_idx = code_node ;
		int* block_indexes = (int*) malloc((2+inode->blocksFileSize)*sizeof(int));

		int count_free =  0 ;
		while(count_free < inode->blocksFileSize) {

			int code_blocks = searchBitmap2(BITMAP_INODES, BIT_FREE);
			if (code_blocks < 0 ) {return(failed("Failed blocks bitmap search."));}
			if (code_blocks == 0) {return(failed("Data bitmap full. Cannot write inode.")); }

			// Collect all indexes in a list
			block_indexes[count_free] = code_blocks;
			count_free++;
		}

		DWORD sector = map_inode_to_sector(inode_idx);

		BYTE* buffer_sector = (BYTE*) malloc(INODES_PER_SECTOR*sizeof(T_INODE));
		if(read_sector(sector, buffer_sector)!=SUCCESS) return(failed("WriteNewNode: Failed to read sector"));

		BYTE* inode_as_bytes = (BYTE*) malloc(sizeof(T_INODE));
		INODE_to_BYTE(inode, inode_as_bytes);

		DWORD offset = (inode_idx % INODES_PER_SECTOR)*INODE_SIZE_BYTES;
		DWORD inode_initial_byte = sector + offset;
		for (int i = 0; i < 8; i++) {
			strncpy(
				(char*)&(buffer_sector[inode_initial_byte + i*4]),
				(char*)&(inode_as_bytes[i*4]),
				4);
		}

		if(write_sector(sector, buffer_sector) != SUCCESS) {
			return(failed("WriteNewInode, WriteSector failed: shit "));
		}
		return SUCCESS;
	}
	return SUCCESS;
}
/*-----------------------------------------------------------------------------*/
unsigned char* get_block(int sector, int offset, int n)
{
	if(offset > SECTOR_SIZE)
		return NULL;

	unsigned char* buffer = (BYTE*)malloc(SECTOR_SIZE);
	if(!read_sector(sector, buffer))
		return NULL;

	unsigned char* block = (BYTE*)malloc(n);
	memcpy( &block, &buffer, n);

	return block;
}

T_INODE* get_root(){

	// get first inode (root dir)
	DWORD sector = map_inode_to_sector(0);

	//get first block at the inodes sector
	unsigned char* block = get_block(sector, 0, sizeof(T_INODE));

	T_INODE* root = (T_INODE*) &block;

	return root;
}

T_INODE* search_root_for_filename(T_INODE* root, char* filename)
{
	return 0;
}

T_INODE* new_file(T_INODE* root, char* filename)
{
	//cria registro
	T_RECORD* rec = (T_RECORD*)malloc(sizeof(T_RECORD));
	rec->TypeVal=0x01;

	memset(rec->name, '\0', sizeof(rec->name));
	strcpy(rec->name, filename);

	// identificador de inode
	rec->inodeNumber=0;

	//adiciona registro em diretorio
	//write2() ?

	return NULL;
}

int set_file_open(T_INODE* f)
{

	for(int i=0; i <= MAX_FILES_OPEN; i++)
	{
		if(open_files[i].inode==NULL)
		{
			open_files[i].inode = f;
			open_files[i].current_pointer = 0;

			return i;
		}

	}

	return -1;
}

int set_file_close(FILE2 handle)
{
	if(handle >= MAX_FILES_OPEN || handle < 0)
		return FAILED;


	open_files[handle].inode = NULL;
	open_files[handle].current_pointer = -1;

	return SUCCESS;
}

int init_open_files()
{
	for(int i=0; i < MAX_FILES_OPEN; i++)
	{
		open_files[i].inode = NULL;
		open_files[i].current_pointer = -1;
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

	for(int i=0; i < block_size; i++){
		if(read_sector(start_data_sector, buffer) != SUCCESS) {
			return failed("Failed to read MBR"); }
		//iterate pointers in sector
		for(int j=0; j < 64; j++){
			if(buffer[j]!=0x00)
				remove_pointer_from_bitmap(buffer[j], start_data_sector, block_size, 1);
		}
	}
	free(buffer);
	return SUCCESS;
}

int iterate_doublePtr(T_INODE* inode, DWORD start_inode_sector, DWORD start_data_sector, DWORD block_size){

	//realiza leitura de um setor do bloco de indices
	BYTE* sector1 = (BYTE*)malloc(SECTOR_SIZE*sizeof(BYTE));
	BYTE* sector2 = (BYTE*)malloc(SECTOR_SIZE*sizeof(BYTE));

	//iterate through sectors in block1
	for(int i1=0; i1 < block_size; i1++)
	{
		if(read_sector(start_inode_sector, sector1) != SUCCESS) {
			return failed("Failed to read MBR"); }

		//iterate through pointers in sector1
		for(int j1=0; j1 < 64; j1++)
		{
			if(sector1[j1]!=0x00){

				//iterate through sectors in block2
				for(int i2=0; i2 < block_size; i2++)
				{
					if(read_sector(start_inode_sector, sector2) != SUCCESS) {
						return failed("Failed to read MBR");
					}

					//iterate through pointers in sector2
					for(int j2=0; j2 < 64; j2++)
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



int remove_file_content(T_INODE* inode)
{
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

int remove_record(T_INODE* root, char* filename)
{

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

	if(initialize_superblock(sectors_per_block) != SUCCESS)
		return failed("Failed to read superblock.");

	if(initialize_inode_area() != SUCCESS)
		return(failed("Format2: Failed to initialize inode area"));

	if(initialize_bitmaps() != SUCCESS)
		return(failed("Format2: Failed to initialize bitmap area"));
	// Afterwards: the rest is data blocks.
	// first block onwards after inodes is reserved to
	// file data, directory files, and index blocks for big files.
	// OBS: precisa atualizar algo no MBR apos dar format na particao??
	// ID do superbloco eh id do filesystem usado
	// nome da particao no MBR acho que eh outra coisa.
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

	mounted = (BOLA_DA_VEZ*)malloc(sizeof(BOLA_DA_VEZ));
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
	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Desmonta a partição atualmente montada, liberando o ponto de montagem.
-----------------------------------------------------------------------------*/
int unmount(void) {

	if(mounted == NULL){
		return(failed("No partition to unmount."));
	}
	if(closedir2() != SUCCESS){
		return(failed("Unmount failed: could not close root dir."));
	}
	free(mounted->superblock);
	free(mounted);
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
	T_INODE* root = get_root();

	T_INODE* f = search_root_for_filename(root, filename);

	if(f != NULL)
		return FAILED;

	T_INODE* f2 = new_file(root, filename);

	FILE2 handle = set_file_open(f2);
	return handle;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para remover (apagar) um arquivo do disco.
-----------------------------------------------------------------------------*/
int delete2 (char *filename) {
	T_INODE* root = get_root();

	T_INODE* f = search_root_for_filename(root, filename);

	remove_file_content(f);

	remove_record(root, filename);

	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Função que abre um arquivo existente no disco.
-----------------------------------------------------------------------------*/
FILE2 open2 (char *filename) {

	T_INODE* root = get_root();
	if(root==NULL)
		return FAILED;

	T_INODE* f = search_root_for_filename(root, filename);
	if(f==NULL)
		return FAILED;


	FILE2 handle = set_file_open(f);
	return handle;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para fechar um arquivo.
-----------------------------------------------------------------------------*/
int close2 (FILE2 handle) {
	return set_file_close(handle);;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para realizar a leitura de uma certa quantidade
		de bytes (size) de um arquivo.
-----------------------------------------------------------------------------*/
int read2 (FILE2 handle, char *buffer, int size) {
	return -1;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para realizar a escrita de uma certa quantidade
		de bytes (size) de  um arquivo.
-----------------------------------------------------------------------------*/
int write2 (FILE2 handle, char *buffer, int size) {
	return -1;
}

/*-----------------------------------------------------------------------------
Função:	Função que abre um diretório existente no disco.
-----------------------------------------------------------------------------*/
int opendir2 (void) {
	if (init() != SUCCESS) return(failed("OpenDir: failed to initialize"));

	if(!is_mounted()) return(failed("No partition mounted yet."));

	// Get mounted partition, load root directory, set entry to zero.
	if(!is_root_open())
		load_root();

	mounted->root->open = true;
	mounted->root->entry_index = 0;
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

	memcpy((char*)&(dentry->name), &(data[0]), (MAX_FILE_NAME_SIZE+1)*sizeof(char));
	dentry->fileType = data[MAX_FILE_NAME_SIZE+1];
	dentry->fileSize = to_int(&(data[MAX_FILE_NAME_SIZE+2]), sizeof(DWORD));
	return SUCCESS;
}

int DIRENTRY_to_BYTE(DIRENT2* dentry, BYTE* bytes){
	strncpy((char*)&(bytes[0]), (char*)&(dentry->name), (MAX_FILE_NAME_SIZE+1)*sizeof(char));
	bytes[MAX_FILE_NAME_SIZE+1] = dentry->fileType;
	strncpy((char*)&(bytes[MAX_FILE_NAME_SIZE+2]), (char*)DWORD_to_BYTE(dentry->fileSize, sizeof(DWORD)), sizeof(DWORD));
	return SUCCESS;
}

int access_inode(int inode_index, T_INODE* return_inode) {

	// Root directory is first inode in first inode-sector
	DWORD sector = mounted->fst_inode_sector;
	BYTE* buffer = (BYTE*)malloc(INODES_PER_SECTOR * sizeof(T_INODE));
	if(read_sector(sector, buffer) != SUCCESS){
		return(failed("AccessDirectory: Failed to read dir sector."));
	}
	if(BYTE_to_INODE(buffer, inode_index , return_inode) != SUCCESS)
		return(failed("Failed BYTE to INODE translation"));
	return SUCCESS;
}
int max_pointers_in_block(){
	T_SUPERBLOCK* sb = mounted->superblock;

	int max_pointers_in_block = (sb->blockSize * SECTOR_SIZE)/4 ;
	return max_pointers_in_block;
}
int max_entries_in_block(){
	// block size in BYTES div by sizeof ENTRY
	T_SUPERBLOCK* sb = mounted->superblock;

	int max_entries_in_block = (sb->blockSize * SECTOR_SIZE)/sizeof(T_RECORD);
	return max_entries_in_block;
}

// TODO: TA RETORNANDO O SETOR ONDE ENCONTROU A ENTRY (>= 1)
// RETORNO ZERO AQUI É NOT FOUND
// RETORNO NEGATIVO É DEU PAU.
int find_entry(int partition, int entry_block, char* filename) {
	// Helpful pointer
	T_SUPERBLOCK* sb = mounted->superblock;
	BYTE* buffer = (BYTE*) malloc(sizeof(BYTE)*SECTOR_SIZE);
	int size = sizeof(T_RECORD);
	int max_entries_in_sector = ENTRIES_PER_SECTOR;
	char* name = (char*)malloc(sizeof(char)*51);

	// We were given a block, read sector by sector.
	for (int sector = 0; sector < sb->blockSize; sector++){
		if(read_sector(entry_block*sb->blockSize + sector, buffer) != SUCCESS)
			return(failed("Deu errado aqui"));
		// Iterate a sector with like 4 entries.
		for (int entry = 0; entry < max_entries_in_sector; entry++) {

			strncpy((char*)name, (char*) &(buffer[1+entry*size]), 51);
			if (strcmp(name, filename) == 0){
				print("ACHOU");
				return entry_block*sb->blockSize + sector;
			}
		}
	}
	return 0;//NOT FOUND
}

int sweep_root_by_index(int index, DIRENT2* dentry) {
	return FAILED;
}


//TODO
int sweep_root_by_name(char* filename, DIRENT2* dentry) {

	if(!is_mounted()) return FAILED;
	if(!is_root_open()) return FAILED;

	// Helpful pointers
	T_SUPERBLOCK* sb = mounted->superblock;
	T_DIRECTORY* rt = mounted->root;

	BYTE* buffer = alloc_sector();

	// TODO: verificar se ponteiro no inode é absoluto para um bloco (ou setor!)
	// ou relativo aa particao

	int ptr_block = rt->inode->dataPtr[0];
	for (int sector = 0; sector < sb->blockSize; sector++){

		if(read_sector(ptr_block*sb->blockSize + sector, buffer) != SUCCESS)
			return(failed("Deu pau"));
		// Here we have a sector made of 4-byte pointers to Entry Blocks.
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
	if(! is_root_open()) {
		if(load_root()!=SUCCESS) return FAILED;
	}
	T_DIRECTORY* rt = mounted->root;

	if(rt->entry_index >= rt->max_entries) {
		return(failed("Entry index exceeds size limit."));
	}

	if(rt->entry_index < 2*mounted->entries_per_block){
		// acessa ponteiro direto para bloco de Entradas
	}
	else if (rt->entry_index < mounted->pointers_per_block*mounted->entries_per_block) {
		// acessa ponteiro para bloco de indices, cada um para um bloco de Entradas
	}
	else {
		// acessa ponteiro para bloco de indices composto de ponteiros
		// para blocos de indices cada indice apontando para um bloco de Entradas.
	}
	mounted->root->entry_index++;
	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para fechar um diretório.
-----------------------------------------------------------------------------*/
int closedir2 (void) {
	if (init() != SUCCESS) return(failed("CloseDir: failed to initialize"));

	if (!is_mounted() || !is_root_open()) return(failed("ClosedDir: no directory open."));

	/*
	for (int i ; i < MAX_FILES_OPEN; i++){
		if (mounted->root->open_files[i] != invalido/null){
			//close file still open, saving back modifications etc.
		}
	}
	mounted->root->open = false;
		//TODO: não acho que precise desalocar o diretorio mas somente deixar como null ptr
	mounted->root = NULL;
	*/
	return SUCCESS;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para criar um caminho alternativo (softlink)
-----------------------------------------------------------------------------*/
int sln2 (char *linkname, char *filename) {
	return -1;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para criar um caminho alternativo (hardlink)
-----------------------------------------------------------------------------*/
int hln2(char *linkname, char *filename) {
	return -1;
}
