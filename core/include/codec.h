#pragma once

#include <cstdint>
#include <functional>
#include <ostream>
#include <string>

class AVCodecContext;
class AVCodec;
class AVPacket;
class Frame;
class AVCodecParameters;

using FrameReceiveCB  = std::function<void(Frame&)>;
using PacketReceiveCB = std::function<void(AVPacket*)>;

struct EncoderParam
{
    bool        needEncode = false;
    std::string codecName  = "";
    int         codecId    = -1;
    int64_t     bitRate    = -1;
    int         profile    = -1;

    // for audio
    int      sampleFmt     = -1;
    uint64_t channelLayout = 0;
    int      sampleRate    = -1;

    // for video
    // codec level
    int level   = 0;
    int width   = 0;
    int height  = 0;
    int gopSize = 0;
    // minimum interval of I Frame
    int keyintMin = 0;
    // max frame count of B frame(one gop)
    int maxBFrame = 0;
    // if has B frame, set 1
    int hasBFrame = 0;
    // reference frame count
    int refs = 0;
    // pix format
    int pixFmt = -1;
    // fps
    int framerate = 0;

    // control param
    // find encode by name
    bool byName = false;
    // find encode by codecId
    bool byId = false;
};

struct DecoderParam
{
    bool               needDecode = false;
    std::string        codecName  = "";
    int                codecId    = -1;
    AVCodecParameters* avCodecPar = nullptr;

    // control param
    // find encode by name
    bool byName = false;
    // find encode by codecId
    bool byId = false;
};

struct CodecParam
{
    EncoderParam encodeParam;
    DecoderParam decodeParam;
};

enum CodecMediaType
{
    CODEC_MEDIA_UNKNOW = -1,
    CODEC_MEDIA_AUDIO,
    CODEC_MEDIA_VIDEO,
};

class Codec
{
public:
    Codec(const CodecParam& initParam, CodecMediaType mediaType);
    // dsiable copy-ctor and move-ctor
    Codec(const Codec&) = delete;
    Codec& operator=(const Codec) = delete;
    Codec(Codec&&)                = delete;
    Codec& operator=(Codec&&) = delete;

    virtual ~Codec();

public:
    virtual bool encodeEnable() const
    {
        return m_encodeEnable;
    }
    virtual void encode(Frame& frame, AVPacket* pkt, PacketReceiveCB cb, bool isFlush = false);

    virtual bool decodeEnable() const
    {
        return m_decodeEnable;
    }
    virtual void decode(Frame& frame, AVPacket* pkt, FrameReceiveCB cb, bool isFlush = false);

public:
    // util func
    AVCodecContext* getCodecCtx(bool isEncode) const;
    AVCodec*        getCodec(bool isEncode) const;

    int         format(bool isEncode) const;
    int         frameSize(bool isEncode) const;
    uint64_t    channelLayout(bool isEncode) const;
    int         sampleRate(bool isEncode) const;
    const char* codecName(bool isEncode) const;

protected:
    // bool openDecoder(AVCodecContext* ctx, const std::string& name);
    // bool openDecoder(AVCodecContext* ctx, int codecId);
    bool checkSupport(AVCodec* codec, const CodecParam& initParam, bool isEncode);
    bool checkAudioSupport(AVCodec* codec, int format, uint64_t channelLayout, int64_t sampleRate);

private:
    AVCodecContext* m_encodeCodecCtx = nullptr;
    AVCodecContext* m_decodeCodecCtx = nullptr;
    AVCodec*        m_encodeCodec    = nullptr;
    AVCodec*        m_decodeCodec    = nullptr;

    bool m_encodeEnable = false;
    bool m_decodeEnable = false;

    CodecMediaType m_codecMediaType;
};

class AudioCodec : public Codec
{
public:
    AudioCodec(const CodecParam& initParam);
    // dsiable copy-ctor and move-ctor
    AudioCodec(const AudioCodec&) = delete;
    AudioCodec& operator=(const AudioCodec) = delete;
    AudioCodec(AudioCodec&&)                = delete;
    AudioCodec& operator=(AudioCodec&&) = delete;

    ~AudioCodec();
};

class VideoCodec : public Codec
{
public:
    VideoCodec(const CodecParam& initParam);
    // dsiable copy-ctor and move-ctor
    VideoCodec(const VideoCodec&) = delete;
    VideoCodec& operator=(const VideoCodec) = delete;
    VideoCodec(VideoCodec&&)                = delete;
    VideoCodec& operator=(VideoCodec&&) = delete;
    ~VideoCodec();

public:
    int width(bool isEncode) const;
    int height(bool isEncode) const;
    int pixFormat(bool isEncode) const;
};
