
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
typedef struct t2fs_superbloco 	T_SUPERBLOCK;
typedef struct t2fs_inode 			T_INODE;
typedef struct t2fs_record 		 	T_RECORD;
#define SUCCESS 0
#define FAILED -1
#define SECTOR_SIZE 256
#define INODE_SIZE_BYTES 32
#define ENTRY_SIZE_BYTES sizeof(T_RECORD)
#define INODES_PER_SECTOR SECTOR_SIZE / INODE_SIZE_BYTES
#define ENTRIES_PER_SECTOR SECTOR_SIZE / ENTRY_SIZE_BYTES

#define	BITMAP_INODES	0
#define	BITMAP_BLOCKS	1
#define BIT_FREE 0
#define BIT_OCCUPIED 1

#define	TYPEVAL_INVALIDO	0x00
#define	TYPEVAL_REGULAR		0x01
#define	TYPEVAL_LINK		0x02

#define MAX_FILE_NAME_SIZE 255
#define MAX_FILES_OPEN 10

#define SECTOR_DISK_MBR 0
#define SECTOR_PARTITION_SUPERBLOCK 0
#define error() printf("Error thrown at %s:%s:%d\n",FILE,__FUNCTION__,LINE);
/* **************************************************************** */
typedef struct Partition{
	DWORD initial_sector;
	DWORD final_sector;
	BYTE partition_name[24];
} PARTITION;

typedef struct Mbr{
	DWORD version;
	DWORD sector_size;
	DWORD initial_byte;
	DWORD num_partitions;
	PARTITION* disk_partitions;
} MBR;

struct Open_file{
	T_INODE* inode;
	int current_pointer;
};
/* **************************************************************** */
// Auxiliary functions
int init();
int read_MBR_from_disk(BYTE* master_sector, MBR* mbr);
int initialize_superblock(int partition, int sectors_per_block);
int write_superblock_to_partition(int partition);
void calculate_checksum(T_SUPERBLOCK* sb);
int init_open_files();
// Conversion from/to little-endian unsigned chars
DWORD to_int(BYTE* bytes, int num_bytes);
BYTE* to_BYTE(DWORD value, int num_bytes);
// Debugging
int failed(char* msg) {printf("%s\n", msg);return FAILED;}
void print(char* msg) {printf("%s\n", msg);}
void* null(char* msg) {printf("%s\n", msg);return (void*)NULL;}
/* **************************************************************** */
// GLOBAL VARIABLES
MBR disk_mbr;
T_SUPERBLOCK* partition_superblocks;

// GLOBAL OPEN FILE VARIABLES
struct Open_file open_files[MAX_FILES_OPEN];

DWORD* current_entry;
DWORD partition;
int root = -1 ;
char* mount_path; // uma string "/{partition name}...." ?

// duvidas:
// como fazer a string de montagem dinamicamente em c

T_INODE dummy_inode;

// FILE2 open_files[MAX_FILES_OPEN];




// Maximum of 10 open file handles at once
//(***can be same file multiple times!!!)
int fopen_count;
boolean t2fs_initialized = false;

// OBS o .c original incluia nas assinaturas uma entrada DIR2
// que assumo equivaler a um typedef int para uma handle de dir
// mas o .h nao tem isso e nosso trabalho só usa diretorio raiz,
// entao tirei esse argumento aqui do .c em opendir readdir e closedir

/* **************************************************************** */

DWORD to_int(BYTE* bytes, int num_bytes) {
	// Bytes stored in little endian format
	// Least significant byte comes first on the lowest index, highest comes last
	DWORD value = 0;
	for (int i = 0; i < num_bytes ; ++i){
		value |= (DWORD)(bytes[i] << 8*i) ;
	}
	return value;
}

DWORD new_to_int(BYTE* bytes, int num_bytes) {
	// doesnt flip the endianness because C do dat
	DWORD value = 0;
	for (int i =0; i<num_bytes; i++) {
		value =0 ;
		value |= (DWORD)(bytes[i] << 8*(num_bytes-1-i));
	}
	return value;
}

BYTE* to_BYTE(DWORD value, int num_bytes) {

	BYTE* bytes = (BYTE*)malloc(num_bytes*sizeof(BYTE));
	strncpy((char*)bytes, (char*)"\0", num_bytes );
	for (int i = 0; i < num_bytes; ++i) {
		bytes[i] = (value >> (8*i))&0xFF;
	}
	return bytes;
}


int init(){

	if (t2fs_initialized) return SUCCESS;

	// Reads first disk sector with raw MBR data
	BYTE* master_sector = (BYTE*)malloc(SECTOR_SIZE*sizeof(BYTE));
	if(read_sector(SECTOR_DISK_MBR, master_sector) != SUCCESS) {
		return failed("Failed to read MBR"); }
	// Read disk master boot record into application space
	if(read_MBR_from_disk(master_sector, &disk_mbr) != SUCCESS) {
		return failed("Fu"); }
	// Alloc a superblock per existing partition
	partition_superblocks = (T_SUPERBLOCK*)malloc(sizeof(T_SUPERBLOCK)*disk_mbr.num_partitions);
	//  Initialize open files
	if(init_open_files() != SUCCESS) {
		return failed("Failed to initialize open files");	}

	current_entry = (DWORD*)malloc(sizeof(DWORD)*disk_mbr.num_partitions);
	for (int i=0; i < disk_mbr.num_partitions; i++)
		current_entry[i] = 0;

	t2fs_initialized = true;
	return SUCCESS;
}

void report_superblock(int partition ){
	printf("********************************\n");
	printf("Superblock info for partition %d\n", partition);
	T_SUPERBLOCK* sb = &(partition_superblocks[partition]);
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


DWORD first_inode_sector(int partition){
	T_SUPERBLOCK* sb = &(partition_superblocks[partition]);

	DWORD sector = disk_mbr.disk_partitions[partition].initial_sector;
	sector += sb->superblockSize * sb->blockSize;
	sector += sb->freeInodeBitmapSize * sb->blockSize;
	sector += sb->freeBlocksBitmapSize * sb->blockSize;

	return sector;
}
DWORD first_data_sector(int partition){
	T_SUPERBLOCK* sb = &(partition_superblocks[partition]);

	DWORD sector = first_inode_sector(partition);
	sector += sb->inodeAreaSize * sb->blockSize;
	return sector;
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

int initialize_superblock(int partition, int sectors_per_block) {

	if (partition >= disk_mbr.num_partitions) return failed("Invalid partition bye");

	int first_sector = disk_mbr.disk_partitions[partition].initial_sector;
	int last_sector  = disk_mbr.disk_partitions[partition].final_sector;
	int num_sectors = last_sector - first_sector + 1;
	// Gets pointer to superblock for legibility
	T_SUPERBLOCK* sb = &(partition_superblocks[partition]);

	strncpy(sb->id, "T2FS", 4); //ou talvez "SF2T"... --> nope
	sb->version = 0x7E32; //ou 0x327E  --> nope
	sb->superblockSize = 1; // ou 0x01 0x00  --> nope
	sb->blockSize = sectors_per_block;
	sb->diskSize = num_sectors / sectors_per_block; //Number of logical blocks in formatted disk.
	// bitmap has "num_blocks_formatted" bits
	// freeBlocksBitmapSize is the number of blocks needed to store the bitmap.
	// therefore: "diskSize in blocks" bits, divided by 8 to get bytes.
	// bytes divided by ( number of bytes per sector * number of sectors per block).
	sb->freeBlocksBitmapSize = sb->diskSize / 8 / (disk_mbr.sector_size * sectors_per_block);
	// 10% of the partition blocks are reserved to inodes (ROUND UP)
	sb->inodeAreaSize = (int)(ceil(0.10*sb->diskSize)); // qty in blocks
	sb->freeInodeBitmapSize = sb->inodeAreaSize / 8 / (disk_mbr.sector_size * sectors_per_block);

	calculate_checksum(sb);
	// Superblock filled, needs to be saved into disk
	// at sector 0 of its partition (first sector)
	// "all superblock values are little-endian"
	// so theres probably something wrong in declaration
	//write_sector(first_sector, (BYTE*)sb);

	// Realmente nao sei se isso vai dar certo
	// Se nao der: precisa uma funcao dedicada para passar toda
	// a estrutura superbloco para disco, campo a campo,
	// em formato unsigned char / BYTE little endian. fun

	// (edit) nao foi tao dificil actually. implemented function that
	// converts everything to little endian BYTE

	// edit 2: nao precisava converter forçadamente pq o C faz isso automático
	write_superblock_to_partition(partition);
	return SUCCESS;
}

int write_superblock_to_partition(int partition) {
	BYTE* output = (BYTE*)malloc(SECTOR_SIZE*sizeof(BYTE));
	// Gets pointer to application-space superblock
	T_SUPERBLOCK* sb = &(partition_superblocks[partition]);

	output[0] = sb->id[3];
	output[1] = sb->id[2];
	output[2] = sb->id[1];
	output[3] = sb->id[0];
	strncpy((char*)&(output[4]), (char*)to_BYTE(sb->version, 2),2);
	strncpy((char*)&(output[6]), (char*)to_BYTE(sb->superblockSize, 2),2);
	strncpy((char*)&(output[8]), (char*)to_BYTE(sb->freeBlocksBitmapSize, 2),2);
	strncpy((char*)&(output[10]), (char*)to_BYTE(sb->freeInodeBitmapSize, 2),2);
	strncpy((char*)&(output[12]), (char*)to_BYTE(sb->inodeAreaSize, 2),2);
	strncpy((char*)&(output[14]), (char*)to_BYTE(sb->blockSize, 2),2);
	strncpy((char*)&(output[16]), (char*)to_BYTE(sb->diskSize, 4),4);
	strncpy((char*)&(output[20]), (char*)to_BYTE(sb->Checksum, 4),4);

	if (write_sector(
		disk_mbr.disk_partitions[partition].initial_sector, output) != SUCCESS) {
		return failed("Failed to write superblock to partition sector");
	}
	else return SUCCESS;
}

int read_MBR_from_disk(BYTE* master_sector, MBR* mbr) {
	// reads the logical master boot record sector into a special structure of type MBR
	mbr->version = to_int(&(master_sector[0]), 2);
	mbr->sector_size = to_int(&(master_sector[2]), 2);
	mbr->initial_byte = to_int(&(master_sector[4]), 2);
	mbr->num_partitions = to_int(&(master_sector[6]),2);
	mbr->disk_partitions = (PARTITION*)malloc(sizeof(PARTITION)*mbr->num_partitions);
	for(int i = 0; i < mbr->num_partitions; ++i){
		int j = 8 + i*32; //32 bytes per partition in the boot record
		mbr->disk_partitions[i].initial_sector = to_int(&(master_sector[j]),4);
		mbr->disk_partitions[i].final_sector = to_int(&(master_sector[j+4]),4);
		// String possivelmente tem que inverter a ordem
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
	strncpy((char*)&(temp4[0]), (char*)to_BYTE(sb->version, 2), 2);
	strncpy((char*)&(temp4[2]), (char*)to_BYTE(sb->superblockSize,2), 2);
	checksum += to_int(temp4, 4);
	// freeBlocksBitmapSize + freeInodeBitmapSize (2B each)
	strncpy((char*)&(temp4[0]), (char*)to_BYTE(sb->freeBlocksBitmapSize, 2), 2);
	strncpy((char*)&(temp4[2]), (char*)to_BYTE(sb->freeInodeBitmapSize,2), 2);
	checksum += to_int(temp4, 4);
	// inodeAreaSize + blockSize (2B each)
	strncpy((char*)&(temp4[0]), (char*)to_BYTE(sb->inodeAreaSize, 2), 2);
	strncpy((char*)&(temp4[2]), (char*)to_BYTE(sb->blockSize,2), 2);
	checksum += to_int(temp4, 4);
	// diskSize (a DWORD of 4 Bytes)
	checksum += sb->diskSize;
	// One's complement
	sb->Checksum = ~checksum;
}

int initialize_inode_area(int partition){
	// Helpful pointer
	T_SUPERBLOCK* sb = &(partition_superblocks[partition]);
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
	int start_sector = disk_mbr.disk_partitions[partition].initial_sector;
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

int initialize_bitmaps(int partition){
	T_SUPERBLOCK* sb = &(partition_superblocks[partition]);

	// Allocates both Bitmaps (block status and inode status)
	// Input: disk sector where the partition's superblock is
	// (aka, the first sector in a formatted partition)
	// Output: success/error code
	if(openBitmap2(disk_mbr.disk_partitions[partition].initial_sector) != SUCCESS){
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

DWORD map_inode_to_sector(int partition, int inode_index) {

	DWORD sector = first_inode_sector(partition);
	sector += floor(inode_index/INODES_PER_SECTOR);
	return sector;
}
DWORD map_block_to_sector(int partition, int block_index) {

	DWORD sector = first_data_sector(partition);
	sector += block_index;
	return sector;
}

// Input: partition number, inode index in disk, pointer to an allocated inode structure
int read_inode(int partition, int inode_index, T_INODE* inode){

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
	strncpy((char*)&(bytes[0]),  (char*)to_BYTE(inode->blocksFileSize, 4), 4);
	strncpy((char*)&(bytes[4]),  (char*)to_BYTE(inode->bytesFileSize, 4), 4);
	strncpy((char*)&(bytes[8]),  (char*)to_BYTE(inode->dataPtr[0], 4), 4);
	strncpy((char*)&(bytes[12]), (char*)to_BYTE(inode->dataPtr[1], 4), 4);
	strncpy((char*)&(bytes[16]), (char*)to_BYTE(inode->singleIndPtr, 4), 4);
	strncpy((char*)&(bytes[20]), (char*)to_BYTE(inode->doubleIndPtr, 4), 4);
	strncpy((char*)&(bytes[24]), (char*)to_BYTE(inode->RefCounter, 4), 4);
	strncpy((char*)&(bytes[28]), (char*)to_BYTE(inode->reservado, 4), 4);

	return SUCCESS;
}

int write_new_inode(int partition, T_INODE* inode){

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

		DWORD sector = map_inode_to_sector(partition, inode_idx);

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
	DWORD sector = map_inode_to_sector(partition, 0);

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

int remove_file_content(T_INODE* inode)
{
	int superbloco_sector = disk_mbr.disk_partitions[partition].initial_sector;

	if(openBitmap2(superbloco_sector) != SUCCESS)
		return FAILED;

	// // zera bits no bitmap de dados
	// percorre ponteiros de indirecao dupla, traduz ponteiro de dados para posicao na posicao, zera posicoes no bitmap de dados
	// percorre ponteiros de inodes, traduz ponteiro para posicao no bitmap de inodes, zera posicoes no bitmap de inodes

	// percorre ponteiros indiretos de indirecao simples

	// percorre ponteiros de dados, traduz ponteiro para posicao no bitmap de dados, zera posicoes no bitmap de dados

	return SUCCESS;
}

int remove_record(T_INODE* root, char* filename)
{

	int superbloco_sector = disk_mbr.disk_partitions[partition].initial_sector;

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

	if( initialize_superblock(partition, sectors_per_block) != SUCCESS)
		return failed("Failed to read superblock.");

	if(initialize_inode_area(partition) != SUCCESS)
		return(failed("Format2: Failed to initialize inode area"));

	if(initialize_bitmaps(partition) != SUCCESS)
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
	return -1;
}

/*-----------------------------------------------------------------------------
Função:	Desmonta a partição atualmente montada, liberando o ponto de montagem.
-----------------------------------------------------------------------------*/
int unmount(void) {
	return -1;
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
	// testar se ta tudo montadinho
	current_entry[partition] = 0;
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
	strncpy((char*)&(bytes[MAX_FILE_NAME_SIZE+2]), (char*)to_BYTE(dentry->fileSize, sizeof(DWORD)), sizeof(DWORD));
	return SUCCESS;
}

int access_inode(int partition, int inode_index, T_INODE* return_inode) {

	// Root directory is first inode in first inode-sector
	DWORD sector = first_inode_sector(partition);
	BYTE* buffer = (BYTE*)malloc(INODES_PER_SECTOR * sizeof(T_INODE));
	if(read_sector(sector, buffer) != SUCCESS){
		return(failed("AccessDirectory: Failed to read dir sector."));
	}
	if(BYTE_to_INODE(buffer, inode_index , return_inode) != SUCCESS)
		return(failed("Failed BYTE to INODE translation"));
	return SUCCESS;
}
int max_pointers_in_block(int partition){
	T_SUPERBLOCK* sb = &(partition_superblocks[partition]);

	int max_pointers_in_block = (sb->blockSize * SECTOR_SIZE)/4 ;
	return max_pointers_in_block;
}
int max_entries_in_block(int partition){
	// block size in BYTES div by sizeof ENTRY
	T_SUPERBLOCK* sb = &(partition_superblocks[partition]);

	int max_entries_in_block = (sb->blockSize * SECTOR_SIZE)/sizeof(T_RECORD);
	return max_entries_in_block;
}

int max_dentries(int partition){
	// Number of FILE ENTRIES in ROOT DIRECTORY for this partition.

	int total_entries_block =  max_entries_in_block(partition);
	int total_pointers_block = max_pointers_in_block(partition);

	int max_entries = 2*total_entries_block // direct pointers
		+ total_pointers_block*total_entries_block // single indirect to a block
		+ total_pointers_block*total_pointers_block*total_entries_block;

	return max_entries ;
}

// TODO: TA RETORNANDO O SETOR ONDE ENCONTROU A ENTRY (>= 1)
// RETORNO ZERO AQUI É NOT FOUND
// RETORNO NEGATIVO É DEU PAU.
int find_entry(int partition, int entry_block, char* filename) {
	// Helpful pointer
	T_SUPERBLOCK* sb = &(partition_superblocks[partition]);
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

int sweep_directory_by_index(int partition, int index, DIRENT2* dentry) {
	return FAILED;
}

int sweep_directory_by_name(int partition, char* filename, DIRENT2* dentry) {
	// Helpful pointer
	T_SUPERBLOCK* sb = &(partition_superblocks[partition]);

	T_INODE* dnode = (T_INODE*) malloc(sizeof(T_INODE));
	if(access_inode(partition, 0, dnode)!=SUCCESS){
		return(failed("Failed to access directory node"));
	}

	//int total_entries = max_dentries(partition);

	BYTE* buffer = (BYTE*) malloc(sizeof(BYTE)*SECTOR_SIZE);

	// TODO: verificar se ponteiro no inode é absoluto para um bloco (ou setor!)
	// ou relativo aa particao

	int ptr_block = dnode->dataPtr[0];
	for (int sector = 0; sector < sb->blockSize; sector++){

		if(read_sector(ptr_block*sb->blockSize + sector, buffer) != SUCCESS)
			return(failed("Deu pau"));
		// Here we have a sector made of 4-byte pointers to Entry Blocks.

	}

	// checar se nome em algum registro em
	// dnode->dataPtr[0];
	// dnode->dataPtr[1];
	// dnode->singleIndPtr;
	// dnode->doubleIndPtr;



	return FAILED;
}


/*-----------------------------------------------------------------------------
Função:	Função usada para ler as entradas de um diretório.
-----------------------------------------------------------------------------*/
int readdir2 (DIRENT2 *dentry) {
	if (init() != SUCCESS) return(failed("ReadDir: failed to initialize"));

	// each call to readdir returns a single entry.
	// then, ups the internal counter to the next entry for the next call.
	// return error code if:
	// can`t read it for some reason
	// no more valid entries in the open dir.
	T_INODE* dir_node = (T_INODE*) malloc(sizeof(T_INODE));
	if(access_inode(partition, 0, dir_node)!=SUCCESS){
		return(failed("Failed to access directory node"));
	}

	int total_entries = max_dentries(partition);
	int entries_per_block = 0;
	int total_pointers = max_pointers_in_block(partition);

	if(current_entry[partition] >= total_entries) {
		return(failed("Invalid directory entry"));
	}

	if(current_entry[partition] < 2*total_pointers){
		// acessa ponteiro direto para bloco de Entradas
	}
	else if (current_entry[partition] < total_pointers*entries_per_block) {
		// acessa ponteiro para bloco de indices, cada um para um bloco de Entradas
	}
	else {
		// acessa ponteiro para bloco de indices composto de ponteiros
		// para blocos de indices cada indice apontando para um bloco de Entradas.
	}

	current_entry++;
	return SUCCESS;
}




/*-----------------------------------------------------------------------------
Função:	Função usada para fechar um diretório.
-----------------------------------------------------------------------------*/
int closedir2 (void) {
	if (init() != SUCCESS) return(failed("CloseDir: failed to initialize"));
	// checar se o dir ta aberto
	// toggle como closed?
	// la fin
	return -1;
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
