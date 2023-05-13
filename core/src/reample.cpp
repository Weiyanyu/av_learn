#include "resample.h"
#include "device.h"
#include "../../utils/include/log.h"
#include <cmath>

extern "C"
{
#include <libswresample/swresample.h>
}



SwrConvertor::SwrConvertor(const SwrContextParam& swrParam, int packetSize)
    :m_enable(false),
     m_ctxParam(swrParam)
{
    if (swrParam.in_ch_layout != swrParam.out_ch_layout
        || swrParam.in_sample_fmt != swrParam.out_sample_fmt
        || swrParam.in_sample_rate != swrParam.out_sample_rate)
    {
        m_enable = true;
    }
    if (!m_enable) 
    {
        AV_LOG_D("don't need swr convetor");
        return;
    }

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
        AV_LOG_E("failed to alloct swr context.");
        return;
    }

    if (swr_init(m_swrCtx) < 0)
    {
        AV_LOG_E("failed to init swr context.");
        return;
    }

    int tempLineSize = 0;
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
    av_samples_alloc_array_and_samples(&m_srcData, &m_srcLineSize, m_inChannel, m_inSamples, swrParam.in_sample_fmt, inAlgin);
    av_samples_alloc_array_and_samples(&m_dstData, &m_dstLineSize, m_outChannel, m_outSamples, swrParam.out_sample_fmt, outAlgin);
    av_samples_alloc_array_and_samples(&m_tempData, &tempLineSize, m_outChannel, m_outSamples * 4, swrParam.out_sample_fmt, 0);
    
    m_curOutputBufferSize = 0;
    m_fullOutputBufferSize = av_samples_get_buffer_size(NULL, m_outChannel, m_outSamples, swrParam.out_sample_fmt, outAlgin);
    AV_LOG_D("src line size %d dstLineSize %d m_inSamples %d, m_outSamples %d tempLineSize %d m_fullOutputBufferSize %d", m_srcLineSize, m_dstLineSize, m_inSamples, m_outSamples, tempLineSize, m_fullOutputBufferSize);

}

SwrConvertor::~SwrConvertor()
{
    AV_LOG_D("release resource of SwrConvertor");
    if (m_swrCtx)
    {
        swr_free(&m_swrCtx);
    }
    if (m_srcData)
    {
        av_freep(&m_srcData[0]);
        av_free(m_srcData);
    }
    if (m_dstData)
    {
        av_freep(&m_dstData[0]);
        av_free(m_dstData);
    }

    if (m_tempData)
    {
        av_freep(&m_tempData[0]);
        av_free(m_tempData);
    }
}

std::pair<uint8_t**, int> SwrConvertor::convert(uint8_t* data, int size)
{
    if (!m_swrCtx) return {nullptr, 0};
    memcpy(static_cast<void*>(m_srcData[0]), static_cast<void*>(data), size);

    // create temp data, receive swrConvetr data
    uint8_t** newData = nullptr;
    int linesize = 0;
    if (m_outSamples < 32)
    {
        av_samples_alloc_array_and_samples(&newData, &linesize, m_outChannel, m_outSamples, m_ctxParam.out_sample_fmt, m_outSamples);
    }
    else
    {
        av_samples_alloc_array_and_samples(&newData, &linesize, m_outChannel, m_outSamples, m_ctxParam.out_sample_fmt, 0);
    }
    if (!newData || !newData[0])
    {
        AV_LOG_E("alloc newData error");
        return {nullptr, 0};
    }

    int nbSampleOutput = swr_convert(m_swrCtx,       //重采样的上下文
                newData,                           //输出结果缓冲区
                m_outSamples,                        //每个通道的采样数
                (const uint8_t **)m_srcData,         //输入缓冲区
                m_inSamples);                        //输入单个通道的采样数


    if (nbSampleOutput < 0)
    {
        char errors[1024];
        av_strerror(nbSampleOutput, errors, sizeof(errors));
        AV_LOG_E("error:%s", errors);
        return {nullptr, 0};
    }
    int outBufferSize = nbSampleOutput * m_outChannel * m_outSampleSize;
    // AV_LOG_D("m_outChannel %d outBufferSize %d nbSampleOutput %d m_inSamples %d m_outSamples %d linesize %d", m_outChannel, outBufferSize, nbSampleOutput, m_inSamples, m_outSamples, linesize);

    memcpy((void**)(m_tempData[0] + m_curOutputBufferSize), newData[0], outBufferSize);
    m_curOutputBufferSize += outBufferSize;

    int nbSampleRemain = 0;
    while ((nbSampleRemain = swr_convert(m_swrCtx, newData, m_outSamples, NULL, 0)) > 0)
    {
        
        int remainOutputBufferSize = nbSampleRemain * m_outChannel * m_outSampleSize;
        AV_LOG_D("nbSampleRemain %d remain buffer sieze %d", nbSampleRemain, remainOutputBufferSize);
        memcpy((void**)(m_tempData[0] + m_curOutputBufferSize), newData[0], remainOutputBufferSize);
        m_curOutputBufferSize += remainOutputBufferSize;
    }

    if (newData)
    {
        av_freep(&newData[0]);
        av_free(newData);
    }

    if (m_curOutputBufferSize >= m_fullOutputBufferSize)
    {
        memcpy((void**)(m_dstData[0]), m_tempData[0], m_fullOutputBufferSize);
        m_curOutputBufferSize -= m_fullOutputBufferSize;
        memcpy((void**)(m_tempData[0]), m_tempData[0] + m_fullOutputBufferSize, m_curOutputBufferSize);
        return {m_dstData, m_fullOutputBufferSize};
    }
    return {nullptr, 0};
}

std::pair<uint8_t**, int> SwrConvertor::flushRemain()
{
    if (m_curOutputBufferSize > 0)
    {
        int outBufferSize = 0;
        if (m_curOutputBufferSize >= m_fullOutputBufferSize)
        {
            outBufferSize = m_fullOutputBufferSize;
        }
        else
        {
            outBufferSize = m_curOutputBufferSize;
        }
        memcpy((void**)(m_dstData[0]), m_tempData[0], outBufferSize);
        m_curOutputBufferSize -= outBufferSize;
        if (m_curOutputBufferSize > 0)
            memcpy((void**)(m_tempData[0]), m_tempData[0] + outBufferSize, m_curOutputBufferSize);
        return {m_dstData, outBufferSize};
    }
    return {nullptr, 0}; 
}