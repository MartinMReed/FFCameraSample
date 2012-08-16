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
#include <bb/cascades/Application>
#include <bb/cascades/ForeignWindow>
#include <bb/cascades/Container>
#include <bb/cascades/StackLayout>
#include <bb/cascades/DockLayout>
#include <bb/cascades/DockLayoutProperties>
#include <bb/cascades/Button>
#include <bb/cascades/Label>
#include <bb/cascades/Page>

#include <camera/camera_api.h>
#include <screen/screen.h>
#include <bps/soundplayer.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <libgen.h>

#include "ffcamerasampleapp.hpp"

using namespace bb::cascades;

#define SECOND 1000000
#define VIDEO_WIDTH 288
#define VIDEO_HEIGHT 512
//#define VIDEO_WIDTH 1080
//#define VIDEO_HEIGHT 1920
#define FILENAME (char*)"/accounts/1000/shared/camera/VID_TEST.mpg"

FFCameraSampleApp::FFCameraSampleApp()
        : mCameraHandle(CAMERA_HANDLE_INVALID), record(false)
{
    // create our foreign window
    // Using .id() in the builder is equivalent to mViewfinderWindow->setWindowId()
    mViewfinderWindow = ForeignWindow::create().id(QString("cameraViewfinder"));

    // NOTE that there is a bug in ForeignWindow in 10.0.6 whereby the
    // SCREEN_PROPERTY_SOURCE_SIZE is updated when windows are attached.
    // We don't want this to happen, so we are disabling WindowFrameUpdates.
    // What this means is that if the ForeignWindow geometry is changed, then
    // the underlying screen window properties are not automatically updated to
    // match.  You will have to manually do so by listening for controlFrameChanged
    // signals.  This is outside of the scope of this sample.
    mViewfinderWindow->setWindowFrameUpdateEnabled(false);

    QObject::connect(mViewfinderWindow, SIGNAL(windowAttached(unsigned long,
                    const QString &, const QString &)), this, SLOT(onWindowAttached(unsigned long,
                    const QString &,const QString &)));

    // NOTE that there is a bug in ForeignWindow in 10.0.6 whereby
    // when a window is detached, it's windowHandle is not reset to 0.
    // We need to connect a detach handler to implement a workaround.
    QObject::connect(mViewfinderWindow, SIGNAL(windowDetached(unsigned long,
                    const QString &, const QString &)), this, SLOT(onWindowDetached(unsigned long,
                    const QString &,const QString &)));

    // create a bunch of camera control buttons
    // NOTE: some of these buttons are not initially visible
    mStartFrontButton = Button::create("Front Camera");
    mStartRearButton = Button::create("Rear Camera");
    mStopButton = Button::create("Stop Camera");
    mStopButton->setVisible(false);
    mStartStopButton = Button::create("Record Start");
    mStartStopButton->setVisible(false);

    // connect actions to the buttons
    QObject::connect(mStartFrontButton, SIGNAL(clicked()), this, SLOT(onStartFront()));
    QObject::connect(mStartRearButton, SIGNAL(clicked()), this, SLOT(onStartRear()));
    QObject::connect(mStopButton, SIGNAL(clicked()), this, SLOT(onStopCamera()));
    QObject::connect(mStartStopButton, SIGNAL(clicked()), this, SLOT(onStartStopRecording()));
    mStatusLabel = Label::create("filename");
    mStatusLabel->setVisible(false);

    // using dock layout mainly.  the viewfinder foreign window sits in the center,
    // and the buttons live in their own container at the bottom.
    // a single text label sits at the top of the screen to report recording status.
    Container* container = Container::create().layout(DockLayout::create())
            .add(Container::create().layoutProperties(DockLayoutProperties::create()
            .horizontal(HorizontalAlignment::Center)
            .vertical(VerticalAlignment::Center))
            .add(mViewfinderWindow))
            .add(Container::create().layoutProperties(DockLayoutProperties::create()
            .horizontal(HorizontalAlignment::Left).vertical(VerticalAlignment::Top))
            .add(mStatusLabel))
            .add(Container::create().layoutProperties(DockLayoutProperties::create()
            .horizontal(HorizontalAlignment::Center).vertical(VerticalAlignment::Bottom))
            .layout(StackLayout::create().direction(LayoutDirection::LeftToRight))
            .add(mStartFrontButton)
            .add(mStartRearButton)
            .add(mStartStopButton)
            .add(mStopButton));

    Application::setScene(Page::create().content(container));

    ffc_context = (ffcamera_context*) malloc(sizeof(ffcamera_context));
    memset(ffc_context, 0, sizeof(ffcamera_context));
}

FFCameraSampleApp::~FFCameraSampleApp()
{
    delete mViewfinderWindow;

    free(ffc_context);
    ffc_context = NULL;
}

void FFCameraSampleApp::onWindowAttached(unsigned long handle, const QString &group, const QString &id)
{
    qDebug() << "onWindowAttached: " << handle << ", " << group << ", " << id;
    screen_window_t win = (screen_window_t) handle;

    // set screen properties to mirror if this is the front-facing camera
    int i = (mCameraUnit == CAMERA_UNIT_FRONT);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_MIRROR, &i);

    // put the viewfinder window behind the cascades window
    i = -1;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_ZORDER, &i);

    // scale the viewfinder window to fit the display
    int size[] = {768, 1280};
    screen_set_window_property_iv(win, SCREEN_PROPERTY_SIZE, size);

    // make the window visible.  by default, the camera creates an invisible
    // viewfinder, so that the user can decide when and where to place it
    i = 1;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_VISIBLE, &i);

    // There is a bug in ForeignWindow in 10.0.6 which defers window context
    // flushing until some future UI update.  As a result, the window will
    // not actually be visible until someone flushes the context.  This is
    // fixed in the next release.  For now, we will just manually flush the
    // window context.
    screen_context_t ctx;
    screen_get_window_property_pv(win, SCREEN_PROPERTY_CONTEXT, (void**) &ctx);
    screen_flush_context(ctx, 0);
}

void FFCameraSampleApp::onWindowDetached(unsigned long handle, const QString &group, const QString &id)
{
    // There is a bug in ForeignWindow in 10.0.6 whereby the windowHandle is not
    // reset to 0 when a detach event happens.  We must forcefully zero it here
    // in order for a re-attach to work again in the future.
    mViewfinderWindow->setWindowHandle(0);
}

int FFCameraSampleApp::createViewfinder(camera_unit_t cameraUnit, const QString &group, const QString &id)
{
    if (mCameraHandle != CAMERA_HANDLE_INVALID) return EBUSY;

    mCameraUnit = cameraUnit;

    if (camera_open(mCameraUnit, CAMERA_MODE_RW, &mCameraHandle) != CAMERA_EOK) return EIO;

    // configure viewfinder properties so our ForeignWindow can find the resulting screen window
    camera_set_videovf_property( mCameraHandle, CAMERA_IMGPROP_WIN_GROUPID, group.toStdString().c_str());
    camera_set_videovf_property( mCameraHandle, CAMERA_IMGPROP_WIN_ID, id.toStdString().c_str());

    camera_set_videovf_property( mCameraHandle,
            CAMERA_IMGPROP_WIDTH, VIDEO_WIDTH,
            CAMERA_IMGPROP_HEIGHT, VIDEO_HEIGHT);

    camera_set_video_property( mCameraHandle,
            CAMERA_IMGPROP_WIDTH, VIDEO_WIDTH,
            CAMERA_IMGPROP_HEIGHT, VIDEO_HEIGHT);

    if (camera_start_video_viewfinder(mCameraHandle, vf_callback, NULL, this) != CAMERA_EOK)
    {
        camera_close(mCameraHandle);
        mCameraHandle = CAMERA_HANDLE_INVALID;
        return EIO;
    }

    mStartFrontButton->setVisible(false);
    mStartRearButton->setVisible(false);
    mStopButton->setVisible(true);
    mStartStopButton->setText("Start Recording");
    mStartStopButton->setVisible(true);
    mStartStopButton->setEnabled(true);
    return EOK;
}

void FFCameraSampleApp::onStartFront()
{
    if (!mViewfinderWindow) return;
    const QString windowGroup = mViewfinderWindow->windowGroup();
    const QString windowId = mViewfinderWindow->windowId();
    createViewfinder(CAMERA_UNIT_FRONT, windowGroup, windowId);
}

void FFCameraSampleApp::onStartRear()
{
    if (!mViewfinderWindow) return;
    const QString windowGroup = mViewfinderWindow->windowGroup();
    const QString windowId = mViewfinderWindow->windowId();
    createViewfinder(CAMERA_UNIT_REAR, windowGroup, windowId);
}

void FFCameraSampleApp::onStopCamera()
{
    if (mCameraHandle == CAMERA_HANDLE_INVALID) return;

    // NOTE that closing the camera causes the viewfinder to stop.
    // When the viewfinder stops, it's window is destroyed and the
    // ForeignWindow object will emit a windowDetached signal.
    camera_close(mCameraHandle);
    mCameraHandle = CAMERA_HANDLE_INVALID;

    // reset button visibility
    mStartStopButton->setVisible(false);
    mStopButton->setVisible(false);
    mStartFrontButton->setVisible(true);
    mStartRearButton->setVisible(true);
}

void FFCameraSampleApp::onStartStopRecording()
{
    if (mCameraHandle == CAMERA_HANDLE_INVALID) return;

    if (record)
    {
        record = false;

        qDebug() << "stop requested";

        ffcamera_stop(ffc_context);

        mStartStopButton->setText("Start Recording");
        mStopButton->setEnabled(true);
        mStatusLabel->setVisible(false);

        return;
    }

    qDebug() << "start requested";

    struct stat buf;
    if (stat(FILENAME, &buf) != -1)
    {
        fprintf(stderr, "deleting old file...\n");
        remove(FILENAME);
    }

    file = fopen(FILENAME, "wb");

    if (!file)
    {
        fprintf(stderr, "could not open %s: %d: %s\n", FILENAME, errno,strerror(errno));
        exit(1);
        return;
    }

    AVCodecContext *codec_context = NULL;
    ffcamera_default_codec(CODEC_ID_MPEG2VIDEO, 288, 512, &codec_context);

    codec_context->pix_fmt = PIX_FMT_YUV420P;
    codec_context->width = VIDEO_WIDTH;
    codec_context->height = VIDEO_HEIGHT;
    codec_context->bit_rate = 400000;
    codec_context->time_base.num = 1;
    codec_context->time_base.den = 30;
    codec_context->ticks_per_frame = 2;
    codec_context->gop_size = 15;
    codec_context->colorspace = AVCOL_SPC_SMPTE170M;
    codec_context->thread_count = 2;

    ffcamera_init(ffc_context);
    ffcamera_set_close_callback(ffc_context, ffc_context_close, this);
    ffc_context->codec_context = codec_context;
    ffc_context->fd = fileno(file);
    ffcamera_start(ffc_context);

    record = true;

    mStartStopButton->setText("Stop Recording");
    mStopButton->setEnabled(false);
    mStatusLabel->setText(basename(FILENAME));
    mStatusLabel->setVisible(true);
}

void vf_callback(camera_handle_t handle, camera_buffer_t* buf, void* arg)
{
    if (buf->frametype != CAMERA_FRAMETYPE_NV12) return;

    FFCameraSampleApp* app = (FFCameraSampleApp*) arg;
    app->update_fps(buf);
    app->print_fps(buf);

    ffcamera_vfcallback(handle, buf, app->ffc_context);
}

void FFCameraSampleApp::update_fps(camera_buffer_t* buf)
{
    fps.push_back(buf->frametimestamp);
    while (fps.back() - fps.front() > SECOND)
    {
        fps.pop_front();
    }
}

void FFCameraSampleApp::print_fps(camera_buffer_t* buf)
{
    static int64_t frametimestamp = 0;
    if (!frametimestamp || buf->frametimestamp - frametimestamp >= SECOND)
    {
        frametimestamp = buf->frametimestamp;
        qDebug() << "fps[" << fps.size() << "]";
    }
}

void ffc_context_close(ffcamera_context *ffc_context, void *arg)
{
    FFCameraSampleApp* app = (FFCameraSampleApp*) arg;

    ffcamera_close(ffc_context);

    fclose(app->file);
    app->file = NULL;
}
