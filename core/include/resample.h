#pragma once

#include <cstdint>
#include <utility>

extern "C"
{
#include <libavutil/samplefmt.h>
}

#define TEMP_BUFFER_RATIO 2
struct ReampleParam
{
    // for audio
    int64_t             outChannelLayout;
    enum AVSampleFormat outSampleFmt;
    int64_t             outSampleRate;
    int64_t             inChannelLayout;
    enum AVSampleFormat inSampleFmt;
    int64_t             inSampleRate;
    int                 logOffset;
    void*               logCtx;
    int                 fullOutputBufferSize = -1;

    // for video
    int inWidth  = 0;
    int inHeight = 0;
    int inPixFmt = -1;

    int outWidth  = 0;
    int outHeight = 0;
    int outPixFmt = -1;
};

class SwrContext;
class SwrConvertor
{
public:
    SwrConvertor() = default;
    SwrConvertor(const ReampleParam& resampleParam);

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
    int          m_inChannel     = 0;
    int          m_outChannel    = 0;
    int          m_inSampleSize  = 0;
    int          m_outSampleSize = 0;
    ReampleParam m_ctxParam;

    int m_srcLineSize = 0;
    int m_dstLineSize = 0;

    int m_curOutputBufferSize  = 0;
    int m_fullOutputBufferSize = 0;
};