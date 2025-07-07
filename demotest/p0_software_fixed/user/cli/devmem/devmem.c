// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include<time.h>
#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/sysmacros.h>
#include <sys/file.h>
#include <linux/pci_regs.h>
#include <ifc_reglib_osdep.h>
#include <ifc_libmqdma.h>
#include <ifc_qdma_utils.h>
#include "./devmem.h"

#define UIO_PATH_FMT    "/sys/class/uio/uio%u"
#define UIO_MAX_DEVICE          256

#define UIO_PCI_VENDOR_ID       0x1172
#define UIO_PCI_DEVICE_ID       0x0000
#define UIO_DEVICE_PATH_FMT	"/sys/class/uio/uio%u/device"

static const struct ifc_pci_id uio_pci_id = {
        .vend = UIO_PCI_VENDOR_ID,
        .devid = UIO_PCI_DEVICE_ID,
};

void show_help(void)
{

	printf("Usage:\n");
	printf("Read:  ./devmem <bdf> <bar> <addr>\n");
	printf("Write: ./devmem <bdf> <bar> <addr> <val>\n");
	printf("<addr>,<val> should be in hex\n");
	return;
}

static int
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
        return toks;
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
        f = fopen(path, "r");
        if (!f) {
                printf("resource file failed\n");
                return -1;
        }

        for (i = 0; i < PCI_BAR_MAX; i++) {
                char *strings[3];

                if (fgets(buf, sizeof(buf), f) == NULL)
                        continue;

                pci_parse_one_entry(buf, sizeof(buf), strings, 3);
                start = strtoull(strings[0], NULL, 16);
                end = strtoull(strings[1], NULL, 16);
                if (start == end)
                        continue;
                r[i].len = end - start + 1;
        }
        fclose(f);

        return 0;
}

static __u64
ifc_pci_sysfs_read(const char *uio_path, const char *file)
{
        __u64 val = 0;
        char *path;
        int fd;
        int n;

        path = malloc(PATH_MAX);
        if (!path)
                return 0;

        snprintf(path, PATH_MAX, "%s/device/%s", uio_path, file);
        fd = open(path, O_RDONLY);
        if (fd < 0)
                goto bail;
        n = read(fd, path, PATH_MAX);
        close(fd);
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
ifc_pci_sysfs_check_slot_id(char *bdf, int i)
{
	char *token1, *token2;
	char uio_path[PATH_MAX];
	char *path;
	size_t len;
	FILE *fd;
	int n,ret;

	path = (char *)malloc(PATH_MAX);
	if (!path)
		return 0;
	ret=snprintf(uio_path, sizeof(uio_path), UIO_DEVICE_PATH_FMT, i);
	if (ret < 0)
		return 0;
	ret=snprintf(path, PATH_MAX, "%s/%s", uio_path, "uevent");
	if (ret < 0)
		return 0;

	fd = ifc_qdma_fopen(path, "rx");
	if (fd == NULL)
		goto bail;

	while ((n = getline(&path, &len, fd)) != -1) {
		token1 = strtok(path, "=");
		if (token1 && strncmp(token1, "PCI_SLOT_NAME", strlen("PCI_SLOT_NAME")) == 0) {
			token2 = strtok(NULL, "\n");
			if (strncmp(bdf, token2, 256) == 0)
				return 0;;
		}
	}
	fclose(fd);
bail:
	free(path);
	return -1;
}

static int
ifc_uio_match_device(const char *uio_path, const struct ifc_pci_id *pci_id)
{
        __u16 vend;
        __u16 dev;

        vend = ifc_pci_sysfs_get_vendor_id(uio_path);
        if (vend != pci_id->vend)
                goto no_match;

        dev = ifc_pci_sysfs_get_device_id(uio_path);
        if (dev != pci_id->devid)
                goto no_match;

        return 1;

no_match:
        return 0;
}

static int
ifc_uio_scan_pci_by_id(struct ifc_pci_device *pdev, const struct ifc_pci_id *pci_id, char *bdf)
{
	char uio_path[PATH_MAX];
	struct dirent *e;
	int nr_uio = 0;
	int done = 0;
	DIR *dir;
	int i;

	for (i = 0; i < UIO_MAX_DEVICE; i++) {
		snprintf(uio_path, sizeof(uio_path), UIO_PATH_FMT, i);
		dir = opendir(uio_path);
		if (!dir)
			continue;
		while ((e = readdir(dir))) {
			int shortprefix_len = sizeof("device") - 1;

			if (strncmp(e->d_name, "device", shortprefix_len) != 0)
				continue;
			if (!ifc_uio_match_device(uio_path, pci_id))
				continue;
			if (ifc_pci_sysfs_check_slot_id(bdf, i))
				continue;

			pdev->uio_id = i;
			nr_uio++;
			done = 1;
			break;
		}
		closedir(dir);
		if (done == 1)
			break;
	}

	return nr_uio;
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
	int i;

	uio_id = pdev->uio_id;
	r = pdev->r;
	ifc_pci_sysfs_parse_resource(uio_id, &r[0]);

	for (i = 0; i < PCI_BAR_MAX; i++) {
		snprintf(path, sizeof(path),
			 "/sys/class/uio/uio%d/device/resource%d",
			 uio_id, i);
		fd = open(path, O_RDWR | O_SYNC);
		if (fd < 0)
			continue;
		if (!r[i].len)
			continue;
		addr = mmap(0, r[i].len, PROT_READ | PROT_WRITE,
			    MAP_SHARED, fd, offset);
		if (addr == MAP_FAILED) {
			printf("resource[%d]: map failed\n", i);
			continue;
		}
		r[i].map = addr;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	struct ifc_pci_device pdev;
	char bdf[256];
	int nr_scan;
	uint64_t addr;

	int operation = 0;
	int bar = -1;
	uint32_t val = 0;

	if (argc <= 3){
		show_help();
		goto out;
	}

	ifc_qdma_strncpy(bdf, sizeof(bdf), argv[1], 20);
	bar = atoi(argv[2]);
	#ifdef IFC_32BIT_SUPPORT
	sscanf(argv[3], "0x%llx", &addr);
	#else
	sscanf(argv[3], "0x%lx", &addr);
	#endif
	if (argc >= 5) {
		operation = 1;
		sscanf(argv[4], "0x%x", &val);
	}

	nr_scan = ifc_uio_scan_pci_by_id(&pdev, &uio_pci_id, bdf);
	if (nr_scan == 0) {
		printf("uio/pci dev not found\n");
		return 0;
	}

	ifc_pci_map_resource(&pdev);

	if ((bar < 0) || (bar > 5)) {
		printf("Invalid BAR\n");
		show_help();
		goto out;
	}

	if (addr > pdev.r[bar].len) {
		printf("Address out of range\n");
		goto out;
	}

	if (operation == 0) {
		if (bar == 2){
#ifdef IFC_32BIT_SUPPORT
			printf ("0x%0llx\n",
				*((uint64_t *)(pdev.r[bar].map + addr)));
#else
			printf ("0x%0lx\n",
				*((uint64_t *)(pdev.r[bar].map + addr)));
#endif 
 }
		else
			printf ("0x%0x\n",
				*((uint32_t *)(pdev.r[bar].map + addr)));
	} else {
		if (addr > pdev.r[bar].len) {
			printf("Address out of range\n");
			goto out;
		}
		*((int *)(pdev.r[bar].map + addr)) = val;
	}

	return 0;
out:

	return 0;

}
