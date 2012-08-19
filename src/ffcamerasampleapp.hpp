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

#include <bb/cascades/ForeignWindow>
#include <bb/cascades/Button>
#include <bb/cascades/Label>

#include "libffcamapi/ffcamapi.h"
#include "libffviewfinder/ffviewfinder.h"
#include <deque>

using namespace bb::cascades;

void ffvf_context_close(ffviewfinder_context *ffvf_context, void *arg);
void ffvf_frame_callback(ffviewfinder_context *ffvf_context, AVFrame *frame, int i, void *arg);

void ffc_context_close(ffcamera_context *ffc_context, void *arg);
void vf_callback(camera_handle_t handle, camera_buffer_t* buf, void* arg);

class FFCameraSampleApp : public QObject
{
    friend void ffvf_context_close(ffviewfinder_context *ffvf_context, void *arg);
    friend void ffvf_frame_callback(ffviewfinder_context *ffvf_context, AVFrame *frame, int i, void *arg);
    friend void ffc_context_close(ffcamera_context *ffc_context, void *arg);
    friend void vf_callback(camera_handle_t handle, camera_buffer_t* buf, void* arg);

Q_OBJECT
    public slots:

    void onWindowAttached(unsigned long handle, const QString &group, const QString &id);
    void onWindowDetached(unsigned long handle, const QString &group, const QString &id);
    void onStartFront();
    void onStartRear();
    void onStartDecoder();
    void onStopCamera();
    void onStartStopRecording();

public:

    FFCameraSampleApp();
    ~FFCameraSampleApp();

private:

    int createViewfinder(camera_unit_t cameraUnit, const QString &group, const QString &id);

    void update_fps(camera_buffer_t* buf);
    void print_fps(camera_buffer_t* buf);

    void start_encoder(CodecID codec_id);
    void start_decoder(CodecID codec_id);

    ForeignWindow *mViewfinderWindow;
    Button *mStartFrontButton;
    Button *mStartRearButton;
    Button *mStartDecoderButton;
    Button *mStopButton;
    Button *mStartStopButton;
    Label *mStatusLabel;
    camera_handle_t mCameraHandle;
    camera_unit_t mCameraUnit;

    FILE *file;
    bool record, decode;
    std::deque<int64_t> fps;
    ffcamera_context *ffc_context;
    ffviewfinder_context *ffvf_context;
};

#endif
