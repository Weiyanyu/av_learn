#pragma once
#include <string>
#include <iostream>
#include <fstream>

struct AudioEncoderParam
{
    bool needEncode = false;
    std::string codecName;
    int sampleFmt;
    uint64_t channelLayout;
    int sampleRate;
    int64_t bitRate;
    int profile;
};

class AVCodecContext;
class AVPacket;
class Frame;

class AudioEncoder
{
public:
    AudioEncoder(const AudioEncoderParam& initParam);
    ~AudioEncoder();

    void encode(Frame& frame, AVPacket* pkt, std::ostream& os, bool isFlush = false);
    bool enable() const { return m_enable; }
private:
    AVCodecContext* m_codecCtx = nullptr;
    bool m_enable = false;
};