
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
typedef unsigned char 					BYTE;
typedef unsigned short int  		WORD;
typedef unsigned int        		DWORD;
typedef struct t2fs_superbloco 	T_SUPERBLOCK;
typedef struct t2fs_inode 			T_INODE;
typedef struct t2fs_record 		 	T_RECORD;
#define SUCCESS 0
#define FAILED -1
#define SECTOR_SIZE 256
#define INODE_SIZE_BYTES 32
#define INODES_PER_SECTOR SECTOR_SIZE / INODE_SIZE_BYTES

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
#define error() printf("Error thrown at %s:%s:%d\n",FILE,_FUNCTION__,LINE);
/* **************************************************************** */
typedef struct Partition{
	unsigned int initial_sector;
	unsigned int final_sector;
	unsigned char partition_name[24];
} PARTITION;

typedef struct Mbr{
	unsigned int version;
	unsigned int sector_size;
	unsigned int initial_byte;
	unsigned int num_partitions;
	PARTITION* disk_partitions;
} MBR;
/* **************************************************************** */
// Auxiliary functions
int init();
int read_MBR_from_disk(BYTE* master_sector, MBR* mbr);
int initialize_superblock(int partition, int sectors_per_block);
int write_superblock_to_partition(int partition);
void calculate_checksum(T_SUPERBLOCK* sb) ;
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
int root = -1 ;
char* mount_path; // uma string "/{partition name}...." ?
// duvidas:
// como fazer a string de montagem dinamicamente em c

T_INODE dummy_inode;

FILE2 open_files[MAX_FILES_OPEN];
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
	BYTE* master_sector = (BYTE*)malloc(SECTOR_SIZE);
	if(read_sector(SECTOR_DISK_MBR, master_sector) != SUCCESS) {
		return failed("Failed to read MBR"); }
	// Read disk master boot record into application space
	if(read_MBR_from_disk(master_sector, &disk_mbr) != SUCCESS) {
		return failed("Fu"); }
	// Alloc a superblock per existing partition
	partition_superblocks = (T_SUPERBLOCK*)
			malloc(sizeof(T_SUPERBLOCK)*disk_mbr.num_partitions);


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
	T_INODE* inodes = (T_INODE*)malloc(INODES_PER_SECTOR * sizeof(T_INODE));
	for (int i = 0 ; i < INODES_PER_SECTOR; i++){
		memcpy( &(inodes[i]), &dummy_inode, sizeof(T_INODE));
	}
	// Area de inodes em blks * setores por blk
	printf("Tamanho de um inode: %d", (int) sizeof(T_INODE));
	for (int sector=0; sector < sb->inodeAreaSize * sb->blockSize; sector++){
		if( write_sector(sector, (unsigned char*)inodes) != SUCCESS){
			free(inodes);
			return(failed("Failed to write inode sector in disk"));
		}
	}
	free(inodes);
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
	return -1;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para remover (apagar) um arquivo do disco.
-----------------------------------------------------------------------------*/
int delete2 (char *filename) {
	return -1;
}

/*-----------------------------------------------------------------------------
Função:	Função que abre um arquivo existente no disco.
-----------------------------------------------------------------------------*/
FILE2 open2 (char *filename) {
	return -1;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para fechar um arquivo.
-----------------------------------------------------------------------------*/
int close2 (FILE2 handle) {
	return -1;
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
	return -1;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para ler as entradas de um diretório.
-----------------------------------------------------------------------------*/
int readdir2 (DIRENT2 *dentry) {
	return -1;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para fechar um diretório.
-----------------------------------------------------------------------------*/
int closedir2 (void) {
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
