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
    Frame();
    Frame(const FrameParam& initParam);
    // dsiable copy-ctor and move-ctor
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame) = delete;
    Frame(Frame&&) = delete;
    Frame& operator=(Frame&&) = delete;

    ~Frame();
public:
    bool isValid() const { return m_valid; }
    int32_t getLineSize(int idx) const;
    
    AVFrame* getAVFrame() const { return m_avFrame; }
    void writeAudioData(uint8_t** audioData, int32_t audioDataSize);
    
private:
    AVFrame* m_avFrame;
    bool m_valid;
};