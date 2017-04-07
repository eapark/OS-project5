/*
Main program for the virtual memory project.
Make all of your modifications to this file.
You may add or rearrange any code or data as you need.
The header files page_table.h and disk.h explain
how to use the page table and disk interfaces.
*/

#include "page_table.h"
#include "disk.h"
#include "program.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

#define true 1
#define false 0
typedef int bool;

int npages, nframes;
int npageFaults, ndiskReads, ndiskWrites;
struct disk *disk;
struct page_table *pt;

char* physmem;

int FRAME_SIZE = PAGE_SIZE;

int pagesAssigned=0;

struct NODE{
	int value;
	struct NODE* next;

};

struct FIFO{
	struct NODE* head;
	struct NODE* tail;
	int size;
};

struct FIFO fifo;

void FIFO_init(struct FIFO* fifo)
{
	fifo->head = NULL;
	fifo->tail = NULL;
	fifo->size = 0;
}

int FIFO_pop(struct FIFO* fifo)
{
	if(fifo->size > 0)
	{
		struct NODE* temp = fifo->head->next;
		int old_v = fifo->head->value;
		free(fifo->head);
		fifo->head = temp;
		fifo->size--;
		return old_v;
	}
	return -1;
}

void FIFO_push(struct FIFO* fifo, int  newVal)
{
	struct NODE* newNode = malloc(sizeof(struct NODE));
	newNode->value = newVal;
	newNode->next = NULL;

	if(fifo->size == 0)
	{
		fifo->head = newNode;
		fifo->tail = newNode;
		fifo->head->next = NULL;
		fifo->tail->next = NULL;
	}
	else
	{
		fifo->tail->next = newNode;
		fifo->tail = newNode;
	}
	fifo->size++;
}


///////////////////////////
struct RANDL{
	int * list;
	int size;
	int assigned;


};


void RANDL_init(struct RANDL* rl, int size)
{
	rl->list = malloc(size*sizeof(int));
	rl->size = size;
	rl->assigned = 0;

}

int RANDL_add(struct RANDL* rl, int npage)
{
	if(rl->assigned < rl->size)
	{
		rl->list[rl->assigned] = npage;
		rl->assigned++;
		return -1;

	}
	else
	{
		int replace = rand()%nframes;
		int opage = rl->list[replace];
		rl->list[replace] = npage;
		return opage;

	}


}

struct RANDL randl;


struct LRU_E2{
	int page;
	int frame;
	int bits;
};

struct LRU_E{
	int page;
	bool probed;
};

struct LRU{
	//struct ENTRY* EArray;
	struct LRU_E* pArray;
	struct LRU_E2* pArray2;
	//int cRank; // current rank we'll assign
	int size;
	int notFoundCount;
	int assigned; // filled spots in EArray 
};

struct LRU lru;

struct PROBER
{
	struct LRU* lru;
	int interval;
	int index;
	int nframes;

};

struct PROBER prober;

pthread_mutex_t alarmLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lruLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarmCond = PTHREAD_COND_INITIALIZER;
pthread_t alarm_thread;


void alarmHandler(int sig)
{
	pthread_cond_signal(&alarmCond);
	pthread_mutex_lock(&lruLock); // MODIFY LATER>>>>>>>>>
	// BEHAVIOR GOES HERE
	int page = lru.pArray[prober.index].page;
	if(page > -1)
	{
		lru.pArray[prober.index].probed = true;
		//printf("alarmHandler: page: %d frame: %d\n", page, prober.index);
		page_table_set_entry(pt, page, prober.index, 0);
		prober.index = (prober.index+1) % nframes;		
	}
	/////
	pthread_mutex_unlock(&lruLock);
}

void *timer_func(void *arg)
{
	signal(SIGALRM, alarmHandler);
	struct itimerval it_val;	/* for setting itimer */

	if (signal(SIGALRM,  alarmHandler) == SIG_ERR) {
		perror("Unable to catch SIGALRM");
		exit(1);
	}
	it_val.it_value.tv_sec =     prober.interval/1000000;
	it_val.it_value.tv_usec =    prober.interval  % 1000000;	
	it_val.it_interval = it_val.it_value;
	if (setitimer(ITIMER_REAL, &it_val, NULL) == -1) {
		perror("error calling setitimer()");
  		exit(1);
	}

	return NULL;
}

void PROBER_init(struct PROBER* p, struct LRU* lru, int interval)
{
	p->lru = lru;
	p->interval = interval;
	p->index = 0;
	p->nframes = nframes; // Global

//	alarmLock = PTHREAD_MUTEX_INITIALIZER;
//	alarmCond = PTHREAD_COND_INITIALIZER;
	pthread_create(&alarm_thread, NULL, timer_func, &lru);

	//return;
	
}


void LRU_init(struct LRU *lru)
{
	lru->pArray = malloc(nframes * sizeof(struct LRU_E));
	lru->pArray2 = malloc(npages * sizeof(struct LRU_E2));
	lru->assigned = 0;
	lru->notFoundCount =0;

//	lruLock = PTHREAD_MUTEX_INITIALIZER;

	int i;
	for(i = 0; i < nframes; i++)
	{
		lru->pArray[i].page = -1;
		lru->pArray[i].probed = false;

	}
	for(i = 0; i < npages; i++)
	{
		lru->pArray2[i].page = i;
		lru->pArray2[i].frame = -1;
		lru->pArray2[i].bits = 0;
		
	}

}

void LRU_unprobe(int frame)
{
	lru.pArray[frame].probed = false;


}
struct LRU_add_ret
{
	int opage;
	int obits;
};

struct LRU_add_ret LRU_add(struct LRU *lru, int newPage)
{
	//struct LRU_E newE;
	//newE.page = newPage;
	//newE.rank = lru->cRank;
	pthread_mutex_lock(&lruLock);
	int opage;
	int obits;
	struct LRU_add_ret lar;
	if(lru->assigned < nframes)
	{
		lru->pArray2[newPage].frame = lru->assigned;
		lru->pArray2[newPage].bits = PROT_READ;
		lru->pArray[lru->assigned].probed = false;
		lru->pArray[lru->assigned].page = newPage;
		lru->assigned = lru->assigned + 1;
		pthread_mutex_unlock(&lruLock);
		lar.opage = -1;
		lar.obits = 0;
		return lar;
	}
	else
	{
		int i;
		int evicted = -1;
		int cIndex = -1;
		for(i=0; i<nframes; i++)
		{
			cIndex = (prober.index + i) % nframes;
			if(lru->pArray[cIndex].probed)
			{
				evicted = cIndex;
				break;

			}

		}
		if(evicted == -1) // no probed found
		{
			evicted = rand() % nframes;
			lru->notFoundCount++;		
		}
		opage = lru->pArray[evicted].page;
		obits = lru->pArray2[opage].bits;
		lru->pArray[evicted].page = newPage;
		lru->pArray[evicted].probed = false;
		lru->pArray2[newPage].frame = evicted;
		lru->pArray2[newPage].bits = PROT_READ;
		lru->pArray2[opage].frame = -1;
		lru->pArray2[opage].bits = 0;
		pthread_mutex_unlock(&lruLock);
		
		lar.opage = opage;
		lar.obits = obits;
		return lar;
	}
}

void LRU_setWrite(int page)
{
	lru.pArray2[page].bits = (PROT_READ | PROT_WRITE);

}

//////////////////////////

void page_fault_handler_rand( struct page_table *pt, int page )
{

	int frame, bits;
	page_table_get_entry(pt, page, &frame, &bits);

	if(pagesAssigned < nframes)
	{
			if(bits == 0)
			{
				npageFaults++;
				page_table_set_entry(pt, page, pagesAssigned, PROT_READ);
				disk_read(disk, page, &physmem[pagesAssigned * FRAME_SIZE]); //
				ndiskReads++;
				RANDL_add(&randl, pagesAssigned);
				pagesAssigned++;
			}
			else if(bits == PROT_READ)
			{
				page_table_set_entry(pt, page, frame, PROT_READ | PROT_WRITE);
			}
	}
	else
	{
		if(bits == 0)
		{
			npageFaults++;
			int opage = RANDL_add(&randl, page);
			int oframe;
			int obits;
			page_table_get_entry(pt, opage, &oframe, &obits);
			if( obits == (PROT_READ | PROT_WRITE) )
			{
				disk_write(disk, opage, &physmem[FRAME_SIZE * oframe]);
				ndiskWrites++;
			}

			disk_read(disk, page, &physmem[FRAME_SIZE * oframe]);
			ndiskReads++;
			page_table_set_entry(pt, page, oframe, PROT_READ);
			page_table_set_entry(pt, opage, 0, 0);
		}
		else if (bits == PROT_READ)
		{
			page_table_set_entry(pt, page, frame, PROT_READ | PROT_WRITE);
		}
	}

}

void test_FIFO(void)
{
	struct FIFO f;
	int i;

	for(i = 0; i < 10; i++)
	{
		FIFO_push(&f, i);
		
	}
	int d;
	for(i = 20; i < 32; i++)
	{
		d = FIFO_pop(&f);

		FIFO_push(&f, i);


	}
	


}

void page_fault_handler_fifo( struct page_table *pt, int page )
{
	int frame, bits;
	page_table_get_entry(pt, page, &frame, &bits);
	if(pagesAssigned < nframes)
	{
			if(bits == 0)
			{
				npageFaults++;
				page_table_set_entry(pt, page, pagesAssigned, PROT_READ);
				disk_read(disk, page, &physmem[pagesAssigned * FRAME_SIZE]);
				ndiskReads++;
				pagesAssigned++;
				FIFO_push(&fifo, page);
			}
			else if(bits == PROT_READ)
			{
				page_table_set_entry(pt, page, frame, PROT_READ | PROT_WRITE);
			}
	}
	else
	{
		if(bits == 0)
		{
			npageFaults++;
			int opage = FIFO_pop(&fifo);
			int oframe;
			int obits;
			page_table_get_entry(pt, opage, &oframe, &obits);
			if( obits == (PROT_READ | PROT_WRITE) )
			{
				disk_write(disk, opage, &physmem[FRAME_SIZE * oframe]);
				ndiskWrites++;
			}

			disk_read(disk, page, &physmem[FRAME_SIZE * oframe]);
			ndiskReads++;
			page_table_set_entry(pt, page, oframe, PROT_READ);
			page_table_set_entry(pt, opage, 0, 0);
			FIFO_push(&fifo, page);
		}
		else if (bits == PROT_READ)
		{
			page_table_set_entry(pt, page, frame, PROT_READ | PROT_WRITE);
		}
	}
	//printf("page fault on page #%d\n",page);
	//exit(1);
}


void page_fault_handler_lru ( struct page_table *pt, int page )
{
	int frame, bits;
	page_table_get_entry(pt, page, &frame, &bits);

	if(pagesAssigned < nframes)
	{
			if(bits == 0)
			{
				if(lru.pArray2[page].bits == 0)
				{		
						//printf("handler: pages assigned are %d\n", pagesAssigned);
						npageFaults++;
						//printf("page is being replaced");
						page_table_set_entry(pt, page, pagesAssigned, PROT_READ);
						//printf("Putting page %d into frame %d\n",page, pagesAssigned);
						disk_read(disk, page, &physmem[pagesAssigned * FRAME_SIZE]);
						ndiskReads++;
						//printf("Reading data from page %d into frame%d\n", page, pagesAssigned);
						LRU_add(&lru, page);
						LRU_unprobe(pagesAssigned);
						pagesAssigned++;
						//printf("Adding page %d to lru\n",page);
				}
				else
				{
					//printf("Page unprobed\n");
					LRU_unprobe(frame);
					page_table_set_entry(pt, page, frame, PROT_READ);
				}
			}
			else if(bits == PROT_READ)
			{
				LRU_setWrite(page);
				LRU_unprobe(frame);
				//printf("setting write rights to page %d at frame %d\n", page, frame);
				page_table_set_entry(pt, page, frame, PROT_READ | PROT_WRITE);
			}
	}
	else
	{
		if(bits == 0)
		{
			if(lru.pArray2[page].bits == 0)
			{
					//printf("Page is being replaced\n");
					npageFaults++;
					
					struct LRU_add_ret lar = LRU_add(&lru, page);
					int oframe;
					int obits;
					page_table_get_entry(pt, lar.opage, &oframe, &obits);
					
					//printf("Replacing page %d with bits %d at frame %d, with page %d \n",lar.opage, obits, oframe, page);
					if( lar.obits == (PROT_READ | PROT_WRITE) )
					{
						disk_write(disk, lar.opage, &physmem[FRAME_SIZE * oframe]);
						ndiskWrites++;
						//printf("WRiting data from page %d at frame %d to disk\n", lar.opage, oframe);
					}
					LRU_unprobe(oframe);

					disk_read(disk, page, &physmem[FRAME_SIZE * oframe]);
					ndiskReads++;
					//printf("Reading data from disk at page %d into frame %d\n", page, oframe);
					page_table_set_entry(pt, page, oframe, PROT_READ);
					//printf("Setting page table entry of page %d at frame %d\n", page, oframe);
					page_table_set_entry(pt, lar.opage, 0, 0);
					//printf("Setting page table entry of page %d to 0 and is now disabled\n",lar.opage);
			}
			else
			{
					//printf("Page unprobed\n");
					page_table_set_entry(pt, page, frame, PROT_READ);
					LRU_unprobe(frame);
				

			}
		}
		else if (bits == PROT_READ)
		{
			LRU_setWrite(page);
			LRU_unprobe(frame);
			page_table_set_entry(pt, page, frame, PROT_READ | PROT_WRITE);
			//printf("Setting protection bits to RW at page %d and frame %d\n", page, frame);
		}
	}
	//printf("------------------------------\n");
}

///////////////////////////

int main( int argc, char *argv[] )
{
	if(argc!=5) {
		printf("use: virtmem <npages> <nframes> <rand|fifo|lru|custom> <sort|scan|focus>\n");
		return 1;
	}
	
	npages = atoi(argv[1]);
	nframes = atoi(argv[2]);
	npageFaults = 0;
	ndiskReads = 0;
	ndiskWrites = 0;
	const char *policy  = argv[3];
	const char *program = argv[4];

	srand(time(NULL));

	FIFO_init(&fifo);
	LRU_init(&lru);
	RANDL_init(&randl, nframes);
	PROBER_init(&prober, &lru, 1000);

	disk = disk_open("myvirtualdisk",npages);
	if(!disk) {
		fprintf(stderr,"couldn't create virtual disk: %s\n",strerror(errno));
		return 1;
	}


	if(!strcmp(policy, "rand"))
	{
		pt = page_table_create( npages, nframes, page_fault_handler_rand );
	}
	else if(!strcmp(policy, "fifo"))
	{
		pt = page_table_create( npages, nframes, page_fault_handler_fifo );
	}
	else if(!strcmp(policy, "custom"))
	{
		pt = page_table_create( npages, nframes, page_fault_handler_lru );
	}
	else
	{
		fprintf(stderr, "unknown policy: %s\n", policy);
		return 1;
	}

	if(!pt) {
		fprintf(stderr,"couldn't create page table: %s\n",strerror(errno));
		return 1;
	}

	char *virtmem = page_table_get_virtmem(pt);

	physmem = page_table_get_physmem(pt);

	if(!strcmp(program,"sort")) {
		sort_program(virtmem,npages*PAGE_SIZE);

	} else if(!strcmp(program,"scan")) {
		scan_program(virtmem,npages*PAGE_SIZE);

	} else if(!strcmp(program,"focus")) {
		focus_program(virtmem,npages*PAGE_SIZE);

	} else {
		fprintf(stderr,"unknown program: %s\n",argv[4]);
		return 1;
	}

	page_table_delete(pt);
	disk_close(disk);

	printf("PageFault %d DiskRead %d DiskWrite %d\n", npageFaults, ndiskReads, ndiskWrites);

	//printf("There were %d page faults\n",npageFaults); 

	return 0;
}
