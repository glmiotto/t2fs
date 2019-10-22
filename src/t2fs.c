
/**
*/
#include "t2fs.h"
#include "t2disk.h"
#include "apidisk.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
/* **************************************************************** */
typedef unsigned char BYTE;
#define SUCCESS 0
#define FAILED -1
#define SECTOR_SIZE 256
#define error() printf("Error thrown at %s:%s:%d\n",FILE,_FUNCTION__,LINE);

// Debugging
int failed(char* msg) {printf("%s\n", msg);return FAILED;}
void print(char* msg) {printf("%s\n", msg);}
void* null(char* msg) {printf("%s\n", msg);return (void*)NULL;}
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


// GLOBAL VARIABLES
FILE2 open_files[10];
// Maximum of 10 open file handles at once
//(***can be same file multiple times!!!)
int fopen_count = 0;

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
	strncpy((char*)bytes, (char*)"\0", 4 );
	for (int i = 0; i < num_bytes; ++i) {
		bytes[i] = (value >> (8*i))&0xFF;
	}
	return bytes;
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

int init_superblock(unsigned char* mbr) {
	return SUCCESS;
}


int read_MBR(BYTE* master_sector, MBR* mbr) {
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

	// flip it and reverse it
	sb->Checksum = ~checksum;
}

/*-----------------------------------------------------------------------------
Função:	Formata logicamente uma partição do disco virtual t2fs_disk.dat para o sistema de
		arquivos T2FS definido usando blocos de dados de tamanho
		corresponde a um múltiplo de setores dados por sectors_per_block.
-----------------------------------------------------------------------------*/
int format2(int partition, int sectors_per_block) {

	BYTE* master_sector = (BYTE*)malloc(SECTOR_SIZE);
	if(read_sector(0, master_sector) != SUCCESS) return failed("Failed to read MBR");
	MBR* disk_mbr = (MBR*)malloc(sizeof(MBR));
	// quero saber se a partition eh valida
	// tem que ler 2 bytes no MBR pra saber qtd de partitions
	if( read_MBR(master_sector, disk_mbr) != SUCCESS) return failed("Fu");

	if (partition >= disk_mbr->num_partitions) return failed("Invalid partition bye");

	int first = disk_mbr->disk_partitions[partition].initial_sector;
	int last  = disk_mbr->disk_partitions[partition].final_sector;
	int num_sectors = last - first + 1;

	int num_blocks_formatted = num_sectors / sectors_per_block;

	// alocar um superbloco
	struct t2fs_superbloco* sb = (struct t2fs_superbloco*)malloc(sizeof(struct t2fs_superbloco));
	// inicializar
	strncpy(sb->id, "T2FS", 4); //ou talvez "SF2T"...
	sb->version = 0x7E32; //ou 0x327E
	sb->superblockSize = 1; // ou 0x01 0x00
	sb->blockSize = sectors_per_block;
	sb->diskSize = num_blocks_formatted;
	// bitmap has "num_blocks_formatted" bits
	sb->freeBlocksBitmapSize = sb->diskSize / 8 / (disk_mbr->sector_size * sectors_per_block);

	// 10% of the partition blocks are reserved to inodes (ROUND UP)
	sb->inodeAreaSize = (int)(ceil(0.10*sb->diskSize)); // qty in blocks
	sb->freeInodeBitmapSize = sb->inodeAreaSize / 8 / (disk_mbr->sector_size * sectors_per_block);

	calculate_checksum(sb);

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
Função:	Função usada para truncar um arquivo. Remove do arquivo
		todos os bytes a partir da posição atual do contador de posição
		(current pointer), inclusive, até o seu final.
-----------------------------------------------------------------------------*/
int truncate2 (FILE2 handle) {
	return -1;
}

/*-----------------------------------------------------------------------------
Função:	Altera o contador de posição (current pointer) do arquivo.
-----------------------------------------------------------------------------*/
int seek2 (FILE2 handle, DWORD offset) {
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
Função:	Função usada para criar um caminho alternativo (softlink) com
		o nome dado por linkname (relativo ou absoluto) para um
		arquivo ou diretório fornecido por filename.
-----------------------------------------------------------------------------*/
int ln2 (char *linkname, char *filename) {
	return -1;
}
