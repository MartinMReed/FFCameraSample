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
#include <bb/cascades/Window>
#include <bb/cascades/ForeignWindowControl>
#include <bb/cascades/Container>
#include <bb/cascades/StackLayout>
#include <bb/cascades/DockLayout>
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

#include <pthread.h>

#include "ffcamerasampleapp.hpp"

using namespace bb::cascades;

#define SECOND 1000000
#define VIDEO_WIDTH 288
#define VIDEO_HEIGHT 512
//#define VIDEO_WIDTH 1080
//#define VIDEO_HEIGHT 1920
#define CODEC_ID CODEC_ID_MPEG2VIDEO
#define FILENAME (char*)"/accounts/1000/shared/camera/VID_TEST.mpg"

// workaround a ForeignWindowControl race condition
#define WORKAROUND_FWC

FFCameraSampleApp::FFCameraSampleApp()
        : mCameraHandle(CAMERA_HANDLE_INVALID), record(false), decode(false)
{
    mViewfinderWindow = ForeignWindowControl::create().windowId(QString("cameraViewfinder"));

    // Allow Cascades to update the native window's size, position, and visibility, but not the source-size.
    // Cascades may otherwise attempt to redefine the buffer source-size to match the window size, which would yield
    // undesirable results.  You can experiment with this if you want to see what I mean.
    mViewfinderWindow->setUpdatedProperties(WindowProperty::Position | WindowProperty::Size | WindowProperty::Visible);

    QObject::connect(mViewfinderWindow,
            SIGNAL(windowAttached(screen_window_t, const QString &, const QString &)),
            this, SLOT(onWindowAttached(screen_window_t, const QString &,const QString &)));

    mStartFrontButton = Button::create("Front")
            .onClicked(this, SLOT(onStartFront()));

    mStartRearButton = Button::create("Rear")
            .onClicked(this, SLOT(onStartRear()));

    mStartDecoderButton = Button::create("Play")
            .onClicked(this, SLOT(onStartStopDecoder()));

    mStopButton = Button::create("Stop Camera")
            .onClicked(this, SLOT(onStopCamera()));
    mStopButton->setVisible(false);

    mStartStopButton = Button::create("Record Start")
            .onClicked(this, SLOT(onStartStopRecording()));
    mStartStopButton->setVisible(false);

    mStatusLabel = Label::create("filename");
    mStatusLabel->setVisible(false);

    Container* container = Container::create().layout(DockLayout::create())
            .add(Container::create().horizontal(HorizontalAlignment::Center).vertical(VerticalAlignment::Center).add(mViewfinderWindow))
            .add(Container::create().horizontal(HorizontalAlignment::Left).vertical(VerticalAlignment::Top).add(mStatusLabel))
            .add(Container::create().horizontal(HorizontalAlignment::Center).vertical(VerticalAlignment::Bottom).layout(StackLayout::create().orientation(LayoutOrientation::LeftToRight))
            .add(mStartFrontButton)
            .add(mStartRearButton)
            .add(mStartDecoderButton)
            .add(mStartStopButton)
            .add(mStopButton));

    Application::instance()->setScene(Page::create().content(container));

    ffe_context = ffenc_alloc();
    ffd_context = ffdec_alloc();

    pthread_mutex_init(&reading_mutex, 0);
    pthread_cond_init(&read_cond, 0);
}

FFCameraSampleApp::~FFCameraSampleApp()
{
    delete mViewfinderWindow;

    ffenc_free(ffe_context);
    ffe_context = NULL;

    ffdec_free(ffd_context);
    ffd_context = NULL;

    pthread_mutex_destroy(&reading_mutex);
    pthread_cond_destroy(&read_cond);
}

void FFCameraSampleApp::onWindowAttached(screen_window_t win, const QString &group, const QString &id)
{
    qDebug() << "onWindowAttached: " << group << ", " << id;

    // set screen properties to mirror if this is the front-facing camera
    int mirror = mCameraUnit == CAMERA_UNIT_FRONT;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_MIRROR, &mirror);

    // put the viewfinder window behind the cascades window
    int z = -2;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_ZORDER, &z);

    // scale the viewfinder window to fit the display
    int size[] = { 768, 1280 };
    screen_set_window_property_iv(win, SCREEN_PROPERTY_SIZE, size);

    // make the window visible.  by default, the camera creates an invisible
    // viewfinder, so that the user can decide when and where to place it
    int visible = 1;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_VISIBLE, &visible);

#ifdef WORKAROUND_FWC
    // seems we still need a workaround in R9 for a potential race due to
    // ForeignWindowControl updating/flushing the window's properties in
    // parallel with the execution of the onWindowAttached() handler.
    mViewfinderWindow->setVisible(false);
    mViewfinderWindow->setVisible(true);
#endif
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

void FFCameraSampleApp::onStartStopDecoder()
{
    if (decode)
    {
        decode = false;
        ffdec_stop(ffd_context);
        pthread_cond_signal(&read_cond);
        mStartDecoderButton->setText("Play");
        return;
    }

    if (!start_decoder(CODEC_ID)) return;

    decode = true;

    mStartDecoderButton->setText("Stop");
}

void FFCameraSampleApp::onStartStopRecording()
{
    if (mCameraHandle == CAMERA_HANDLE_INVALID) return;

    if (record)
    {
        record = false;

        onStartStopDecoder();

        ffenc_stop(ffe_context);

        mStartStopButton->setText("Start Recording");
        mStopButton->setEnabled(true);
        mStatusLabel->setVisible(false);

        return;
    }

    if (!start_encoder(CODEC_ID)) return;

    record = true;

    onStartStopDecoder();

    mStartStopButton->setText("Stop Recording");
    mStopButton->setEnabled(false);
    mStatusLabel->setText(basename(FILENAME));
    mStatusLabel->setVisible(true);
}

bool FFCameraSampleApp::start_decoder(CodecID codec_id)
{
    struct stat buf;
    if (stat(FILENAME, &buf) == -1)
    {
        fprintf(stderr, "file not found %s\n", FILENAME);
        return false;
    }

    read_file = fopen(FILENAME, "rb");

    if (!read_file)
    {
        fprintf(stderr, "could not open %s: %d: %s\n", FILENAME, errno, strerror(errno));
        return false;
    }

    AVCodec *codec = avcodec_find_decoder(codec_id);

    if (!codec)
    {
        av_register_all();
        codec = avcodec_find_decoder(codec_id);

        if (!codec)
        {
            fprintf(stderr, "could not find codec\n");
            return false;
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

    decode_read = 0;

    ffdec_reset(ffd_context);
    ffdec_set_close_callback(ffd_context, ffd_context_close, this);
    ffdec_set_read_callback(ffd_context, ffd_read_callback, this);
    ffd_context->codec_context = codec_context;

    if (avcodec_open2(codec_context, codec, NULL) < 0)
    {
        av_free(codec_context);
        fprintf(stderr, "could not open codec context\n");
        return false;
    }

    screen_window_t window;
    ffdec_create_view(ffd_context, Application::instance()->mainWindow()->groupId(), "HelloForeignWindowAppID", &window);

//    int window_size[] = { 768, 1280 };
//    screen_set_window_property_iv(window, SCREEN_PROPERTY_SIZE, window_size);

    if (ffdec_start(ffd_context) != FFDEC_OK)
    {
        fprintf(stderr, "could not start ffdec\n");
        ffdec_close(ffd_context);
        return false;
    }

    qDebug() << "started ffdec_context";

    return true;
}

bool FFCameraSampleApp::start_encoder(CodecID codec_id)
{
    struct stat buf;
    if (stat(FILENAME, &buf) != -1)
    {
        fprintf(stderr, "deleting old file...\n");
        remove(FILENAME);
    }

    write_file = fopen(FILENAME, "wb");

    if (!write_file)
    {
        fprintf(stderr, "could not open %s: %d: %s\n", FILENAME, errno, strerror(errno));
        return false;
    }

    AVCodec *codec = avcodec_find_encoder(codec_id);

    if (!codec)
    {
        av_register_all();
        codec = avcodec_find_encoder(codec_id);

        if (!codec)
        {
            fprintf(stderr, "could not find codec\n");
            return false;
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

    ffenc_reset(ffe_context);
    ffenc_set_close_callback(ffe_context, ffe_context_close, this);
    ffenc_set_write_callback(ffe_context, ffe_write_callback, this);
    ffe_context->codec_context = codec_context;

    if (avcodec_open2(codec_context, codec, NULL) < 0)
    {
        av_free(codec_context);
        fprintf(stderr, "could not open codec context\n");
        return false;
    }

    if (ffenc_start(ffe_context) != FFENC_OK)
    {
        fprintf(stderr, "could not start ffenc\n");
        ffenc_close(ffe_context);
        return false;
    }

    qDebug() << "started ffe_context";

    return true;
}

void vf_callback(camera_handle_t handle, camera_buffer_t* buf, void* arg)
{
    if (buf->frametype != CAMERA_FRAMETYPE_NV12) return;

    FFCameraSampleApp* app = (FFCameraSampleApp*) arg;
    app->update_fps(buf);
    app->print_fps(buf);

    ffenc_add_frame(app->ffe_context, buf);
}

int ffd_read_callback(ffdec_context *ffd_context, uint8_t *buf, ssize_t size, void *arg)
{
    FFCameraSampleApp* app = (FFCameraSampleApp*) arg;

    int read;
    do
    {
        fseek(app->read_file, app->decode_read, SEEK_SET);
        read = fread(buf, 1, size, app->read_file);

        if (read > 0)
        {
            app->decode_read += read;
            break;
        }

        pthread_mutex_lock(&app->reading_mutex);
        pthread_cond_wait(&app->read_cond, &app->reading_mutex);
        pthread_mutex_unlock(&app->reading_mutex);
    }
    while (app->decode);
    return read;
}

void ffe_write_callback(ffenc_context *ffe_context, uint8_t *buf, ssize_t size, void *arg)
{
    FFCameraSampleApp* app = (FFCameraSampleApp*) arg;
    fwrite(buf, 1, size, app->write_file);
    pthread_cond_signal(&app->read_cond);
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

void ffe_context_close(ffenc_context *ffe_context, void *arg)
{
    qDebug() << "closing ffenc_context";

    FFCameraSampleApp* app = (FFCameraSampleApp*) arg;

    ffenc_close(ffe_context);

    fclose(app->write_file);
    app->write_file = NULL;
}

void ffd_context_close(ffdec_context *ffd_context, void *arg)
{
    qDebug() << "closing ffdec_context";

    FFCameraSampleApp* app = (FFCameraSampleApp*) arg;

    ffdec_close(ffd_context);

    fclose(app->read_file);
    app->read_file = NULL;
}
