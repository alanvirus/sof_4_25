// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <fnmatch.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/sysmacros.h>
#include <sys/file.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>
#include <linux/pci_regs.h>
#include <ifc_reglib_osdep.h>
#include <ifc_mqdma.h>
#include <ifc_env.h>
#include <ifc_env_config.h>
#include <ifc_qdma_utils.h>
#include <ifc_libmqdma.h>
#include <mcdma_ip_params.h>
#ifndef IFC_MCDMA_EXTERNL_DESC
#include <regs/qdma_regs_2_registers.h>
#else
#include <regs/qdma_ext_regs_2_registers.h>
#endif

#ifdef DEBUG
static int ifc_hugepage_memtest(int n);
#endif

const struct ifc_pci_id uio_pci_id = {
	.vend = UIO_PCI_VENDOR_ID,
	.devid = UIO_PCI_DEVICE_ID,
};

void *ifc_dma_malloc(size_t x)
{
	struct alloc_ele *ele = (struct alloc_ele *)env_ctx.last_page;

	if(ele == NULL)
		return NULL;

	/* check for reserved memory limit */
	if (env_ctx.last_page >= env_ctx.ctx_base +  IFC_CTX_MEM)
		return NULL;

	ele->len = x;
	env_ctx.last_page += sizeof(struct alloc_ele) + x;
	return &ele->data[0];
}

void ifc_dma_free(void *p)
{
	/*  We are re-using this memory. So, not releasing
	 */
	(void)p;
}

struct hp_page_alloc {
	void *next;
};

void *ifc_desc_ring_malloc(size_t len)
{
	struct hp_page_alloc *pg;
	void *ptr;

	if (len < 4096)
		len = 4096;

	if (env_ctx.alloc_ctx == NULL) {
		env_ctx.alloc_ctx = malloc(sizeof(*pg));
		if (env_ctx.alloc_ctx == NULL)
			return NULL;
		pg = env_ctx.alloc_ctx;
		pg->next = env_ctx.hugepage;
	}

	pg = env_ctx.alloc_ctx;
	ptr = pg->next;
#ifdef IFC_32BIT_SUPPORT
	pg->next = (void *)(uintptr_t)((uint64_t)(uintptr_t)pg->next + len);
#else
	pg->next = (void *)((uint64_t)pg->next + len);
#endif
	return ptr;
}

void ifc_desc_ring_free(void *ptr)
{
	/*  We are re-using this memory. So, not releasing
	 */
	(void) ptr;
}


/**
 * ifc_qdma_request_malloc - allocate buffer for I/O request
 * @len - size of data buffer for I/O request
 *
 * Application should allocate buffer and request structure
 * with this API or similar variants. Please note, buffer
 * should be DMA-able.
 */
struct ifc_qdma_request *ifc_request_malloc(size_t len)
{
	struct ifc_qdma_io_req_sw *ctx;
	struct ifc_qdma_request *r;
	uint64_t chunk_num;
	uint64_t addr;
	uint64_t i, w, chunks;
	int seg;

	uint64_t length = 0;
	int num_chunks = 0;
	int done = 0;

	if (len > env_ctx.buf_size) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "size more than supported\n");
		return NULL;
	}


	length = ((uint64_t)env_ctx.nr_hugepages * IFC_NUM_CHUNKS_PER_HUGE_PAGE)/64;

	if (pthread_mutex_lock(&env_ctx.env_lock) != 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				"Acquiring mutex got failed \n");
		return NULL;
	}
	for (w = 0; w < length; w++) {
		if (hugepage_req_alloc_mask[w] == -1ULL)
                        continue;
		for (i = 0; i < 64; i++) {
			if (((uint64_t)hugepage_req_alloc_mask[w]) & BIT_ULL(i))
				continue;
			hugepage_req_alloc_mask[w] |= BIT_ULL(i);
			done = 1;
			break;
		}
		if (done)
			break;
	}
	if (pthread_mutex_unlock(&env_ctx.env_lock) != 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				"Releasing mutex got failed \n");
		return NULL;
	}

	if (!done) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR, "no memory\n");
		return NULL;
	}

	i += w * 64;

	/*  get the segment number */
	num_chunks = 0;
	for (seg = 0; seg < env_ctx.seg_tab_len; seg++) {
		chunks = ((env_ctx.seg_tab[seg].length)/env_ctx.buf_size);
		if (i < num_chunks + chunks)
			break;
		num_chunks += chunks;
	}

	/* get the huge page number*/
	chunk_num = i - num_chunks;

	/* evaluate the address */
#ifdef IFC_32BIT_SUPPORT
	addr = (uint64_t)(uintptr_t)env_ctx.seg_tab[seg].virt + (uint64_t)(uintptr_t)(chunk_num * env_ctx.buf_size);
#else
	addr = (uint64_t)env_ctx.seg_tab[seg].virt + (uint64_t)(chunk_num * env_ctx.buf_size);
#endif
	/* populate the IO request object */
	r = (struct ifc_qdma_request *)malloc(sizeof(*r));
	if (r == NULL)
		return NULL;

	/* populate the IO request object */
	ctx = (struct ifc_qdma_io_req_sw *)malloc(sizeof(struct ifc_qdma_io_req_sw));
	if (ctx == NULL) {
		free(r);
		return NULL;
	}
	ctx->index = i;

#ifdef IFC_32BIT_SUPPORT
	r->buf = (void *)(uintptr_t)addr;
	r->phy_addr = mem_virt2phys((void*)(uintptr_t)addr);
#else
	r->buf = (void *)addr;
	r->phy_addr = mem_virt2phys((void*)addr);
#endif
	r->len = len;
	r->ctx = ctx;
	r->flags = 0;

	return r;
}

void ifc_request_free(void *req)
{
	struct ifc_qdma_io_req_sw *ctx;
	struct ifc_qdma_request *r;
	uint32_t i;

	if (req == NULL)
		return;

	r = (struct ifc_qdma_request *) req;
	ctx = (struct ifc_qdma_io_req_sw *)r->ctx;
	if (ctx == NULL)
		return;

	i = ctx->index / 64;

	if (pthread_mutex_lock(&env_ctx.env_lock) != 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				"Acquiring mutex got failed \n");
		return;
	}
	if (i < (int)ARRAY_SIZE(hugepage_req_alloc_mask))
		hugepage_req_alloc_mask[i] &= ~BIT_ULL(ctx->index % 64);
	if (pthread_mutex_unlock(&env_ctx.env_lock) != 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				"Releasing mutex got failed \n");
		return;
	}

	free(r->ctx);
	r->ctx = NULL;
	free(req);
}

static void
pci_parse_one_entry(char *line, size_t len, char *tokens[],
		    int max_toks)
{
	int starttok = 1;
	int toks = 0;
	unsigned int i;

	for (i = 0; i < len; i++) {
		if (line[i] == '\0')
			break;
		if (starttok) {
			starttok = 0;
			tokens[toks++] = &line[i];

			if (toks >= max_toks)
				break;
		}
		if (line[i] == ' ') {
			line[i] = '\0';
			starttok = 1;
		}
	}
}

static int
ifc_pci_sysfs_parse_resource(int uio_id, struct ifc_pci_resource *r)
{
	char path[PATH_MAX];
	char buf[256];
	__u64 start;
	__u64 end;
	FILE *f;
	int i;

	snprintf(path, sizeof(path), "/sys/class/uio/uio%d/device/resource",
		uio_id);

	f = ifc_qdma_fopen(path, "rx");
	if (!f) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Failed to open resource file\n");
		return -1;
	}

	for (i = 0; i < PCI_BAR_MAX; i++) {
		char *strings[3];

		if (fgets(buf, sizeof(buf), f) == NULL)
			continue;

		pci_parse_one_entry(buf, sizeof(buf), strings, 3);
		start = strtoull(strings[0], NULL, 16);
		if (start == ULLONG_MAX) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				"Invalid start offset\n");
			continue;
		}
		end = strtoull(strings[1], NULL, 16);
		if (end == ULLONG_MAX) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				"Invalid end offset\n");
			continue;
		}
		if (start == end)
			continue;
		r[i].len = end - start + 1;
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_INFO,
			     "resource[%d]: 0x%llx - 0x%llx: 0x%llx\n",
			     i, start,
		             end, r[i].len);
	}
	fclose(f);

	return 0;
}

int ifc_mcdma_get_first_bar(struct ifc_pci_device *pdev)
{
	int i;
	for (i = 0; i < PCI_BAR_MAX; i++) {
		if (pdev->r[i].map)
			return i;
	}
	return -1;
}

static int
ifc_pci_map_resource(struct ifc_pci_device *pdev)
{
	struct ifc_pci_resource *r;
	char path[PATH_MAX];
	off_t offset = 0;
	void *addr;
	int uio_id;
	int fd;
	int i, ret = 0;

	if(pdev == NULL){
		printf("PCI device is null\n");
		return -1;
	}

	uio_id = pdev->uio_id;
	r = pdev->r;
	ret = ifc_pci_sysfs_parse_resource(uio_id, &r[0]);
	if(ret != 0)
		printf("Failed to parse sysfs resources\n");


	for (i = 0; i < PCI_BAR_MAX; i++) {
		snprintf(path, sizeof(path),
			 "/sys/class/uio/uio%d/device/resource%d",
			 uio_id, i);

		fd = ifc_qdma_open(path, O_RDWR | O_SYNC | O_EXCL);
		if (fd < 0)
			continue;
		if (!r[i].len) {
			close(fd);
			continue;
		}
		addr = mmap(0, r[i].len, PROT_READ | PROT_WRITE,
			    MAP_SHARED, fd, offset);
		if (addr == MAP_FAILED) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				     "resource[%d]: map failed\n", i);
			close(fd);
			continue;
		}
		r[i].map = addr;
		close(fd);
		pdev->num_bars++;
	}
        pdev->region[IFC_GCSR_REGION].len = 0x20000;
#ifdef IFC_32BIT_SUPPORT
        pdev->region[IFC_GCSR_REGION].map = (void *)(uintptr_t)((uint64_t)(uintptr_t)pdev->r[0].map + 0x200000);
#else
        pdev->region[IFC_GCSR_REGION].map = (void *)((uint64_t)pdev->r[0].map + 0x200000);
#endif
	return 0;
}

void
ifc_pci_unmap_resource(struct ifc_pci_device *pdev)
{
	int i;

	for (i = 0; i < PCI_BAR_MAX; i++) {
		if (!pdev->r[i].len)
			continue;
		munmap(pdev->r[i].map, pdev->r[i].len);
	}
}

static __u64
ifc_pci_sysfs_read(const char *uio_path, const char *file)
{
	__u64 val = 0;
	char *path;
	int fd;
	int n;

	path = (char *)calloc(PATH_MAX, sizeof(char));
	if (!path)
		return 0;

	snprintf(path, PATH_MAX, "%s/%s", uio_path, file);

	fd = ifc_qdma_open(path, O_RDONLY | O_EXCL);
	if (fd < 0)
		goto bail;
	n = read(fd, path, PATH_MAX);
	close(fd);

	if (n >= PATH_MAX)
		goto bail;

	if (n > 0)
		val = strtoull(path, NULL, 16);
bail:
	free(path);
	return val;
}

static int
ifc_pci_sysfs_get_device_id(const char *uio_path)
{
	return ifc_pci_sysfs_read(uio_path, "device");
}

static int
ifc_pci_sysfs_get_vendor_id(const char *uio_path)
{
	return ifc_pci_sysfs_read(uio_path, "vendor");
}

static int
ifc_pci_sysfs_check_driver_name(const char *uio_path)
{
	char *token1, *token2;
	char *path;
	size_t len =0;
	FILE *fd;
	int n =0,ret, rc = -1;

	path = (char *)calloc(PATH_MAX, sizeof(char));
	if (!path)
		return 0;

	ret=snprintf(path, PATH_MAX, "%s/%s", uio_path, "uevent");
	if (ret < 0)
		goto bail;

	fd = ifc_qdma_fopen(path, "rx");
	if (fd == NULL)
		goto bail;

	while ((n = getline(&path, &len, fd)) != -1) {
		token1 = strtok(path, "=");
		if (token1 == NULL)
			break;
		if (token1 && strncmp(token1, "DRIVER", strlen("DRIVER")) == 0) {
			token2 = strtok(NULL, "\n");
			if ((strncmp(token2, "ifc_uio", strlen("ifc_uio"))) == 0) {
				rc = 0;
				break;
			}
		}
	}
	fclose(fd);
bail:
	free(path);
	return rc;
}

static int
ifc_pci_sysfs_update_slot_id(struct ifc_env_ctx *ctx, int i)
{
	char *token1, *token2;
	char uio_path[PATH_MAX];
	__u64 val = 0;
	char *path;
	size_t len=0;
	FILE *fd;
	int n =0,ret = 0;

	path = (char *)malloc(PATH_MAX);
	if (!path)
		return 0;
	ret=snprintf(uio_path, sizeof(uio_path), UIO_DEVICE_PATH_FMT, i);
	if (ret < 0)
		goto bail;
	ret=snprintf(path, PATH_MAX, "%s/%s", uio_path, "uevent");
	if (ret < 0)
		goto bail;

	fd = ifc_qdma_fopen(path, "rx");
	if (fd == NULL)
		goto bail;

	while ((n = getline(&path, &len, fd)) != -1) {
		token1 = strtok(path, "=");
		if (token1 == NULL)
			break;
		if (token1 && strncmp(token1, "PCI_SLOT_NAME", strlen("PCI_SLOT_NAME")) == 0) {
			token2 = strtok(NULL, "\n");
			ret = ifc_qdma_strncpy(ctx->uio_devices[i].pci_slot_name,
					 sizeof(ctx->uio_devices[i].pci_slot_name),
					 token2,
					 4096);
			if(ret)
				printf("Failed to copy device and slot\n");
			break;
		}
	}
	fclose(fd);
bail:
	free(path);
	return val;
}

int
ifc_uio_match_device(const char *uio_path, const struct ifc_pci_id *pci_id)
{
	__u16 vend;
	__u16 dev;
	int ret =0;

	vend = ifc_pci_sysfs_get_vendor_id(uio_path);
	if (vend != pci_id->vend)
		goto no_match;

	dev = ifc_pci_sysfs_get_device_id(uio_path);
	if (dev != pci_id->devid)
		goto no_match;

	ret = ifc_pci_sysfs_check_driver_name(uio_path);
	if (ret != 0) {
		goto no_match;
	}

	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_INFO,
		     "found device @ %s (ven %x, dev %x)\n",
		     uio_path, vend, dev);
	return 1;

no_match:
	return 0;
}

static int
ifc_uio_scan_pci_by_id(struct ifc_env_ctx *ctx, const struct ifc_pci_id *pci_id)
{
	char uio_path[PATH_MAX];
	char device_path[PATH_MAX];
	struct dirent *e;
	int nr_uio = 0;
	int dir_fd;
	DIR *dir;
	int i = 0;

	for (i = 0; i < UIO_MAX_DEVICE; i++) {
		snprintf(uio_path, sizeof(uio_path), UIO_PATH_FMT, i);
		snprintf(device_path, sizeof(device_path), UIO_DEVICE_PATH_FMT, i);

		dir = opendir(uio_path);
		if (!dir)
			continue;

		dir_fd = dirfd(dir);
		if (flock(dir_fd, LOCK_EX)) {
			closedir(dir);
			return -1;
		}

		while ((e = readdir(dir))) {
			int shortprefix_len = sizeof("device") - 1;

			if (strncmp(e->d_name, "device", shortprefix_len) != 0)
				continue;
			if (!ifc_uio_match_device(device_path, pci_id))
				continue;

			ctx->uio_devices[nr_uio++].uio_id = i;
			ifc_pci_sysfs_update_slot_id(ctx, i);
		}
		flock(dir_fd, LOCK_UN);
		closedir(dir);
	}

	return nr_uio;
}

#define PFN_MASK_SIZE	8

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
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			     "stat failed\n");
		return -1;
	}

	fd = ifc_qdma_open("/proc/self/pagemap", O_RDONLY | O_EXCL);
	if (fd < 0)
		return -1;

	pfn = (unsigned long)virtaddr / page_size;
	offset = pfn * sizeof(uint64_t);
	if (lseek(fd, offset, SEEK_SET) == (off_t) -1) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			     "seek error\n");
		close(fd);
		return -1;
	};

	retval = ifc_qdma_get_mtime(fname, &mtime2);
	if (retval) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
			     "stat failed\n");
		close(fd);
		return -1;
	}

	if (mtime1.tv_sec != mtime2.tv_sec
			|| mtime1.tv_nsec != mtime2.tv_nsec) {
		IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR,
				"file got modified after open \n");
		close(fd);
		return -1;
	}

	retval = read(fd, &page, PFN_MASK_SIZE);
	close(fd);
	if (retval != PFN_MASK_SIZE)
		return -1;
	if ((page & 0x7fffffffffffffULL) == 0)
		return -1;

	return ((page & 0x7fffffffffffffULL) * page_size)
		+ ((unsigned long)virtaddr & (page_size - 1));
}

static void hugepage_file_fmt_get(char *filename, size_t len, int i)
{
	char const *filename_template = "/dev/hugepages/perfq_example_page";

	snprintf(filename, len, "%s_%d_%d", filename_template, i, getpid());

	return;
}

static int
ifc_qdma_phy_addr_cmp(const void *a, const void *b)
{
        const struct ifc_hugepage_seg *h1 = a;
        const struct ifc_hugepage_seg *h2 = b;

        if (h1->phys < h2->phys)
                return -1;
        else if (h1->phys > h2->phys)
                return 1;
        else
                return 0;
}

static int
update_segment_table(struct ifc_hugepage_seg *hugepages, int page_start, int page_end)
{
	int i = env_ctx.seg_tab_len;
	if (unlikely(i >= NUM_SEGMENTS))
		return -1;
	env_ctx.seg_tab[i].seg_id =  env_ctx.seg_tab_len;
	env_ctx.seg_tab[i].phys =  hugepages[page_start].phys;
	env_ctx.seg_tab[i].virt =  hugepages[page_start].virt;
	env_ctx.seg_tab[i].length =  (page_end - page_start) * hugepages[page_start].size;
	env_ctx.seg_tab[i].refcnt =  0;
	env_ctx.seg_tab_len++;
	return 0;
}

static int process_hugepages(struct ifc_hugepage_seg *tmp_hp,
			     int num)
{
	int i = 0, new_seg = 0;;
	int start_page = 0;
	struct ifc_hugepage_seg *prev, *cur;
	if (tmp_hp == NULL)
		return -1;
	prev = NULL;
	for (i = 0; i < num; i++) {
		new_seg = 0;
		cur = &tmp_hp[i];
		if (i == 0)
			new_seg = 1;
		else if ((cur->phys - prev->phys) != cur->size)
                        new_seg = 1;
#ifdef IFC_32BIT_SUPPORT
		else if (((uint64_t)(uintptr_t)((uint64_t)(uintptr_t)cur->virt - (uint64_t)(uintptr_t)prev->virt)) !=
			 cur->size)
#else
		else if (((uint64_t)((uint64_t)cur->virt - (uint64_t)prev->virt)) !=
			 cur->size)
#endif
                        new_seg = 1;
		if (new_seg) {
			if (i != 0)
				update_segment_table(tmp_hp, start_page, i);
			start_page = i;
		}
		prev = cur;
	}
	update_segment_table(tmp_hp, start_page, i);
	return 0;
}


static int
clear_huge_pages(void)
{
	DIR *dir = NULL;
	struct stat buf;
	struct dirent *dent;
	int dfd, fd, lck_result;
    char const_filter[] = "perfq_example_page*"; /* matches hugepage files */
	char path[4096];

	if (lstat("/dev/hugepages/", &buf) || !S_ISDIR(buf.st_mode)) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Unable to open hugepage directory\n");
		return -1;
	}

	/* open directory */
	dir = opendir("/dev/hugepages/");
	if (!dir) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Unable to open hugepage directory\n");
		goto error;
	}

	dfd = dirfd(dir);
    if (flock(dfd, LOCK_EX)) {
            closedir(dir);
            return -1;
    }

	dent = readdir(dir);
	if (!dent) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "Unable to read hugepage directory \n");
		goto error;
	}

	while(dent != NULL){
		/* skip files that don't match the hugepage pattern */
		if (fnmatch(const_filter, dent->d_name, 0) > 0) {
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
		if(1)if (fd == -1) {
			dent = readdir(dir);
			continue;
		}
		/* non-blocking lock */
		lck_result = flock(fd, LOCK_EX | LOCK_NB);

		/* if lock succeeds, remove the file */
		if(1)if (lck_result != -1)
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
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
		     "Error while clearing hugepage dir\n");

	return -1;
}

/**
 * ifc_allocated_chunks - Get total number of DMA buffer chunks allocated
 * @Returns total allocated chunks
 */
int ifc_allocated_chunks(void)
{
	int chunks;

	chunks = IFC_NUM_CHUNKS_PER_HUGE_PAGE * env_ctx.nr_hugepages;
	chunks -= (IFC_CHUNKS_FOR_DESC + IFC_CHUNKS_FOR_CTX);
	return chunks;
}

int probe_and_map_hugepage(void)
{
#ifdef IFC_32BIT_SUPPORT
	uint64_t hugepage_sz = 1ULL << 21;
#else
	uint64_t hugepage_sz = 1ULL << 30;
#endif
	struct ifc_hugepage_seg *tmp_hp;
	uint32_t nr_hugepages = NUM_HUGE_PAGES;
	char filename[128];
	uint64_t iova= IFC_QDMA_IOMMU_BASE_IOVA;
	void *hugepage;
	int b, w;
	uint32_t i ,tot_chunk;
	int errnum;
#ifdef DEBUG
	int ret;
#endif
	int fd;
	
	if (clear_huge_pages())
		return -1;

        tmp_hp = (struct ifc_hugepage_seg*)malloc(NUM_HUGE_PAGES * sizeof(struct ifc_hugepage_seg));
        if (tmp_hp == NULL)
               return -1;

	memset(tmp_hp, 0, nr_hugepages * sizeof(struct ifc_hugepage_seg));

	for (i = 0; i < nr_hugepages; i++) {
		hugepage_file_fmt_get(filename, sizeof(filename), i);

		fd = ifc_qdma_open(filename, O_CREAT | O_RDWR | O_EXCL);
		if (fd < 0) {
			errnum = errno;
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				     "%s open failed: %s\n", filename, strerror( errnum ));
			free(tmp_hp);
			return -1;
		}

		hugepage = mmap(0, hugepage_sz, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_POPULATE, fd, 0);
		if (hugepage == MAP_FAILED) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				     "mmap faled\n");
			close(fd);
			free(tmp_hp);
			return -1;
		}

		if (flock(fd, LOCK_SH | LOCK_NB) == -1) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				     "file lock failed\n");
			close(fd);
			free(tmp_hp);
			return -1;
		}

		close(fd);

		tmp_hp[i].virt = hugepage;
		tmp_hp[i].phys = __mem_virt2phys(hugepage);
		tmp_hp[i].size = hugepage_sz;
	}

        qsort(&tmp_hp[0], nr_hugepages,
             sizeof(struct ifc_hugepage_seg), ifc_qdma_phy_addr_cmp);

	if (process_hugepages(tmp_hp, nr_hugepages))  {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "failed while processing huge pages");
	}

	for (i = 0; i < nr_hugepages; i++) {
		env_ctx.hp[i].virt = tmp_hp[i].virt;
		env_ctx.hp[i].phys = tmp_hp[i].phys;
		env_ctx.hp[i].size = tmp_hp[i].size;
		env_ctx.hp[i].iova = iova;
		iova += tmp_hp[i].size;
	}
	env_ctx.nr_hugepages = nr_hugepages;
	env_ctx.hugepage_sz = hugepage_sz;
	env_ctx.hugepage = env_ctx.hp[0].virt;

	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
			"Hugepage table\n");
	for (i = 0; i < nr_hugepages; i++) {
#ifdef IFC_32BIT_SUPPORT
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
				"i:%u virt:0x%llx phys:0x%llx size:0x%llx\n", i,
				(uint64_t)(uintptr_t)env_ctx.hp[i].virt, env_ctx.hp[i].phys,
				env_ctx.hp[i].size);
#else
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_DEBUG,
				"i:%u virt:0x%lx phys:0x%lx size:0x%lx\n", i,
				(uint64_t)env_ctx.hp[i].virt, env_ctx.hp[i].phys,
				env_ctx.hp[i].size);
#endif
	}
#ifdef DEBUG
	for (i = 0; i < nr_hugepages; i++) {
		ret = ifc_hugepage_memtest(i);
		if (ret == -1)
			return -1;
	}
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_INFO, "memtest passed\n");
#endif
	tot_chunk =IFC_CHUNKS_FOR_DESC + IFC_CHUNKS_FOR_CTX;
#ifdef IFC_32BIT_SUPPORT
	if(tot_chunk >16777216)
		tot_chunk =16777216;
//printf(" IFC_CHUNKS_FOR_DESC + IFC_CHUNKS_FOR_CTX %d \n", IFC_CHUNKS_FOR_DESC + IFC_CHUNKS_FOR_CTX);
#endif
	for (i = 0; i < tot_chunk; i++) {
		w = i / 64;
		b = i % 64;
		hugepage_req_alloc_mask[w] |= BIT_ULL(b);
	}

	free(tmp_hp);
	return 0;
}

uint64_t mem_virt2phys(const void *virtaddr)
{
#ifdef IFC_32BIT_SUPPORT
	uint64_t virt = (uint64_t)(uintptr_t)virtaddr;
#else
	uint64_t virt = (uint64_t)virtaddr;
#endif
	struct ifc_hugepage_seg *hp;
	int i;

	for (i = 0, hp = &env_ctx.hp[0]; i < env_ctx.nr_hugepages; i++, hp++) {
//	printf( " hpvirt %llx hpsize %llx virt %llx i %d \n",(uint64_t)(uintptr_t)hp->virt,hp->size,virt,i);
#ifdef IFC_32BIT_SUPPORT
		if ((virt >= (uint64_t)(uintptr_t)hp->virt) &&
		    (virt < ((uint64_t)(uintptr_t)hp->virt + hp->size))){
#if defined(UIO_SUPPORT) || defined (NO_IOMMU_MODE)
			return virt - (uint64_t)(uintptr_t) hp->virt + hp->phys;}
#else
			return virt - (uint64_t)(uintptr_t) hp->virt + hp->iova;
#endif
#else
		if ((virt >= (uint64_t)hp->virt) &&
		    (virt < ((uint64_t)hp->virt + hp->size)))
#if defined(UIO_SUPPORT) || defined (NO_IOMMU_MODE)
			return virt - (uint64_t) hp->virt + hp->phys;
#else
			return virt - (uint64_t) hp->virt + hp->iova;
#endif
#endif
	}
#ifdef IFC_32BIT_SUPPORT
	IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR, "invalid address   %llx  \n",  virt);
#else
	IFC_QDMA_LOG(IFC_QDMA_TXRX, IFC_QDMA_ERROR, "invalid address %lx\n",
		     virt);
#endif
	return 0;
}

int unlink_hugepage(int i)
{
	char filename[128];

	hugepage_file_fmt_get(filename, sizeof(filename), i);

	return unlink(filename);
}

#ifdef DEBUG
static int ifc_hugepage_memtest(int n)
{
	uint32_t words_per_page;
	uint64_t *addr, i;

	addr = (uint64_t *)env_ctx.hp[n].virt;
	words_per_page = env_ctx.hp[n].size / sizeof(uint64_t);
	addr = env_ctx.hugepage;
	for (i = 0; i < words_per_page; i++)
		addr[i] = 0xfafafa;

	for (i = 0; i < words_per_page; i++) {
		if (addr[i] != 0xfafafa) {
			IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				     "memtest failed\n");
			return -1;
		}
	}

	return 0;
}
#endif

int ifc_env_init(__attribute__((unused)) const char *bdf)
{
	uint32_t nr_scan = 0;
	uint32_t i;
	int retval = 0;

	/* Initilize env lock */
	if (pthread_mutex_init(&env_ctx.env_lock, NULL) != 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
				"Mutex init failed \n");
		retval = -1;
		return retval;
	}

	if (probe_and_map_hugepage()) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "can't map hugepage\n");
		retval = -1;
		return retval;
	}
	env_ctx.ctx_base = env_ctx.hugepage + IFC_DESC_MEM;
	env_ctx.last_page = env_ctx.hugepage + IFC_DESC_MEM;
#ifdef IFC_32BIT_SUPPORT
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_INFO,
		"allocated hugepage of size %lluMB and mapped to %p va (%llx pa)\n",
		env_ctx.hugepage_sz >> 21,
		env_ctx.hugepage,
		mem_virt2phys(env_ctx.hugepage));
#else
	IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_INFO,
		"allocated hugepage of size %luG and mapped to %p va (%lx pa)\n",
		env_ctx.hugepage_sz >> 21,
		env_ctx.hugepage,
		mem_virt2phys(env_ctx.hugepage));
#endif

	nr_scan = ifc_uio_scan_pci_by_id(&env_ctx, &uio_pci_id);
	if (nr_scan == 0) {
		IFC_QDMA_LOG(IFC_QDMA_INIT, IFC_QDMA_ERROR,
			     "uio/pci dev not found\n");
		return 0;
	}

	env_ctx.nr_device = nr_scan;
	for (i = 0; i < nr_scan; i++){
		retval = ifc_pci_map_resource(&env_ctx.uio_devices[i]);
		if(retval < 0)
			return retval;
	}
	return 0;
}

inline int get_num_ports()
{
        return env_ctx.nr_device;
}

