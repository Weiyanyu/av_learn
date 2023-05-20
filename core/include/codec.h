#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <ostream>

class AVCodecContext;
class AVPacket;
class Frame;
class AVCodecParameters;

using FrameReceiveCB = std::function<void(Frame&)>;
using PacketReceiveCB = std::function<void(AVPacket*)>;
struct AudioEncoderParam
{
    bool needEncode = false;
    std::string codecName = "";
    int codecId = -1;
    int sampleFmt = -1;
    uint64_t channelLayout = 0;
    int sampleRate = -1;
    int64_t bitRate = -1;
    int profile = -1;
    
    // control param
    // find encode by name
    bool byName = false;
    // find encode by codecId
    bool byId = false;
};

struct AudiodecoderParam
{
    bool needDecode = false;
    std::string codecName = "";
    int codecId = -1;
    AVCodecParameters* avCodecPar = nullptr;

    // control param
    // find encode by name
    bool byName = false;
    // find encode by codecId
    bool byId = false;
};

struct AudioCodecParam
{
    AudioEncoderParam encodeParam;
    AudiodecoderParam decodeParam;
};

class Codec
{
public:
    virtual ~Codec() { }
public:
    virtual bool encodeEnable() const = 0;
    virtual void encode(Frame& frame, AVPacket* pkt, PacketReceiveCB cb, bool isFlush = false) = 0;

    virtual bool decodeEnable() const = 0;
    virtual void decode(Frame& frame, AVPacket* pkt, FrameReceiveCB cb, bool isFlush = false) = 0;
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

    void encode(Frame& frame, AVPacket* pkt, PacketReceiveCB cb, bool isFlush = false) override;
    bool encodeEnable() const override { return m_encodeEnable; }

    void decode(Frame& frame, AVPacket* pkt, FrameReceiveCB cb, bool isFlush = false);
    bool decodeEnable() const override { return m_decodeEnable; }


    AVCodecContext* getCodecCtx(bool isEncode);
private:
    AVCodecContext* m_encodeCodecCtx = nullptr;
    AVCodecContext* m_decodeCodecCtx = nullptr;

    bool m_encodeEnable = false;
    bool m_decodeEnable = false;
};

class VideoCodec : public Codec
{
public:
    bool encodeEnable() const override { return m_enable; }
private:
    bool m_enable = false;
};
