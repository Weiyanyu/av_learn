#pragma once

#include <cstdint>
#include <utility>

extern "C"
{
#include <libavutil/samplefmt.h>
}

#define TEMP_BUFFER_RATIO 2
struct SwrContextParam
{
    int64_t             out_ch_layout;
    enum AVSampleFormat out_sample_fmt;
    int64_t             out_sample_rate;
    int64_t             in_ch_layout;
    enum AVSampleFormat in_sample_fmt;
    int64_t             in_sample_rate;
    int                 log_offset;
    void*               log_ctx;
    int                 fullOutputBufferSize = -1;
};

class SwrContext;
class SwrConvertor
{
public:
    SwrConvertor() = default;
    SwrConvertor(const SwrContextParam& swrParam);

    // dsiable copy-ctor and move-ctor
    SwrConvertor(const SwrConvertor&) = delete;
    SwrConvertor& operator=(const SwrConvertor) = delete;
    SwrConvertor(SwrConvertor&&)                = delete;
    SwrConvertor& operator=(SwrConvertor&&) = delete;

    ~SwrConvertor();

public:
    bool enable() const
    {
        return m_enable && m_swrCtx != nullptr;
    }
    std::pair<uint8_t**, int>
    convert(uint8_t** srcData, int srcSize, uint8_t** dstData, int inSamples, int outSamples);

    bool hasRemain() const
    {
        return m_curOutputBufferSize > 0;
    }
    std::pair<uint8_t**, int> flushRemain(uint8_t** dstData);

    int64_t calcNBSample(int inSampleRate, int inNBSample, int outSampleRate);

private:
    bool        m_enable   = false;
    SwrContext* m_swrCtx   = nullptr;
    uint8_t*    m_tempData = nullptr;

    // in/out param
    int             m_inChannel     = 0;
    int             m_outChannel    = 0;
    int             m_inSampleSize  = 0;
    int             m_outSampleSize = 0;
    SwrContextParam m_ctxParam;

    int m_srcLineSize = 0;
    int m_dstLineSize = 0;

    int m_curOutputBufferSize  = 0;
    int m_fullOutputBufferSize = 0;
};