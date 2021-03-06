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

#ifndef _UNIFYFS_CLIENT_READ_H
#define _UNIFYFS_CLIENT_READ_H

#include "unifyfs-internal.h"

// headers for client-server RPCs
#include "unifyfs_client_rpcs.h"
#include "margo_client.h"

/* Structure used by the client to track completion state for a
 * set of read requests submitted by a single client syscall.
 * The server will return data for each read request in separate
 * rpc calls. */
typedef struct {
    unsigned int id;         /* unique id for this set of read requests */
    unsigned int n_reads;    /* number of read requests */
    unsigned int n_complete; /* number of completed requests */
    unsigned int n_error;    /* number of requests that encountered errors */
    read_req_t* reqs;        /* array of read requests */

    /* the following is for synchronizing access/updates to above state */
    ABT_mutex sync;

    /* pthread mutex and condition used to signal the client thread that
     * issued the mread that the full set of requests has been processed */
    pthread_mutex_t mutex;
    pthread_cond_t completed;
} client_mread_status;

/* an arraylist to maintain the active mread requests for the client */
extern arraylist_t* active_mreads;

/* Create a new mread request containing the n_reads requests provided
 * in read_reqs array */
client_mread_status* client_create_mread_request(int n_reads,
                                                 read_req_t* read_reqs);

/* Remove the mread status */
int client_remove_mread_request(client_mread_status* mread);

/* Retrieve the mread request corresponding to the given request_id */
client_mread_status* client_get_mread_status(unsigned int request_id);

/* Update the mread status for the request at the given req_index.
 * If the request is now complete, update the request's completion state
 * (i.e., errcode and nread) */
int client_update_mread_request(client_mread_status* mread,
                                unsigned int req_index,
                                int req_complete,
                                int req_error);


/* For the given read request and extent (file offset, length), calculate
 * the coverage including offsets from the beginning of the request and extent
 * and the coverage length. Return a pointer to the segment within the request
 * buffer where read data should be placed. */
char* get_extent_coverage(read_req_t* req,
                          size_t extent_file_offset,
                          size_t extent_length,
                          size_t* out_req_offset,
                          size_t* out_ext_offset,
                          size_t* out_length);

void update_read_req_coverage(read_req_t* req,
                              size_t extent_byte_offset,
                              size_t extent_length);

/* process a set of client read requests */
int process_gfid_reads(read_req_t* in_reqs, int in_count);

#endif // UNIFYFS_CLIENT_READ_H
