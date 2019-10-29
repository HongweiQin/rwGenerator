#include<stdio.h>

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
#define BUFSIZE 65536

int main(int argc,char *argv[]){
	int fd;
	char *filepath;
	char op;
	unsigned long start, len;
	unsigned long lseekret, rwret;
	int discardret;
	char buf[BUFSIZE];
	unsigned long long range[2];
	struct stat stat_buf;
	int fstatret;

	if(argc!=2){
		printf("usage: rwGenerator @path_to_file\n");
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
		if(len>65536){
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
			printf("fstat ret=%d, dev[%lu] ino[%lu] mode[0x%x]\n",
						fstatret, stat_buf.st_dev,
						stat_buf.st_ino, stat_buf.st_mode);
		} else {
			break;
		}
	}
	
	printf("closing file\n");
	close(fd);
	return 0;
}

