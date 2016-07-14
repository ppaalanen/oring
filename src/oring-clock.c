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

#include "config.h"

#include <stdlib.h>
#include <assert.h>

#include "oring-clock.h"
#include "timespec-util.h"

static int64_t
oring_clock_delta_nsec(const struct oring_clock *oc,
		       const struct timespec *ts)
{
	struct timespec delta;

	timespec_sub(&delta, ts, &oc->base);

	return timespec_to_nsec(&delta);
}

/** Initialize a clock
 *
 * \param oc The clock.
 * \param clock_id The clock id, see clock_gettime().
 * \param epoch Epoch for this clock.
 *
 * You cannot represent time values before the given epoch. The clock value
 * at epoch is zero nanoseconds. The clock starts as thawed.
 */
void
oring_clock_init(struct oring_clock *oc, clockid_t clock_id,
		 const struct timespec *epoch)
{
	oc->clock_id = clock_id;
	oc->base = *epoch;
	oc->offset = 0;
	oc->frozen = false;
}

/** Initialize a clock with current time as the epoch
 *
 * \param oc The clock.
 * \param clock_id The clock id, see clock_gettime().
 *
 * Otherwise the same as oring_clock_init(), except the epoch is chosen by
 * calling clock_gettime() with the given clock_id.
 */
void
oring_clock_init_now(struct oring_clock *oc, clockid_t clock_id)
{
	struct timespec now;
	int ret;

	ret = clock_gettime(clock_id, &now);
	assert(ret == 0);

	oring_clock_init(oc, clock_id, &now);
}

/** Freeze the clock
 *
 * \param oc The clock.
 * \param now The time instant to freeze it at.
 *
 * The clock is frozen to the given time instant. Reading the clock with
 * any time instant at or after freeze point will return the clock value
 * at the freeze point.
 *
 * The clock must not be already frozen. The given time instant must be at or
 * after the time instant given in the last oring_clock_init() or
 * oring_clock_thaw() calls.
 */
void
oring_clock_freeze(struct oring_clock *oc, const struct timespec *now)
{
	int64_t delta;
	uint64_t nsec;

	assert(oc->frozen == false);

	delta = oring_clock_delta_nsec(oc, now);
	assert(delta >= 0);

	nsec = oc->offset + delta;
	assert(nsec >= oc->offset);

	oc->base = *now;
	oc->offset = nsec;
	oc->frozen = true;
}

/** Thaw the clock
 *
 * \param oc The clock.
 * \param now The time instant to thaw it at.
 *
 * The clock thaws at the given time instant and accumulates time again.
 * The clock value continues counting from the point it was frozen at.
 *
 * The clock must be frozen. The given time instant must be at or after
 * the time instant given in the last call to oring_clock_freeze().
 */
void
oring_clock_thaw(struct oring_clock *oc, const struct timespec *now)
{
	int64_t delta;

	assert(oc->frozen == true);

	delta = oring_clock_delta_nsec(oc, now);
	assert(delta >= 0);

	oc->base = *now;
	oc->frozen = false;
}

/** Get clock value in nanoseconds
 *
 * \param oc The clock.
 * \param ts The time instant to convert to clock value.
 * \return The clock value at the given time instant.
 *
 * Returns the number of nanoseconds from epoch to the given time instant,
 * excluding the time the clock was frozen.
 *
 * It is not possible to query time instants before the epoch.
 *
 * If the clock is frozen and the time instant is after the freeze point,
 * the returned value is the value at the freeze point. If the clock is
 * frozen and the time instant is before the freeze point, the clock value
 * is extrapolated backward from the freeze point, not taking into account
 * earlier freezes.
 */
uint64_t
oring_clock_get_nsec(const struct oring_clock *oc, const struct timespec *ts)
{
	int64_t delta;
	uint64_t nsec;

	delta = oring_clock_delta_nsec(oc, ts);
	assert(delta >= 0 || (uint64_t)(-delta) <= oc->offset);
	assert(delta < 0 || UINT64_MAX - (uint64_t)delta >= oc->offset);

	nsec = oc->offset + delta;

	if (oc->frozen && nsec > oc->offset)
		return oc->offset;

	return nsec;
}

/** Get current clock value in nanoseconds
 *
 * \param oc The clock.
 * \return The clock value right now.
 *
 * Otherwise the same as oring_clock_get_nsec(), except the time instant is
 * chosen by calling clock_gettime() with the clock_id used to initialize
 * the clock.
 */
uint64_t
oring_clock_get_nsec_now(const struct oring_clock *oc)
{
	struct timespec now;
	int ret;

	ret = clock_gettime(oc->clock_id, &now);
	assert(ret == 0);

	return oring_clock_get_nsec(oc, &now);
}
