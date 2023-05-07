extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}


#include "device.h"
#include "resample.h"

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
    {
        avformat_close_input(&m_fmtCtx);
    }
}

void Device::audioRecord(const std::string& outFilename, const SwrContextParam& swrParam)
{
    std::ofstream ofs(outFilename, std::ios::out);
    int recordCnt = 5000;
    AVPacket audioPacket;
    int audioPacketSize = 0;

    av_init_packet(&audioPacket);

    // pre-read a frame
    if (av_read_frame(m_fmtCtx, &audioPacket) >= 0)
    {
        audioPacketSize = audioPacket.size;
    }
    else
    {
        av_log(NULL, AV_LOG_ERROR, "can't read any frame");
        return;
    }

    SwrConvertor swrConvertor(swrParam, audioPacketSize);

    do
    {
        recordCnt--;

        if (swrConvertor.enable())
        {
            auto [outputData, outputSize] = swrConvertor.convert(audioPacket.data, audioPacket.size);
            ofs.write(reinterpret_cast<char*>(outputData[0]), outputSize);
        }
        else
        {
            ofs.write(reinterpret_cast<char*>(audioPacket.data), audioPacket.size);
            av_log(NULL, AV_LOG_DEBUG, "audio packet size: %d\n", audioPacket.size);
        }

        av_packet_unref(&audioPacket);
    } while (av_read_frame(m_fmtCtx, &audioPacket) == 0 && recordCnt > 0);
    
}