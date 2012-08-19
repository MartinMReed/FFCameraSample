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
        : mCameraHandle(CAMERA_HANDLE_INVALID), record(false), decode(false)
{
    // create our foreign window
    // Using .id() in the builder is equivalent to mViewfinderWindow->setWindowId()
    mViewfinderWindow = ForeignWindow::create().id(QString("cameraViewfinder"));

    createForeignWindow(ForeignWindow::mainWindowGroupId(), "HelloForeignWindowAppID");

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
    mStartFrontButton = Button::create("Front");
    mStartRearButton = Button::create("Rear");
    mStartDecoderButton = Button::create("Decode");
    mStopButton = Button::create("Stop Camera");
    mStopButton->setVisible(false);
    mStartStopButton = Button::create("Record Start");
    mStartStopButton->setVisible(false);

    // connect actions to the buttons
    QObject::connect(mStartFrontButton, SIGNAL(clicked()), this, SLOT(onStartFront()));
    QObject::connect(mStartRearButton, SIGNAL(clicked()), this, SLOT(onStartRear()));
    QObject::connect(mStartDecoderButton, SIGNAL(clicked()), this, SLOT(onStartDecoder()));
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
            .add(mStartDecoderButton)
            .add(mStartStopButton)
            .add(mStopButton));

    Application::setScene(Page::create().content(container));

    ffc_context = ffcamera_alloc();
    ffvf_context = ffviewfinder_alloc();
}

bool FFCameraSampleApp::createForeignWindow(const QString &group, const QString id)
{
    QByteArray groupArr = group.toAscii();
    QByteArray idArr = id.toAscii();

    // You must create a context before you create a window.
    screen_create_context(&mScreenCtx, SCREEN_APPLICATION_CONTEXT);

    // Create a child window of the current window group, join the window group and set
    // a window id.
    screen_create_window_type(&mScreenWindow, mScreenCtx, SCREEN_CHILD_WINDOW);
    screen_join_window_group(mScreenWindow, groupArr.constData());
    screen_set_window_property_cv(mScreenWindow, SCREEN_PROPERTY_ID_STRING, idArr.length(), idArr.constData());

    // In this application we will render to a pixmap buffer and then blit that to
    // the window, we set the usage to native (default is read and write but we do not need that here).
    int usage = SCREEN_USAGE_NATIVE;
    screen_set_window_property_iv(mScreenWindow, SCREEN_PROPERTY_USAGE, &usage);

    int video_size[2] =
            { VIDEO_WIDTH, VIDEO_HEIGHT };
    screen_set_window_property_iv(mScreenWindow, SCREEN_PROPERTY_BUFFER_SIZE, video_size);
    screen_set_window_property_iv(mScreenWindow, SCREEN_PROPERTY_SOURCE_SIZE, video_size);

    // Use negative Z order so that the window appears under the main window.
    // This is needed by the ForeignWindow functionality.
    int z = -1;
    screen_set_window_property_iv(mScreenWindow, SCREEN_PROPERTY_ZORDER, &z);

    // Set the window position on screen.
    int pos[2] =
            { 0, 0 };
    screen_set_window_property_iv(mScreenWindow, SCREEN_PROPERTY_POSITION, pos);

    // Finally create the window buffers, in this application we will only use one buffer.
    screen_create_window_buffers(mScreenWindow, 1);

    // In this sample we use a pixmap to render to, a pixmap. This allows us to have
    // full control of exactly which pixels we choose to push to the screen.
    screen_pixmap_t screen_pix;
    screen_create_pixmap(&screen_pix, mScreenCtx);

    // A combination of write and native usage is necessary to blit the pixmap to screen.
    usage = SCREEN_USAGE_WRITE | SCREEN_USAGE_NATIVE;
    screen_set_pixmap_property_iv(screen_pix, SCREEN_PROPERTY_USAGE, &usage);

    int format = SCREEN_FORMAT_YUV420;
    screen_set_pixmap_property_iv(screen_pix, SCREEN_PROPERTY_FORMAT, &format);

    // Set the width and height of the buffer to correspond to the one we specified in QML.
    screen_set_pixmap_property_iv(screen_pix, SCREEN_PROPERTY_BUFFER_SIZE, video_size);

    // Create the pixmap buffer and get a reference to it for rendering in the doNoise function.
    screen_create_pixmap_buffer(screen_pix);
    screen_get_pixmap_property_pv(screen_pix, SCREEN_PROPERTY_RENDER_BUFFERS, (void **) &mScreenPixelBuffer);

    // We get the stride (the number of bytes between pixels on different rows), its used
    // later on when we perform the rendering to the pixmap buffer.
    screen_get_buffer_property_iv(mScreenPixelBuffer, SCREEN_PROPERTY_STRIDE, &mStride);

//    // scale the window to be fullscreen
//    int window_size[] =
//            { 768, 1280 };
//    screen_set_window_property_iv(mScreenWindow, SCREEN_PROPERTY_SIZE, window_size);

    return true;
}

FFCameraSampleApp::~FFCameraSampleApp()
{
    delete mViewfinderWindow;

    ffcamera_free(ffc_context);
    ffc_context = NULL;

    ffviewfinder_free(ffvf_context);
    ffvf_context = NULL;
}

void FFCameraSampleApp::onWindowAttached(unsigned long handle, const QString &group, const QString &id)
{
    qDebug() << "onWindowAttached: " << handle << ", " << group << ", " << id;
    screen_window_t win = (screen_window_t) handle;

    // set screen properties to mirror if this is the front-facing camera
    int i = (mCameraUnit == CAMERA_UNIT_FRONT);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_MIRROR, &i);

    // put the viewfinder window behind the cascades window
    i = -2;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_ZORDER, &i);

    // scale the viewfinder window to fit the display
    int size[] =
            { 768, 1280 };
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
    mStartDecoderButton->setVisible(false);
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
    onStartDecoder();
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
    mStartDecoderButton->setVisible(true);
}

void FFCameraSampleApp::onStartDecoder()
{
    if (record || decode) return;

    struct stat buf;
    if (stat(FILENAME, &buf) == -1)
    {
        fprintf(stderr, "file not found %s\n", FILENAME);
        return;
    }

    file = fopen(FILENAME, "rb");

    if (!file)
    {
        fprintf(stderr, "could not open %s: %d: %s\n", FILENAME, errno, strerror(errno));
        return;
    }

    CodecID codec_id = CODEC_ID_MPEG2VIDEO;
    AVCodec *codec = avcodec_find_decoder(codec_id);

    if (!codec)
    {
        av_register_all();
        codec = avcodec_find_decoder(codec_id);

        if (!codec)
        {
            fprintf(stderr, "could not find codec\n");
            return;
        }
    }

    AVCodecContext *codec_context = avcodec_alloc_context3(codec);
    codec_context->pix_fmt = PIX_FMT_YUV420P;
    codec_context->width = VIDEO_WIDTH;
    codec_context->height = VIDEO_HEIGHT;
    codec_context->thread_count = 2;

    if (codec->capabilities & CODEC_CAP_TRUNCATED)
    {
        // we do not send complete frames
        codec_context->flags |= CODEC_FLAG_TRUNCATED;
    }

    ffviewfinder_reset(ffvf_context);
    ffviewfinder_set_close_callback(ffvf_context, ffvf_context_close, this);
    ffviewfinder_set_frame_callback(ffvf_context, ffvf_frame_callback, this);
    ffvf_context->codec_context = codec_context;
    ffvf_context->fd = fileno(file);

    if (avcodec_open2(codec_context, codec, NULL) < 0)
    {
        av_free(codec_context);
        fprintf(stderr, "could not open codec context\n");
        return;
    }

    if (ffviewfinder_start(ffvf_context) != FFVF_OK)
    {
        fprintf(stderr, "could not start ffviewfinder\n");
        ffviewfinder_close(ffvf_context);
        return;
    }

    decode = true;
}

void FFCameraSampleApp::start_encoder(CodecID codec_id)
{
    struct stat buf;
    if (stat(FILENAME, &buf) != -1)
    {
        fprintf(stderr, "deleting old file...\n");
        remove(FILENAME);
    }

    file = fopen(FILENAME, "wb");

    if (!file)
    {
        fprintf(stderr, "could not open %s: %d: %s\n", FILENAME, errno, strerror(errno));
        return;
    }

    AVCodec *codec = avcodec_find_encoder(codec_id);

    if (!codec)
    {
        av_register_all();
        codec = avcodec_find_encoder(codec_id);

        if (!codec)
        {
            fprintf(stderr, "could not find codec\n");
            return;
        }
    }

    AVCodecContext *codec_context = avcodec_alloc_context3(codec);
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

    ffcamera_reset(ffc_context);
    ffcamera_set_close_callback(ffc_context, ffc_context_close, this);
    ffc_context->codec_context = codec_context;
    ffc_context->fd = fileno(file);

    if (avcodec_open2(codec_context, codec, NULL) < 0)
    {
        av_free(codec_context);
        fprintf(stderr, "could not open codec context\n");
        return;
    }

    if (ffcamera_start(ffc_context) != FFCAMERA_OK)
    {
        fprintf(stderr, "could not start ffcamera\n");
        ffcamera_close(ffc_context);
        return;
    }
}

void FFCameraSampleApp::onStartStopRecording()
{
    if (mCameraHandle == CAMERA_HANDLE_INVALID) return;

    if (decode) return;

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

    CodecID codec_id = CODEC_ID_MPEG2VIDEO;
    start_encoder(codec_id);

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

void FFCameraSampleApp::show_frame(AVFrame *frame)
{
    unsigned char *ptr = NULL;

    int width = frame->width;
    int height = frame->height;

    int blitParameters[] =
            { SCREEN_BLIT_SOURCE_WIDTH, width, SCREEN_BLIT_SOURCE_HEIGHT, height, SCREEN_BLIT_END };

    screen_get_buffer_property_pv(mScreenPixelBuffer, SCREEN_PROPERTY_POINTER, (void**) &ptr);

    uint8_t *srcy = frame->data[0];
    uint8_t *srcu = frame->data[1];
    uint8_t *srcv = frame->data[2];

    unsigned char *y = ptr;
    unsigned char *u = y + (height * mStride);
    unsigned char *v = u + ((height / 2) * (mStride / 2));

    for (int i = 0; i < height; i++)
    {
        int doff = i * mStride;
        int soff = i * frame->linesize[0];
        memcpy(&y[doff], &srcy[soff], frame->width);
    }

    for (int i = 0; i < height / 2; i++)
    {
        int doff = i * mStride / 2;
        int soff = i * frame->linesize[1];
        memcpy(&u[doff], &srcu[soff], frame->width / 2);
    }

    for (int i = 0; i < height / 2; i++)
    {
        int doff = i * mStride / 2;
        int soff = i * frame->linesize[2];
        memcpy(&v[doff], &srcv[soff], frame->width / 2);
    }

    // Get the window buffer, blit the pixels to it and post the window update.
    screen_get_window_property_pv(mScreenWindow, SCREEN_PROPERTY_RENDER_BUFFERS, (void**) mScreenBuf);
    screen_blit(mScreenCtx, mScreenBuf[0], mScreenPixelBuffer, blitParameters);

    int dirty_rects[4] =
            { 0, 0, width, height };
    screen_post_window(mScreenWindow, mScreenBuf[0], 1, dirty_rects, 0);
}

void ffvf_frame_callback(ffviewfinder_context *ffvf_context, AVFrame *frame, int i, void *arg)
{
    FFCameraSampleApp* app = (FFCameraSampleApp*) arg;
    app->show_frame(frame);
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
    qDebug() << "closing ffcamera_context";

    FFCameraSampleApp* app = (FFCameraSampleApp*) arg;

    ffcamera_close(ffc_context);

    fclose(app->file);
    app->file = NULL;
}

void ffvf_context_close(ffviewfinder_context *ffvf_context, void *arg)
{
    qDebug() << "closing ffviewfinder_context";

    FFCameraSampleApp* app = (FFCameraSampleApp*) arg;

    ffviewfinder_close(ffvf_context);

    fclose(app->file);
    app->file = NULL;

    app->decode = false;
}
