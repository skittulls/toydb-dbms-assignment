/* pf.h: externs, data types, and error codes for Paged File Interface */
#ifndef PF_H
#define PF_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TRUE
#define TRUE 1		
#endif
#ifndef FALSE
#define FALSE 0
#endif

/************** Error Codes *********************************/
#define PFE_OK		0	/* OK */
#define PFE_NOMEM	-1	/* no memory */
#define PFE_NOBUF	-2	/* no buffer space */
#define PFE_PAGEFIXED 	-3	/* page already fixed in buffer */
#define PFE_PAGENOTINBUF -4	/* page to be unfixed is not in the buffer */
#define PFE_UNIX	-5	/* unix error */
#define PFE_INCOMPLETEREAD -6	/* incomplete read of page from file */
#define PFE_INCOMPLETEWRITE -7	/* incomplete write of page to file */
#define PFE_HDRREAD	-8	/* incomplete read of header from file */
#define PFE_HDRWRITE	-9	/* incomplete write of header to file */
#define PFE_INVALIDPAGE -10	/* invalid page number */
#define PFE_FILEOPEN	-11	/* file already open */
#define	PFE_FTABFULL	-12	/* file table is full */
#define PFE_FD		-13	/* invalid file descriptor */
#define PFE_EOF		-14	/* end of file */
#define PFE_PAGEFREE	-15	/* page already free */
#define PFE_PAGEUNFIXED	-16	/* page already unfixed */

/* Internal errors */
#define PFE_PAGEINBUF	-17	/* new page to be allocated already in buffer */
#define PFE_HASHNOTFOUND -18	/* hash table entry not found */
#define PFE_HASHPAGEEXIST -19	/* page already exist in hash table */

/* Page payload size: 4092 bytes so sizeof(PFfpage) = 4 + 4092 = 4096 (1 physical disk block) */
#define PF_PAGE_SIZE	4092

/* Page replacement strategies */
#define PF_STRATEGY_LRU 0
#define PF_STRATEGY_MRU 1

/* I/O Statistics */
typedef struct PF_Stats {
    long logical_reads;     /* User page get requests */
    long logical_writes;    /* User page writes/modifications */
    long physical_reads;    /* Actual OS read() calls */
    long physical_writes;   /* Actual OS write() calls */
    long buffer_hits;       /* Page found in buffer cache */
    long buffer_misses;     /* Page required disk fetch */
    long pages_allocated;   /* New pages allocated */
    long pages_disposed;    /* Pages freed/disposed */
} PF_Stats;

/* Error number of last error */
extern int PFerrno;

/* PF Layer Public API */
void PF_Init(void);
void PF_PrintError(char *s);
int PF_CreateFile(char *fname);
int PF_DestroyFile(char *fname);
int PF_OpenFile(char *fname);
int PF_OpenFileWithStrategy(char *fname, int strategy);
int PF_CloseFile(int fd);
int PF_GetFirstPage(int fd, int *pagenum, char **pagebuf);
int PF_GetNextPage(int fd, int *pagenum, char **pagebuf);
int PF_GetThisPage(int fd, int pagenum, char **pagebuf);
int PF_AllocPage(int fd, int *pagenum, char **pagebuf);
int PF_DisposePage(int fd, int pagenum);
int PF_UnfixPage(int fd, int pagenum, int dirty);
int PF_MarkDirty(int fd, int pagenum);

/* Statistics and Buffer Tuning API */
void PF_GetStats(PF_Stats *stats);
void PF_ResetStats(void);
void PF_PrintStats(void);
void PF_SetBufferSize(int numBuffers);
int PF_GetBufferSize(void);
int PF_GetFileStrategy(int fd);
int PF_GetNumPages(int fd);

#ifdef __cplusplus
}
#endif

#endif /* PF_H */
