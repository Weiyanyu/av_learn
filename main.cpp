
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
void testReadPCMAndEncode();
void testReadVideoDataFromFile();

int main()
{
    initParam();
    // testReadAudioFromDevice();
    // testReadAudioFromFile();
    // testReadPCMAndEncode();
    testReadVideoDataFromFile();
    return 0;
}

void testReadAudioFromDevice()
{
    // create a device
    Device device("hw:0", DeviceType::AUDIO);

    SwrContextParam swrCtxParam = 
    {
        .out_ch_layout = AV_CH_LAYOUT_STEREO,
        .out_sample_fmt = AV_SAMPLE_FMT_S16,
        .out_sample_rate = 44100,
        .in_ch_layout = AV_CH_LAYOUT_STEREO,
        .in_sample_fmt = AV_SAMPLE_FMT_S16,
        .in_sample_rate = 48000,
        .log_offset = 0,
        .log_ctx = nullptr
    };

    EncoderParam audioEncodeParam
    {
        .needEncode = true,
        .codecName = "libfdk_aac",
        .sampleFmt = AV_SAMPLE_FMT_S16,
        .channelLayout = AV_CH_LAYOUT_STEREO,
        .sampleRate = 44100,
        .bitRate = 0,
        .profile = FF_PROFILE_AAC_HE_V2,
        .byName = true,
    };

    CodecParam encoderParam =
    {
        .encodeParam = audioEncodeParam
    };
    // start record and save output file
    device.readAudio("", "out.aac", swrCtxParam, encoderParam);
}

void testReadAudioFromFile()
{
    Device device("/home/yeonon/learn/av/demo/build/sample-6s.mp3", DeviceType::ENCAPSULATE_FILE);
    device.readAudioDataToPCM("out3.pcm", AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, 44100);
}

void testReadPCMAndEncode()
{
    Device device("/home/yeonon/learn/av/demo/build/sample-6s.mp3", DeviceType::ENCAPSULATE_FILE);
    EncoderParam audioEncodeParam
    {
        .needEncode = true,
        .codecName = "libfdk_aac",
        .sampleFmt = AV_SAMPLE_FMT_S16,
        .channelLayout = AV_CH_LAYOUT_STEREO,
        .sampleRate = 44100,
        .bitRate = 0,
        .profile = FF_PROFILE_AAC_HE_V2,
        .byName = true,
    };

    CodecParam encoderParam =
    {
        .encodeParam = audioEncodeParam
    };

    SwrContextParam swrCtxParam = 
    {
        .out_ch_layout = AV_CH_LAYOUT_STEREO,
        .out_sample_fmt = AV_SAMPLE_FMT_S16,
        .out_sample_rate = 44100,
        .in_ch_layout = AV_CH_LAYOUT_STEREO,
        .in_sample_fmt = AV_SAMPLE_FMT_S16,
        .in_sample_rate = 44100,
        .log_offset = 0,
        .log_ctx = nullptr
    };

    device.readAudio("/home/yeonon/learn/av/demo/build/out3.pcm", "out100.aac", swrCtxParam, encoderParam);
}

void testReadVideoDataFromFile()
{
    Device device("/home/yeonon/learn/av/demo/build/sample-5s.mp4", DeviceType::ENCAPSULATE_FILE);
    device.readVideoDataToYUV("", "./outYuv", {}, 1280, 720, AVPixelFormat::AV_PIX_FMT_YUV420P);
}
