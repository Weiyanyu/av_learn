#pragma once

#include <cstdint>


struct FrameParam
{
    bool enable = false;
    int pktSize;
    uint64_t channelLayout;
    int format;
};

class AVFrame;
class Frame
{
public:
    Frame(const FrameParam& initParam);
    ~Frame();
public:
    AVFrame* getAVFrame() const { return m_avFrame; }
    void writeAudioData(uint8_t** audioData, int32_t audioDataSize);
private:
    AVFrame* m_avFrame;
};