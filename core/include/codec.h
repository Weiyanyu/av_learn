#pragma once

#include <cstdint>
#include <string>

class AVCodecContext;
class AVPacket;
class Frame;

struct AudioCodecParam
{
    bool needEncode = false;
    std::string codecName;
    int sampleFmt;
    uint64_t channelLayout;
    int sampleRate;
    int64_t bitRate;
    int profile;
};

class Codec
{
public:
    virtual ~Codec() { }
public:
    virtual bool enable() const = 0;
    virtual void encode(Frame& frame, AVPacket* pkt, std::ostream& os, bool isFlush = false) = 0;
};

class AudioCodec : public Codec
{
public:
    AudioCodec(const AudioCodecParam& initParam);
    // dsiable copy-ctor and move-ctor
    AudioCodec(const AudioCodec&) = delete;
    AudioCodec& operator=(const AudioCodec) = delete;
    AudioCodec(AudioCodec&&) = delete;
    AudioCodec& operator=(AudioCodec&&) = delete;

    ~AudioCodec();

    void encode(Frame& frame, AVPacket* pkt, std::ostream& os, bool isFlush = false) override;
    bool enable() const override { return m_enable; }
private:
    AVCodecContext* m_codecCtx = nullptr;
    bool m_enable = false;
};

class VideoCodec : public Codec
{
public:
    bool enable() const override { return m_enable; }
private:
    bool m_enable = false;
};
