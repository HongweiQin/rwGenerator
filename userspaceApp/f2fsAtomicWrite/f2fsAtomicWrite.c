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
	volatile short start;
	enum condition stop_cond;
	char filePrefix[32];
	struct timespec startTime;
	struct timespec endTime;
};

void printConfig(struct globalConfig *gf)
{
	printf("-------print config--------\n");
	printf("num threads = %lu\n", gf->numthreads);
	printf("submit window size = %lu\n", gf->submit_window);
	printf("Runtime = %lu Seconds\n", gf->runtime);
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
	int ret, nreap;
	io_context_t ctx;
	long long offset = 0;
	int i;
	int k = 0;
	struct write_req *request_array;
	struct io_event *events;
	struct iocb **ios;
	int testcount = 3;
	unsigned long long totalSubmitted = 0;

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
	//setup queue
	memset(&ctx, 0, sizeof(ctx));
	if(io_setup(gf->submit_window, &ctx)){
		printf("io_setup errorn\n");
		goto out2;
	}

	request_array = (struct write_req *)calloc(budget, sizeof(*request_array));
	if (!request_array)
	{
		printf("Not enough memory for request array\n");
		goto out3;
	}

	
	events = (struct io_event *)calloc(budget, sizeof(*events));
	if (!events)
	{
		printf("Not enough memory for events array\n");
		goto out4;
	}

	ios = (struct iocb **)calloc(budget, sizeof(struct iocb *));
	if (!ios)
	{
		printf("Not enough memory for ios array\n");
		goto out5;
	}
	for (i = 0; i < budget; i++) {
		request_array[i].index = i;
		ios[i] = &request_array[i].iocb;
	}

	tinfo->ready = 1;
	wait_for_set(&gf->start);

	//start
#ifdef USE_GLOBAL_ATOMIC
#ifndef USE_ATOMIC
	ioctl(fd, F2FS_IOC_START_ATOMIC_WRITE);
#endif
#endif
	for (i = 0;
			(i < budget) && test_continue(tinfo, gf);
			i++)
	{
		io_prep_pwrite(ios[i], fd, request_array[i].buf, 4096, offset);
		offset += 4096;
		if (offset > (1<<20))
			offset = 0;

		tinfo->currentBudget--;
		totalSubmitted++;
		//printf("Thread %lu budget--\n",
		//			tinfo->thread_num);

		//submit
#ifdef USE_ATOMIC
		ioctl(fd, F2FS_IOC_START_ATOMIC_WRITE);
#endif
submit_again:
		ret = io_submit(ctx, 1, &ios[i]);
		if (ret == -EAGAIN)
			goto submit_again;
		else if (ret != 1) {
			//FIXME
			printf("io submit failed, ret=%d\n", ret);
		}
#ifdef USE_ATOMIC
		ioctl(fd, F2FS_IOC_COMMIT_ATOMIC_WRITE);
#endif
	}

	for (;tinfo->currentBudget != budget;) {
		//Here we don't set wait timer. So if any request doesn't return, 
		//we'll not return even if time expire.
		nreap = io_getevents(ctx, 1, budget, events, NULL);
		//printf("Thread %d reap %d requests\ncb=%d,b=%d\n",
		//		tinfo->thread_num, nreap,
		//		tinfo->currentBudget, budget);
		for (i = 0; i < nreap; i++)
		{
			struct io_event *event = &events[i];
			struct write_req *req = (struct write_req *)event->obj;
			int index = req->index;

			//printf("reap %d's content: data=%p, obj=%p, res = %llu\n",
			//		i, event->data, event->obj, event->res);

			//Here we just omit the result of the request, assuming it succeeded.
			tinfo->currentBudget++;
			//printf("Thread %lu budget++\n",
			//		tinfo->thread_num);
			if (testcount && test_continue(tinfo, gf)) {
				//testcount--;
				io_prep_pwrite(ios[index], fd, request_array[index].buf, 4096, offset);
				offset += 4096;
				if (offset > (1<<20))
					offset = 0;

				tinfo->currentBudget--;
				totalSubmitted++;
				//printf("Thread %lu budget--\n",
				//	tinfo->thread_num);
				//submit
#ifdef USE_ATOMIC
				ioctl(fd, F2FS_IOC_START_ATOMIC_WRITE);
#endif
submit_again2:
				ret = io_submit(ctx, 1, &ios[index]);
				if (ret == -EAGAIN)
					goto submit_again2;
				else if (ret != 1) {
					//FIXME
					printf("io submit failed, ret=%d\n", ret);
				}
#ifdef USE_ATOMIC
				ioctl(fd, F2FS_IOC_COMMIT_ATOMIC_WRITE);
#endif
			} 
		}
		//printf("endloop cb=%d,b=%d\n",
		//		tinfo->currentBudget, budget);
	}

#ifdef USE_GLOBAL_ATOMIC
#ifndef USE_ATOMIC
	ioctl(fd, F2FS_IOC_COMMIT_ATOMIC_WRITE);
#endif
#endif

	//Time is up! Leave!
	printf("Thread %lu totalSubmitted=%llu\n",
			tinfo->thread_num,
			totalSubmitted);
	free(ios);
out5:
	free(events);
out4:
	free(request_array);
out3:
	io_destroy(ctx);
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
	printf("Please input the prefix of filename (less than 28 Bytes)\n");
	scanf("%s", &gf->filePrefix);
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


