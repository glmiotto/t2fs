
#ifndef __BITMAP2__
#define	__BITMAP2__

#define	BITMAP_INODE	0
#define	BITMAP_DADOS	1


/*------------------------------------------------------------------------
Função:	Abre os bitmaps de uma partição
Entra:	Número do setor onde se encontra o superbloco
Retorna: ==0, se sucesso
		 !=0, se erro
------------------------------------------------------------------------*/
int openBitmap2 (int superbloco_sector);

/*------------------------------------------------------------------------
Função:	Fecha os bitmaps de uma partição.
		Garante que as informações que estão em cache serão atualizadas no disco
Entra:	-
Retorna: ==0, se sucesso
		 !=0, se erro
------------------------------------------------------------------------*/
int closeBitmap2 (void);

/*------------------------------------------------------------------------
	Recupera o bit indicado do bitmap solicitado
Entra:
	handle -> bitmap
		==0 -> i-node
		!=0 -> blocos de dados
	bitNumber -> bit a ser retornado
Retorna:
	Sucesso: valor do bit: ZERO ou UM (0 ou 1)
	Erro: número negativo
------------------------------------------------------------------------*/
int	getBitmap2 (int handle, int bitNumber);

/*------------------------------------------------------------------------
	Seta o bit indicado do bitmap solicitado
Entra:
	handle -> bitmap
		==0 -> i-node
		!=0 -> blocos de dados
	bitNumber -> bit a ser retornado
	bitValue -> valor a ser escrito no bit
		==0 -> coloca bit em 0
		!=0 -> coloca bit em 1
Retorna
	Sucesso: ZERO (0)
	Erro: número negativo
------------------------------------------------------------------------*/
int	setBitmap2 (int handle, int bitNumber, int bitValue);

/*------------------------------------------------------------------------
	Procura no bitmap solicitado pelo valor indicado
Entra:
	handle -> bitmap
		==0 -> i-node
		!=0 -> blocos de dados
	bitValue -> valor procurado
Retorna
	Sucesso
		Achou o bit: índice associado ao bit (número positivo)
		Não achou: ZERO
	Erro: número negativo
------------------------------------------------------------------------*/
int	searchBitmap2 (int handle, int bitValue);

#endif
