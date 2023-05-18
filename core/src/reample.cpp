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

    m_tempData = static_cast<uint8_t*>(av_malloc(swrParam.fullOutputBufferSize * TEMP_BUFFER_RATIO));
    m_curOutputBufferSize = 0;
    m_fullOutputBufferSize = swrParam.fullOutputBufferSize;
    AV_LOG_D("tempBufferSize %d m_fullOutputBufferSize %d", swrParam.fullOutputBufferSize * TEMP_BUFFER_RATIO, m_fullOutputBufferSize);
}

SwrConvertor::~SwrConvertor()
{
    AV_LOG_D("release resource of SwrConvertor");
    if (m_swrCtx)
    {
        AV_LOG_D("release m_swrCtx");
        swr_free(&m_swrCtx);
    }

    if (m_tempData)
    {
        av_free(m_tempData);
    }
}

std::pair<uint8_t**, int> SwrConvertor::convert(uint8_t** srcData, int srcSize, uint8_t** dstData, int inSamples, int outSamples)
{
    if (!m_swrCtx) return {nullptr, 0};

    int nbSampleOutput = swr_convert(m_swrCtx,      //重采样的上下文
                dstData,                           //输出结果缓冲区
                outSamples,                        //每个通道的采样数
                (const uint8_t **)srcData,         //输入缓冲区
                inSamples);                        //输入单个通道的采样数

    if (nbSampleOutput < 0)
    {
        char errors[1024];
        av_strerror(nbSampleOutput, errors, sizeof(errors));
        AV_LOG_E("error:%s", errors);
        return {nullptr, 0};
    }
    int outBufferSize = nbSampleOutput * m_outChannel * m_outSampleSize;
    memcpy(m_tempData + m_curOutputBufferSize, dstData[0], outBufferSize);

    m_curOutputBufferSize += outBufferSize;

    int nbSampleRemain = 0;
    while ((nbSampleRemain = swr_convert(m_swrCtx, dstData, outSamples, NULL, 0)) > 0)
    {
        int remainOutputBufferSize = nbSampleRemain * m_outChannel * m_outSampleSize;
        memcpy(m_tempData + m_curOutputBufferSize, dstData[0], remainOutputBufferSize);
        m_curOutputBufferSize += remainOutputBufferSize;
    }

    if (m_curOutputBufferSize >= m_fullOutputBufferSize)
    {
        memcpy(dstData[0], m_tempData, m_fullOutputBufferSize);
        m_curOutputBufferSize -= m_fullOutputBufferSize;
        memcpy(m_tempData, m_tempData + m_fullOutputBufferSize, m_curOutputBufferSize);
        return {dstData, m_fullOutputBufferSize};
    }
    return {nullptr, 0};
}

std::pair<uint8_t**, int> SwrConvertor::flushRemain(uint8_t** dstData)
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
        memcpy(dstData[0], m_tempData, outBufferSize);
        m_curOutputBufferSize -= outBufferSize;
        if (m_curOutputBufferSize > 0)
            memcpy(m_tempData, m_tempData + outBufferSize, m_curOutputBufferSize);
        return {dstData, outBufferSize};
    }
    return {nullptr, 0}; 
}

int64_t SwrConvertor::calcNBSample(int inSampleRate, int inNBSample, int outSampleRate)
{
    if (!enable())
    {
        return -1;
    }
    return av_rescale_rnd(swr_get_delay(m_swrCtx, inSampleRate) + inNBSample,
                                    outSampleRate,inSampleRate,AV_ROUND_UP);
}