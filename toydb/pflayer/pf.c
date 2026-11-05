/* pf.c: Paged File Interface Routines+ support routines */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include "pf.h"
#include "pftypes.h"
#include "../common/config.h"

/* To keep system V and PC users happy */
#ifndef L_SET
#define L_SET 0
#endif

int PFerrno = PFE_OK;	/* last error message */
PF_Stats PF_stats;	/* global I/O profiling stats */

static PFftab_ele PFftab[PF_FTAB_SIZE]; /* table of opened files */

/* true if file descriptor fd is invalid */
#define PFinvalidFd(fd) ((fd) < 0 || (fd) >= PF_FTAB_SIZE \
				|| PFftab[fd].fname == NULL)

/* true if page number "pagenum" of file "fd" is invalid in the
sense that it's <0 or >= # of pages in the file */
#define PFinvalidPagenum(fd,pagenum) ((pagenum)<0 || (pagenum) >= \
				PFftab[fd].hdr.numpages)

/****************** Internal Support Functions *****************************/
static char *savestr(str)
char *str;		/* string to be saved */
{
char *s;

	if ((s = (char *)malloc(strlen(str)+1)) != NULL)
		strcpy(s,str);
	return(s);
}

static int PFtabFindFname(fname)
char *fname;		/* file name to find */
{
int i;

	for (i=0; i < PF_FTAB_SIZE; i++){
		if(PFftab[i].fname != NULL && strcmp(PFftab[i].fname,fname) == 0)
			return(i);
	}
	return(-1);
}

static int PFftabFindFree(void)
{
int i;

	for (i=0; i < PF_FTAB_SIZE; i++)
		if (PFftab[i].fname == NULL)
			return(i);
	return(-1);
}

int PF_GetNumOpenFiles(void)
{
int i;
int count = 0;

	for (i=0; i < PF_FTAB_SIZE; i++)
		if (PFftab[i].fname != NULL)
			count++;
	return(count);
}

int PFreadfcn(fd,pagenum,buf)
int fd;	/* file descriptor */
int pagenum; /* page number */
PFfpage *buf;
{
int error;

	if (lseek(PFftab[fd].unixfd,
		(long)pagenum*sizeof(PFfpage)+PF_HDR_SIZE, L_SET)<0){
		PFerrno = PFE_UNIX;
		return(PFerrno);
	}

	if((error=read(PFftab[fd].unixfd,(char *)buf,sizeof(PFfpage)))
			!= sizeof(PFfpage)){
		if (error < 0)
			PFerrno = PFE_UNIX;
		else
			PFerrno = PFE_INCOMPLETEREAD;
		return(PFerrno);
	}

	return(PFE_OK);
}

int PFwritefcn(fd,pagenum,buf)
int fd;		/* file descriptor */
int pagenum;	/* page number */
PFfpage *buf;	/* page buffer */
{
int error;

	if (lseek(PFftab[fd].unixfd,
		(long)pagenum*sizeof(PFfpage)+PF_HDR_SIZE, L_SET)<0){
		PFerrno = PFE_UNIX;
		return(PFerrno);
	}

	if((error=write(PFftab[fd].unixfd,(char *)buf,sizeof(PFfpage)))
			!= sizeof(PFfpage)){
		if (error <0)
			PFerrno = PFE_UNIX;
		else
			PFerrno = PFE_INCOMPLETEWRITE;
		return(PFerrno);
	}

	return(PFE_OK);
}

/******************** Interface Routines ***********************************/

void PF_Init(void)
{
int i;

	/* Auto-load toydb.cfg if present */
	config_load(NULL);
	PF_BufPoolSize = cfg_get_pool_size();

	for (i=0; i < PF_FTAB_SIZE; i++){
		PFftab[i].fname = NULL;
		PFftab[i].strategy = cfg_get_default_strategy();
	}

	PFhashInit();
	PF_ResetStats();
}

int PF_CreateFile(fname)
char *fname;	/* name of file to create */
{
int fd;		/* unix file descriptor */
PFhdr_str hdr;	/* file header */
int error;

	if ((fd=open(fname,O_CREAT|O_EXCL|O_WRONLY, 0664))<0){
		PFerrno = PFE_UNIX;
		return(PFerrno);
	}

	hdr.firstfree = PF_PAGE_LIST_END;
	hdr.numpages = 0;

	if ((error=write(fd,(char *)&hdr,sizeof(hdr))) != sizeof(hdr)){
		if (error < 0)
			PFerrno = PFE_UNIX;
		else 
			PFerrno = PFE_HDRWRITE;
		close(fd);
		unlink(fname);
		return(PFerrno);
	}

	if (close(fd) < 0){
		PFerrno = PFE_UNIX;
		return(PFerrno);
	}

	return(PFE_OK);
}

int PF_DestroyFile(fname)
char *fname;		/* file name to destroy */
{
	if (PFtabFindFname(fname) >= 0){
		PFerrno = PFE_FILEOPEN;
		return(PFerrno);
	}

	if (unlink(fname) < 0){
		PFerrno = PFE_UNIX;
		return(PFerrno);
	}

	return(PFE_OK);
}

int PF_OpenFile(fname)
char *fname;		/* file name to open */
{
	return PF_OpenFileWithStrategy(fname, cfg_get_default_strategy());
}

int PF_OpenFileWithStrategy(fname, strategy)
char *fname;		/* file name to open */
int strategy;		/* PF_STRATEGY_LRU or PF_STRATEGY_MRU */
{
int count;		/* # of bytes read */
int fd;			/* PF file descriptor */

	if ((fd=PFftabFindFree()) < 0){
		PFerrno = PFE_FTABFULL;
		return(PFerrno);
	}

	if ((PFftab[fd].fname= savestr(fname))== NULL){
		PFerrno = PFE_NOMEM;
		return(PFerrno);
	}

	if ((PFftab[fd].unixfd = open(fname,O_RDWR))< 0){
		free(PFftab[fd].fname);
		PFftab[fd].fname = NULL;
		PFerrno = PFE_UNIX;
		return(PFerrno);
	}

	if ((count=read(PFftab[fd].unixfd,(char *)&PFftab[fd].hdr,PF_HDR_SIZE))
			!= PF_HDR_SIZE){
		free(PFftab[fd].fname);
		PFftab[fd].fname = NULL;
		close(PFftab[fd].unixfd);
		if (count < 0)
			PFerrno = PFE_UNIX;
		else
			PFerrno = PFE_HDRREAD;
		return(PFerrno);
	}

	PFftab[fd].hdrchanged = FALSE;
	PFftab[fd].strategy = (strategy == PF_STRATEGY_MRU) ? PF_STRATEGY_MRU : PF_STRATEGY_LRU;
	return(fd);
}

int PF_GetFileStrategy(fd)
int fd;
{
	if (PFinvalidFd(fd)) return -1;
	return PFftab[fd].strategy;
}

int PF_GetNumPages(fd)
int fd;
{
	if (PFinvalidFd(fd)) return -1;
	return PFftab[fd].hdr.numpages;
}

int PF_CloseFile(fd)
int fd;		/* file descriptor of file to close */
{
int error;

	if (PFinvalidFd(fd)){
		PFerrno = PFE_FD;
		return(PFerrno);
	}

	/* Flush and release all buffer pages belonging to this file */
	if ((error=PFbufReleaseFile(fd,PFwritefcn))!= PFE_OK)
		return(error);

	/* Write header if changed */
	if (PFftab[fd].hdrchanged){
		if (lseek(PFftab[fd].unixfd,(long)0,L_SET)<0){
			PFerrno = PFE_UNIX;
			return(PFerrno);
		}

		if((error=write(PFftab[fd].unixfd, (char *)&PFftab[fd].hdr,
				PF_HDR_SIZE))!= PF_HDR_SIZE){
			if (error < 0)
				PFerrno = PFE_UNIX;
			else
				PFerrno = PFE_HDRWRITE;
			return(PFerrno);
		}
		PF_stats.physical_writes++;
		PFftab[fd].hdrchanged = FALSE;
	}

	if (close(PFftab[fd].unixfd) < 0){
		PFerrno = PFE_UNIX;
		return(PFerrno);
	}

	free(PFftab[fd].fname);
	PFftab[fd].fname = NULL;

	return(PFE_OK);
}

int PF_GetFirstPage(fd,pagenum,pagebuf)
int fd;	/* file descriptor */
int *pagenum;	/* page number of first page */
char **pagebuf;	/* pointer to the pointer to buffer */
{
	*pagenum = -1;
	return(PF_GetNextPage(fd,pagenum,pagebuf));
}

int PF_GetNextPage(fd,pagenum,pagebuf)
int fd;	/* file descriptor of the file */
int *pagenum;	/* old page number on input, new page number on output */
char **pagebuf;	/* pointer to pointer to buffer of page data */
{
int temppage;	/* page number to scan for next valid page */
int error;	/* error code */
PFfpage *fpage;	/* pointer to file page */

	if (PFinvalidFd(fd)){
		PFerrno = PFE_FD;
		return(PFerrno);
	}

	if (*pagenum < -1 || *pagenum >= PFftab[fd].hdr.numpages){
		PFerrno = PFE_INVALIDPAGE;
		return(PFerrno);
	}

	for (temppage= *pagenum+1; temppage < PFftab[fd].hdr.numpages; temppage++){
		if ((error=PFbufGet(fd,temppage,&fpage,PFreadfcn,
					PFwritefcn,PFftab[fd].strategy))!= PFE_OK)
			return(error);
		else if (fpage->nextfree == PF_PAGE_USED){
			*pagenum = temppage;
			*pagebuf = (char *)fpage->pagebuf;
			return(PFE_OK);
		}

		if ((error=PFbufUnfix(fd,temppage,FALSE))!= PFE_OK)
			return(error);
	}

	PFerrno = PFE_EOF;
	return(PFerrno);
}

int PF_GetThisPage(fd,pagenum,pagebuf)
int fd;		/* file descriptor */
int pagenum;	/* page number to read */
char **pagebuf;	/* pointer to pointer to page data */
{
int error;
PFfpage *fpage;

	if (PFinvalidFd(fd)){
		PFerrno = PFE_FD;
		return(PFerrno);
	}

	if (PFinvalidPagenum(fd,pagenum)){
		PFerrno = PFE_INVALIDPAGE;
		return(PFerrno);
	}

	if ((error=PFbufGet(fd,pagenum,&fpage,PFreadfcn,PFwritefcn,PFftab[fd].strategy))!= PFE_OK){
		if (error == PFE_PAGEFIXED)
			*pagebuf = fpage->pagebuf;
		return(error);
	}

	if (fpage->nextfree == PF_PAGE_USED){
		*pagebuf = (char *)fpage->pagebuf;
		return(PFE_OK);
	} else {
		if (PFbufUnfix(fd,pagenum,FALSE)!= PFE_OK){
			printf("internal error: PF_GetThisPage()\n");
			exit(1);
		}
		PFerrno = PFE_INVALIDPAGE;
		return(PFerrno);
	}
}

int PF_AllocPage(fd,pagenum,pagebuf)
int fd;		/* file descriptor */
int *pagenum;	/* page number */
char **pagebuf;	/* pointer to pointer to page buffer */
{
PFfpage *fpage;	/* pointer to file page */
int error;

	if (PFinvalidFd(fd)){
		PFerrno= PFE_FD;
		return(PFerrno);
	}

	if (PFftab[fd].hdr.firstfree != PF_PAGE_LIST_END){
		*pagenum = PFftab[fd].hdr.firstfree;
		if ((error=PFbufGet(fd,*pagenum,&fpage,PFreadfcn,
					PFwritefcn,PFftab[fd].strategy))!= PFE_OK)
			return(error);
		PFftab[fd].hdr.firstfree = fpage->nextfree;
		PFftab[fd].hdrchanged = TRUE;
	} else {
		*pagenum = PFftab[fd].hdr.numpages;
		if ((error=PFbufAlloc(fd,*pagenum,&fpage,PFwritefcn,PFftab[fd].strategy))!= PFE_OK)
			return(error);
	
		PFftab[fd].hdr.numpages++;
		PFftab[fd].hdrchanged = TRUE;

		if ((error=PFbufUsed(fd,*pagenum))!= PFE_OK){
			printf("internal error: PF_AllocPage()\n");
			exit(1);
		}
	}

	fpage->nextfree = PF_PAGE_USED;
	*pagebuf = fpage->pagebuf;
	
	PF_stats.pages_allocated++;
	PF_stats.logical_writes++;
	return(PFE_OK);
}

int PF_DisposePage(fd,pagenum)
int fd;		/* file descriptor */
int pagenum;	/* page number */
{
PFfpage *fpage;	/* pointer to file page */
int error;

	if (PFinvalidFd(fd)){
		PFerrno = PFE_FD;
		return(PFerrno);
	}

	if (PFinvalidPagenum(fd,pagenum)){
		PFerrno = PFE_INVALIDPAGE;
		return(PFerrno);
	}

	if ((error=PFbufGet(fd,pagenum,&fpage,PFreadfcn,PFwritefcn,PFftab[fd].strategy))!= PFE_OK)
		return(error);
	
	if (fpage->nextfree != PF_PAGE_USED){
		if (PFbufUnfix(fd,pagenum,FALSE)!= PFE_OK){
			printf("internal error: PF_DisposePage()\n");
			exit(1);
		}
		PFerrno = PFE_PAGEFREE;
		return(PFerrno);
	}

	fpage->nextfree = PFftab[fd].hdr.firstfree;
	PFftab[fd].hdr.firstfree = pagenum;
	PFftab[fd].hdrchanged = TRUE;

	PF_stats.pages_disposed++;
	return(PFbufUnfix(fd,pagenum,TRUE));
}

int PF_UnfixPage(fd,pagenum,dirty)
int fd;	/* file descriptor */
int pagenum;	/* page number */
int dirty;	/* true if file is dirty */
{
	if (PFinvalidFd(fd)){
		PFerrno = PFE_FD;
		return(PFerrno);
	}

	if (PFinvalidPagenum(fd,pagenum)){
		PFerrno = PFE_INVALIDPAGE;
		return(PFerrno);
	}

	return(PFbufUnfix(fd,pagenum,dirty));
}

int PF_MarkDirty(fd, pagenum)
int fd;
int pagenum;
{
PFbpage *bpage;

	if (PFinvalidFd(fd)){
		PFerrno = PFE_FD;
		return(PFerrno);
	}

	if (PFinvalidPagenum(fd,pagenum)){
		PFerrno = PFE_INVALIDPAGE;
		return(PFerrno);
	}

	if ((bpage = PFhashFind(fd, pagenum)) == NULL){
		PFerrno = PFE_PAGENOTINBUF;
		return(PFerrno);
	}

	if (!bpage->dirty){
		PF_stats.logical_writes++;
	}
	bpage->dirty = TRUE;

	return(PFE_OK);
}

void PF_GetStats(PF_Stats *stats)
{
	if (stats != NULL)
		*stats = PF_stats;
}

void PF_ResetStats(void)
{
	memset(&PF_stats, 0, sizeof(PF_stats));
}

void PF_PrintStats(void)
{
	printf("=== PF Layer I/O Statistics ===\n");
	printf("Logical Reads   : %ld\n", PF_stats.logical_reads);
	printf("Logical Writes  : %ld\n", PF_stats.logical_writes);
	printf("Physical Reads  : %ld\n", PF_stats.physical_reads);
	printf("Physical Writes : %ld\n", PF_stats.physical_writes);
	printf("Buffer Hits     : %ld\n", PF_stats.buffer_hits);
	printf("Buffer Misses   : %ld\n", PF_stats.buffer_misses);
	printf("Pages Allocated : %ld\n", PF_stats.pages_allocated);
	printf("Pages Disposed  : %ld\n", PF_stats.pages_disposed);
	if (PF_stats.logical_reads > 0){
		printf("Buffer Hit Ratio: %.2f%%\n",
			(double)PF_stats.buffer_hits * 100.0 / (double)PF_stats.logical_reads);
	}
	printf("================================\n");
}

void PF_SetBufferSize(int numBuffers)
{
	if (numBuffers >= 3) {
		PF_BufPoolSize = numBuffers;
		PFbufClearFreeList();
	}
}

int PF_GetBufferSize(void)
{
	return PF_BufPoolSize;
}

/* error messages */
static char *PFerrormsg[]={
"No error",
"No memory",
"No buffer space",
"Page already fixed in buffer",
"page to be unfixed is not in the buffer",
"unix error",
"incomplete read of page from file",
"incomplete write of page to file",
"incomplete read of header from file",
"incomplete write of header to file",
"invalid page number",
"file already open",
"file table full",
"invalid file descriptor",
"end of file",
"page already free",
"page already unfixed",
"new page to be allocated already in buffer",
"hash table entry not found",
"page already in hash table"
};

void PF_PrintError(s)
char *s;	/* string to write */
{
	fprintf(stderr,"%s",s);
	fprintf(stderr,":%s",PFerrormsg[-1*PFerrno]);
	if (PFerrno == PFE_UNIX)
		perror(" ");
	else
		fprintf(stderr,"\n");
}
