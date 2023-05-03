#if defined(__cplusplus)
extern "C"
{
#endif
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#if defined(__cplusplus)
}
#endif

#include "device.h"
#include <iostream>
#include <fstream>
#include <stdio.h>


Device::Device(const std::string& deviceName)
    :m_deviceName(deviceName)
{
    // get format
    AVInputFormat* audioInputFormat = av_find_input_format("alsa");
    AVDictionary* options = nullptr;
    // open audio device
    if (auto ret = avformat_open_input(&m_fmtCtx, m_deviceName.c_str(), audioInputFormat, &options); ret < 0)
    {
        char errors[1024];
        av_strerror(ret, errors, sizeof(errors));
        av_log(NULL, AV_LOG_ERROR, "Failed to open audio device(%s)\nerror:%s\n", m_deviceName.c_str(), errors);
        return;
    }
    else
    {
        av_log(NULL, AV_LOG_DEBUG, "success to open audio device(%s)\n", m_deviceName.c_str());
    }

}

Device::~Device()
{
    if (m_fmtCtx)
        avformat_close_input(&m_fmtCtx);
}

void Device::record(const std::string& outFilename)
{
    std::ofstream ofs(outFilename, std::ios::out);
    int recordCnt = 5000;
    AVPacket audioPacket;
    av_init_packet(&audioPacket);

    char *out = "audio.pcm";
    FILE *outfile = fopen(out, "wb+");

    // for resample
    SwrContext* swrCtx = nullptr;
    swrCtx = swr_alloc_set_opts(
        nullptr, 
        AV_CH_LAYOUT_STEREO, 
        AV_SAMPLE_FMT_S16, 
        44100, 
        AV_CH_LAYOUT_STEREO, 
        AV_SAMPLE_FMT_S16, 
        44100, 
        0, 
        nullptr
    );

    if (!swrCtx)
    {
        av_log(NULL, AV_LOG_ERROR, "failed to alloct swr context.");
        return;
    }

    if (swr_init(swrCtx) < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "failed to init swr context.");
        return;
    }

    uint8_t** srcData = nullptr;
    int srcLineSize = 0;
    // 64/2 = 32 32/2 = 16
    av_samples_alloc_array_and_samples(&srcData, &srcLineSize, 1, 16, AV_SAMPLE_FMT_S16, 0);

    uint8_t** dstData = nullptr;
    int dstLineSize = 0;
    av_samples_alloc_array_and_samples(&dstData, &dstLineSize, 1, 16, AV_SAMPLE_FMT_S16, 0);

    while (av_read_frame(m_fmtCtx, &audioPacket) == 0 && recordCnt > 0)
    {
        recordCnt--;

        memcpy(static_cast<void*>(srcData[0]), static_cast<void*>(audioPacket.data), audioPacket.size);

        int ret = swr_convert(swrCtx,                    //重采样的上下文
                    dstData,                   //输出结果缓冲区
                    16,                        //每个通道的采样数
                    (const uint8_t **)srcData, //输入缓冲区
                    16);                       //输入单个通道的采样数

        av_log(NULL, AV_LOG_DEBUG, "audio packet size : %d dstLineSize: %d srcLineSize %d\n ret %d\n", audioPacket.size, dstLineSize, srcLineSize, ret);

        ofs.write(reinterpret_cast<char*>(dstData[0]), dstLineSize);
        fwrite(dstData[0], 1, dstLineSize, outfile);
        fflush(outfile);
        av_packet_unref(&audioPacket);
    }

    if (srcData)
    {
        av_freep(&srcData[0]);
    }
    av_free(srcData);
    if (dstData)
    {
        av_freep(&dstData[0]);
    }
    av_free(dstData);

    swr_free(&swrCtx);
}