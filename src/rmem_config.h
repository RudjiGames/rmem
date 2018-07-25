/*
 * Copyright (c) 2018 by Milos Tosic. All Rights Reserved.
 * License: http://www.opensource.org/licenses/BSD-2-Clause
 */

#ifndef RMEM_CONFIG_H
#define RMEM_CONFIG_H

/// If enabled, reduces capture file size by keeping a recored of already 
/// saved stack traces and storing their hash instead
#define RMEM_ENABLE_STACK_TRACE_HASHING			1

/// If enabled, keeps stack traces along with their hashes for the purpose
/// of solving possible hash collisions. Reduces performance!
#define RMEM_STACK_TRACE_HASHING_PARANOIA		0

/// Size of the stack trace hash table, smaller hash table size reduces memory
/// overhead of profiling but increases likelihood of collisions.
#define RMEM_STACK_TRACE_HASH_TABLE_SIZE		256*1024

/// Maximum depth of stack trace for captures, for code bases with very deep 
/// call-stacks this value should be increased in order to build stack trace
/// trees and tree maps more accurately during profiling phase
///
/// For platforms with heavy memory constraints, keeping this value low will
/// minimize the impact of profiling on application functionality.
#define RMEM_MAX_STACK_TRACES					256

/// If enabled, compresses the output stream using LZ4. Note that this is a
/// CPU intensive process and might cause noticeable spikes in CPU usage.
/// However, as the amount of data written to HDD is significantly reduced it
/// may offset for performance loss caused by compression cost.
/// One may experiment with changing the write out buffer size (in rmem_hook.h)
/// to find the right balance between capture size and performance impact.
#define RMEM_ENABLE_LZ4_COMPRESSION				1

/// If enabled, no allocation tracking is done until rmemStartCapture() is called
#define RMEM_ENABLE_DELAYED_CAPTURE				0

#endif // RMEM_CONFIG_H

