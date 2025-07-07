// 3-Clause BSD license
/*-
*Copyright (C) 2019-2020 Intel Corporation
*SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef _IFC_REGLIB_OSDEP_H_
#define _IFC_REGLIB_OSDEP_H_ 1

#include <stdio.h>
#include <syslog.h>
#include <stdint.h>
#include <linux/types.h>

#include <time.h>
#include <errno.h>
#include <stdint.h>

#include "ifc_status.h"

extern int verbosity;

#define LOG_STDOUT	-1
#define LOG_STDERR	-2

#define MSG(l, ...)	do { if (LOG_##l < 0)			\
				printf(__VA_ARGS__);		\
			     else if (LOG_##l <= verbosity)	\
				syslog(LOG_##l, __VA_ARGS__);	\
			} while (0)

#define TIMEOUT_ADJUSTMENT(timeout, adjust) \
		do { if (timeout > IFC_SHMRW_TIME_DEFAULT) \
			timeout = IFC_SHMRW_TIME_DEFAULT; \
		else if (timeout < adjust) \
			timeout = adjust; \
		else \
			timeout -= adjust; \
		} while (0)

#define usec_delay(n)	do { struct timespec t = { 0, 1000 * (n) }; int _rc; \
				while ((_rc = nanosleep(&t, &t)) == -1 && \
				       errno == EINTR)			\
					if (_rc == -1)			\
						MSG(ERR, "nanosleep %m\n"); \
			} while (0)
#define msec_delay(n)	usec_delay(1000 * (n))

static inline void msleep(int ms)
{
	struct timespec ts = {
		.tv_nsec = ms * 1000000L
	};

	while (nanosleep(&ts, &ts) == -1) {
		if (errno != EINTR)
			return;
	}
}

static inline void udelay(int us)
{
	struct timespec ts = {
		.tv_nsec = us * 1000L
	};

	while (nanosleep(&ts, &ts) == -1) {
		if (errno != EINTR)
			return;
	}
}

#define BIT(x)	(1u << (x))
#define BIT_ULL(x)	(1ull << (x))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define PCI_HAVE_Uxx_TYPES	/* let pci/pci.h not define Uxx types */
#ifndef __define_u64
#define __define_u64
typedef __u64 u64;
#endif
typedef __u32 u32;
typedef __u16 u16;
typedef __u8 u8;

#define __always_unused __attribute__((__unused__))

#define likely(x) __builtin_expect(!(x), 0)
#define unlikely(x) __builtin_expect(!!(x), 0)

#endif /* !_IFC_REGLIB_OSDEP_H_ */
