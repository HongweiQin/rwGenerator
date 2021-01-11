#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <time.h>
#include <libaio.h>

#define USE_ATOMIC
//#define USE_GLOBAL_ATOMIC

#define MAX_THREADS (128)
#define MAX_SUBMIT_WINDOW (64)
#define MAX_RUNTIME (3600*24)
#define MIN_RUNTIME (1)
#define MAX_NR_FILE_PAGES (1 << 18)
#define MIN_NR_FILE_PAGES (1)

#define F2FS_IOCTL_MAGIC        0xf5
#define F2FS_IOC_START_ATOMIC_WRITE     _IO(F2FS_IOCTL_MAGIC, 1)
#define F2FS_IOC_COMMIT_ATOMIC_WRITE    _IO(F2FS_IOCTL_MAGIC, 2)
#define F2FS_IOC_START_VOLATILE_WRITE   _IO(F2FS_IOCTL_MAGIC, 3)
#define F2FS_IOC_ABORT_VOLATILE_WRITE   _IO(F2FS_IOCTL_MAGIC, 5)
#define F2FS_IOC_GET_FEATURES           _IOR(F2FS_IOCTL_MAGIC, 12, u32)
#define F2FS_FEATURE_ATOMIC_WRITE 0x0004


clockid_t clk_id = CLOCK_MONOTONIC;
//	clk_id = CLOCK_REALTIME;
//	clk_id = CLOCK_MONOTONIC;
//  clk_id = CLOCK_BOOTTIME;
//  clk_id = CLOCK_PROCESS_CPUTIME_ID;

enum condition {
	TIME_BASED = 0,
	COUNT_BASED,
	NR_CONDITIONS,
};

struct globalConfig {
	unsigned long numthreads;
	unsigned long submit_window;
	unsigned long runtime;
	unsigned long testCount;
	volatile short start;
	enum condition stop_cond;
	char filePrefix[32];
	struct timespec startTime;
	struct timespec endTime;
	unsigned long max_npages_per_file;
};

void printConfig(struct globalConfig *gf)
{
	printf("-------print config--------\n");
	printf("num threads = %lu\n", gf->numthreads);
	printf("submit window size = %lu\n", gf->submit_window);
	printf("Runtime = %lu Seconds\n", gf->runtime);
	printf("TestCount = %lu\n", gf->testCount);
	printf("file size = %lu pages\n", gf->max_npages_per_file);
	printf("filename prefix = %s\n", gf->filePrefix);
	printf("stop condition = %d\n", gf->stop_cond);
	printf("---------------------------\n");
}

struct thread_info {    /* Used as argument to thread_start() */
	pthread_t thread_id;        /* ID returned by pthread_create() */
	unsigned long       thread_num;       /* Application-defined thread # */
	volatile short ready;
	struct globalConfig *gf;
	char filename[64];
	int fd;
	int budget;
	int currentBudget;
	unsigned long currentCount;
};

struct write_req {
	struct iocb iocb;
	char buf[4096];
	int index;
};

void init_tinfo(struct thread_info *tinfo, unsigned long tn, struct globalConfig *gf)
{
	tinfo->ready = 0;
	tinfo->thread_num = tn;
	tinfo->gf = gf;
	sprintf(tinfo->filename, "%s/file%lu",
				gf->filePrefix, tn);
}

void wait_for_set(volatile short *p)
{
	while (!*p)
		;
	return;
}

int time_within(struct timespec *currentTime,
					struct timespec *endTime)
{
	//printf("currentTime:%lus %lu nsec, endTime:%lus %lu nsec\n",
	//	currentTime->tv_sec,
	//	currentTime->tv_nsec,
	//	endTime->tv_sec,
	//	endTime->tv_nsec);
	if (currentTime->tv_sec == endTime->tv_sec)
		return currentTime->tv_nsec < endTime->tv_nsec;
	else
		return currentTime->tv_sec < endTime->tv_sec;
}

int test_continue(struct thread_info *tinfo,
						struct globalConfig *gf)
{
	struct timespec currentTime;

	if (gf->testCount) {
		if (!tinfo->currentCount)
			return 0;
		tinfo->currentCount--;
	}

	switch (gf->stop_cond)
	{
	case TIME_BASED:
		//FIXME: check return value
		clock_gettime(clk_id, &currentTime);
		return time_within(&currentTime, &gf->endTime);
	default:
		printf("Err: stop condition unrecognized (%d)\n",
					gf->stop_cond);
		return 0;
	}
}


static void *
thread_fn(void *arg)
{
	struct thread_info *tinfo = arg;
	struct globalConfig *gf = tinfo->gf;
	int openflag = O_CREAT|O_RDWR|O_TRUNC|S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
	int fd;
	int budget = gf->submit_window;
	long long offset = 0;
	int i;
	unsigned long long totalSubmitted = 0;
	char (*wbuf)[4096];
	unsigned long maxfilesize = gf->max_npages_per_file;
	unsigned long tid = tinfo->thread_num;
	struct timespec testtime1, testtime2, testtime3, testtime4;
	

	//Create test file
	fd = tinfo->fd = open(tinfo->filename, openflag);
	if (tinfo->fd < 0) {
		printf("Thread %d open test file failed, err(%d)",
				tinfo->thread_num, errno);
		tinfo->ready = 1;
		goto out;
	}

	tinfo->budget = 
		tinfo->currentBudget = gf->submit_window;
	tinfo->currentCount = gf->testCount;
	//allocate write buffer
	wbuf = (char (*)[4096]) calloc(budget, 4096);
	if (!wbuf)
		goto out2;

	tinfo->ready = 1;
	wait_for_set(&gf->start);

	//start
#ifdef USE_GLOBAL_ATOMIC
#ifndef USE_ATOMIC
	ioctl(fd, F2FS_IOC_START_ATOMIC_WRITE);
#endif
#endif

	for (;;)
	{
		if (tid == 1) {
			clock_gettime(clk_id, &testtime1);
			printf("start11=%lds %ldns\n",
						testtime1.tv_sec,
						testtime1.tv_nsec);
		}
#ifdef USE_ATOMIC
		ioctl(fd, F2FS_IOC_START_ATOMIC_WRITE);
#endif
		if (tid == 1) {
			clock_gettime(clk_id, &testtime2);
			printf("start22=%lds %ldns\n",
						testtime2.tv_sec,
						testtime2.tv_nsec);
		}
		for (i = 0;
			i < budget;
			i++) {
			if (!test_continue(tinfo, gf)) {
#ifdef USE_ATOMIC
				ioctl(fd, F2FS_IOC_COMMIT_ATOMIC_WRITE);
#endif
				goto testout;
			}
			if (tid == 1) {
				clock_gettime(clk_id, &testtime2);
				printf("\t111=%lds %ldns\n",
							testtime2.tv_sec,
							testtime2.tv_nsec);
			}
			pwrite(fd, wbuf + i, 4096, offset*4096);
			if (tid == 1) {
				clock_gettime(clk_id, &testtime2);
				printf("\t222=%lds %ldns\n",
							testtime2.tv_sec,
							testtime2.tv_nsec);
			}
			//printf("write addr = %p\n", wbuf + i);
			offset ++;
			if (offset >= maxfilesize)
				offset = 0;
			totalSubmitted++;
		}
		if (tid == 1) {
			clock_gettime(clk_id, &testtime3);
			printf("end11=%lds %ldns\n",
						testtime3.tv_sec,
						testtime3.tv_nsec);
		}
#ifdef USE_ATOMIC
		ioctl(fd, F2FS_IOC_COMMIT_ATOMIC_WRITE);
#endif
		if (tid == 1) {
			clock_gettime(clk_id, &testtime4);
			printf("end22=%lds %ldns\n",
						testtime4.tv_sec,
						testtime4.tv_nsec);
		}
	}
testout:

#ifdef USE_GLOBAL_ATOMIC
#ifndef USE_ATOMIC
	ioctl(fd, F2FS_IOC_COMMIT_ATOMIC_WRITE);
#endif
#endif

	//Time is up! Leave!
	printf("Thread %lu totalSubmitted=%llu\n",
			tinfo->thread_num,
			totalSubmitted);

	free(wbuf);
out2:
	close(fd);
out:
	return NULL;
}

int testSetup(struct globalConfig *gf,
					pthread_attr_t *attr,
					struct thread_info **ptinfo)
{
	unsigned long i;
	int ret;
	struct thread_info *tinfo;

	printf("Allocate memory for pthread_create() arguments\n");
	*ptinfo = tinfo = calloc(gf->numthreads, sizeof(struct thread_info));
    if (!tinfo) {
		printf("Failed at %d\n", __LINE__);
		return -1;
    }

	ret = pthread_attr_init(attr);
    if (ret)
        goto errOut;

	for (i = 0; i < gf->numthreads; i++)
	{
		init_tinfo(&tinfo[i], i, gf);
		printf("Creating the %luth thread\n", i);
		ret = pthread_create(&tinfo[i].thread_id, attr,
                                  &thread_fn, &tinfo[i]);
        if (ret)
        {
        	//FIXME: stop already created threads
        	printf("Create thread %d failed with %d\n",
        					i, ret);
			goto errOut;
		}
	}

	return 0;
errOut:
	free(tinfo);
	return -1;
}

void runTest(struct globalConfig *gf,
					struct thread_info *tinfo)
{
	unsigned long numThreads = gf->numthreads;
	unsigned long i;
	int ret;

	for (i = 0; i < numThreads; i++)
		wait_for_set(&tinfo[i].ready);

	//FIXME: check ret value
	ret = clock_gettime(clk_id, &gf->startTime);
	gf->endTime = gf->startTime;
	gf->endTime.tv_sec += gf->runtime;
	//printf("ret: %i\n", ret);
	//printf("tp.tv_sec: %lld\n", tp.tv_sec);
	//printf("tp.tv_nsec: %ld\n", tp.tv_nsec);
	gf->start = 1;
}

void config_global(struct globalConfig *gf)
{
	gf->start = 0;
	gf->stop_cond = TIME_BASED;
	printf("Please input number of threads\n");
	scanf("%lu", &gf->numthreads);
	getchar();
	if (gf->numthreads > MAX_THREADS)
		gf->numthreads = MAX_THREADS;
	printf("Please input submit window size for each thread\n");
	scanf("%lu", &gf->submit_window);
	getchar();
	if (gf->submit_window > MAX_SUBMIT_WINDOW)
		gf->submit_window = MAX_SUBMIT_WINDOW;
	if (gf->submit_window < 1)
		gf->submit_window = 1;
	printf("Please input runtime (seconds)\n");
	scanf("%lu", &gf->runtime);
	getchar();
	if (gf->runtime > MAX_RUNTIME)
		gf->runtime = MAX_RUNTIME;
	if (gf->runtime < MIN_RUNTIME)
		gf->runtime = MIN_RUNTIME;
	
	printf("Please input filesize (nr pages)\n");
	scanf("%lu", &gf->max_npages_per_file);
	getchar();
	if (gf->max_npages_per_file > MAX_NR_FILE_PAGES)
		gf->max_npages_per_file = MAX_NR_FILE_PAGES;
	if (gf->max_npages_per_file < MIN_NR_FILE_PAGES)
		gf->max_npages_per_file = MIN_NR_FILE_PAGES;
	printf("Please input the prefix of filename (less than 28 Bytes)\n");
	scanf("%s", &gf->filePrefix);
	getchar();
	printf("Please input testCount (0 if don't count)\n");
	scanf("%lu", &gf->testCount);
	getchar();
}

int main()
{
	struct globalConfig gf;
	char ch;
	pthread_attr_t attr;
	struct thread_info *tinfo;
	unsigned long i;

reconfig:
	config_global(&gf);
	printConfig(&gf);
	printf("Type y to start test, others to reconfig\n");
	ch = getchar();
	while (getchar()!='\n');
	if (ch != 'y')
		goto reconfig;

	if (testSetup(&gf, &attr, &tinfo))
	{
		printf("test setup failed\n");
		return 0;
	}

	runTest(&gf, tinfo);

	for (i = 0; i < gf.numthreads; i++)
	{
		void *res;
		int ret;

		ret = pthread_join(tinfo[i].thread_id, &res);
		if (ret)
        	printf("Err ret = %d\n", ret);
	}
	printf("All threads returned. Free up space\n");
	free(tinfo);
	return 0;
}


