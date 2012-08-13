FFCameraSample uses the native camera API to record video. However instead of using the platform encoder to write to an MP4 file, this app will use FFmpeg to write out to an MPEG-2 file. This app currently only records video and not audio.

The FFmpeg related functionality is provided through [libffcamapi](https://github.com/hardisonbrewing/libffcamapi) which has been copied into the `src` directory.

Related support forum post:  
[Camera API NV12 frame to AVFrame (FFmpeg)](http://supportforums.blackberry.com/t5/Native-Development/Camera-API-NV12-frame-to-AVFrame-FFmpeg/td-p/1842089)

Building the FFmpeg libs for ARM:

	$ ./configure --enable-cross-compile --cross-prefix=arm-unknown-nto-qnx8.0.0eabi- --arch=armv7 --disable-debug --enable-optimizations --enable-asm --disable-static --enable-shared --target-os=qnx --disable-ffplay --disable-ffserver --disable-ffprobe --prefix=`pwd`/target  
	$ make install  

Built using the 10.0.6 BlackBerry 10 NDK, and tested on the BlackBerry Dev Alpha device.