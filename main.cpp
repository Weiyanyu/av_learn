
#if defined(__cplusplus)
extern "C"
{
#endif
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#if defined(__cplusplus)
}
#endif

#include <string>
#include <iostream>
#include <fstream>
#include "device.h"
#include "resample.h"
#include "codec.h"
#include "log.h"

void initParam()
{
    avdevice_register_all();    
    // av_log_set_level(AV_LOG_DEBUG);
}

void testReadAudioFromDevice();
void testReadAudioFromFile();
void testReadImageDataAndEncodeVideo();
void testReadVideoDataFromFile();
void testReadPCMAndEncode();

int main()
{
    initParam();
    // testReadAudioFromDevice();
    // testReadAudioFromFile();
    // testReadPCMAndEncode();
    // testReadVideoDataFromFile();
    testReadImageDataAndEncodeVideo();
    return 0;
}

void testReadAudioFromDevice()
{
    // create a device
    AudioDevice device("hw:0", DeviceType::AUDIO);

    ReampleParam swrCtxParam = 
    {
        .outChannelLayout = AV_CH_LAYOUT_STEREO,
        .outSampleFmt = AV_SAMPLE_FMT_S16,
        .outSampleRate = 44100,
        .inChannelLayout = AV_CH_LAYOUT_STEREO,
        .inSampleFmt = AV_SAMPLE_FMT_S16,
        .inSampleRate = 48000,
        .logOffset = 0,
        .logCtx = nullptr
    };

    EncoderParam audioEncodeParam
    {
        .needEncode = true,
        .codecName = "libfdk_aac",
        .bitRate = 0,
        .profile = FF_PROFILE_AAC_HE_V2,
        .sampleFmt = AV_SAMPLE_FMT_S16,
        .channelLayout = AV_CH_LAYOUT_STEREO,
        .sampleRate = 44100,
        .byName = true,
    };

    CodecParam encoderParam =
    {
        .encodeParam = audioEncodeParam
    };
    // start record and save output file

    ReadDeviceDataParam readParams
    {
        .outFilename = "out.aac",
        .resampleParam = swrCtxParam,
        .codecParam = encoderParam
    };

    device.readAndEncode(readParams);
}

void testReadAudioFromFile()
{
    AudioDevice device("/home/yeonon/learn/av/demo/build/sample-6s.mp3", DeviceType::ENCAPSULATE_FILE);

    ReadDeviceDataParam readParams
    {
        .outFilename = "out3.pcm",
        .outChannelLayout = AV_CH_LAYOUT_STEREO,
        .outSampleFmt = AV_SAMPLE_FMT_S16,
        .outSampleRate = 44100,
    };

    device.readAndDecode(readParams);
}

void testReadPCMAndEncode()
{
    AudioDevice device;
    EncoderParam audioEncodeParam
    {
        .needEncode = true,
        .codecName = "libfdk_aac",
        .bitRate = 0,
        .profile = FF_PROFILE_AAC_HE_V2,
        .sampleFmt = AV_SAMPLE_FMT_S16,
        .channelLayout = AV_CH_LAYOUT_STEREO,
        .sampleRate = 44100,
        .byName = true,
    };

    CodecParam encoderParam =
    {
        .encodeParam = audioEncodeParam
    };

    ReampleParam swrCtxParam = 
    {
        .outChannelLayout = AV_CH_LAYOUT_STEREO,
        .outSampleFmt = AV_SAMPLE_FMT_S16,
        .outSampleRate = 44100,
        .inChannelLayout = AV_CH_LAYOUT_STEREO,
        .inSampleFmt = AV_SAMPLE_FMT_S16,
        .inSampleRate = 44100,
        .logOffset = 0,
        .logCtx = nullptr
    };

    ReadDeviceDataParam readParams
    {
        .inFilename = "/home/yeonon/learn/av/demo/build/out3.pcm",
        .outFilename = "out100.aac",
        .resampleParam = swrCtxParam,
        .codecParam = encoderParam
    };

    device.readAndEncode(readParams);
}

void testReadVideoDataFromFile()
{
    VideoDevice device("/home/yeonon/learn/av/demo/build/file_example_MP4_1920_18MG.mp4", DeviceType::ENCAPSULATE_FILE);

    ReadDeviceDataParam readParams
    {
        .outFilename = "./out0.yuv",
        .outWidth = 1920,
        .outHeight = 1080,
        .outPixFormat = AVPixelFormat::AV_PIX_FMT_YUV420P,
    };

    device.readAndDecode(readParams);
}

void testReadImageDataAndEncodeVideo()
{
    VideoDevice device;
    ReampleParam scaleParam
    {
        .inWidth = 1920,
        .inHeight = 1080,
        .inPixFmt = AVPixelFormat::AV_PIX_FMT_YUV420P,
        .outWidth = 1280,
        .outHeight = 720,
        .outPixFmt = AVPixelFormat::AV_PIX_FMT_YUV420P,
    };
    EncoderParam encodeParam
    {
        .needEncode = true,
        .codecName = "libx264",
        .bitRate = 600000,
        .profile = FF_PROFILE_H264_HIGH_444,
        .level = 50,
        .width = 1280,
        .height = 720,
        .gopSize = 250,
        .keyintMin = 50,
        .maxBFrame = 3,
        .hasBFrame = 1,
        .refs = 3,
        .pixFmt = AVPixelFormat::AV_PIX_FMT_YUV420P,
        .framerate = 15,
        .byName = true
    };
    CodecParam codecParam
    {
        .encodeParam = encodeParam,
    };

    ReadDeviceDataParam readParams
    {
        .inFilename = "out0.yuv",
        .outFilename = "out0.h264",
        .resampleParam = scaleParam,
        .codecParam = codecParam,
    };

    device.readAndEncode(readParams);
}
