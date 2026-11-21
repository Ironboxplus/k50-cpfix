/* SPDX-License-Identifier: GPL-2.0 */
/* Minimal assertion counter shared by the host tests. */
#ifndef CPFIX_TEST_CHECK_H
#define CPFIX_TEST_CHECK_H

#include <stdio.h>

static int passed, failed;

#define EQ(got, want, ...)                                                   \
	do {                                                                 \
		long long g_ = (got), w_ = (want);                           \
		if (g_ == w_) {                                              \
			passed++;                                            \
		} else {                                                     \
			failed++;                                            \
			printf("FAIL %s:%d: %s = %lld, want %lld  (", __FILE__, \
			       __LINE__, #got, g_, w_);                      \
			printf(__VA_ARGS__);                                 \
			printf(")\n");                                       \
		}                                                            \
	} while (0)

/* got <= limit */
#define LE(got, limit, ...)                                                  \
	do {                                                                 \
		long long g_ = (got), l_ = (limit);                          \
		if (g_ <= l_) {                                              \
			passed++;                                            \
		} else {                                                     \
			failed++;                                            \
			printf("FAIL %s:%d: %s = %lld, limit %lld  (", __FILE__, \
			       __LINE__, #got, g_, l_);                      \
			printf(__VA_ARGS__);                                 \
			printf(")\n");                                       \
		}                                                            \
	} while (0)

static inline int check_report(void)
{
	printf("passed %d failed %d total %d\n", passed, failed, passed + failed);
	return failed ? 1 : 0;
}

#endif
