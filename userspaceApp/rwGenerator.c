#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#ifndef BLKDISCARD
#define BLKDISCARD	_IO(0x12,119)
#endif
#ifndef BLKSECDISCARD
#define BLKSECDISCARD	_IO(0x12,125)
#endif
#ifndef BLKGETSIZE
#define BLKGETSIZE	_IO(0x12,96)
#endif
#ifndef BLKGETSIZE64
#define BLKGETSIZE64	_IOR(0x12,114, size_t)
#endif
#ifndef BLKSSZGET
#define BLKSSZGET	_IO(0x12,104)
#endif


/*
int open(const char *pathname, int flags);
int open(const char *pathname, int flags, mode_t mode);
int creat(const char *pathname, mode_t mode);
int openat(int dirfd, const char *pathname, int flags);
int openat(int dirfd, const char *pathname, int flags, mode_t mode);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int ioctl(int fd, unsigned long request, ...);
*/
//#define BUFSIZE 65536
#define BUFSIZE 4194304


int main(int argc,char *argv[]){
	int fd;
	char *filepath;
	char op;
	unsigned long start, len;
	unsigned long lseekret, rwret;
	int discardret;
	char *buf;
	unsigned long long range[2];
	struct stat stat_buf;
	int fstatret;

	if(argc!=2){
		printf("usage: rwGenerator @path_to_file\n");
		return 0;
	}

	buf = (char *)malloc(BUFSIZE);
	if (!buf) {
		printf("malloc %lu failed\n", BUFSIZE);
		return 0;
	}
	
	filepath = argv[1];
	fd = open(filepath,O_RDWR);
	if(fd==-1){
		printf("open %s failed\n",filepath);
		return 0;
	}
	printf("open %s finished\n",filepath);
	for(;;){
		printf("input command( r/w/d startByte lenInBytes ):\n");
		op = getchar();
		if(op!='r' && op != 'w')
			goto others;
		scanf("%lu%lu",&start,&len);
		getchar();
		if(len>BUFSIZE){
			printf("lenInBytes exceeds %lu\n",BUFSIZE);
			continue;
		}
		printf("execute command [%c %lu %lu]\n",op,start,len);
		lseekret = lseek(fd,start,SEEK_SET);
		if(lseekret != start){
			printf("WARNING: lseekret != start\n");
		}
		if(op=='r'){
			rwret = read(fd,buf,len);
			if(rwret != len)
				printf("WARNING: rret != len\n");
		}else if(op=='w'){
			rwret = write(fd, buf, len);
			if(rwret != len)
				printf("WARNING: wret != len\n");
		}
		continue;
others:
		if (op=='d') {
			scanf("%lu%lu",&start,&len);
			getchar();
			range[0] = start;
			range[1] = len;
			discardret = ioctl(fd, BLKSECDISCARD, &range);
			printf("discard %llu %llu, ret = %d\n",
					range[0], range[1], discardret);
		} else if(op=='s') {
			getchar();
			fstatret = fstat(fd, &stat_buf);
			printf("fstat ret=%d, dev[%lu] ino[%lu] mode[0x%x] size[%ld]\n",
						fstatret, stat_buf.st_dev,
						stat_buf.st_ino, stat_buf.st_mode,
						stat_buf.st_size);
		} else if(op=='i') {
			int cmd;
			int ret;
			unsigned int total_sectors, sector_size;
			unsigned long ts;

			scanf("%d", &cmd);
			getchar();
			switch (cmd)
			{
			case 1:
				//BLKSSZGET
				ret = ioctl(fd, BLKSSZGET, &sector_size);
				printf("BLKSSZGET ret[%d] sectorsize[%u]\n",
						ret, sector_size);
				break;
			case 2:
				//BLKGETSIZE
				ret = ioctl(fd, BLKGETSIZE, &total_sectors);
				printf("BLKGETSIZE ret[%d] total_sectors[%u]\n",
						ret, total_sectors);
				break;
			case 3:
				//BLKGETSIZE64
				ret = ioctl(fd, BLKGETSIZE64, &ts);
				printf("BLKGETSIZE64 ret[%d] ts[%lu]\n",
						ret, ts);
				break;
			}
		} else {
			break;
		}
	}
	
	printf("closing file\n");
	free((void *)buf);
	close(fd);
	return 0;
}

