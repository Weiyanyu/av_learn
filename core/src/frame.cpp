#include "frame.h"
#include "../../utils/include/log.h"


extern "C"
{
#include <libavutil/frame.h>
#include <libavutil/channel_layout.h>
};


Frame::Frame(const FrameParam& initParam)
    :m_avFrame(nullptr)
{
    if (!initParam.enable)
    {
        AV_LOG_D("don't need create frame");
        return;
    }
    m_avFrame = av_frame_alloc();
    if (!m_avFrame)
    {
        AV_LOG_E("Fialed to alloc frame");
        return;
    }
    
    int channels = av_get_channel_layout_nb_channels(initParam.channelLayout);
    int sampleSize = av_get_bytes_per_sample((AVSampleFormat)initParam.format);
    m_avFrame->nb_samples = std::ceil(initParam.pktSize / channels / sampleSize);
    m_avFrame->channel_layout = initParam.channelLayout;
    m_avFrame->format = initParam.format;

    av_frame_get_buffer(m_avFrame, 0);
    if (!m_avFrame->data[0])
    {
        av_frame_free(&m_avFrame);
        AV_LOG_E("Failed to alloc frame buffer");
        return;
    }

    AV_LOG_D("frame nb_samples %d channle layout %d fromat %d", m_avFrame->nb_samples, m_avFrame->channel_layout, m_avFrame->format);
}

Frame::~Frame()
{
    if (m_avFrame)
    {
        av_frame_free(&m_avFrame);
    }
}

void Frame::writeAudioData(uint8_t** audioData, int32_t audioDataSize)
{
    memcpy((void*)m_avFrame->data[0], audioData[0], audioDataSize);
}

