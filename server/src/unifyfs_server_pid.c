/*
 * Copyright (c) 2020, Lawrence Livermore National Security, LLC.
 * Produced at the Lawrence Livermore National Laboratory.
 *
 * Copyright 2020, UT-Battelle, LLC.
 *
 * LLNL-CODE-741539
 * All rights reserved.
 *
 * This is the license for UnifyFS.
 * For details, see https://github.com/LLNL/UnifyFS.
 * Please read https://github.com/LLNL/UnifyFS/LICENSE for full license text.
 */

#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <limits.h>

#include "unifyfs_configurator.h"
#include "unifyfs_global.h"
#include "margo_server.h"
#include "unifyfs_server_rpcs.h"

extern unifyfs_cfg_t server_cfg;

static int* server_pids; // = NULL
static pthread_cond_t server_pid_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t server_pid_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct timespec server_pid_timeout;

static int alloc_server_pids(void)
{
    int ret = 0;
    pthread_mutex_lock(&server_pid_mutex);
    if (NULL == server_pids) {
        server_pids = (int*) calloc(glb_pmi_size, sizeof(int));
        if (NULL == server_pids) {
            LOGERR("failed to allocate memory (%s)", strerror(errno));
            ret = ENOMEM;
        }
    }
    pthread_mutex_unlock(&server_pid_mutex);
    return ret;
}

static int server_pid_invoke_rpc(void)
{
    int ret = 0;
    hg_return_t hret = 0;
    hg_handle_t handle;
    server_pid_in_t in;
    server_pid_out_t out;

    in.rank = glb_pmi_rank;
    in.pid = server_pid;

    hret = margo_create(unifyfsd_rpc_context->svr_mid,
                        glb_servers[0].margo_svr_addr,
                        unifyfsd_rpc_context->rpcs.server_pid_id,
                        &handle);
    if (hret != HG_SUCCESS) {
        LOGERR("failed to create rpc handle (ret=%d)", hret);
        return UNIFYFS_ERROR_MARGO;
    }

    hret = margo_forward(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("failed to forward rpc (ret=%d)", hret);
        return UNIFYFS_ERROR_MARGO;
    }

    hret = margo_get_output(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("failed to get rpc result (ret=%d)", hret);
        return UNIFYFS_ERROR_MARGO;
    }

    ret = out.ret;

    margo_free_output(handle, &out);
    margo_destroy(handle);

    return ret;
}

static void server_pid_rpc(hg_handle_t handle)
{
    int ret = 0;
    hg_return_t hret = 0;
    server_pid_in_t in;
    server_pid_out_t out;

    hret = margo_get_input(handle, &in);
    if (hret != HG_SUCCESS) {
        LOGERR("failed to get input (ret=%d)", hret);
        return;
    }

    ret = alloc_server_pids();
    if (ret) {
        LOGERR("failed to allocate pid array");
        return;
    }
    assert((int)in.rank < glb_pmi_size);
    pthread_mutex_lock(&server_pid_mutex);
    server_pids[in.rank] = (int) in.pid;
    pthread_mutex_unlock(&server_pid_mutex);

    out.ret = 0;
    hret = margo_respond(handle, &out);
    if (hret != HG_SUCCESS) {
        LOGERR("failed to respond rpc (ret=%d)", hret);
        return;
    }

    margo_free_input(handle, &in);
    margo_destroy(handle);

    ret = pthread_cond_signal(&server_pid_cond);
    if (ret) {
        LOGERR("failed to signal condition (%s)", strerror(ret));
    }
}
DEFINE_MARGO_RPC_HANDLER(server_pid_rpc);

static inline int set_pidfile_timeout(void)
{
    int ret = 0;
    long timeout_sec = 0;

    if (server_cfg.server_init_timeout) {
        ret = configurator_int_val(server_cfg.server_init_timeout,
                                   &timeout_sec);
        if (ret) {
            LOGERR("failed to read configuration");
            return ret;
        }
    }

    clock_gettime(CLOCK_REALTIME, &server_pid_timeout);
    server_pid_timeout.tv_sec += timeout_sec;

    return 0;
}

static int create_server_pid_file(void)
{
    int i = 0;
    int ret = 0;
    char filename[UNIFYFS_MAX_FILENAME] = { 0, };
    FILE* fp = NULL;

    if (!server_pids) {
        LOGERR("cannot access the server pids");
        return EINVAL;
    }

    snprintf(filename, sizeof(filename), "%s/%s",
             server_cfg.sharedfs_dir, UNIFYFSD_PID_FILENAME);

    fp = fopen(filename, "w");
    if (!fp) {
        LOGERR("failed to create file %s (%s)", filename, strerror(errno));
        return errno;
    }

    for (i = 0; i < glb_pmi_size; i++) {
        fprintf(fp, "[%d] %d\n", i, server_pids[i]);
    }

    fclose(fp);

    return ret;
}

int unifyfs_publish_server_pids(void)
{
    int ret = UNIFYFS_SUCCESS;

    if (glb_pmi_rank > 0) {
        /* publish my pid to server 0 */
        ret = server_pid_invoke_rpc();
        if (ret) {
            LOGERR("failed to invoke pid rpc (%s)", strerror(ret));
        }
    } else {
        ret = alloc_server_pids();
        if (ret) {
            return ret;
        }

        ret = set_pidfile_timeout();
        if (ret) {
            return ret;
        }

        pthread_mutex_lock(&server_pid_mutex);
        server_pids[0] = server_pid;

        /* keep checking count of reported servers until all have reported
         * or we hit the timeout */
        do {
            int count = 0;
            for (int i = 0; i < glb_pmi_size; i++) {
                if (server_pids[i] > 0) {
                    count++;
                }
            }
            if (count == glb_pmi_size) {
                ret = create_server_pid_file();
                if (UNIFYFS_SUCCESS == ret) {
                    LOGDBG("servers ready to accept client connections");
                }
                break;
            }
            ret = pthread_cond_timedwait(&server_pid_cond,
                                         &server_pid_mutex,
                                         &server_pid_timeout);
            if (ETIMEDOUT == ret) {
                LOGERR("some servers failed to initialize within timeout");
                break;
            } else if (ret) {
                LOGERR("failed to wait on condition (err=%d, %s)",
                       errno, strerror(errno));
                break;
            }
        } while (1);

        free(server_pids);
        server_pids = NULL;

        pthread_mutex_unlock(&server_pid_mutex);
    }

    return ret;
}

