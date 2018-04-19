//#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
//#include <linux/idr.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kdev_t.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/list_sort.h>
//#include <linux/mm.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/poison.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/t10-pi.h>
#include <linux/types.h>
//#include <scsi/sg.h>
//#include <asm-generic/io-64-nonatomic-lo-hi.h>

#include <linux/proc_fs.h>
//#include <linux/seq_file.h>

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "[rwGenerator] " fmt
#define MAXNRPAGES 256
#define MAXNRPAGES_ORDER 8

struct block_device *bdev;
unsigned long rwbuffer;
fmode_t mode = FMODE_READ|FMODE_WRITE|FMODE_LSEEK;

static void rGen_end_io(struct bio *bio)
{
	struct bio_vec *bv;
	int i;
	bio_for_each_segment_all(bv, bio, i) {
		struct page *page = bv->bv_page;
		if (!bio->bi_status) {
			SetPageUptodate(page);
		} else {
			ClearPageUptodate(page);
			SetPageError(page);
		}
		unlock_page(page);
	}
	bio_put(bio);
	return;
}

static void wGen_end_io(struct bio *bio)
{
	struct bio_vec *bv;
	int i;
	bio_for_each_segment_all(bv, bio, i) {
		struct page *page = bv->bv_page;
		if (bio->bi_status) {
			set_page_dirty(page);
			SetPageError(page);
		}
		end_page_writeback(page);
		if(PageLocked(page)){
			pr_notice("pageIsLocked\n");
			unlock_page(page);
		}
	}
	bio_put(bio);
	return;
}

static void handle_close(void){
	if(bdev){
		blkdev_put(bdev,mode);
		bdev = NULL;
		pr_notice("bdev closed\n");
	}
	return;
}


static void handle_open(char *fname,int maxlen){
	int i;
	for(i=0;i<maxlen;i++)
		if(fname[i]=='\n')
			fname[i] = '\0';
	handle_close();
	bdev = blkdev_get_by_path(fname, mode, "rwGenerator");
	if (IS_ERR(bdev)){
		pr_err("open %s failed\n",fname);
		bdev = NULL;
		return ;
	}
	pr_notice("open finished\n");
	return;
}

//usage: r startPageNumber nrPages
static void handle_read(char *comm){
	unsigned long startPN,nrPages;
	unsigned long i;
	struct bio *bio;
	struct page *page;
	if(!bdev)
		return;
	sscanf(comm,"%lu %lu",&startPN,&nrPages);
	if(nrPages>MAXNRPAGES)
		nrPages = MAXNRPAGES;
	
	bio= bio_alloc(GFP_KERNEL,
				min_t(unsigned long, nrPages, BIO_MAX_PAGES));
	
	bio_set_dev(bio, bdev);
	bio->bi_iter.bi_sector = startPN << 3;
	bio->bi_end_io = rGen_end_io;
	bio->bi_private = NULL;
	bio_set_op_attrs(bio, REQ_OP_READ, 0);
	for(i=0;i<nrPages;i++){
		page = virt_to_page(rwbuffer+(i<<12));
		bio_add_page(bio,page,PAGE_SIZE,0);
	}
	submit_bio(bio);
	pr_notice("r %lu %lu finished\n",startPN,nrPages);
	return;
}

//usage: w(s) startPageNumber nrPages
static void handle_write(char *comm){
	unsigned long startPN,nrPages;
	unsigned long i;
	struct bio *bio;
	struct page *page;
	unsigned issync=0;
	if(!bdev)
		return;
	if(comm[0]=='s')
		issync=REQ_SYNC;
	sscanf(comm+1,"%lu %lu",&startPN,&nrPages);
	if(nrPages>MAXNRPAGES)
		nrPages = MAXNRPAGES;

	bio= bio_alloc(GFP_KERNEL,
				min_t(unsigned long, nrPages, BIO_MAX_PAGES));
	
	bio_set_dev(bio, bdev);
	bio->bi_iter.bi_sector = startPN << 3;
	bio->bi_end_io = wGen_end_io;
	bio->bi_private = NULL;
	bio_set_op_attrs(bio, REQ_OP_WRITE|issync, 0);
	for(i=0;i<nrPages;i++){
		page = virt_to_page(rwbuffer+(i<<12));
		bio_add_page(bio,page,PAGE_SIZE,0);
		set_page_writeback(page);
	}
	submit_bio(bio);
	pr_notice("w %lu %lu finished\n",startPN,nrPages);
	return;
}

static ssize_t rwGen_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	char usrCommand[512];
	int ret;
	ret = copy_from_user(usrCommand, buffer,count);
	//pr_notice("command:%s",usrCommand);
	switch(usrCommand[0]){
		case 'o':
			handle_open(usrCommand+2,count-2);
			break;
		case 'c':
			handle_close();
			break;
		case 'r':
			handle_read(usrCommand+2);
			break;
		case 'w':
			handle_write(usrCommand+1);
			break;
	}
	return count;
}

static const struct file_operations rwGen_proc_fops = {
  .owner = THIS_MODULE,
  //.open = rwGen_proc_open,
  //.read = seq_read,
  .write = rwGen_write,
  //.llseek = seq_lseek,
 // .release = single_release,
};


static int __init rwGen_init(void)
{
	pr_notice("rwGen init\n");
	bdev=NULL;
	rwbuffer = __get_free_pages(GFP_KERNEL,MAXNRPAGES_ORDER);
	if(!rwbuffer)
		goto allocBufFailed;
	proc_create("rwGen", 0, NULL, &rwGen_proc_fops);
	return 0;
	free_pages(rwbuffer,MAXNRPAGES_ORDER);
allocBufFailed:
	return -ENOMEM;
}

static void __exit rwGen_exit(void)
{
	pr_notice("rwGen exit\n");
	remove_proc_entry("rwGen", NULL);
	handle_close();
	free_pages(rwbuffer,MAXNRPAGES_ORDER);
	return;
}

MODULE_AUTHOR("Hongwei Qin <glqhw@hust.edu.cn>");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
module_init(rwGen_init);
module_exit(rwGen_exit);

