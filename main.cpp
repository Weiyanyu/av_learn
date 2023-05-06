
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

void initParam()
{
    avdevice_register_all();    
    av_log_set_level(AV_LOG_DEBUG);
}

int main()
{
    initParam();

    // create a device
    Device device("hw:0");

    SwrContextParam swrCtxParam = 
    {
        .enable = true,
        .out_ch_layout = AV_CH_LAYOUT_STEREO,
        .out_sample_fmt = AV_SAMPLE_FMT_S16,
        .out_sample_rate = 44100,
        .in_ch_layout = AV_CH_LAYOUT_STEREO,
        .in_sample_fmt = AV_SAMPLE_FMT_S16,
        .in_sample_rate = 48000,
        .log_offset = 0,
        .log_ctx = nullptr
    };
    // start record and save output file
    device.audioRecord("out.pcm", swrCtxParam);
    
    return 0;
}