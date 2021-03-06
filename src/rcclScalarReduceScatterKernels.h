/*
Copyright (c) 2017 - Present Advanced Micro Devices, Inc.
All rights reserved.
*/

#pragma once

/**
 * @file rcclScalarReduceScatterKernels.h
 * @brief Kernels to implement allreduce operation
 *
 * This file contains implementation of kernels used by rcclReduceScatter
 *
 */

//! @brief Definition of RcclKernelScalarReduceScatter
//! Gather data from all gpus, does reduction on them and store to current gpu
//! destination buffer
template <typename DataType_t, rcclRedOp_t Op>
__global__ void RcclKernelScalarReduceScatter(RingNode_t* pcurr_track,
                                          const void* send_buff, void* recv_buff,
                                          int count, int offset) {
    int tx = threadIdx.x;
    int bx = blockIdx.x;
    int tid = tx + bx * knum_vectors_per_workgroup;

    //! Get pointers to current gpu source and destination buffers
    const DataType_t* curr_src_buff = reinterpret_cast<const DataType_t*>(send_buff);
    DataType_t* curr_dst_buff = (DataType_t*)recv_buff;
    if (send_buff == recv_buff)
        curr_dst_buff += offset;

    //! Use only count number of workitems to do the reduction operation
    if (tid < count) {
        //! Get peer gpu tracker
        RingNode_t* pnext_track = pcurr_track->next_gpu;

        //! Find absolute index the gpu operates on
        int index = tid + offset;

        DataType_t result = curr_src_buff[index];

        //! Iterate over all the gpus, gather data from them and do reduction
        //! operation on them
        while (pnext_track != pcurr_track) {
            DataType_t* next_src_buff =
                reinterpret_cast<DataType_t*>(pnext_track->src_buffer);

            if (Op == rcclSum) result = result + next_src_buff[index];
            if (Op == rcclProd) result = result * next_src_buff[index];
            if (Op == rcclMax)
                result = result > next_src_buff[index] ? result
                                                       : next_src_buff[index];
            if (Op == rcclMin)
                result = result < next_src_buff[index] ? result
                                                       : next_src_buff[index];

            //! Get next gpu tracker
            pnext_track = pnext_track->next_gpu;
        }

        curr_dst_buff[tid] = result;
    }

    __syncthreads();
}

//! @brief Definition of RcclKernelMoveRest
//! Gather data (which is not operated on by current gpu) from all gpus
template <typename DataType_t>
__global__ void RcclKernelMoveRest(RingNode_t* pcurr_track, int num_gpus,
                                   int rank, int count_per_gpu,
                                   int max_count_per_gpu) {
    int tx = threadIdx.x;
    int bx = blockIdx.x;
    int tid = tx + bx * knum_workitems;

    //! Get pointers to current gpu source and destination buffers
    const DataType_t* curr_src_buff =
        reinterpret_cast<const DataType_t*>(pcurr_track->src_buffer);
    DataType_t* curr_dst_buff =
        reinterpret_cast<DataType_t*>(pcurr_track->dst_buffer);

    int count = count_per_gpu;

    //! If the rank of peer gpu is last the last gpu, update the number of
    //! elements it operates on
    if (rank == num_gpus - 1) {
        count = max_count_per_gpu;
    }

    //! Read data from peer gpu and store it to current gpu destination
    //! buffer
    if (tid < count) {
        curr_dst_buff[tid] =
            curr_src_buff[tid + rank * count_per_gpu];
    }
}