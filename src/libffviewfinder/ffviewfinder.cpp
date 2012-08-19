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

#include "ffviewfinder.h"

#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>

typedef struct
{
    bool running;
    int frame_count;
    void (*frame_callback)(ffviewfinder_context *ffvf_context, AVFrame *frame, int i, void *arg);
    void *frame_callback_arg;
    int (*read_callback)(ffviewfinder_context *ffvf_context, const uint8_t *buf, ssize_t size, void *arg);
    void *read_callback_arg;
    void (*close_callback)(ffviewfinder_context *ffvf_context, void *arg);
    void *close_callback_arg;
} ffviewfinder_reserved;

void* decoding_thread(void* arg);

ffviewfinder_context *ffviewfinder_alloc()
{
    ffviewfinder_context *ffvf_context = (ffviewfinder_context*) malloc(sizeof(ffviewfinder_context));
    memset(ffvf_context, 0, sizeof(ffviewfinder_context));

    ffviewfinder_reset(ffvf_context);

    return ffvf_context;
}

void ffviewfinder_reset(ffviewfinder_context *ffvf_context)
{
    ffviewfinder_reserved *ffvf_reserved = (ffviewfinder_reserved*) ffvf_context->reserved;
    if (!ffvf_reserved) ffvf_reserved = (ffviewfinder_reserved*) malloc(sizeof(ffviewfinder_reserved));
    memset(ffvf_reserved, 0, sizeof(ffviewfinder_reserved));

    memset(ffvf_context, 0, sizeof(ffviewfinder_context));
    ffvf_context->reserved = ffvf_reserved;
}

ffviewfinder_error ffviewfinder_set_close_callback(ffviewfinder_context *ffvf_context,
        void (*close_callback)(ffviewfinder_context *ffvf_context, void *arg),
        void *arg)
{
    ffviewfinder_reserved *ffvf_reserved = (ffviewfinder_reserved*) ffvf_context->reserved;
    if (!ffvf_reserved) return FFVF_NOT_INITIALIZED;
    ffvf_reserved->close_callback = close_callback;
    ffvf_reserved->close_callback_arg = arg;
    return FFVF_OK;
}

ffviewfinder_error ffviewfinder_set_read_callback(ffviewfinder_context *ffvf_context,
        int (*read_callback)(ffviewfinder_context *ffvf_context, const uint8_t *buf, ssize_t size, void *arg),
        void *arg)
{
    ffviewfinder_reserved *ffvf_reserved = (ffviewfinder_reserved*) ffvf_context->reserved;
    if (!ffvf_reserved) return FFVF_NOT_INITIALIZED;
    ffvf_reserved->read_callback = read_callback;
    ffvf_reserved->read_callback_arg = arg;
    return FFVF_OK;
}

ffviewfinder_error ffviewfinder_set_frame_callback(ffviewfinder_context *ffvf_context,
        void (*frame_callback)(ffviewfinder_context *ffvf_context, AVFrame *frame, int i, void *arg),
        void *arg)
{
    ffviewfinder_reserved *ffvf_reserved = (ffviewfinder_reserved*) ffvf_context->reserved;
    if (!ffvf_reserved) return FFVF_NOT_INITIALIZED;
    ffvf_reserved->frame_callback = frame_callback;
    ffvf_reserved->frame_callback_arg = arg;
    return FFVF_OK;
}

ffviewfinder_error ffviewfinder_close(ffviewfinder_context *ffvf_context)
{
    AVCodecContext *codec_context = ffvf_context->codec_context;

    if (codec_context)
    {
        if (avcodec_is_open(codec_context))
        {
            avcodec_close(codec_context);
        }

        av_free(codec_context);
        codec_context = ffvf_context->codec_context = NULL;
    }

    return FFVF_OK;
}

ffviewfinder_error ffviewfinder_free(ffviewfinder_context *ffvf_context)
{
    free(ffvf_context->reserved);
    ffvf_context->reserved = NULL;
    free(ffvf_context);
    return FFVF_OK;
}

ffviewfinder_error ffviewfinder_start(ffviewfinder_context *ffvf_context)
{
    ffviewfinder_reserved *ffvf_reserved = (ffviewfinder_reserved*) ffvf_context->reserved;
    if (!ffvf_reserved) return FFVF_NOT_INITIALIZED;
    if (ffvf_reserved->running) return FFVF_ALREADY_RUNNING;
    if (!ffvf_context->codec_context) return FFVF_NO_CODEC_SPECIFIED;

    ffvf_reserved->frame_count = 0;
    ffvf_reserved->running = true;

    pthread_t pthread;
    pthread_create(&pthread, 0, &decoding_thread, ffvf_context);

    return FFVF_OK;
}

ffviewfinder_error ffviewfinder_stop(ffviewfinder_context *ffvf_context)
{
    ffviewfinder_reserved *ffvf_reserved = (ffviewfinder_reserved*) ffvf_context->reserved;
    if (!ffvf_reserved) return FFVF_NOT_INITIALIZED;
    if (!ffvf_reserved->running) return FFVF_ALREADY_STOPPED;

    ffvf_reserved->running = false;

    return FFVF_OK;
}

void* decoding_thread(void* arg)
{
    ffviewfinder_context* ffvf_context = (ffviewfinder_context*) arg;
    ffviewfinder_reserved *ffvf_reserved = (ffviewfinder_reserved*) ffvf_context->reserved;
    AVCodecContext *codec_context = ffvf_context->codec_context;

    AVPacket packet;
    int got_frame;

    int decode_buffer_length = 4096;
    uint8_t decode_buffer[decode_buffer_length + FF_INPUT_BUFFER_PADDING_SIZE];
    memset(decode_buffer + decode_buffer_length, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    AVFrame *frame = avcodec_alloc_frame();

    while (ffvf_reserved->running)
    {
        if (!ffvf_reserved->read_callback) packet.size = read(ffvf_context->fd, decode_buffer, decode_buffer_length);
        else packet.size = ffvf_reserved->read_callback(ffvf_context, decode_buffer, decode_buffer_length,
                ffvf_reserved->read_callback_arg);

        if (!packet.size) break;

        packet.data = decode_buffer;

        while (ffvf_reserved->running && packet.size > 0)
        {
            // reset the AVPacket
            av_init_packet(&packet);

            got_frame = 0;
            int decode_result = avcodec_decode_video2(codec_context, frame, &got_frame, &packet);

            if (decode_result < 0)
            {
                fprintf(stderr, "Error while decoding frame %d\n", ffvf_reserved->frame_count);
                exit(1);
            }

            if (got_frame)
            {
                if (ffvf_reserved->frame_callback) ffvf_reserved->frame_callback(
                        ffvf_context, frame, ffvf_reserved->frame_count,
                        ffvf_reserved->frame_callback_arg);

                ffvf_reserved->frame_count++;
            }

            packet.size -= decode_result;
            packet.data += decode_result;
        }
    }

    if (ffvf_reserved->running)
    {
        // reset the AVPacket
        av_init_packet(&packet);
        packet.data = NULL;
        packet.size = 0;

        got_frame = 0;
        avcodec_decode_video2(codec_context, frame, &got_frame, &packet);

        if (got_frame)
        {
            if (ffvf_reserved->frame_callback) ffvf_reserved->frame_callback(
                    ffvf_context, frame, ffvf_reserved->frame_count,
                    ffvf_reserved->frame_callback_arg);

            ffvf_reserved->frame_count++;
        }
    }

    av_free(frame);
    frame = NULL;

    if (ffvf_reserved->close_callback) ffvf_reserved->close_callback(
            ffvf_context, ffvf_reserved->close_callback_arg);

    return 0;
}
