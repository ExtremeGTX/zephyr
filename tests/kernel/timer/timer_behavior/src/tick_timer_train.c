/*
 * Copyright (c) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/time_units.h>
#include <zephyr/tc_util.h>
#include <zephyr/ztest.h>

#define TIMERS 4
#define TEST_SECONDS 10
#define MAX_CALLBACKS (CONFIG_SYS_CLOCK_TICKS_PER_SEC*TEST_SECONDS)/TIMERS

struct timer_wrapper {
	uint32_t callbacks;
	struct k_timer tm;
	uint32_t last_isr;
	int64_t last_scheduled;
	uint32_t max_delta;
};

K_SEM_DEFINE(timers_sem, 0, K_SEM_MAX_LIMIT);

struct timer_wrapper timers[TIMERS];

void tm_fn(struct k_timer *tm)
{
	struct timer_wrapper *tm_wrap =
		CONTAINER_OF(tm, struct timer_wrapper, tm);
	uint32_t now = k_cycle_get_32();

	if (tm_wrap->last_isr != 0) {
		uint32_t delta = now - tm_wrap->last_isr;

		tm_wrap->max_delta = delta > tm_wrap->max_delta ? delta : tm_wrap->max_delta;
	}
	tm_wrap->last_isr = now;
	tm_wrap->callbacks++;
	if (tm_wrap->callbacks >= MAX_CALLBACKS) {
		k_timer_stop(tm);
		k_sem_give(&timers_sem);
	} else {
		int64_t next = tm_wrap->last_scheduled + TIMERS;

		tm_wrap->last_scheduled = next;
		k_timer_start(tm, K_TIMEOUT_ABS_TICKS(next), K_NO_WAIT);
	}
}


/**
 * @brief Test timers can be scheduled 1 tick apart without issues
 *
 * Schedules timers with absolute scheduling with a 1 tick
 * period. Measures the total time elapsed and tries to run
 * some fake busy work while doing so. If the print outs don't show up or
 * the timer train is late to the station, the test fails.
 */
ZTEST(timer_tick_train, test_one_tick_timer_train)
{
	const uint32_t max_time = TEST_SECONDS*1000 + 1000;

	TC_PRINT("Initializing %u Timers, Tick Rate %uHz, Expecting %u callbacks in %u ms\n",
		 TIMERS, CONFIG_SYS_CLOCK_TICKS_PER_SEC, MAX_CALLBACKS, max_time);

	for (int i = 0; i < TIMERS; i++) {
		k_timer_init(&timers[i].tm, tm_fn, NULL);
		timers[i].max_delta = 0;
	}

	TC_PRINT("Starting Timers with Skews\n");
	int64_t tick = k_uptime_ticks();

	for (int i = 0; i < TIMERS; i++) {
		timers[i].last_scheduled = tick + i;
		k_timer_start(&timers[i].tm, K_TIMEOUT_ABS_TICKS(timers[i].last_scheduled),
			      K_NO_WAIT);
	}

#ifdef CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER
	uint64_t start_cycle = k_cycle_get_64();
#else
	uint32_t start_time_ms = k_uptime_get();
#endif

	uint32_t remaining_timers = TIMERS;

	/* Do work in the meantime, proving there's enough time to do other things */
	uint32_t busy_loops = 0;

	while (true) {
		while (k_sem_take(&timers_sem, K_NO_WAIT) == 0) {
			remaining_timers--;

		}
		if (remaining_timers == 0) {
			break;
		}
		TC_PRINT("Faking busy work, remaining timers is %u, timer callbacks %u\n",
			 remaining_timers, timers[0].callbacks);
		busy_loops++;
		k_busy_wait(250000);
	}

#ifdef CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER
	uint64_t end_cycle = k_cycle_get_64();
#else
	uint64_t end_time_ms = k_uptime_get();
#endif

#ifdef CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER
	uint64_t delta_cycles = end_cycle - start_cycle;
	uint32_t delta_time = k_cyc_to_ms_floor32(delta_cycles);
#else
	uint32_t delta_time = end_time_ms - start_time_ms;
#endif

	TC_PRINT("One Tick Timer Train Done, took %u ms, busy loop ran %u times\n",
		 delta_time, busy_loops);

	uint32_t max_delta = 0;

	for (int i = 0; i < TIMERS; i++) {
		TC_PRINT("Timer %d max delta %u cycles or %u us\n", i, timers[i].max_delta,
			 k_cyc_to_us_near32(timers[i].max_delta));
		max_delta = timers[i].max_delta > max_delta ? timers[i].max_delta : max_delta;
	}

	for (int i = 0; i < TIMERS; i++) {
		k_timer_stop(&timers[i].tm);
	}

	const uint32_t expected_busy_loops = TEST_SECONDS*4;
	const uint32_t acceptable_busy_loops = expected_busy_loops - expected_busy_loops/10;

	zassert_true(busy_loops > acceptable_busy_loops,
		     "Expected thread to run while 1 tick timers are firing");

	zassert_true(delta_time < max_time,
		     "Expected timer train to finish in under %u milliseconds, took %u", max_time,
		     delta_time);
}

ZTEST_SUITE(timer_tick_train, NULL, NULL, NULL, NULL, NULL);
