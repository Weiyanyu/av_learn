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

#include "device.h"
#include <iostream>
#include <fstream>

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
    avformat_close_input(&m_fmtCtx);
}

void Device::record(const std::string& outFilename)
{
    std::ofstream ofs(outFilename, std::ios::out);
    int recordCnt = 5000;
    AVPacket audioPacket;
    av_init_packet(&audioPacket);
    int ret = 0;
    ret = av_read_frame(m_fmtCtx, &audioPacket);
    av_log(NULL, AV_LOG_DEBUG, "ret is %d", ret);

    while (av_read_frame(m_fmtCtx, &audioPacket) == 0 && recordCnt > 0)
    {
        av_log(NULL, AV_LOG_DEBUG, "audeo packet size : %d\n", audioPacket.size);
        recordCnt--;
        ofs.write(reinterpret_cast<char*>(audioPacket.data), audioPacket.size);
        av_packet_unref(&audioPacket);
    }
}