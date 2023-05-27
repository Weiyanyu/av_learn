extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>

#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "../../utils/include/log.h"
#include "codec.h"
#include "device.h"
#include "frame.h"
#include "resample.h"

#include <fstream>
#include <iostream>

#include <sys/stat.h>
#include <unistd.h>

Device::Device()
    : m_deviceName("")
    , m_deviceType(DeviceType::PCM_FILE)
{ }

Device::Device(const std::string& deviceName, DeviceType deviceType)
    : m_deviceName(deviceName)
    , m_deviceType(deviceType)
{
    // get format
    AVInputFormat* inputFormat = nullptr;
    std::string    url         = deviceName;
    switch(m_deviceType)
    {
    case DeviceType::AUDIO:
        inputFormat = av_find_input_format("alsa");
        break;
    case DeviceType::VIDEO:
        inputFormat = av_find_input_format("Video4Linux2");
        break;
    case DeviceType::PCM_FILE:
    case DeviceType::ENCAPSULATE_FILE:
        inputFormat = nullptr;
        break;
    default:
        AV_LOG_E("unkown device type %d", (int)m_deviceType);
        break;
    }

    AVDictionary* options = nullptr;
    // open device
    if(auto ret = avformat_open_input(&m_fmtCtx, url.c_str(), inputFormat, &options); ret < 0)
    {
        char errors[1024];
        av_strerror(ret, errors, sizeof(errors));
        AV_LOG_E("Failed to open audio device(%s)\nerror:%s", m_deviceName.c_str(), errors);
        return;
    }
    else
    {
        AV_LOG_D("success to open audio device(%s)", m_deviceName.c_str());
    }

    if(deviceType == DeviceType::ENCAPSULATE_FILE)
    {
        if(avformat_find_stream_info(m_fmtCtx, NULL) < 0)
        {
            AV_LOG_E("can't read audio stream info");
            return;
        }
    }
}
Device::~Device()
{
    if(m_fmtCtx)
    {
        AV_LOG_D("release fmt ctx");
        avformat_close_input(&m_fmtCtx);
    }
}

// ---------------------------- Util Function ----------------------------

int Device::findStreamIdxByMediaType(int mediaType)
{
    if(!m_fmtCtx)
        return -1;
    int streamIdx = -1;
    for(size_t i = 0; i < m_fmtCtx->nb_streams; i++)
    {
        if(m_fmtCtx->streams[i]->codecpar->codec_type == static_cast<AVMediaType>(mediaType))
        {
            streamIdx = i;
            break;
        }
    }
    return streamIdx;
}

AVFormatContext* Device::getFmtCtx() const
{
    return m_fmtCtx;
}