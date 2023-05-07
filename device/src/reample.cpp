#include "resample.h"
#include "device.h"
#include <cmath>

extern "C"
{
#include <libswresample/swresample.h>
}



SwrConvertor::SwrConvertor(const SwrContextParam& swrParam, int packetSize)
{
    if (!swrParam.enable) return;

    // swr bufer param
    m_inChannel = av_get_channel_layout_nb_channels(swrParam.in_ch_layout);
    m_outChannel = av_get_channel_layout_nb_channels(swrParam.out_ch_layout);
    m_inSampleSize = av_get_bytes_per_sample(swrParam.in_sample_fmt);
    m_outSampleSize = av_get_bytes_per_sample(swrParam.out_sample_fmt);
    
    m_inSamples = std::ceil(packetSize / m_inChannel / m_inSampleSize);
    m_outSamples = std::ceil(packetSize / m_outChannel / m_outSampleSize);

    m_swrCtx = swr_alloc_set_opts(
        nullptr, 
        swrParam.out_ch_layout, 
        swrParam.out_sample_fmt, 
        swrParam.out_sample_rate, 
        swrParam.in_ch_layout, 
        swrParam.in_sample_fmt, 
        swrParam.in_sample_rate, 
        swrParam.log_offset, 
        swrParam.log_ctx
    );

    if (!m_swrCtx)
    {
        av_log(NULL, AV_LOG_ERROR, "failed to alloct swr context.");
        return;
    }

    if (swr_init(m_swrCtx) < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "failed to init swr context.");
        return;
    }


    int srcLineSize = 0;
    int dstLineSize = 0;

    int inAlgin = 0;
    int outAlgin = 0;
    if (m_inSamples < 32)
    {
        inAlgin = m_inSamples;
    }
    if (m_outSamples < 32)
    {
        outAlgin = m_outSamples;
    }
    av_samples_alloc_array_and_samples(&m_srcData, &srcLineSize, m_inChannel, m_inSamples, swrParam.in_sample_fmt, inAlgin);
    av_samples_alloc_array_and_samples(&m_dstData, &dstLineSize, m_outChannel, m_outSamples, swrParam.out_sample_fmt, outAlgin);
    
    av_log(NULL, AV_LOG_DEBUG, "src line size %d dstLineSize %dm_inSamples %d, m_outSamples %d\n ", srcLineSize, dstLineSize, m_inSamples, m_outSamples);
    m_enable = true;
}

SwrConvertor::~SwrConvertor()
{
    if (m_swrCtx)
    {
        av_log(NULL, AV_LOG_DEBUG, "release swr context\n");
        swr_free(&m_swrCtx);
    }
    if (m_srcData)
    {
        av_log(NULL, AV_LOG_DEBUG, "release m_srcData\n");
        av_freep(&m_srcData[0]);
        av_free(m_srcData);
    }
    if (m_dstData)
    {
        av_log(NULL, AV_LOG_DEBUG, "release m_dstData\n");
        av_freep(&m_dstData[0]);
        av_free(m_dstData);
    }
}

std::pair<uint8_t**, int> SwrConvertor::convert(uint8_t* data, int size)
{
    if (!m_swrCtx) return {nullptr, 0};
    memcpy(static_cast<void*>(m_srcData[0]), static_cast<void*>(data), size);
    int nbSampleOutput = swr_convert(m_swrCtx,       //重采样的上下文
                m_dstData,                           //输出结果缓冲区
                m_outSamples,                        //每个通道的采样数
                (const uint8_t **)m_srcData,         //输入缓冲区
                m_inSamples);                        //输入单个通道的采样数

    int outputSize = nbSampleOutput * m_outChannel * m_outSampleSize;
    av_log(NULL, AV_LOG_DEBUG, "resample outputSize %d\n", outputSize);
    return {m_dstData, outputSize};
}