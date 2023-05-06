#pragma once

#include <utility>
#include <cstdint>

class SwrContext;
class SwrContextParam;
class SwrConvertor
{
public:
    SwrConvertor() = default;
    SwrConvertor(const SwrContextParam& swrParam, int packetSize);


    // dsiable copy-ctor and move-ctor
    SwrConvertor(const SwrConvertor&) = delete;
    SwrConvertor& operator=(const SwrConvertor) = delete;
    SwrConvertor(SwrConvertor&&) = delete;
    SwrConvertor& operator=(SwrConvertor&&) = delete;

    ~SwrConvertor();

    std::pair<uint8_t**, int> convert(uint8_t* data, int size);

public:
    bool enable() const { return m_enable; }
private:
    bool m_enable = false;
    SwrContext* m_swrCtx = nullptr;

    uint8_t** m_srcData = nullptr;
    uint8_t** m_dstData = nullptr;

    // in/out param
    int m_inChannel = 0;
    int m_outChannel = 0;
    int m_inSampleSize = 0;
    int m_outSampleSize = 0;
    int m_inSamples = 0;
    int m_outSamples = 0;
};