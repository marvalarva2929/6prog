#include "tdmm.h"
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>

const size_t HEAP_SIZE = 65536 + sizeof(memList);

alloc_strat_e strategy;
memList *list;
int regions = 0;

void tprint() {
	//return;
	memList *tlist = list;
	while (tlist) {
		if (tlist->header.isFree) printf("Free: %lu\n", tlist->header.size);
		else printf("Mallocd: %lu\n", tlist->header.size);
		tlist = tlist->next;
	}
	printf("\n");
}

/* Merge consecutive free blocks to prevent fragmentation. */
void collapseFree() {
	memList *tlist = list;
	while (tlist) {
		/* Only try to merge if this block is free AND has a next block */
		if (tlist->header.isFree && tlist->next != NULL) {
			/* Keep merging as long as the next block is also free */
			while (tlist->next != NULL 
					&& tlist->next->header.isFree
					&& tlist->header.region == tlist->next->header.region) {
				memList *n = tlist->next;
				/* Absorb n's size (including its header) into tlist */
				tlist->header.size +=  n->header.size + sizeof(memList);
				/* Unlink n — no free() since this memory is part of our mmap region */
				tlist->next = n->next;
			}
		}
		tlist = tlist->next;
	}
}

/*
 * Carve `requestedSize` bytes out of `mem `, splitting off any remainder
 * into a new free node. Does nothing if the block is already allocated.
 */
void allocateMem(memList *mem, size_t requestedSize) {
	if (!mem->header.isFree) return;
	
	size_t totalNeeded = requestedSize + sizeof(memList);
	size_t remaining   = mem->header.size - requestedSize;

	/* Only split if the leftover space can actually hold a new node + some data */
	if (remaining > sizeof(memList)) {
		/* Place the new free node immediately after the user data */
		memList *newnext = (memList *)((char *)mem + totalNeeded);
		newnext->header.isFree = 1;
		newnext->header.size   = remaining - sizeof(memList);
		newnext->header.region = mem->header.region;
		newnext->next          = mem->next;

		mem->next = newnext;
	}
	/* If there's no room for a split, just hand over the whole block as-is */

	mem->header.isFree = 0;
	mem->header.size   = requestedSize;
}

memList *getMem(size_t size) {
	memList * ret;
	void *mem = mmap(NULL, size, PROT_READ | PROT_WRITE,
	                 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (mem == MAP_FAILED) {
		fprintf(stderr, "mmap failed\n");
	}

	ret = (memList *)mem;
	ret->header.size   = size - sizeof(memList);
	ret->header.isFree = 1;
	ret->header.region = regions++;
	ret->next          = NULL;
	return ret;
}

void addList(memList* toAdd) {
	memList* tlist = list;
	while (tlist->next) {
		tlist = tlist->next;
	}
	tlist->next = toAdd;
}

void t_init(alloc_strat_e strat) {
	strategy = strat;
	list = getMem(HEAP_SIZE);
	tprint();
}

void *t_malloc(size_t requestedSize) {
	if (requestedSize == 0) return NULL;

	requestedSize = (requestedSize + 7) & ~7;

	memList *candidate = NULL;
	memList *tlist     = list;
	size_t   totalNeeded = sizeof(memList) + requestedSize;

	switch (strategy) {

		case FIRST_FIT:
			/* Take the first block that is large enough */
			while (tlist) {
				if (tlist->header.isFree && tlist->header.size >= requestedSize) {
					candidate = tlist;
					break;
				}
				tlist = tlist->next;
			}
			break;

		case BEST_FIT:
			/* Find the smallest block that still fits — minimises wasted space */
			while (tlist) {
				if (tlist->header.isFree && tlist->header.size >= requestedSize) {
					if (candidate == NULL ||
					    tlist->header.size < candidate->header.size) {
						candidate = tlist;
					}
				}
				tlist = tlist->next;
			}
			break;

		case WORST_FIT:
			/* Find the largest free block — leaves the biggest remainder after splitting */
			while (tlist) {
				if (tlist->header.isFree && tlist->header.size >= requestedSize) {
					if (candidate == NULL ||
					    tlist->header.size > candidate->header.size) {
						candidate = tlist;
					}
				}
				tlist = tlist->next;
			}
			break;

		default:
			break;
	}

	if (!candidate) {
		memList * getBlock = getMem(sizeof(memList) + requestedSize);
		addList(getBlock);
		allocateMem(getBlock, requestedSize);
		return (void *)(getBlock + 1);
	}
	
	allocateMem(candidate, requestedSize);
	tprint();
	/* Return a pointer to the user-data region, just past the memList header */
	return (void *)(candidate + 1);
}

void t_free(void *ptr) {
	if (!ptr) return;

	memList *tlist = (memList*)ptr - 1;
	if (!tlist->header.isFree) {
		tlist->header.isFree = 1;
		collapseFree();
		tprint();
		return;
	}

	fprintf(stderr, "t_free: pointer not found or already freed\n");
}


