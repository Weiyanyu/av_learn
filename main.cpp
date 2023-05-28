
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
    testReadPCMAndEncode();
    // testReadVideoDataFromFile();
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
    device.readData("", "out.aac", swrCtxParam, encoderParam);
}

void testReadAudioFromFile()
{
    AudioDevice device("/home/yeonon/learn/av/demo/build/sample-6s.mp3", DeviceType::ENCAPSULATE_FILE);
    device.readAudioDataToPCM("out3.pcm", AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, 44100);
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

    device.readData("/home/yeonon/learn/av/demo/build/out3.pcm", "out100.aac", swrCtxParam, encoderParam);
}

void testReadVideoDataFromFile()
{
    VideoDevice device("/home/yeonon/learn/av/demo/build/sample-5s.mp4", DeviceType::ENCAPSULATE_FILE);
    device.readVideoDataToYUV("", "./outYuv", {}, 1280, 720, AVPixelFormat::AV_PIX_FMT_YUV420P);
}

void testReadImageDataAndEncodeVideo()
{
    VideoDevice device;

    // device.readData("", "", )
}
