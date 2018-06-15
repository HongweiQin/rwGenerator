//see: https://www.fsl.cs.sunysb.edu/~vass/linux-aio.txt
#define _GNU_SOURCE		/* syscall() is not POSIX */
#define BUFSIZE (65536)
#define BUFPAGES (BUFSIZE/4096)

#include <stdio.h>		/* for perror() */
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>		/* for syscall() */
//#include <sys/syscall.h>	/* for __NR_* definitions */
//#include <linux/aio_abi.h>	/* for AIO types and constants */
#include <fcntl.h>		/* O_RDWR */
#include <string.h>		/* memset() */
#include <inttypes.h>	/* uint64_t */
#include <libaio.h> //for io_prep_pwrite etc.

#include <sys/stat.h>
#include <sys/types.h>



#if 0
inline int io_setup(unsigned nr, aio_context_t *ctxp)
{
	return syscall(__NR_io_setup, nr, ctxp);
}

inline int io_destroy(aio_context_t ctx) 
{
	return syscall(__NR_io_destroy, ctx);
}

inline int io_submit(aio_context_t ctx, long nr,  struct iocb **iocbpp) 
{
 	return syscall(__NR_io_submit, ctx, nr, iocbpp);
}

inline int io_getevents(aio_context_t ctx, long min_nr, long max_nr,
		struct io_event *events, struct timespec *timeout)
{
	return syscall(__NR_io_getevents, ctx, min_nr, max_nr, events, timeout);
}
#endif

int main(int argc,char *argv[])
{
	char *filepath;
	//aio_context_t ctx;
	io_context_t aio_ctx;
	struct iocb cb0,cb1;
	struct iocb *cbs[2];
	char data[BUFSIZE];
	char data1[BUFSIZE];
	struct io_event events[2];
	int ret;
	int fd;
	unsigned long startPN,nrPages,verifyNum;
	int i;
	unsigned long *pnum;
	int err;

	if(argc!=5){
		printf("usage: libaioGen @path_to_file @startPN @nrPages @verifyNum\n");
		return 0;
	}
	filepath = argv[1];

	sscanf(argv[2],"%lu",&startPN);
	sscanf(argv[3],"%lu",&nrPages);
	sscanf(argv[4],"%lu",&verifyNum);
	if(nrPages > BUFPAGES)
		nrPages = BUFPAGES;

	printf("startPN=0x%lx,nrPages=0x%lx,verifyNum=0x%lx\n",startPN,nrPages,verifyNum);

	pnum = (unsigned long *)data;

	for(i=0;i<BUFSIZE/8;i++){
		pnum[i] = verifyNum;
	}

	fd = open(filepath, O_RDWR | O_CREAT);
	if (fd < 0) {
		perror("open error");
		return -1;
 	}
	printf("open finished\n");
	//getchar();


	err = io_queue_init(128, &aio_ctx);

#if 0
	ctx = 0;

	ret = io_setup(128, &ctx);
 	if (ret < 0) {
		perror("io_setup error");
 		return -1;
	}
#endif

	io_prep_pwrite(&cb0, fd, data, nrPages<<12, startPN<<12);
	io_prep_pread(&cb1, fd, data1, 32768, 0);

#if 0
	/* setup I/O control block */
	memset(&cb0, 0, sizeof(cb0));
	cb0.aio_fildes = fd;
 	cb0.aio_lio_opcode = IOCB_CMD_PWRITEV;
	/* command-specific options */
	cb0.aio_buf = (uint64_t)data;
	cb0.aio_offset = startPN<<12;
	cb0.aio_nbytes = nrPages<<12;
#endif

#if 0
	/* setup I/O control block */
	memset(&cb1, 0, sizeof(cb1));
	cb1.aio_fildes = fd;
 	cb1.aio_lio_opcode = IOCB_CMD_PREAD;
	/* command-specific options */
	cb1.aio_buf = (uint64_t)data1;
	cb1.aio_offset = 0;
	cb1.aio_nbytes = 8192;
#endif

	


	cbs[0] = &cb0;
	cbs[1] = &cb1;

	ret = io_submit(aio_ctx, 2, cbs);//
	if (ret <0) {
	   	perror("io_submit error");
		return  -1;
	}
	else{
		printf("io_submit finished, ret=%d\n",ret);
	}

#if 0
/* get the reply */
	ret = io_getevents(ctx, 2, 2, events, NULL);//
	printf("io_getevents finished, ret=%d\n", ret);
#endif
	ret = io_getevents(aio_ctx, 2, 2, events, NULL);
	printf("io_getevents finished, ret=%d\n", ret);




	io_destroy(aio_ctx);

#if 0
	ret = io_destroy(ctx);//
	if (ret < 0) {
		perror("io_destroy error");
		return -1;
	}
#endif
  
 	return 0;
}
