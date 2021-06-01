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
#include <linux/syscalls.h>

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
int muteState;

static void rGen_end_io(struct bio *bio)
{
	struct bio_vec *bv;
	int i;
	//pr_notice("%s\n",__FUNCTION__);
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
	int first=1;
	unsigned long buffer=0,count=0;

	//pr_notice("%s\n",__FUNCTION__);
	bio_for_each_segment_all(bv, bio, i) {
		struct page *page = bv->bv_page;
		count++;
		if(first){
			first=0;
			buffer = (unsigned long)page_address(page);
			//pr_notice("wGen_endio:buffer=%lu\n",buffer);
		}
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
	if(buffer){
		free_pages(buffer,get_count_order(count));
	}
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
	int j;
	struct bio *bio;
	struct page *page;
	int verify;
	unsigned long verifyNum;
	int pageErr;
	unsigned long *pagebase;
	int error;
	if(!bdev)
		return;
	verify=0;
	if(comm[0]=='v'){
		verify = 1;
		sscanf(comm+2,"%lu %lu %lu",&startPN,&nrPages,&verifyNum);
	}
	else{
		sscanf(comm+1,"%lu %lu",&startPN,&nrPages);
	}
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
		error = lock_page_killable(page);
		if (unlikely(error)){
			pr_err("before readpage, can't lock page!\n");
			goto readpage_error;
		}
		ClearPageUptodate(page);
		pagebase = (unsigned long *) (rwbuffer + (i<<12));
		bio_add_page(bio,page,PAGE_SIZE,0);
		for(j=0;j<64;j++)
			pagebase[j] = j;
		
	}
	submit_bio(bio);
	//wait for completion
	for(i=0;i<nrPages;i++){
		page = virt_to_page(rwbuffer+(i<<12));
		if (!PageUptodate(page)) {
			error = lock_page_killable(page);
			if (unlikely(error)){
				pr_err("waiting for completion, can't lock page!\n");
				goto readpage_error;
			}
			if (!PageUptodate(page)){
				pr_err("IO error, page not uptodate!\n");
				goto readpage_error;
			}
			unlock_page(page);
		}
	}
	
	if(verify){
		for(i=0;i<nrPages;i++){
			pagebase = (unsigned long *) (rwbuffer + (i<<12));
			pageErr=0;
			for(j=0;j<64;j++){
				if(pagebase[j] != verifyNum)
					pageErr=1;
			}
			if(pageErr){
				pr_notice("page[%lu+%lu]'s data corrupted\n",startPN,i);
				pr_notice("-----Dump64BData-----\n");
				for(j=0;j<8;j++)
					pr_notice("0x%lx\n",pagebase[j]);
				pr_notice("-----DumpFinished-----\n");
			}
		}
		if(!muteState)
			pr_notice("rv %lu %lu %lu finished\n",startPN,nrPages,verifyNum);
	}
	else{
		if(!muteState)
			pr_notice("r %lu %lu finished\n",startPN,nrPages);
	}
	return;
readpage_error:
	return;
}

//usage: f
static void handle_flush(char *comm)
{
	blkdev_issue_flush(bdev, GFP_KERNEL, NULL);
}

//usage: w(s)(v)(p)(f)(n) startPageNumber nrPages (verifyNum)
static void handle_write(char *comm){
	unsigned long startPN,nrPages;
	unsigned long i;
	struct bio *bio;
	struct page *page;
	unsigned bioflag = 0;
	int verify = 0;
	unsigned long verifyNum;
	unsigned long *pagebase;
	int j;
	char *pch = comm;
	unsigned long buffer;
	if(!bdev)
		return;

continue_parse:
	switch (*pch)
	{
	case 's':
		bioflag |= REQ_SYNC;
		pch++;
		goto continue_parse;
	case 'v':
		verify = 1;
		pch++;
		goto continue_parse;
	case 'f':
		bioflag |= REQ_FUA;
		pch++;
		goto continue_parse;
	case 'n':
		bioflag |= REQ_NOMERGE;
		pch++;
		goto continue_parse;
	case 'p':
		bioflag |= REQ_PREFLUSH;
		pch++;
		goto continue_parse;
	default:
		break;
	}

	if (verify)
		sscanf(pch,"%lu %lu %lu",&startPN,&nrPages,&verifyNum);
	else
		sscanf(pch,"%lu %lu",&startPN,&nrPages);
	if (nrPages > MAXNRPAGES) {
		pr_warn("Out of bound nrPages, set it to %d\n",
							MAXNRPAGES);
		nrPages = MAXNRPAGES;
	}

	bio= bio_alloc(GFP_KERNEL,
				min_t(unsigned long, nrPages, BIO_MAX_PAGES));
	
	bio_set_dev(bio, bdev);
	bio->bi_iter.bi_sector = startPN << 3;
	bio->bi_end_io = wGen_end_io;
	bio->bi_private = NULL;
	bio_set_op_attrs(bio, REQ_OP_WRITE|bioflag, 0);
	buffer = __get_free_pages(GFP_ATOMIC,get_count_order(nrPages));
	//pr_notice("allocate buffer=%lu\n",buffer);
	for(i=0;i<nrPages;i++){
		page = virt_to_page(buffer+(i<<12));
		bio_add_page(bio,page,PAGE_SIZE,0);
		set_page_writeback(page);
		if(verify){
			pagebase = (unsigned long *) (buffer+(i<<12));
			for(j=0;j<64;j++)
				pagebase[j] = verifyNum;
		}
	}
	pr_notice("%s, submit\n",
				__func__);
	submit_bio(bio);
	pr_notice("%s, return\n",
				__func__);


	for(i=0;i<nrPages;i++){
		page = virt_to_page(buffer+(i<<12));
		wait_on_page_writeback(page);
	}
	pr_notice("%s, all pages written back\n",
				__func__);
	if (!muteState) {
		if(verify)
			pr_notice("wv %lu %lu %lu finished\n",startPN,nrPages,verifyNum);
		else
			pr_notice("w %lu %lu finished\n",startPN,nrPages);
	}
	return;
}

int blkdev_issue_discard_test(struct block_device *bdev, sector_t sector,
		sector_t nr_sects, gfp_t gfp_mask, unsigned long flags)
{
	struct bio *bio = NULL;
	struct blk_plug plug;
	int ret;

	blk_start_plug(&plug);
	ret = __blkdev_issue_discard(bdev, sector, nr_sects, gfp_mask, flags,
			&bio);
	pr_notice("%s, ret=%d\n",
				__func__, ret);
	if (!ret && bio) {
		ret = submit_bio_wait(bio);
		if (ret == -EOPNOTSUPP)
			ret = 0;
		bio_put(bio);
	}
	blk_finish_plug(&plug);

	return ret;
}


int blkdev_ioctl_test(struct block_device *bdev, fmode_t mode, unsigned cmd,
			unsigned long arg)
{
	uint64_t range[2];
	uint64_t start, len;
	unsigned long flags = BLKDEV_DISCARD_SECURE;
	struct request_queue *q = bdev_get_queue(bdev);
	struct address_space *mapping = bdev->bd_inode->i_mapping;


	if (!(mode & FMODE_WRITE)) {
		pr_notice("%s, 1\n", __func__);
		return -EBADF;
	}

	if (!blk_queue_discard(q)) {
		pr_notice("%s, 2\n", __func__);
		return -EOPNOTSUPP;
	}

	if (copy_from_user(range, (void __user *)arg, sizeof(range))) {
		pr_notice("%s, 3\n", __func__);
		return -EFAULT;
	}

	start = range[0];
	len = range[1];

	pr_notice("%s, start0x%llx len0x%llx\n",
				__func__, start, len);

	if (start & 511)
		return -EINVAL;
	if (len & 511)
		return -EINVAL;

	if (start + len > i_size_read(bdev->bd_inode)) {
		pr_notice("%s, 4 isize %lld\n",
				__func__, i_size_read(bdev->bd_inode));
		return -EINVAL;
	}
	truncate_inode_pages_range(mapping, start, start + len - 1);
	return blkdev_issue_discard_test(bdev, start >> 9, len >> 9,
				    GFP_KERNEL, flags);
}



//usage: d startPageNumber nrPages
static void handle_discard(char *comm){
	uint64_t range[2];
	int ret;
	mm_segment_t old_fs;

	sscanf(comm + 1,"%llu%llu",
				&range[0], &range[1]);
	if (range[1] > MAXNRPAGES) {
		pr_warn("Out of bound nrPages, set it to %d\n",
							MAXNRPAGES);
		range[1] = MAXNRPAGES;
	}
	range[0] <<= 12;
	range[1] <<= 12;
	old_fs = get_fs();
    set_fs(KERNEL_DS);
	ret = blkdev_ioctl_test(bdev, mode, BLKSECDISCARD, (unsigned long)range);
	set_fs(old_fs);
	if (!muteState) {
		pr_notice("d %llu %llu finished, ret=%d\n",
					range[0], range[1], ret);
	}
}

//usage: s startPageNumber nrPages first_offset
static void handle_special(char *comm){
	unsigned long startPN,nrPages;
	unsigned long i;
	struct bio *bio;
	struct page *page;
	unsigned issync=0;
	unsigned long fst_offset;
	char *pch = comm;
	unsigned long buffer;
	if(!bdev)
		return;

	sscanf(pch+1, "%lu %lu %lu",
		&startPN, &nrPages, &fst_offset);

	if (fst_offset >= PAGE_SIZE) {
		pr_warn("Invalid fst_offset value[%lu], automatically set it to 0\n",
							fst_offset);
		fst_offset = 0;
	}
	if (fst_offset)
		nrPages++;
	if (nrPages > MAXNRPAGES) {
		pr_warn("Out of bound nrPages, set it to %d. Clear fst_offset to 0\n",
							MAXNRPAGES);
		nrPages = MAXNRPAGES;
		fst_offset = 0;
	}

	bio= bio_alloc(GFP_KERNEL,
				min_t(unsigned long, nrPages, BIO_MAX_PAGES));
	
	bio_set_dev(bio, bdev);
	bio->bi_iter.bi_sector = startPN << 3;
	bio->bi_end_io = wGen_end_io;
	bio->bi_private = NULL;
	bio_set_op_attrs(bio, REQ_OP_WRITE|issync, 0);
	buffer = __get_free_pages(GFP_ATOMIC,get_count_order(nrPages));
	//pr_notice("allocate buffer=%lu\n",buffer);
	if (fst_offset) {
		page = virt_to_page(buffer);
		bio_add_page(bio,page,PAGE_SIZE-fst_offset,fst_offset);
		set_page_writeback(page);
		for(i=1;i<nrPages-1;i++){
			page = virt_to_page(buffer+(i<<12));
			bio_add_page(bio,page,PAGE_SIZE,0);
			set_page_writeback(page);
		}
		page = virt_to_page(buffer+((nrPages-1)<<12));
		bio_add_page(bio,page,fst_offset,0);
		set_page_writeback(page);
	} else {
		for(i=0;i<nrPages;i++){
			page = virt_to_page(buffer+(i<<12));
			bio_add_page(bio,page,PAGE_SIZE,0);
			set_page_writeback(page);
		}
	}
	submit_bio(bio);

	if(!muteState)
		pr_notice("w %lu %lu %lu finished\n",
				startPN, nrPages, fst_offset);
	return;
}

//usage: mo/mf : mute on/off
static void handle_mute(char *comm){

	if (*comm == 'o')
		muteState = 1;
	else if(*comm == 'f')
		muteState = 0;

	pr_notice("Mute %s\n", muteState?"on":"off");
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
			handle_read(usrCommand+1);
			break;
		case 'w':
			handle_write(usrCommand+1);
			break;
		case 'f':
			handle_flush(usrCommand+1);
			break;
		case 'd':
			handle_discard(usrCommand+1);
			break;
		case 's':
			handle_special(usrCommand+1);
			break;
		case 'm':
			handle_mute(usrCommand+1);
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
	muteState = 0;
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


