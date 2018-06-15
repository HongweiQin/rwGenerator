#include<stdio.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

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
*/
#define BUFSIZE 65536

int main(int argc,char *argv[]){
	int fd;
	char *filepath;
	char op;
	unsigned long start,len;
	unsigned long lseekret,rwret;
	char buf[BUFSIZE];
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
		printf("input command( r/w startByte lenInBytes ):\n");
		op = getchar();
		if(op!='r' && op != 'w')
			break;
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
		else{
			break;
		}
	}
	
	printf("closing file\n");
	close(fd);
	return 0;
}
