/*
 * Copyright © 2016 Pekka Paalanen <pq@iki.fi>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef ORING_CLOCK_H
#define ORING_CLOCK_H

#include <stdbool.h>
#include <time.h>
#include <stdint.h>

struct oring_clock {
	clockid_t clock_id;
	struct timespec base;
	uint64_t offset;
	bool frozen;
};

void
oring_clock_init(struct oring_clock *oc, clockid_t clock_id,
		 const struct timespec *epoch);

void
oring_clock_init_now(struct oring_clock *oc, clockid_t clock_id);

void
oring_clock_freeze(struct oring_clock *oc, const struct timespec *now);

void
oring_clock_thaw(struct oring_clock *oc, const struct timespec *now);

uint64_t
oring_clock_get_nsec(const struct oring_clock *oc, const struct timespec *ts);

uint64_t
oring_clock_get_nsec_now(const struct oring_clock *oc);

const char *
clock_get_name(clockid_t clock_id);

#endif /* ORING_CLOCK_H */
