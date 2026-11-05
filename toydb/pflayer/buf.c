/* buf.c: buffer management routines. The interface routines are:
PFbufGet(), PFbufUnfix(), PFbufAlloc(), PFbufReleaseFile(), PFbufUsed() and
PFbufPrint() */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "pf.h"
#include "pftypes.h"

int PF_BufPoolSize = PF_DEFAULT_BUFS; /* dynamic buffer limit, defaults to 20 */

static int PFnumbpage = 0;	/* # of buffer pages in memory */
static int PFnumused = 0;	/* # of active used buffer pages in PFfirstbpage */
static PFbpage *PFfirstbpage = NULL;	/* ptr to first buffer page, or NULL */
static PFbpage *PFlastbpage = NULL;	/* ptr to last buffer page, or NULL */
static PFbpage *PFfreebpage = NULL;	/* list of free buffer pages */

static long PF_access_counter = 0;	/* monotonic timestamp for LRU/MRU */

extern PF_Stats PF_stats;
extern int PF_GetNumOpenFiles(void);

void PFbufClearFreeList(void)
/****************************************************************************
SPECIFICATIONS:
	Free all pages in the free list and decrease PFnumbpage.
	Ensures pool size reductions never keep excess free memory.
*****************************************************************************/
{
	PFbpage *curr = PFfreebpage;
	while (curr != NULL) {
		PFbpage *next = curr->nextpage;
		free(curr);
		if (PFnumbpage > 0)
			PFnumbpage--;
		curr = next;
	}
	PFfreebpage = NULL;
}

int PFbufGetNumUsed(void)
{
	return PFnumused;
}

static void PFbufInsertFree(bpage)
PFbpage *bpage;
/****************************************************************************
SPECIFICATIONS:
	Insert the buffer page pointed by "bpage" into the free list.
*****************************************************************************/
{
	bpage->nextpage = PFfreebpage;
	PFfreebpage = bpage;
}

static void PFbufLinkHead(bpage)
PFbpage *bpage;		/* pointer to buffer page to be linked */
/****************************************************************************
SPECIFICATIONS:
	Link the buffer page pointed by "bpage" as the head
	of the used buffer list.
*****************************************************************************/
{
	bpage->nextpage = PFfirstbpage;
	bpage->prevpage = NULL;
	if (PFfirstbpage != NULL)
		PFfirstbpage->prevpage = bpage;
	PFfirstbpage = bpage;
	if (PFlastbpage == NULL)
		PFlastbpage = bpage;
	PFnumused++;
}
	
void PFbufUnlink(bpage)
PFbpage *bpage;		/* buffer page to be unlinked from the used list */
/****************************************************************************
SPECIFICATIONS:
	Unlink the page pointed by bpage from the buffer list.
*****************************************************************************/
{
	if (PFfirstbpage == bpage)
		PFfirstbpage = bpage->nextpage;
	
	if (PFlastbpage == bpage)
		PFlastbpage = bpage->prevpage;
	
	if (bpage->nextpage != NULL)
		bpage->nextpage->prevpage = bpage->prevpage;
	
	if (bpage->prevpage != NULL)
		bpage->prevpage->nextpage = bpage->nextpage;

	bpage->prevpage = bpage->nextpage = NULL;
	if (PFnumused > 0)
		PFnumused--;
}

static PFbpage *SelectVictim(target_fd, strategy)
int target_fd;
int strategy;
/****************************************************************************
SPECIFICATIONS:
	Select a victim frame among unfixed buffer pages.
	Enforces fair quota to prevent starvation. If target_fd has at least
	fair_quota frames or no other unfixed frames exist, evicts from target_fd
	using target_fd's strategy (LRU or MRU). Otherwise, steals from other
	files using LRU (coldest frame) so MRU sequential scans never evict hot
	index pages of other files.
*****************************************************************************/
{
	PFbpage *curr;
	PFbpage *victim = NULL;
	int file_candidates = 0;
	int other_file_candidates = 0;
	int num_open = PF_GetNumOpenFiles();
	int fair_quota;

	if (num_open < 1) num_open = 1;
	fair_quota = PF_MAX_BUFS / num_open;
	if (fair_quota < 3) fair_quota = 3;

	/* First count candidates */
	for (curr = PFfirstbpage; curr != NULL; curr = curr->nextpage) {
		if (!curr->fixed) {
			if (curr->fd == target_fd)
				file_candidates++;
			else
				other_file_candidates++;
		}
	}

	if (file_candidates == 0 && other_file_candidates == 0) {
		/* All pages in buffer are fixed */
		return NULL;
	}

	if (file_candidates >= fair_quota || other_file_candidates == 0) {
		/* Evict from target_fd candidates using target_fd's strategy */
		long best_time = 0;
		int first_candidate = 1;

		for (curr = PFfirstbpage; curr != NULL; curr = curr->nextpage) {
			if (!curr->fixed && curr->fd == target_fd) {
				if (first_candidate) {
					victim = curr;
					best_time = curr->access_time;
					first_candidate = 0;
				} else if (strategy == PF_STRATEGY_MRU) {
					/* MRU: candidate with maximum access_time */
					if (curr->access_time > best_time) {
						victim = curr;
						best_time = curr->access_time;
					}
				} else {
					/* LRU: candidate with minimum access_time */
					if (curr->access_time < best_time) {
						victim = curr;
						best_time = curr->access_time;
					}
				}
			}
		}
	} else {
		/* Target file is below fair quota: steal from other files using LRU */
		long min_time = 0;
		int first_candidate = 1;

		for (curr = PFfirstbpage; curr != NULL; curr = curr->nextpage) {
			if (!curr->fixed && curr->fd != target_fd) {
				if (first_candidate) {
					victim = curr;
					min_time = curr->access_time;
					first_candidate = 0;
				} else if (curr->access_time < min_time) {
					victim = curr;
					min_time = curr->access_time;
				}
			}
		}
	}

	return victim;
}

static int PFbufInternalAlloc(bpage, writefcn, target_fd, strategy)
PFbpage **bpage;	/* pointer to pointer to buffer bpage to be allocated */
int (*writefcn)();
int target_fd;
int strategy;
/****************************************************************************
SPECIFICATIONS:
	Allocate a buffer page and set *bpage to point to it.
	Reuses free list if available, mallocs if under pool limit, or evicts
	a victim page according to the fair replacement policy.
*****************************************************************************/
{
	PFbpage *tbpage;	/* temporary pointer to buffer page */
	int error;

	/* 1. Free list has an available buffer AND active used frames is under pool limit */
	if (PFnumused < PF_MAX_BUFS && PFfreebpage != NULL) {
		*bpage = PFfreebpage;
		PFfreebpage = (*bpage)->nextpage;
	}
	/* 2. Pool limit has not been reached (allocate new buffer frame) */
	else if (PFnumused < PF_MAX_BUFS && PFnumbpage < PF_MAX_BUFS) {
		if ((*bpage = (PFbpage *)malloc(sizeof(PFbpage))) == NULL) {
			*bpage = NULL;
			PFerrno = PFE_NOMEM;
			return(PFerrno);
		}
		PFnumbpage++;
	}
	/* 3. Pool is full (PFnumused >= PF_MAX_BUFS): select victim to evict */
	else {
		tbpage = SelectVictim(target_fd, strategy);
		if (tbpage == NULL) {
			*bpage = NULL;
			PFerrno = PFE_NOBUF;
			return(PFerrno);
		}

		/* Flush dirty victim to disk */
		if (tbpage->dirty) {
			if ((error = (*writefcn)(tbpage->fd, tbpage->page, &tbpage->fpage)) != PFE_OK) {
				*bpage = NULL;
				return(error);
			}
			PF_stats.physical_writes++;
			tbpage->dirty = FALSE;
		}

		/* Delete victim from hash table */
		if ((error = PFhashDelete(tbpage->fd, tbpage->page)) != PFE_OK) {
			*bpage = NULL;
			return(error);
		}

		/* Unlink victim from buffer list */
		PFbufUnlink(tbpage);
		*bpage = tbpage;
	}

	/* Link page as head of used list */
	PFbufLinkHead(*bpage);
	return(PFE_OK);
}

/************************* Interface to the Outside World ****************/

int PFbufGet(fd, pagenum, fpage, readfcn, writefcn, strategy)
int fd;			/* file descriptor */
int pagenum;		/* page number */
PFfpage **fpage;	/* pointer to pointer to file page */
int (*readfcn)();	/* function to read a page */
int (*writefcn)();	/* function to write a page */
int strategy;		/* replacement strategy for this file */
/****************************************************************************
SPECIFICATIONS:
	Get page "pagenum" of file "fd". Set *fpage to point to it.
	Guarantees: logical_reads == buffer_hits + buffer_misses.
	Returns PFE_PAGEFIXED if page is already fixed (matching ToyDB spec).
*****************************************************************************/
{
	PFbpage *bpage;
	int error;

	PF_stats.logical_reads++;

	if ((bpage = PFhashFind(fd, pagenum)) == NULL) {
		PF_stats.buffer_misses++;

		/* Allocate a buffer frame */
		if ((error = PFbufInternalAlloc(&bpage, writefcn, fd, strategy)) != PFE_OK) {
			*fpage = NULL;
			return(error);
		}

		/* Read page from disk */
		PF_stats.physical_reads++;
		if ((error = (*readfcn)(fd, pagenum, &bpage->fpage)) != PFE_OK) {
			PFbufUnlink(bpage);
			PFbufInsertFree(bpage);
			*fpage = NULL;
			return(error);
		}

		/* Insert into hash table */
		if ((error = PFhashInsert(fd, pagenum, bpage)) != PFE_OK) {
			PFbufUnlink(bpage);
			PFbufInsertFree(bpage);
			*fpage = NULL;
			return(error);
		}

		/* Initialize frame fields */
		bpage->fd = fd;
		bpage->page = pagenum;
		bpage->dirty = FALSE;
		bpage->fixed = 0;
	} else {
		PF_stats.buffer_hits++;
	}

	/* If page is already fixed, return PFE_PAGEFIXED per ToyDB specification */
	if (bpage->fixed) {
		*fpage = &bpage->fpage;
		PFerrno = PFE_PAGEFIXED;
		return(PFerrno);
	}

	/* Pin page in buffer */
	bpage->fixed = 1;
	bpage->access_time = ++PF_access_counter;

	/* Move to head of list */
	PFbufUnlink(bpage);
	PFbufLinkHead(bpage);

	*fpage = &bpage->fpage;
	return(PFE_OK);
}

int PFbufUnfix(fd, pagenum, dirty)
int fd;
int pagenum;
int dirty;
/****************************************************************************
SPECIFICATIONS:
	Unfix page "pagenum" of file "fd".
	Only increments logical_writes if dirty is TRUE and page was clean.
*****************************************************************************/
{
	PFbpage *bpage;

	if ((bpage = PFhashFind(fd, pagenum)) == NULL) {
		PFerrno = PFE_PAGENOTINBUF;
		return(PFerrno);
	}

	if (!bpage->fixed) {
		PFerrno = PFE_PAGEUNFIXED;
		return(PFerrno);
	}

	if (dirty) {
		if (!bpage->dirty) {
			PF_stats.logical_writes++;
		}
		bpage->dirty = TRUE;
	}

	/* Unpin page */
	bpage->fixed = 0;
	bpage->access_time = ++PF_access_counter;

	/* Move to head of list */
	PFbufUnlink(bpage);
	PFbufLinkHead(bpage);

	return(PFE_OK);
}

int PFbufAlloc(fd, pagenum, fpage, writefcn, strategy)
int fd;
int pagenum;
PFfpage **fpage;
int (*writefcn)();
int strategy;
/****************************************************************************
SPECIFICATIONS:
	Allocate a buffer for a newly created page "pagenum" of file "fd".
*****************************************************************************/
{
	PFbpage *bpage;
	int error;

	*fpage = NULL;

	if ((bpage = PFhashFind(fd, pagenum)) != NULL) {
		PFerrno = PFE_PAGEINBUF;
		return(PFerrno);
	}

	if ((error = PFbufInternalAlloc(&bpage, writefcn, fd, strategy)) != PFE_OK)
		return(error);

	if ((error = PFhashInsert(fd, pagenum, bpage)) != PFE_OK) {
		PFbufUnlink(bpage);
		PFbufInsertFree(bpage);
		return(error);
	}

	bpage->fd = fd;
	bpage->page = pagenum;
	bpage->fixed = 1;
	bpage->dirty = FALSE;
	bpage->access_time = ++PF_access_counter;

	*fpage = &bpage->fpage;
	return(PFE_OK);
}

int PFbufReleaseFile(fd, writefcn)
int fd;
int (*writefcn)();
/****************************************************************************
SPECIFICATIONS:
	Release all pages of file "fd" from buffer and flush dirty pages to disk.
*****************************************************************************/
{
	PFbpage *bpage;
	PFbpage *temppage;
	int error;

	bpage = PFfirstbpage;
	while (bpage != NULL) {
		if (bpage->fd == fd) {
			if (bpage->fixed) {
				PFerrno = PFE_PAGEFIXED;
				return(PFerrno);
			}

			if (bpage->dirty) {
				if ((error = (*writefcn)(fd, bpage->page, &bpage->fpage)) != PFE_OK)
					return(error);
				PF_stats.physical_writes++;
				bpage->dirty = FALSE;
			}

			if ((error = PFhashDelete(fd, bpage->page)) != PFE_OK) {
				printf("Internal error: PFbufReleaseFile()\n");
				exit(1);
			}

			temppage = bpage;
			bpage = bpage->nextpage;
			PFbufUnlink(temppage);
			PFbufInsertFree(temppage);
		} else {
			bpage = bpage->nextpage;
		}
	}
	return(PFE_OK);
}

int PFbufUsed(fd, pagenum)
int fd;
int pagenum;
/****************************************************************************
SPECIFICATIONS:
	Mark page "pagenum" of file "fd" as dirty and most recently used.
*****************************************************************************/
{
	PFbpage *bpage;

	if ((bpage = PFhashFind(fd, pagenum)) == NULL) {
		PFerrno = PFE_PAGENOTINBUF;
		return(PFerrno);
	}

	if (!bpage->fixed) {
		PFerrno = PFE_PAGEUNFIXED;
		return(PFerrno);
	}

	if (!bpage->dirty) {
		PF_stats.logical_writes++;
	}
	bpage->dirty = TRUE;
	bpage->access_time = ++PF_access_counter;

	PFbufUnlink(bpage);
	PFbufLinkHead(bpage);

	return(PFE_OK);
}

void PFbufPrint(void)
/****************************************************************************
SPECIFICATIONS:
	Print the current page buffers.
*****************************************************************************/
{
	PFbpage *bpage;

	printf("buffer content (pool_size=%d):\n", PF_MAX_BUFS);
	if (PFfirstbpage == NULL) {
		printf("empty\n");
	} else {
		printf("fd\tpage\tfixed\tdirty\taccess_time\tfpage\n");
		for (bpage = PFfirstbpage; bpage != NULL; bpage = bpage->nextpage) {
			printf("%d\t%d\t%d\t%d\t%ld\t%p\n",
				bpage->fd, bpage->page, (int)bpage->fixed,
				(int)bpage->dirty, bpage->access_time, (void *)&bpage->fpage);
		}
	}
}
