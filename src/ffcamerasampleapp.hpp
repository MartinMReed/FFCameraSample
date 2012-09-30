/* Copyright (c) 2012 Martin M Reed
 * Copyright (c) 2012 Research In Motion Limited.
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
#ifndef __FFVIDEOCAMERAAPP_HPP__
#define __FFVIDEOCAMERAAPP_HPP__

#include <QtCore/QObject>
#include <QtCore/QMetaType>

#include <bb/cascades/ForeignWindowControl>
#include <bb/cascades/Button>
#include <bb/cascades/Label>

#include "libffbb/ffbbenc.h"
#include "libffbb/ffbbdec.h"
#include <deque>

using namespace bb::cascades;

void ffd_context_close(ffdec_context *ffd_context, void *arg);
int ffd_read_callback(ffdec_context *ffd_context, uint8_t *buf, ssize_t size, void *arg);

void ffe_context_close(ffenc_context *ffe_context, void *arg);
void vf_callback(camera_handle_t handle, camera_buffer_t* buf, void* arg);
void ffe_write_callback(ffenc_context *ffe_context, uint8_t *buf, ssize_t size, void *arg);

class FFCameraSampleApp : public QObject
{
    friend void ffd_context_close(ffdec_context *ffd_context, void *arg);
    friend int ffd_read_callback(ffdec_context *ffd_context, uint8_t *buf, ssize_t size, void *arg);

    friend void ffe_context_close(ffenc_context *ffe_context, void *arg);
    friend void vf_callback(camera_handle_t handle, camera_buffer_t* buf, void* arg);
    friend void ffe_write_callback(ffenc_context *ffe_context, uint8_t *buf, ssize_t size, void *arg);

Q_OBJECT
    public slots:

    void onWindowAttached(screen_window_t win, const QString &group, const QString &id);
    void onStartFront();
    void onStartRear();
    void onStopCamera();
    void onStartStopDecoder();
    void onStartStopRecording();

public:

    FFCameraSampleApp();
    ~FFCameraSampleApp();

private:

    int createViewfinder(camera_unit_t cameraUnit, const QString &group, const QString &id);

    void update_fps(camera_buffer_t* buf);
    void print_fps(camera_buffer_t* buf);
    void show_frame(AVFrame *frame);

    bool start_encoder(CodecID codec_id);
    bool start_decoder(CodecID codec_id);

    ForeignWindowControl *mViewfinderWindow;
    Button *mStartFrontButton;
    Button *mStartRearButton;
    Button *mStartDecoderButton;
    Button *mStopButton;
    Button *mStartStopButton;
    Label *mStatusLabel;
    camera_handle_t mCameraHandle;
    camera_unit_t mCameraUnit;

    FILE *write_file;
    FILE *read_file;
    int decode_read;
    bool record, decode;
    std::deque<int64_t> fps;
    ffenc_context *ffe_context;
    ffdec_context *ffd_context;
    pthread_mutex_t reading_mutex;
    pthread_cond_t read_cond;
};

#endif
