
/**
*/
#include "t2fs.h"
#include "t2disk.h"
#include "apidisk.h"
#include "bitmap2.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
/* **************************************************************** */
typedef unsigned char BYTE;
#define SUCCESS 0
#define FAILED -1
#define SECTOR_SIZE 256

#define	BITMAP_INODE	0
#define	BITMAP_DADOS	1

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
void calculate_checksum(struct t2fs_superbloco* sb) ;
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
struct t2fs_superbloco* partition_superblocks;
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
	partition_superblocks = (struct t2fs_superbloco*)
			malloc(sizeof(struct t2fs_superbloco)*disk_mbr.num_partitions);


	t2fs_initialized = true;
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

int initialize_superblock(int partition, int sectors_per_block) {

	if (partition >= disk_mbr.num_partitions) return failed("Invalid partition bye");

	int first_sector = disk_mbr.disk_partitions[partition].initial_sector;
	int last_sector  = disk_mbr.disk_partitions[partition].final_sector;
	int num_sectors = last_sector - first_sector + 1;
	// Gets pointer to superblock for legibility
	struct t2fs_superbloco* sb = &(partition_superblocks[partition]);

	strncpy(sb->id, "T2FS", 4); //ou talvez "SF2T"...
	sb->version = 0x7E32; //ou 0x327E
	sb->superblockSize = 1; // ou 0x01 0x00
	sb->blockSize = sectors_per_block;
	sb->diskSize = num_sectors / sectors_per_block; //Number of logical blocks in formatted disk.
	// bitmap has "num_blocks_formatted" bits
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
	write_superblock_to_partition(partition);

	return SUCCESS;
}

int write_superblock_to_partition(int partition) {
	BYTE* output = (BYTE*)malloc(SECTOR_SIZE*sizeof(BYTE));
	// Gets pointer to application-space superblock
	struct t2fs_superbloco* sb = &(partition_superblocks[partition]);

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
		disk_mbr.disk_partitions[partition].initial_sector,
		output) != SUCCESS) {
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

void calculate_checksum(struct t2fs_superbloco* sb) {
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
/*-----------------------------------------------------------------------------
Função:	Formata logicamente uma partição do disco virtual t2fs_disk.dat para o sistema de
		arquivos T2FS definido usando blocos de dados de tamanho
		corresponde a um múltiplo de setores dados por sectors_per_block.
-----------------------------------------------------------------------------*/
int format2(int partition, int sectors_per_block) {

	if(init() != SUCCESS) return failed("Failed to initialize.");

	if( initialize_superblock(partition, sectors_per_block) != SUCCESS){
		return failed("Failed to read superblock.");
	}

	// Allocates both Bitmaps (block status and inode status)
	// Input: disk sector where the partition's superblock is
	// (aka, the first sector in a formatted partition)
	// Output: success/error code
	if( openBitmap2(disk_mbr.disk_partitions[partition].initial_sector) != SUCCESS){
		// provavelmente desalocar alguma coisa
		return failed("Carissimi nao gostou do meu superbloco");
	}

	// Allocate all i-nodes to fill inodeAreaSize ???

	// Afterwards: the rest is data blocks.
	// first block onwards after inodes is reserved to
	// file data, directory files, and index blocks for big files.

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
DIR2 opendir2 (char *pathname) {
	return -1;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para ler as entradas de um diretório.
-----------------------------------------------------------------------------*/
int readdir2 (DIR2 handle, DIRENT2 *dentry) {
	return -1;
}

/*-----------------------------------------------------------------------------
Função:	Função usada para fechar um diretório.
-----------------------------------------------------------------------------*/
int closedir2 (DIR2 handle) {
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
