// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <fnmatch.h>
#include <pthread.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <ifc_libmcmem.h>
#include <ifc_qdma_utils.h>
#include <ifc_mcdma.h>
#include "ifc_mcdma_mem.h"

#define PFN_MASK_SIZE	8

/* Global environment context */
struct ifc_env_ctx env_ctx;

static uint64_t __mem_virt2phys(const void *virtaddr)
{
	int page_size = getpagesize();
	struct timespec mtime1, mtime2;
	const char *fname = "/proc/self/pagemap";
	unsigned long pfn;
	uint64_t page;
	off_t offset;
	int fd, retval;

	retval = ifc_qdma_get_mtime(fname, &mtime1);
	if (retval) {
		printf("Stat failed\n");
		return 0UL;
	}

	fd = ifc_qdma_open("/proc/self/pagemap", O_RDONLY | O_EXCL);
	if (fd < 0) {
		printf("File open failed\n");
		return 0UL;
	}

	pfn = (unsigned long)virtaddr / page_size;
	offset = pfn * sizeof(uint64_t);
	if (lseek(fd, offset, SEEK_SET) == (off_t) -1) {
		printf("Seek error\n");
		close(fd);
		return 0UL;
	};

	retval = ifc_qdma_get_mtime(fname, &mtime2);
	if (retval) {
		printf("Stat failed\n");
		close(fd);
		return 0UL;
	}

	if (mtime1.tv_sec != mtime2.tv_sec
			|| mtime1.tv_nsec != mtime2.tv_nsec) {
		printf("File got modified after open\n");
		close(fd);
		return 0UL;
	}

	retval = read(fd, &page, PFN_MASK_SIZE);
	close(fd);
	if (retval != PFN_MASK_SIZE)
		return 0UL;
	if ((page & 0x7fffffffffffffULL) == 0)
		return 0UL;

	return ((page & 0x7fffffffffffffULL) * page_size)
		+ ((unsigned long)virtaddr & (page_size - 1));
}

static int hugepage_file_fmt_get(char *filename, char *bdf, size_t len, int i)
{
	char const *filename_template = "/dev/hugepages/perfq_example_page";

	snprintf(filename, len, "%s_%s_%d", filename_template, bdf, i);

	return 0;
}

static int clear_huge_pages(char *bdf)
{
	char path[PATH_MAX];
	DIR *dir = NULL;
	struct stat buf;
	struct dirent *dent;
	int dfd, fd, lck_result;
	char filter[PATH_MAX];
	const char file_template[] = "perfq_example_page";

	
	snprintf(filter, PATH_MAX, "%s_%s*", file_template, bdf);
	if (lstat("/dev/hugepages/", &buf) || !S_ISDIR(buf.st_mode)) {
		printf("Unable to open hugepage directory\n");
		return -EACCES;
	}

	/* open directory */
	dir = opendir("/dev/hugepages/");
	if (!dir) {
		printf("Unable to open hugepage directory\n");
		goto error;
	}
	dfd = dirfd(dir);
        if (flock(dfd, LOCK_EX)) {
                closedir(dir);
                return -EACCES;
        }
	dent = readdir(dir);
	if (!dent) {
		printf("Unable to read hugepage directory \n");
		goto error;
	}

	while(dent != NULL){
		/* skip files that don't match the hugepage pattern */
		if (fnmatch(filter, dent->d_name, 0) > 0) {
			dent = readdir(dir);
			continue;
		}

		snprintf(path, sizeof(path), "/dev/hugepages/%s", dent->d_name);
		if (lstat(path, &buf) || S_ISLNK(buf.st_mode)) {
			dent = readdir(dir);
			continue;
		}

		/* try and lock the file */
		fd = openat(dfd, dent->d_name, O_RDONLY);

		/* skip to next file */
		if (fd == -1) {
			dent = readdir(dir);
			continue;
		}

		/* non-blocking lock */
		lck_result = flock(fd, LOCK_EX | LOCK_NB);

		/* if lock succeeds, remove the file */
		if (lck_result != -1)
			unlinkat(dfd, dent->d_name, 0);
		close (fd);
		dent = readdir(dir);
	}

	flock(dfd, LOCK_UN);
	closedir(dir);
	return 0;

error:
	if (dir)
		closedir(dir);
	printf("Error while clearing hugepage dir\n");
	return -EBUSY;
}

static int clear_huge_page(struct ifc_hugepage *hp)
{
	char *path;
	struct stat buf;
	int fd, lck_result;

	path = hp->filename;

	if (lstat(path, &buf) || S_ISLNK(buf.st_mode))
		return -1;

	/* open the file */
	fd = open(path, O_RDONLY);
	if (fd == -1)
		return -1;

	/* non-blocking lock */
	lck_result = flock(fd, LOCK_EX | LOCK_NB);

	/* if lock succeeds, remove the file */
	if (lck_result != -1)
		unlink(path);

	close (fd);
	return 0;
}

#if 0
static int ifc_hugepage_memtest(void *addr, size_t size)
{
	uint32_t words_per_page;
	uint32_t i;
	uint32_t *buf = (uint32_t *)addr;

	words_per_page = size / sizeof(uint32_t);
	printf("%s:Words per page:%u\n", __func__, words_per_page);

	for (i = 0; i < words_per_page; i++)
		buf[i] = 0xfafafafa;

	for (i = 0; i < words_per_page; i++) {
		if (buf[i] != 0xfafafafa) {
			printf("Memory Test Failed\n");
			return -1;
		}
	}

	return 0;
}
#endif

/**
 * ifc_dma_alloc - allocate buffer for I/O request
 *
 * Application should allocate buffer and request structure
 * with this API. Please note, buffer should be DMA-able.
 */
struct ifc_mem_struct *ifc_dma_alloc(void)
{
	uint64_t max_val = 0xFFFFFFFFFFFFFFFF;
	uint64_t *hugepage_bitmap;
	int chunks = env_ctx.chunks;
	struct ifc_mem_struct *r;
	uint64_t phy_addr;
	int page_index;
	uint64_t addr;
	int done = 0;
	int chunk_offset;
	int chunk_num;
	int w, i;

	hugepage_bitmap = env_ctx.hugepage_bitmap;

	if (pthread_mutex_lock(&env_ctx.env_lock) != 0) {
		printf("Failed to acquire memory lock\n");
		return NULL;
	}

	for (w = 0; w < chunks / 64; w++) {
		if (hugepage_bitmap[w] == max_val)
			continue;
		for (i = 0; i < 64; i++) {
			if (hugepage_bitmap[w] & BIT_ULL(i))
				continue;
			hugepage_bitmap[w] |= BIT_ULL(i);
			done = 1;
			break;
		}
		if (done)
			break;
	}

	if (pthread_mutex_unlock(&env_ctx.env_lock) != 0) {
		printf("Failed to release lock\n");
		return NULL;
	}

	if (!done)
		return NULL;

	chunk_num = i + (w * 64);
	page_index = chunk_num / env_ctx.chunks_per_page;
	addr = (uint64_t)(env_ctx.hugepages[page_index].virt_addr);
	chunk_offset = chunk_num % env_ctx.chunks_per_page;
	addr += (chunk_offset * env_ctx.chunk_size);

	/* compute the physical address */
	phy_addr = (uint64_t)(env_ctx.hugepages[page_index].phy_addr);
	phy_addr += (chunk_offset * env_ctx.chunk_size);
	if (phy_addr == 0UL)
		return NULL;

	/* populate the IO request object */
	r = (struct ifc_mem_struct *)malloc(sizeof(struct ifc_mem_struct));
	if (r == NULL) {
		printf("Request object allocation failed\n");
		return NULL;
	}

	r->virt_addr = (void *)addr;
	r->phy_addr = phy_addr;
	r->len = env_ctx.chunk_size;
	r->ctx = chunk_num;
	return r;
}

/**
 * ifc_dma_free - release the passed buffer and add in free pool
 * @r - pointer to the ifc_mem_struct start provided with the buffer
 *
 * Once, DMA transaction is completed, user must release the
 * buffer by this API
 */
void ifc_dma_free(struct ifc_mem_struct *r)
{
	int chunk_num;
	int w, i;

	if (r == NULL)
		return;

	chunk_num = r->ctx;
	w = chunk_num / 64;
	i = chunk_num % 64;

	if (pthread_mutex_lock(&env_ctx.env_lock) != 0) {
		printf("Failed to acquire memory lock\n");
		return;
	}

	env_ctx.hugepage_bitmap[w] &= ~BIT_ULL(i);

	if (pthread_mutex_unlock(&env_ctx.env_lock) != 0) {
		printf("Failed to release lock\n");
		return;
	}

	free(r);
}

static int probe_and_map_hugepage(char *bdf)
{
	uint32_t nr_hugepages = NUM_HUGE_PAGES;
	size_t hugepage_sz = 1ULL << 30;
	struct ifc_hugepage *hp_ctx;
	void *hugepage;
	uint32_t i;
	int errnum;
	int fd;


	/* Allocate contexts for huge pages */
        hp_ctx = (struct ifc_hugepage *)calloc(NUM_HUGE_PAGES, sizeof(struct ifc_hugepage));
        if (hp_ctx == NULL)
               return -ENOMEM;

	/* Clear out old pages */
	if (clear_huge_pages(bdf)){
		free(hp_ctx);
		return -EBUSY;
	}

	for (i = 0U; i < nr_hugepages; i++) {
		hugepage_file_fmt_get(hp_ctx[i].filename, bdf, sizeof(hp_ctx[i].filename), i);

		fd = ifc_qdma_open(hp_ctx[i].filename, O_CREAT | O_RDWR | O_EXCL);
		if (fd < 0) {
			errnum = errno;
			printf("%s open failed: %s\n", hp_ctx[i].filename, strerror(errnum));
			goto error;
		}

		hugepage = mmap(0, hugepage_sz, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_POPULATE, fd, 0);
		if (hugepage == MAP_FAILED) {
			printf("Failed to map huge page\n");
			close(fd);
			goto error;
		}

		if (flock(fd, LOCK_SH | LOCK_NB) == -1) {
			printf("Failed to lock file\n");
			close(fd);
			goto error;
		}

		close(fd);

		hp_ctx[i].virt_addr = hugepage;
		hp_ctx[i].phy_addr = __mem_virt2phys(hugepage);
		hp_ctx[i].size = hugepage_sz;
	}

	env_ctx.hugepages = hp_ctx;
	env_ctx.nr_hugepages = nr_hugepages;

#if 0
	/* Memtest */
	for (i = 0; i < nr_hugepages; i++) {
		ret = ifc_hugepage_memtest(hp_ctx[i].virt_addr, hp_ctx[i].size);
		if (ret == -1)
			goto error;
	}
	printf("Memtest passed\n");
#endif
	return 0;

error:
	for (i = 0; i < nr_hugepages; i++) {
		if (hp_ctx[i].virt_addr)
			munmap(hp_ctx[i].virt_addr, hp_ctx[i].size);
	}
	free(hp_ctx);
	return -EBUSY;
}

/**
 * ifc_allocated_chunks - Get total number of DMA buffer chunks allocated
 * @Returns total allocated chunks
 */
int ifc_allocated_chunks(void)
{
	return env_ctx.chunks;
}

/**
 * ifc_env_init - Allocate & initialize the huge page memory
 * @bdf: BDF of the Device
 * @payload: Size of each memory chunk 
 *
 * @Returns 0, on success. Negative otherwise
 */
int ifc_env_init(char *bdf, size_t payload)
{
	size_t hugepage_sz = 1ULL << 30;
	size_t bsize;

	printf("Welcome to the MCMEM Library\n");

	/* Initilize env lock */
	if (pthread_mutex_init(&env_ctx.env_lock, NULL) != 0) {
		printf("Mutex init failed \n");
		return -ENOLCK;
	}

	/* Map the huge pages */
	if (probe_and_map_hugepage(bdf)) {
		printf("Failed to map hugepages\n");
		return -EBUSY;
	}

	/* create the chunks */
	env_ctx.chunk_size = payload;
	env_ctx.chunks_per_page = hugepage_sz / env_ctx.chunk_size;
	env_ctx.chunks = env_ctx.chunks_per_page * env_ctx.nr_hugepages;
	bsize = env_ctx.chunks / 64;
	env_ctx.hugepage_bitmap = calloc(bsize, sizeof(uint64_t));
	printf("Chunksize:%lu, Chunks per page:%d, #chunks:%d, bsize:%lu\n",
		env_ctx.chunk_size, env_ctx.chunks_per_page, env_ctx.chunks, bsize);

	return 0;
}

/**
 * ifc_env_exit - Free all huge page memory 
 */
void ifc_env_exit(void)
{
	struct ifc_hugepage *hp_ctx;
	int i;

	hp_ctx = env_ctx.hugepages;

	for (i = 0; i < env_ctx.nr_hugepages; i++) {
		if (hp_ctx[i].virt_addr) {
			munmap(hp_ctx[i].virt_addr, hp_ctx[i].size);
			clear_huge_page(&hp_ctx[i]);
		}
	}

	free(env_ctx.hugepages);
	free(env_ctx.hugepage_bitmap);
	pthread_mutex_destroy(&env_ctx.env_lock);
}
