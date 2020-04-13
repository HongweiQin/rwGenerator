#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kdev_t.h>
#include <linux/kthread.h>
#include <linux/kernel.h>
#include <linux/list_sort.h>
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
#include <linux/parser.h>

#include <linux/proc_fs.h>
//#include <linux/seq_file.h>

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "[QIOG] " fmt

#define DEF_MAX_ISSUED_WITHOUT_SCHED (32)
#define DEF_QD (8)

struct block_device *qiog_bdev;
fmode_t mode = FMODE_READ|FMODE_WRITE|FMODE_LSEEK;
char *qiog_holder = "QIOG";
struct list_head qiog_global_jobs;
atomic_t jobID;
struct bio_set qiog_bio_set;
spinlock_t printSummaryLock;


char *op_type_name[] = {
	"read",
	"write",
	"NR_OP"
};

enum {
	Opt_iotype,
	Opt_start_addr,
	Opt_end_addr,
	Opt_MIWOS,
	Opt_qd,
	Opt_pgs_per_rq,
	Opt_count_based,
	Opt_time_based,
	Opt_preflush,
	Opt_fua,
	Opt_err,
};


static match_table_t qiog_tokens = {
	{Opt_iotype, "iotype=%s"},
	{Opt_start_addr, "sa=%u"},
	{Opt_end_addr, "ea=%u"},
	{Opt_MIWOS, "miwos=%u"},
	{Opt_qd, "qd=%u"},
	{Opt_pgs_per_rq, "rqpgs=%u"},
	{Opt_count_based, "countbased=%u"},
	{Opt_time_based, "timebased=%u"},
	{Opt_preflush, "preflush"},
	{Opt_fua, "fua"},
	{Opt_err, NULL},
};


enum op_type {
	qiog_read = 0,
	qiog_write,
	qiog_nr_op
};

struct qiog_request {
	struct bio bio; //Must be the last one
};

struct qiog_job_cfg {
	enum op_type op;
	unsigned long start_addr;	//Inclusive. In bytes.
	unsigned long end_addr;		//Exclusive. In Bytes.
	unsigned int cfg_max_issued_without_sched;
	unsigned int cfg_qd;
	unsigned int cfg_pgs_per_rq;
	unsigned int cfg_count_based;
	unsigned int cfg_time_based;
	unsigned int cfg_flags;
};

struct qiog_job {
	struct list_head list;
	char name[16];
	unsigned int id;
	struct qiog_job_cfg cfg;
	struct task_struct *ts;
	unsigned long total_issued;
	atomic_t on_the_fly;
	int issued_without_sched;
	sector_t current_sec;
	int ready;
};

static void qiog_set_default_cfg(struct qiog_job_cfg *cfg)
{
	cfg->op = qiog_read;
	cfg->start_addr = 0;
	cfg->cfg_max_issued_without_sched = DEF_MAX_ISSUED_WITHOUT_SCHED;
	cfg->cfg_qd = DEF_QD;
	cfg->end_addr = 1 << 30; //1 Gigabytes
	cfg->cfg_pgs_per_rq = 1;
	cfg->cfg_count_based = 1;
	cfg->cfg_time_based = 0;
	cfg->cfg_flags = 0;
}

static void qiog_parse_options(struct qiog_job_cfg *cfg,
									const char *cmd)
{
	char *p, *name;
	substring_t args[MAX_OPT_ARGS];
	char *options, *origoptions;
	//unsigned long long arglu;
	int argi;

	origoptions = options = kstrdup(cmd, GFP_KERNEL);
	if (!options)
		return;

	while ((p = strsep(&options, " ")) != NULL) {
		int token;

		if (!*p)
			continue;

		args[0].to = args[0].from = NULL;
		token = match_token(p, qiog_tokens, args);

		pr_notice("p[%s] token[%d]\n", p, token);
		switch (token) {
		case Opt_iotype:
			name = match_strdup(&args[0]);

			if (!name)
				break;
			if (strlen(name) == 4 && !strncmp(name, "read", 4)) {
				cfg->op = qiog_read;
				pr_notice("set iotype as read\n");
			} else if (strlen(name) == 5 && !strncmp(name, "write", 5)) {
				cfg->op = qiog_write;
				pr_notice("set iotype as write\n");
			}
			kvfree(name);
			break;
		case Opt_start_addr:
			if (args->from && match_int(args, &argi))
				break;
			cfg->start_addr = (unsigned long)argi;
			pr_notice("set start_addr as %u\n",
						argi);
			break;
		case Opt_end_addr:
			if (args->from && match_int(args, &argi))
				break;
			cfg->end_addr = (unsigned long)argi;
			pr_notice("set end_addr as %u\n",
						argi);
			break;
		case Opt_MIWOS:
			if (args->from && match_int(args, &argi))
				break;
			cfg->cfg_max_issued_without_sched = (unsigned long)argi;
			pr_notice("set cfg_max_issued_without_sched as %u\n",
						argi);
			break;
		case Opt_qd:
			if (args->from && match_int(args, &argi))
				break;
			cfg->cfg_qd = (unsigned)argi;
			pr_notice("set queue depth as %d\n",
						argi);
			break;
		case Opt_pgs_per_rq:
			if (args->from && match_int(args, &argi))
				break;
			cfg->cfg_pgs_per_rq = (unsigned)argi;
			pr_notice("set cfg_pgs_per_rq as %d\n",
						argi);
			break;
		case Opt_count_based:
			if (args->from && match_int(args, &argi))
				break;
			cfg->cfg_count_based = (unsigned)argi;
			pr_notice("set cfg_count_based as %d\n",
						argi);
			break;
		case Opt_time_based:
			if (args->from && match_int(args, &argi))
				break;
			cfg->cfg_time_based = (unsigned)argi;
			pr_notice("set cfg_time_based as %d\n",
						argi);
			break;
		case Opt_preflush:
			cfg->cfg_flags |= REQ_PREFLUSH;
			pr_notice("set preflush\n");
			break;
		case Opt_fua:
			cfg->cfg_flags |= REQ_FUA;
			pr_notice("set fua\n");
			break;
		}
	}

	kvfree(origoptions);
}


static void qiog_end_io(struct bio *bio)
{
	struct bvec_iter iter;
	struct bio_vec bv;
	struct qiog_job *job = (struct qiog_job *)bio->bi_private;

	atomic_dec(&job->on_the_fly);
	bio_for_each_segment(bv, bio, iter) {
		__free_page(bv.bv_page);
	}
	bio_put(bio);
}

//TODO: other ways to generate. Add boundary check.
static sector_t qiog_req_generate_bio_sec(struct qiog_job *job,
							struct qiog_job_cfg *cfg)
{
	sector_t cu = job->current_sec;

	job->current_sec += cfg->cfg_pgs_per_rq << 3;
	return cu;
}

struct bio *qiog_new_bio(struct qiog_job *job,
							struct qiog_job_cfg *cfg)
{
	struct qiog_request *req;
	struct bio *bio;
	unsigned bio_op;
	unsigned bio_flags = cfg->cfg_flags;
	unsigned int i;

	bio = bio_alloc_bioset(GFP_KERNEL,
						cfg->cfg_pgs_per_rq,
						&qiog_bio_set);
	if (!bio)
		goto out1;
	req = container_of(bio, struct qiog_request, bio);
	bio_set_dev(bio, qiog_bdev);
	bio->bi_iter.bi_sector = qiog_req_generate_bio_sec(job, cfg);
	bio->bi_end_io = qiog_end_io;
	bio->bi_private = job;

	switch (cfg->op)
	{
	case qiog_read:
		bio_op = REQ_OP_READ;
		break;
	case qiog_write:
		bio_op = REQ_OP_WRITE;
		break;
	default:
		pr_err("%s, op[%d] not supported\n",
					__func__, cfg->op);
		goto out2;
	}

	bio_set_op_attrs(bio, bio_op|bio_flags, 0);

	for (i = 0;
			i < cfg->cfg_pgs_per_rq;
			i++) {
		struct page *page = alloc_page(GFP_ATOMIC);

		if (!page)
			goto out3;
		bio_add_page(bio, page, PAGE_SIZE, 0);
	}

	return bio;
out3:
	do {
		struct bvec_iter iter;
		struct bio_vec bv;

		bio_for_each_segment(bv, bio, iter) {
			__free_page(bv.bv_page);
		}
	} while (0);
out2:
	bio_put(bio);
out1:
	return NULL;
}

static int qiog_job_should_stop(struct qiog_job *job,
							struct qiog_job_cfg *cfg)
{
	if (cfg->cfg_count_based && job->total_issued >= 16)
		return 1;
	if (cfg->cfg_time_based)//TODO: add time based
		return 1;
	return 0;
}

static void qiog_wait_for_pending_requests(struct qiog_job *job)
{
retry:
	if (atomic_read(&job->on_the_fly) > 0) {
		msleep(1);
		goto retry;
	}
}

static void qiog_print_summary(struct qiog_job *job)
{
	spin_lock(&printSummaryLock);
	pr_notice("---------RUNNING RESULT OF JOB[%u]-----------\n",
				job->id);
	pr_notice("Totally issued = %lu\n",
					job->total_issued);
	pr_notice("*********END OF RESULT OF JOB[%u]*********\n",
				job->id);
	spin_unlock(&printSummaryLock);
}


static int qiog_job_thread(void *data)
{
	struct qiog_job *job = data;
	struct qiog_job_cfg *cfg = &job->cfg;
	struct bio *bio;

reset:
	job->total_issued = 0;
	job->current_sec = cfg->start_addr>>9;
	atomic_set(&job->on_the_fly, 0);
	job->issued_without_sched = 0;
	job->ready = 1;
	pr_notice("job %u is ready\n", job->id);
	set_current_state(TASK_INTERRUPTIBLE);
	schedule();

issue_after_sched:
	job->issued_without_sched = 0;
	while (!kthread_should_stop()) {
		//FIXME: accounting works and wait 4 requests to finish
		if (qiog_job_should_stop(job, cfg)) {
			qiog_wait_for_pending_requests(job);
			qiog_print_summary(job);
			goto reset;
		}
		if (atomic_read(&job->on_the_fly) < cfg->cfg_qd) {
			bio = qiog_new_bio(job, cfg);
			if (!bio) {
				schedule();
				goto issue_after_sched;
			}
			submit_bio(bio);
			job->total_issued++;
			atomic_inc(&job->on_the_fly);
			if (++job->issued_without_sched >=
					cfg->cfg_max_issued_without_sched) {
				schedule();
				goto issue_after_sched;
			}
		} else {
			schedule();
			goto issue_after_sched;
		}
	}

	return 0;
}

static void __detach_and_del(struct qiog_job *job)
{
	list_del(&job->list);
	kthread_stop(job->ts);
	kfree(job);
}

/* usage: d @id*/
static void qiog_delete_one(const char *cmd){
	unsigned int del_id;
	struct qiog_job *job, *n;
	char *del_state = "failed";

	sscanf(cmd, "%u", &del_id);

	list_for_each_entry_safe(job, n, &qiog_global_jobs, list) {
		if(job->id == del_id) {
			__detach_and_del(job);
			del_state = "finished";
			break;
		}
	}

	pr_notice("del job[%u] %s\n",
		del_id, del_state);
}

/* usage: da*/
static void qiog_delete_all(void){
	struct qiog_job *job, *n;

	list_for_each_entry_safe(job, n, &qiog_global_jobs, list) {
		__detach_and_del(job);
	}
}

/* usage: r*/
static void qiog_run_all(void){
	struct qiog_job *job, *n;

recheck:
	list_for_each_entry_safe(job, n, &qiog_global_jobs, list) {
		if (!job->ready) {
			schedule();
			goto recheck;
		}
	}
	list_for_each_entry_safe(job, n, &qiog_global_jobs, list) {
		wake_up_process(job->ts);
	}
}


static void qiog_close(void){
	if(qiog_bdev){
		qiog_delete_all();
		blkdev_put(qiog_bdev,mode);
		qiog_bdev = NULL;
		pr_notice("bdev closed\n");
	}
	return;
}


static void qiog_open(char *fname,int maxlen){
	int i;
	for(i=0;i<maxlen;i++)
		if(fname[i]=='\n')
			fname[i] = '\0';
	qiog_close();
	qiog_bdev = blkdev_get_by_path(fname, 
						mode, qiog_holder);
	if (IS_ERR(qiog_bdev)){
		pr_err("open %s failed\n", fname);
		qiog_bdev = NULL;
		return ;
	}
	pr_notice("open finished\n");
	return;
}

static int qiog_new_job(const char *cmd){
	struct qiog_job *newjob;
	int ret = -ENOMEM;

	if (!qiog_bdev) {
		pr_notice("%s failed. NULL bdev\n",
					__func__);
		return -EINVAL;
	}

	newjob = kmalloc(sizeof(*newjob), GFP_KERNEL);
	if (!newjob)
		goto fail_out;
	
	qiog_set_default_cfg(&newjob->cfg);

	//TODO: parse cmd
	qiog_parse_options(&newjob->cfg, cmd);

	newjob->ready = 0;
	newjob->id = atomic_inc_return(&jobID);
	sprintf(newjob->name, "qiog_%u", newjob->id);
	newjob->ts = kthread_create(qiog_job_thread,
								newjob, newjob->name);
	if (IS_ERR(newjob->ts))
		goto fail_out2;

	list_add(&newjob->list, &qiog_global_jobs);
	wake_up_process(newjob->ts);

	return 0;
	kthread_stop(newjob->ts);
fail_out2:
	kfree(newjob);
fail_out:
	return ret;
}

static inline char *QIOG_OP_NAME(enum op_type type)
{
	return op_type_name[type];
}

void printJob(struct qiog_job *job)
{
	if (!job) {
		WARN_ON(1);
		return;
	}
	pr_notice("---job[%s] id[%u]----\n",
				job->name, job->id);
	pr_notice("ready=%d\n", job->ready);
	pr_notice("op=%s\n", QIOG_OP_NAME(job->cfg.op));
	pr_notice("start_addr=0x%lx\n", job->cfg.start_addr);
	pr_notice("end_addr=0x%lx\n", job->cfg.end_addr);
	pr_notice("cfg_max_issued_without_sched=%u\n",
			job->cfg.cfg_max_issued_without_sched);
	pr_notice("cfg_qd=%u\n",
			job->cfg.cfg_qd);
	pr_notice("cfg_pgs_per_rq=%u\n",
			job->cfg.cfg_pgs_per_rq);
	pr_notice("cfg_count_based=%u\n",
			job->cfg.cfg_count_based);
	pr_notice("cfg_time_based=%u\n",
			job->cfg.cfg_time_based);
	pr_notice("cfg_flags=0x%x\n",
			job->cfg.cfg_flags);
	pr_notice("---------------------------\n");
}

static void qiog_print_status(const char *cmd){
	struct qiog_job *job;

	list_for_each_entry(job, &qiog_global_jobs, list) {
		printJob(job);
	}
}

static ssize_t qiog_write_fn(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	char usrCommand[512];
	int i;
	int ret;

	ret = copy_from_user(usrCommand, buffer,count);
	for (i = 0;
			i < 512;
			i++)
		if (usrCommand[i] == '\n')
			usrCommand[i] = '\0';
	//pr_notice("command:[%s]\n",usrCommand);
	switch(usrCommand[0]){
		case 'o':
			qiog_open(usrCommand+2,count-2);
			break;
		case 'c':
			qiog_close();
			break;
		case 'n':
			ret = qiog_new_job(usrCommand+1);
			if (ret)
				pr_err("job creation failed. [%d]\n",
							ret);
			break;
		case 'p':
			qiog_print_status(usrCommand+1);
			break;
		case 'd':
			if (usrCommand[1] == 'a')
				qiog_delete_all();
			else
				qiog_delete_one(usrCommand+1);
			break;
		case 'r':
			qiog_run_all();
			break;
		default:
			pr_notice("command not found\n");
			break;
	}
	return count;
}

static const struct file_operations qiog_proc_fops = {
  .owner = THIS_MODULE,
  .write = qiog_write_fn,
};


static int __init qiog_init(void)
{
	int ret;

	pr_notice("QIOG init\n");
	ret = bioset_init(&qiog_bio_set, BIO_POOL_SIZE,
						offsetof(struct qiog_request, bio),
						BIOSET_NEED_BVECS);
	if (ret)
		goto out1;
	qiog_bdev=NULL;
	atomic_set(&jobID, 0);
	spin_lock_init(&printSummaryLock);
	INIT_LIST_HEAD(&qiog_global_jobs);
	proc_create("qiog", 0, NULL, &qiog_proc_fops);
	return 0;

	bioset_exit(&qiog_bio_set);
out1:
	return -ENOMEM;
}

static void __exit qiog_exit(void)
{
	pr_notice("QIOG exit\n");
	remove_proc_entry("qiog", NULL);
	qiog_close();
	bioset_exit(&qiog_bio_set);
	return;
}

MODULE_AUTHOR("Hongwei Qin <glqhw@hust.edu.cn>");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
module_init(qiog_init);
module_exit(qiog_exit);


