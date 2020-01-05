

#ifndef __LIBT2FS___
#define __LIBT2FS___

typedef int FILE2;
typedef int boolean;

typedef unsigned char BYTE;
typedef unsigned short int WORD;
typedef unsigned int DWORD;

#pragma pack(push, 1)

#define DIRENT_MAX_NAME_SIZE 255
typedef struct {
    char    name[DIRENT_MAX_NAME_SIZE+1]; /* Nome do arquivo cuja entrada foi lida do disco      */
    BYTE    fileType;                   /* Tipo do arquivo: regular (0x01) ou diret�rio (0x02) */
    DWORD   fileSize;                   /* Numero de bytes do arquivo                          */
} DIRENT2;

#pragma pack(pop)

typedef struct t2fs_superbloco 	T_SUPERBLOCK;
typedef struct t2fs_inode 			T_INODE;
typedef struct t2fs_record 		 	T_RECORD;
#define SUCCESS 0
#define FAILED -1
#define INVALID 0
#define SECTOR_SIZE 256
#define INODE_SIZE_BYTES 32
#define ENTRY_SIZE_BYTES sizeof(T_RECORD)
#define DATA_PTR_SIZE_BYTES sizeof(DWORD)
#define INODES_PER_SECTOR (SECTOR_SIZE / INODE_SIZE_BYTES)
#define ENTRIES_PER_SECTOR (SECTOR_SIZE / ENTRY_SIZE_BYTES)

#define	BITMAP_INODES	0
#define	BITMAP_BLOCKS	1
#define BIT_FREE 0
#define BIT_OCCUPIED 1

#define FIRST_VALID_BIT 1
#define NOT_FOUND 0

#define	TYPEVAL_INVALIDO	0x00
#define	TYPEVAL_REGULAR		0x01
#define	TYPEVAL_LINK		0x02

#define MAX_FILENAME_SIZE 50
#define MAX_FILES_OPEN 10

#define DEFAULT_ENTRY 0
#define SECTOR_DISK_MBR 0
#define SECTOR_PARTITION_SUPERBLOCK 0
#define ROOT_INODE 0
#define error() printf("Error thrown at %s:%s:%d\n",FILE,__FUNCTION__,LINE);
/* **************************************************************** */
/* **************************************************************** */

typedef struct Open_file{
  WORD      handle;
  DWORD     inode_index;
  DWORD     current_pointer;
  T_INODE*   inode;
  T_RECORD*  record;
} T_FOPEN;

typedef struct Directory{
  boolean   open;
	T_INODE*  inode;
	DWORD     inode_index;
  T_RECORD* current_entry;
	DWORD 		entry_index;
	DWORD     valid_entry_counter;
  DWORD     total_entries;
  DWORD     max_entries;
  T_FOPEN   open_files[MAX_FILES_OPEN];
  DWORD     num_open_files;
} T_DIRECTORY;

typedef struct Partition{
	DWORD    initial_sector;
	DWORD    final_sector;
	BYTE     partition_name[24];
} PARTITION;

typedef struct WorkingPartition{
	DWORD 				id;
	PARTITION*    mbr_data;
  T_SUPERBLOCK* superblock;
  DWORD         fst_inode_sector;
  DWORD         fst_data_sector;
  T_DIRECTORY*  root;
  DWORD         pointers_per_block;
  DWORD         entries_per_block;
  DWORD         total_inodes;
  DWORD         max_inodes;
} T_MOUNTED;

typedef struct Mbr{
	DWORD      version;
	DWORD      sector_size;
	DWORD      initial_byte;
	DWORD      num_partitions;
	PARTITION* disk_partitions;
} MBR;

int init();
int initialize_superblock(T_SUPERBLOCK* sb, int partition, int sectors_per_block);
int init_open_files();
int calculate_checksum(T_SUPERBLOCK sb);
int initialize_inode_area(T_SUPERBLOCK* sb, int partition);
int initialize_bitmaps(T_SUPERBLOCK* sb, int partition, int sectors_per_block);

int load_mbr(BYTE* master_sector, MBR* mbr);
int load_superblock();
int load_root();

// Validation
boolean is_root_open();
boolean is_root_loaded();
boolean is_mounted();
T_MOUNTED* get_mounted();
boolean is_valid_filename(char* filename);

// Conversion from/to little-endian unsigned chars
DWORD to_int(BYTE* chars, int num_bytes);
BYTE* DWORD_to_BYTE(DWORD value, int num_bytes);
BYTE* WORD_to_BYTE(WORD value, int num_bytes);

// Conversion disk<->logical structures
int BYTE_to_SUPERBLOCK(BYTE* bytes, T_SUPERBLOCK* sb);
int BYTE_to_INODE(BYTE* sector_buffer, int inode_index, T_INODE* inode);
int INODE_to_BYTE(T_INODE* inode, BYTE* bytes);
int BYTE_to_DIRENTRY(BYTE* data, DIRENT2* dentry);
int DIRENTRY_to_BYTE(DIRENT2* dentry, BYTE* bytes);
int RECORD_to_DIRENTRY(T_RECORD* record, DIRENT2* dentry);

/* Utils */
int failed(char* msg);
void print(char* msg);
void* null(char* msg);

void print_RECORD(T_RECORD* record);
void report_superblock();
void report_open_files();
BYTE*     alloc_sector();
T_INODE*  alloc_inode (DWORD quantity);
T_RECORD* alloc_record(DWORD quantity);
DIRENT2*  alloc_dentry(DWORD quantity);
T_INODE*  blank_inode();
T_RECORD* blank_record();
DIRENT2*  blank_dentry();

int teste_superblock(MBR* mbr, T_SUPERBLOCK* sb);

/* Index mapping & search */
int get_data_block_index(T_INODE* inode, DWORD cur_block_number);
int insert_data_block_index(T_INODE* inode, DWORD inode_index, DWORD cur_block_number, DWORD index);

DWORD map_inode_to_sector(int inode_index);
int access_inode(int inode_index, T_INODE* return_inode);
int new_file(char* filename, T_INODE** inode);

int save_inode(DWORD index, T_INODE* inode);
int update_inode(DWORD index, T_INODE inode);
int save_superblock();

int write_block(DWORD block_index, BYTE* data_buffer, DWORD initial_byte, int data_size );
int read_block(DWORD block_index, BYTE* data_buffer, DWORD initial_byte, int data_size );
int wipe_block(DWORD block_index);

// Directory
int find_entry(char* filename, T_RECORD** record);
int find_entry_in_block(DWORD entry_block, char* filename, T_RECORD* record);
int find_indirect_entry(DWORD index_block, char* filename, T_RECORD* record);

int new_entry(T_RECORD* record);

int delete_entry(char* filename);
int delete_indirect_entry(DWORD index_block, char* filename);
int delete_entry_in_block(DWORD entry_block, char* filename);

// Bitmap
int next_bitmap_index(int bitmap_handle, int bit_value);
int set_bitmap_index(int bitmap_handle, DWORD index, int bit_value);

int remove_pointer_from_bitmap(DWORD number, WORD handle);
int remove_file_content(T_INODE* inode);
int iterate_singlePtr(DWORD indirection_block);
int iterate_doublePtr(T_INODE* inode, DWORD double_indirection_block);

/* **************************************************************** */

/*-----------------------------------------------------------------------------
Fun��o: Usada para identificar os desenvolvedores do T2FS.
	Essa fun��o copia um string de identifica��o para o ponteiro indicado por "name".
	Essa c�pia n�o pode exceder o tamanho do buffer, informado pelo par�metro "size".
	O string deve ser formado apenas por caracteres ASCII (Valores entre 0x20 e 0x7A) e terminado por �\0�.
	O string deve conter o nome e n�mero do cart�o dos participantes do grupo.

Entra:	name -> buffer onde colocar o string de identifica��o.
	size -> tamanho do buffer "name" (n�mero m�ximo de bytes a serem copiados).

Sa�da:	Se a opera��o foi realizada com sucesso, a fun��o retorna "0" (zero).
	Em caso de erro, ser� retornado um valor diferente de zero.
-----------------------------------------------------------------------------*/
int identify2 (char *name, int size);


/*-----------------------------------------------------------------------------
Fun��o:	Formata uma parti��o do disco virtual.
		Uma parti��o deve ser montada, antes de poder ser montada para uso.

Entra:	partition -> n�mero da parti��o a ser formatada
		sectors_per_block -> n�mero de setores que formam um bloco, para uso na formata��o da parti��o

Sa�da:	Se a opera��o foi realizada com sucesso, a fun��o retorna "0" (zero).
		Em caso de erro, ser� retornado um valor diferente de zero.
-----------------------------------------------------------------------------*/
int format2(int partition, int sectors_per_block);


/*-----------------------------------------------------------------------------
Fun��o:	Monta a parti��o indicada por "partition" no diret�rio raiz

Entra:	partition -> n�mero da parti��o a ser montada

Sa�da:	Se a opera��o foi realizada com sucesso, a fun��o retorna "0" (zero).
		Em caso de erro, ser� retornado um valor diferente de zero.
-----------------------------------------------------------------------------*/
int mount(int partition);


/*-----------------------------------------------------------------------------
Fun��o:	Desmonta a parti��o atualmente montada, liberando o ponto de montagem.

Entra:	-

Sa�da:	Se a opera��o foi realizada com sucesso, a fun��o retorna "0" (zero).
		Em caso de erro, ser� retornado um valor diferente de zero.
-----------------------------------------------------------------------------*/
int umount(void);


/*-----------------------------------------------------------------------------
Fun��o: Criar um novo arquivo.
	O nome desse novo arquivo � aquele informado pelo par�metro "filename".
	O contador de posi��o do arquivo (current pointer) deve ser colocado na posi��o zero.
	Caso j� exista um arquivo com o mesmo nome, a fun��o dever� retornar um erro de cria��o.
	A fun��o deve retornar o identificador (handle) do arquivo.
	Esse handle ser� usado em chamadas posteriores do sistema de arquivo para fins de manipula��o do arquivo criado.

Entra:	filename -> nome do arquivo a ser criado.

Sa�da:	Se a opera��o foi realizada com sucesso, a fun��o retorna o handle do arquivo (n�mero positivo).
	Em caso de erro, deve ser retornado um valor negativo.
-----------------------------------------------------------------------------*/
FILE2 create2 (char *filename);


/*-----------------------------------------------------------------------------
Fun��o:	Apagar um arquivo do disco.
	O nome do arquivo a ser apagado � aquele informado pelo par�metro "filename".

Entra:	filename -> nome do arquivo a ser apagado.

Sa�da:	Se a opera��o foi realizada com sucesso, a fun��o retorna "0" (zero).
	Em caso de erro, ser� retornado um valor diferente de zero.
-----------------------------------------------------------------------------*/
int delete2 (char *filename);


/*-----------------------------------------------------------------------------
Fun��o:	Abre um arquivo existente no disco.
	O nome desse novo arquivo � aquele informado pelo par�metro "filename".
	Ao abrir um arquivo, o contador de posi��o do arquivo (current pointer) deve ser colocado na posi��o zero.
	A fun��o deve retornar o identificador (handle) do arquivo.
	Esse handle ser� usado em chamadas posteriores do sistema de arquivo para fins de manipula��o do arquivo criado.
	Todos os arquivos abertos por esta chamada s�o abertos em leitura e em escrita.
	O ponto em que a leitura, ou escrita, ser� realizada � fornecido pelo valor current_pointer (ver fun��o seek2).

Entra:	filename -> nome do arquivo a ser apagado.

Sa�da:	Se a opera��o foi realizada com sucesso, a fun��o retorna o handle do arquivo (n�mero positivo)
	Em caso de erro, deve ser retornado um valor negativo
-----------------------------------------------------------------------------*/
FILE2 open2 (char *filename);


/*-----------------------------------------------------------------------------
Fun��o:	Fecha o arquivo identificado pelo par�metro "handle".

Entra:	handle -> identificador do arquivo a ser fechado

Sa�da:	Se a opera��o foi realizada com sucesso, a fun��o retorna "0" (zero).
	Em caso de erro, ser� retornado um valor diferente de zero.
-----------------------------------------------------------------------------*/
int close2 (FILE2 handle);


/*-----------------------------------------------------------------------------
Fun��o:	Realiza a leitura de "size" bytes do arquivo identificado por "handle".
	Os bytes lidos s�o colocados na �rea apontada por "buffer".
	Ap�s a leitura, o contador de posi��o (current pointer) deve ser ajustado para o byte seguinte ao �ltimo lido.

Entra:	handle -> identificador do arquivo a ser lido
	buffer -> buffer onde colocar os bytes lidos do arquivo
	size -> n�mero de bytes a serem lidos

Sa�da:	Se a opera��o foi realizada com sucesso, a fun��o retorna o n�mero de bytes lidos.
	Se o valor retornado for menor do que "size", ent�o o contador de posi��o atingiu o final do arquivo.
	Em caso de erro, ser� retornado um valor negativo.
-----------------------------------------------------------------------------*/
int read2 (FILE2 handle, char *buffer, int size);


/*-----------------------------------------------------------------------------
Fun��o:	Realiza a escrita de "size" bytes no arquivo identificado por "handle".
	Os bytes a serem escritos est�o na �rea apontada por "buffer".
	Ap�s a escrita, o contador de posi��o (current pointer) deve ser ajustado para o byte seguinte ao �ltimo escrito.

Entra:	handle -> identificador do arquivo a ser escrito
	buffer -> buffer de onde pegar os bytes a serem escritos no arquivo
	size -> n�mero de bytes a serem escritos

Sa�da:	Se a opera��o foi realizada com sucesso, a fun��o retorna o n�mero de bytes efetivamente escritos.
	Em caso de erro, ser� retornado um valor negativo.
-----------------------------------------------------------------------------*/
int write2 (FILE2 handle, char *buffer, int size);


/*-----------------------------------------------------------------------------
Fun��o:	Abre o diret�rio raiz da parti��o ativa.
		Se a opera��o foi realizada com sucesso,
		a fun��o deve posicionar o ponteiro de entradas (current entry) na primeira posi��o v�lida do diret�rio.

Entra:	-

Sa�da:	Se a opera��o foi realizada com sucesso, a fun��o retorna "0" (zero).
		Em caso de erro, ser� retornado um valor diferente de zero.
-----------------------------------------------------------------------------*/
int opendir2 (void);


/*-----------------------------------------------------------------------------
Fun��o:	Realiza a leitura das entradas do diret�rio aberto
		A cada chamada da fun��o � lida a entrada seguinte do diret�rio
		Algumas das informa��es dessas entradas devem ser colocadas no par�metro "dentry".
		Ap�s realizada a leitura de uma entrada, o ponteiro de entradas (current entry) ser� ajustado para a  entrada v�lida seguinte.
		S�o considerados erros:
			(a) qualquer situa��o que impe�a a realiza��o da opera��o
			(b) t�rmino das entradas v�lidas do diret�rio aberto.

Entra:	dentry -> estrutura de dados onde a fun��o coloca as informa��es da entrada lida.

Sa�da:	Se a opera��o foi realizada com sucesso, a fun��o retorna "0" (zero).
		Em caso de erro, ser� retornado um valor diferente de zero ( e "dentry" n�o ser� v�lido)
-----------------------------------------------------------------------------*/
int readdir2 (DIRENT2 *dentry);


/*-----------------------------------------------------------------------------
Fun��o:	Fecha o diret�rio identificado pelo par�metro "handle".

Entra:	-

Sa�da:	Se a opera��o foi realizada com sucesso, a fun��o retorna "0" (zero).
		Em caso de erro, ser� retornado um valor diferente de zero.
-----------------------------------------------------------------------------*/
int closedir2 (void);


/*-----------------------------------------------------------------------------
Fun��o:	Cria um link simb�lico (soft link)

Entra:	linkname -> nome do link
		filename -> nome do arquivo apontado pelo link

Sa�da:	Se a opera��o foi realizada com sucesso, a fun��o retorna "0" (zero).
	Em caso de erro, ser� retornado um valor diferente de zero.
-----------------------------------------------------------------------------*/
int sln2(char *linkname, char *filename);


/*-----------------------------------------------------------------------------
Fun��o:	Cria um link estrito (hard link)

Entra:	linkname -> nome do link
		filename -> nome do arquivo apontado pelo link

Sa�da:	Se a opera��o foi realizada com sucesso, a fun��o retorna "0" (zero).
	Em caso de erro, ser� retornado um valor diferente de zero.
-----------------------------------------------------------------------------*/
int hln2(char *linkname, char *filename);




#endif
