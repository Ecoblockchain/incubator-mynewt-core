/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "os/os.h"

#include <assert.h>
#include <string.h>

TAILQ_HEAD(, os_callout) g_callout_list =
  TAILQ_HEAD_INITIALIZER(g_callout_list);

static int callout_drift;

void
callout_drift_set(int drift)
{
    callout_drift = drift;
}

int
callout_drift_get(void)
{
    return (callout_drift);
}

void
os_callout_init(struct os_callout *c, struct os_eventq *evq, void *ev_arg)
{
    memset(c, 0, sizeof(*c));
    c->c_ev.ev_type = OS_EVENT_T_TIMER;
    c->c_ev.ev_arg = ev_arg;
    c->c_evq = evq;
}

void
os_callout_func_init(struct os_callout_func *cf, struct os_eventq *evq,
  os_callout_func_t timo_func, void *ev_arg)
{
    os_callout_init(&cf->cf_c, evq, ev_arg);
    cf->cf_func = timo_func;
}

void
os_callout_stop(struct os_callout *c)
{
    os_sr_t sr;

    OS_ENTER_CRITICAL(sr);

    if (os_callout_queued(c)) {
        TAILQ_REMOVE(&g_callout_list, c, c_next);
        c->c_next.tqe_prev = NULL;
    }

    if (c->c_evq) {
        os_eventq_remove(c->c_evq, &c->c_ev);
    }

    OS_EXIT_CRITICAL(sr);
}

int
os_callout_reset(struct os_callout *c, int32_t ticks)
{
    struct os_callout *entry;
    os_sr_t sr;
    int rc;

    if (ticks < 0) {
        rc = OS_EINVAL;
        goto err;
    }

    OS_ENTER_CRITICAL(sr);

    os_callout_stop(c);

    if (ticks == 0) {
        ticks = 1;
    }

    c->c_ticks = os_time_get() + ticks;

    entry = NULL;
    TAILQ_FOREACH(entry, &g_callout_list, c_next) {
        if (OS_TIME_TICK_LT(c->c_ticks, entry->c_ticks)) {
            break;
        }
    }

    if (entry) {
        TAILQ_INSERT_BEFORE(entry, c, c_next);
    } else {
        TAILQ_INSERT_TAIL(&g_callout_list, c, c_next);
    }

    OS_EXIT_CRITICAL(sr);

    return (0);
err:
    return (rc);
}

void
os_callout_tick(void)
{
    os_sr_t sr;
    struct os_callout *c;
    uint32_t now;

    now = os_time_get();

    while (1) {
        OS_ENTER_CRITICAL(sr);
        c = TAILQ_FIRST(&g_callout_list);
        if (c) {
            if (OS_TIME_TICK_GEQ(now, c->c_ticks)) {
                TAILQ_REMOVE(&g_callout_list, c, c_next);
                c->c_next.tqe_prev = NULL;
            } else {
                c = NULL;
            }
        }
        OS_EXIT_CRITICAL(sr);

        if (c) {
            os_eventq_put(c->c_evq, &c->c_ev);
        } else {
            break;
        }
    }
}

/*
 * Returns the number of ticks to the first pending callout. If there are no
 * pending callouts then return OS_TIMEOUT_NEVER instead.
 */
os_time_t
os_callout_wakeup_ticks(os_time_t now)
{
    os_time_t rt;
    struct os_callout *c;

    OS_ASSERT_CRITICAL();

    c = TAILQ_FIRST(&g_callout_list);
    if (c != NULL) {
        if (OS_TIME_TICK_GEQ(c->c_ticks, now)) {
            rt = c->c_ticks - now;
        } else {
            rt = 0;     /* callout time is in the past */
        }
    } else {
        rt = OS_TIMEOUT_NEVER;
    }

    return (rt);
}
