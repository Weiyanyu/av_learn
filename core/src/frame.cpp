#include "frame.h"
#include "../../utils/include/log.h"


extern "C"
{
#include <libavutil/frame.h>
#include <libavutil/channel_layout.h>
};

Frame::Frame()
    :m_avFrame(nullptr),
     m_valid(false)
{
    m_avFrame = av_frame_alloc();
    if (!m_avFrame)
    {
        AV_LOG_E("Fialed to alloc frame");
        return;
    }
    m_valid = true;
    AV_LOG_D("alloc a empty frame success");
}

Frame::Frame(const FrameParam& initParam)
    :m_avFrame(nullptr),
     m_valid(false)
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
    m_avFrame->nb_samples = std::ceil(initParam.frameSize / channels / sampleSize);
    m_avFrame->channel_layout = initParam.channelLayout;
    m_avFrame->format = initParam.format;

    if (m_avFrame->nb_samples < 32)
    {
        av_frame_get_buffer(m_avFrame, m_avFrame->nb_samples);
    }
    else
    {
        av_frame_get_buffer(m_avFrame, 0);
    }
    if (!m_avFrame->data[0])
    {
        av_frame_free(&m_avFrame);
        AV_LOG_E("Failed to alloc frame buffer");
        return;
    }
    m_valid = true;
    AV_LOG_D("frame nb_samples %d channle layout %ld fromat %d", m_avFrame->nb_samples, m_avFrame->channel_layout, m_avFrame->format);
}

Frame::~Frame()
{
    if (m_avFrame)
    {
        av_frame_free(&m_avFrame);
    }
}

bool Frame::writeAudioData(uint8_t** audioData, int32_t audioDataSize)
{
    if (!m_valid)
    {
        AV_LOG_W("frame is not valid!");
        return false;
    }
    if (m_avFrame->format == AV_SAMPLE_FMT_S16)
    {
        av_samples_fill_arrays(m_avFrame->data, m_avFrame->linesize,
                                audioData[0], m_avFrame->channels,
                                m_avFrame->nb_samples, (AVSampleFormat)m_avFrame->format, 0);
    }
    else
    {
        AV_LOG_E("don't support format %d yet", m_avFrame->format);
        return false;
    }

    return true;
}

int32_t Frame::getLineSize(int idx) const
{
    if (!m_valid) return -1;
    return m_avFrame->linesize[idx]; 
}

int Frame::format() const 
{ 
    if (!m_valid) return -1;
    return m_avFrame->format; 
}
int Frame::channels() const 
{ 
    if (!m_valid) return -1;
    return m_avFrame->channels; 
}
int Frame::nbSamples() const 
{ 
    if (!m_valid) return -1;
    return m_avFrame->nb_samples; 
}

