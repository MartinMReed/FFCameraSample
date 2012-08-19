/* Copyright (c) 2012 Martin M Reed
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FFVIEWFINDER_H
#define FFVIEWFINDER_H

// include math.h otherwise it will get included
// by avformat.h and cause duplicate definition
// errors because of C vs C++ functions
#include <math.h>

extern "C"
{
#define UINT64_C uint64_t
#define INT64_C int64_t
#include <libavformat/avformat.h>
}

#include <sys/types.h>

typedef enum
{
    FFVF_OK = 0,
    FFVF_NOT_INITIALIZED,
    FFVF_NO_CODEC_SPECIFIED,
    FFVF_ALREADY_RUNNING,
    FFVF_ALREADY_STOPPED
} ffviewfinder_error;

typedef struct
{
    /**
     * The codec context to use for encoding.
     */
    AVCodecContext *codec_context;

    /**
     * File descriptor to write the encoded frames to automatically.
     * This is optional. You can choose to use the write_callback
     * and handle writing manually.
     */
    int fd;

    /**
     * For internal use. Do not use.
     */
    void *reserved;
} ffviewfinder_context;

/**
 * Allocate the context with default values.
 */
ffviewfinder_context *ffviewfinder_alloc();

/**
 * Reset the context with default values.
 */
void ffviewfinder_reset(ffviewfinder_context *ffvf_context);

ffviewfinder_error ffviewfinder_set_frame_callback(ffviewfinder_context *ffvf_context,
        void (*frame_callback)(ffviewfinder_context *ffvf_context, AVFrame *frame, int i, void *arg),
        void *arg);

ffviewfinder_error ffviewfinder_set_read_callback(ffviewfinder_context *ffvf_context,
        int (*read_callback)(ffviewfinder_context *ffvf_context, const uint8_t *buf, ssize_t size, void *arg),
        void *arg);

ffviewfinder_error ffviewfinder_set_close_callback(ffviewfinder_context *ffvf_context,
        void (*close_callback)(ffviewfinder_context *ffvf_context, void *arg),
        void *arg);

/**
 * Close the context.
 * This will also close the AVCodecContext if not already closed.
 */
ffviewfinder_error ffviewfinder_close(ffviewfinder_context *ffvf_context);

/**
 * Free the context.
 */
ffviewfinder_error ffviewfinder_free(ffviewfinder_context *ffvf_context);

/**
 * Start recording and encoding the camera frames.
 * Encoding will begin on a background thread.
 */
ffviewfinder_error ffviewfinder_start(ffviewfinder_context *ffvf_context);

/**
 * Stop recording frames. Once all recorded frames have been encoded
 * the background thread will die.
 */
ffviewfinder_error ffviewfinder_stop(ffviewfinder_context *ffvf_context);

#endif
