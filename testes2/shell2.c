
//
// T2FS Shell 2019-02
//


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t2fs.h"


void cmdMan(void);

void cmdWho(void);

void cmdFormat(void);
void cmdMount(void);
void cmdUmount(void);

void cmdLs(void);

void cmdOpen(void);
void cmdRead(void);
void cmdClose(void);

void cmdWrite(void);
void cmdCreate(void);
void cmdDelete(void);

void cmdSln(void);
void cmdHln(void);

void cmdCp(void);
void cmdFscp(void);

void cmdExit(void);

static void dump(char *buffer, int size) {
    int base, i;
    char c;
    for (base=0; base<size; base+=16) {
        printf ("%04d ", base);
        for (i=0; i<16; ++i) {
            if (base+i<size) printf ("%02X ", buffer[base+i]);
            else printf ("   ");
        }

        printf (" *");

        for (i=0; i<16; ++i) {
            if (base+i<size) c = buffer[base+i];
            else c = ' ';

            if (c<' ' || c>'z' ) c = ' ';
            printf ("%c", c );
        }
        printf ("*\n");
    }
}



char helpExit[]  = "             -> finish this shell";
char helpMan[]   = "[comando]    -> command help";
char helpWho[]   = "             -> shows T2FS authors";
char helpLs[]    = "[pahname]    -> list files in [pathname]";

char helpOpen[]  = "[file]       -> open [file] from T2FS";
char helpRead[]  = "[hdl] [siz]  -> read [siz] bytes from file [hdl]";
char helpClose[] = "[hdl         -> close [hdl]";
char helpWrite[] = "[hdl] [str]  -> write [str] bytes to file [hdl]";
char helpCreate[]= "[file]       -> create new [file] in T2FS";
char helpDelete[]= "[file]       -> deletes [file] from T2FS";

char helpHln[]    = "[lnk] [file] -> create hardlink [lnk] to [file]";
char helpSln[]    = "[sln] [file] -> create softlink [sln] to [file]";

char helpCopy[]   = "[src] [dst]  -> copy files: [src] -> [dst]";
char helpFscp[]	  = "[src] [dst]  -> copy files: [src] -> [dst]"
				      "\n    fscp -t [src] [dst]  -> copy HostFS to T2FS"
				      "\n    fscp -f [src] [dst]  -> copy T2FS   to HostFS";

char helpFormat[] = "[part] [spb] -> format partition p with s sectors per block";
char helpMount[]  = "[part]       -> mount partition p";
char helpUmount[] = "             -> unmount actual partition";
					


	
struct {
	char name[20];
	char *helpString;
	void (*f)(void);
} cmdList[] = {
	{ "exit", helpExit, cmdExit }, { "x", helpExit, cmdExit },
	{ "man", helpMan, cmdMan },
	{ "who", helpWho, cmdWho }, { "id", helpWho, cmdWho },
	{ "dir", helpLs, cmdLs }, { "ls", helpLs, cmdLs },

	
	{ "open", helpOpen, cmdOpen },
	{ "read", helpRead, cmdRead }, { "rd", helpRead, cmdRead },
	{ "close", helpClose, cmdClose }, { "cl", helpClose, cmdClose },
	{ "write", helpWrite, cmdWrite }, { "wr", helpWrite, cmdWrite },
	{ "create", helpCreate, cmdCreate }, { "cr", helpCreate, cmdCreate },
	{ "delete", helpDelete, cmdDelete }, { "del", helpDelete, cmdDelete },

	
	{ "sln", helpSln, cmdSln },
        { "hln", helpHln, cmdHln },

        { "format", helpFormat, cmdFormat },
        { "mount", helpMount, cmdMount },
        { "umount", helpUmount, cmdUmount },
	
	
	{ "cp", helpCopy, cmdCp },
	{ "fscp", helpFscp, cmdFscp },
	{ "fim", helpExit, NULL }
};




int main()
{
    char cmd[256];
    char *token;
    int i;
    int flagAchou, flagEncerrar;

    printf ("Testing for T2FS - v 2019-02\n");

    strcpy(cmd, "man");
    token = strtok(cmd," \t");
    cmdMan();

    flagEncerrar = 0;
    while (1) {
        printf ("T2FS> ");
        gets(cmd);
        if( (token = strtok(cmd," \t")) != NULL ) {

			flagAchou = 0;
			for (i=0; strcmp(cmdList[i].name,"fim")!=0; i++) {
				if (strcmp(cmdList[i].name, token)==0) {
					flagAchou = 1;
					cmdList[i].f();
					if (cmdList[i].f==cmdExit) {
						flagEncerrar = 1;
						break;
					}
				}
			}
			if (!flagAchou) printf ("???\n");
        }

        if (flagEncerrar) break;
    }
    return 0;
}

/**
Encerra a operação do teste
*/
void cmdExit(void) {
    printf ("bye, bye!\n");
}

/**
Informa os comandos aceitos pelo programa de teste
*/
void cmdMan(void) {
	int i;
	char *token = strtok(NULL," \t");
	
	// man sem nome de comando
	if (token==NULL) {
		for (i=0; strcmp(cmdList[i].name,"fim")!=0; i++) {
			printf ("%-10s", cmdList[i].name);
			if (i%6==5) printf ("\n");
		}
		printf ("\n");
		return;
	}
	
	// man com nome de comando
	for (i=0; strcmp(cmdList[i].name,"fim")!=0; i++) {
		if (strcmp(cmdList[i].name,token)==0) {
			printf ("%-10s %s\n", cmdList[i].name, cmdList[i].helpString);
		}
	}
	

}
	
/**
Chama da função identify2 da biblioteca e coloca o string de retorno na tela
*/
void cmdWho(void) {
    char name[256];
    int err = identify2(name, 256);
    if (err) {
        printf ("Erro: %d\n", err);
        return;
    }
    printf ("%s\n", name);
}

/**
Copia arquivo dentro do T2FS
Os parametros são:
    primeiro parametro => arquivo origem
    segundo parametro  => arquivo destino
*/
void cmdCp(void) {

    // Pega os nomes dos arquivos origem e destion
    char *src = strtok(NULL," \t");
    char *dst = strtok(NULL," \t");
    if (src==NULL || dst==NULL) {
        printf ("Missing parameter\n");
        return;
    }
    // Abre o arquivo origem, que deve existir
    FILE2 hSrc = open2 (src);
    if (hSrc<0) {
        printf ("Open source file error: %d\n", hSrc);
        return;
    }
    // Cria o arquivo de destino, que será resetado se existir
    FILE2 hDst = create2 (dst);
    if (hDst<0) {
        close2(hSrc);
        printf ("Create destination file error: %d\n", hDst);
        return;
    }
    // Copia os dados de source para destination
    char buffer[2];
    while( read2(hSrc, buffer, 1) == 1 ) {
        write2(hDst, buffer, 1);
    }
    // Fecha os arquicos
    close2(hSrc);
    close2(hDst);

    printf ("Files successfully copied\n");
}

/**
Copia arquivo de um sistema de arquivos para o outro
Os parametros são:
    primeiro parametro => direção da copia
        -t copiar para o T2FS
        -f copiar para o FS do host
    segundo parametro => arquivo origem
    terceiro parametro  => arquivo destino
*/
void cmdFscp(void) {
    // Pega a direção e os nomes dos arquivos origem e destion
    char *direcao = strtok(NULL, " \t");
    char *src = strtok(NULL," \t");
    char *dst = strtok(NULL," \t");
    if (direcao==NULL || src==NULL || dst==NULL) {
        printf ("Missing parameter\n");
        return;
    }
    // Valida direção
    if (strncmp(direcao, "-t", 2)==0) {
        // src == host
        // dst == T2FS

        // Abre o arquivo origem, que deve existir
        FILE *hSrc = fopen(src, "r+");
        if (hSrc==NULL) {
            printf ("Open source file error\n");
            return;
        }
        // Cria o arquivo de destino, que será resetado se existir
        FILE2 hDst = create2 (dst);
        if (hDst<0) {
            fclose(hSrc);
            printf ("Create destination file error: %d\n", hDst);
            return;
        }
        // Copia os dados de source para destination
        char buffer[2];
        while( fread((void *)buffer, (size_t)1, (size_t)1, hSrc) == 1 ) {
            write2(hDst, buffer, 1);
        }
        // Fecha os arquicos
        fclose(hSrc);
        close2(hDst);
    }
    else if (strncmp(direcao, "-f", 2)==0) {
        // src == T2FS
        // dst == host

        // Abre o arquivo origem, que deve existir
        FILE2 hSrc = open2 (src);
        if (hSrc<0) {
            printf ("Open source file error: %d\n", hSrc);
            return;
        }
        // Cria o arquivo de destino, que será resetado se existir
        FILE *hDst = fopen(dst, "w+");
        if (hDst==NULL) {
            printf ("Open destination file error\n");
            return;
        }
        // Copia os dados de source para destination
        char buffer[2];
        while ( read2(hSrc, buffer, 1) == 1 ) {
            fwrite((void *)buffer, (size_t)1, (size_t)1, hDst);
        }
        // Fecha os arquicos
        close2(hSrc);
        fclose(hDst);
    }
    else {
        printf ("Invalid copy direction\n");
        return;
    }

    printf ("Files successfully copied\n");
}

/**
Cria o arquivo informado no parametro
Retorna eventual sinalização de erro
Retorna o HANDLE do arquivo criado
*/
void cmdCreate(void) {
    FILE2 hFile;

    char *token = strtok(NULL," \t");
    if (token==NULL) {
        printf ("Missing parameter\n");
        return;
    }

    hFile = create2 (token);
    if (hFile<0) {
        printf ("Error: %d\n", hFile);
        return;
    }

    printf ("File created with handle %d\n", hFile);
}

/**
Apaga o arquivo informado no parametro
Retorna eventual sinalização de erro
*/
void cmdDelete(void) {

    char *token = strtok(NULL," \t");
    if (token==NULL) {
        printf ("Missing parameter\n");
        return;
    }

    int err = delete2(token);
    if (err<0) {
        printf ("Error: %d\n", err);
        return;
    }

    printf ("File %s was deleted\n", token);
}

/**
Abre o arquivo informado no parametro [0]
Retorna sinalização de erro
Retorna HANDLE do arquivo retornado
*/
void cmdOpen(void) {
    FILE2 hFile;

    char *token = strtok(NULL," \t");
    if (token==NULL) {
        printf ("Missing parameter\n");
        return;
    }

    hFile = open2 (token);
    if (hFile<0) {
        printf ("Error: %d\n", hFile);
        return;
    }

    printf ("File opened with handle %d\n", hFile);
}

/**
Fecha o arquivo cujo handle é o parametro
Retorna sinalização de erro
Retorna mensagem de operação completada
*/
void cmdClose(void) {
    FILE2 handle;

    char *token = strtok(NULL," \t");
    if (token==NULL) {
        printf ("Missing parameter\n");
        return;
    }

    if (sscanf(token, "%d", &handle)==0) {
        printf ("Invalid parameter\n");
        return;
    }

    int err = close2(handle);
    if (err<0) {
        printf ("Error: %d\n", err);
        return;
    }

    printf ("Closed file with handle %d\n", handle);
}


void cmdRead(void) {
    FILE2 handle;
    int size;

    // get first parameter => file handle
    char *token = strtok(NULL," \t");
    if (token==NULL) {
        printf ("Missing parameter\n");
        return;
    }
    if (sscanf(token, "%d", &handle)==0) {
        printf ("Invalid parameter\n");
        return;
    }

    // get second parameter => number of bytes
    token = strtok(NULL," \t");
    if (token==NULL) {
        printf ("Missing parameter\n");
        return;
    }
    if (sscanf(token, "%d", &size)==0) {
        printf ("Invalid parameter\n");
        return;
    }

    // Alloc buffer for reading file
    char *buffer = malloc(size);
    if (buffer==NULL) {
        printf ("Memory full\n");
        return;
    }

    // get file bytes
    int err = read2(handle, buffer, size);
    if (err<0) {
        printf ("Error: %d\n", err);
        return;
    }
    if (err==0) {
        printf ("Empty file\n");
        return;
    }

    // show bytes read
    dump(buffer, err);
    printf ("%d bytes read from file-handle %d\n", err, handle);
    
    free(buffer);
}


void cmdSln(void) {
	char *linkname;
	int err;

    // get first parameter => link name
    char *token = strtok(NULL," \t");
    if (token==NULL) {
        printf ("Missing parameter LINKNAME\n");
        return;
    }
	linkname = token;
	
    // get second parameter => pathname
    token = strtok(NULL," \t");
    if (token==NULL) {
        printf ("Missing parameter PATHNAME\n");
        return;
    }

	// make link
    err = sln2 (linkname, token);
    if (err!=0) {
        printf ("Error: %d\n", err);
        return;
    }

    printf ("Created soft link %s to file %s\n", linkname, token);

}

void cmdHln(void) {
	char *linkname;
	int err;

    // get first parameter => link name
    char *token = strtok(NULL," \t");
    if (token==NULL) {
        printf ("Missing parameter LINKNAME\n");
        return;
    }
	linkname = token;
	
    // get second parameter => pathname
    token = strtok(NULL," \t");
    if (token==NULL) {
        printf ("Missing parameter PATHNAME\n");
        return;
    }

	// make link
    err = hln2 (linkname, token);
    if (err!=0) {
        printf ("Error: %d\n", err);
        return;
    }

    printf ("Created hard link %s to file %s\n", linkname, token);

}



void cmdWrite(void) {
    FILE2 handle;
    int size;
    int err;

    // get first parameter => file handle
    char *token = strtok(NULL," \t");
    if (token==NULL) {
        printf ("Missing parameter\n");
        return;
    }
    if (sscanf(token, "%d", &handle)==0) {
        printf ("Invalid parameter\n");
        return;
    }

    // get second parameter => string
    token = strtok(NULL," \t");
    if (token==NULL) {
        printf ("Missing parameter\n");
        return;
    }
    size = strlen(token);

    // get file bytes
    err = write2(handle, token, size);
    if (err<0) {
        printf ("Error: %d\n", err);
        return;
    }
    if (err!=size) {
        printf ("Erro: escritos %d bytes, mas apenas %d foram efetivos\n", size, err);
        return;
    }

    printf ("%d bytes writen to file-handle %d\n", err, handle);
}


void cmdLs(void) {

//    char *token = strtok(NULL," \t");
//   if (token==NULL) {
//       printf ("Missing parameter\n");
//        return;
//    }

    // Abre o diretório pedido
    int d;
    d = opendir2();
    if (d<0) {
        printf ("Open dir error: %d\n", d);
        return;
    }

    // Coloca diretorio na tela
    DIRENT2 dentry;
    while ( readdir2(&dentry) == 0 ) {
        printf ("%c %8u %s\n", (dentry.fileType==0x02?'d':'-'), dentry.fileSize, dentry.name);
    }

    closedir2();


}

// Comando Format

void cmdFormat(void) {
    int partition;
    int sectorPerBlock;

    // get first parameter => partition
    char *token = strtok(NULL," \t");
    if (token==NULL) {
        printf ("Missing parameter\n");
        return;
    }
    if (sscanf(token, "%d", &partition)==0) {
        printf ("Invalid parameter\n");
        return;
    }

    // get second parameter => number of bytes
    token = strtok(NULL," \t");
    if (token==NULL) {
        printf ("Missing parameter\n");
        return;
    }
    if (sscanf(token, "%d", &sectorPerBlock)==0) {
        printf ("Invalid parameter\n");
        return;
    }

    // seek
    int err = format2(partition, sectorPerBlock);
    if (err<0) {
        printf ("Error: %d\n", err);
        return;
    }

    printf ("Format complete -> partition %d sectors per block %d\n", partition, sectorPerBlock);
    
}

//comando mount

void cmdMount(void) {
    int partition;

    // get first parameter => partition
    char *token = strtok(NULL," \t");
    if (token==NULL) {
        printf ("Missing parameter\n");
        return;
    }
    if (sscanf(token, "%d", &partition)==0) {
        printf ("Invalid parameter\n");
        return;
    }


    // mount
    int err = mount(partition);
    if (err<0) {
        printf ("Error: %d\n", err);
        return;
    }

    printf ("Root FS mounted on partition %d\n", partition);
    
}

// comando umount
void cmdUmount(void) {
    int err = umount();
    if (err) {
        printf ("Erro: %d\n", err);
        return;
    }
    printf ("Unmount done\n");
}



