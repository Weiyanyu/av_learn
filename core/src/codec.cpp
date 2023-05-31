#include "codec.h"
#include "../../utils/include/log.h"
#include "frame.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

#include <ostream>

// -------------------------- Base Codec --------------------------
Codec::Codec(const CodecParam& initParam, MediaType mediaType)
    : m_encodeCodecCtx(nullptr)
    , m_decodeCodecCtx(nullptr)
    , m_encodeEnable(false)
    , m_decodeEnable(false)
    , m_codecMediaType(mediaType)
{
    if(initParam.encodeParam.needEncode)
    {
        AVCodec* encodeCodec = nullptr;
        if(initParam.encodeParam.byName)
        {
            encodeCodec = avcodec_find_encoder_by_name(initParam.encodeParam.codecName.c_str());
        }
        else if(initParam.encodeParam.byId)
        {
            encodeCodec =
                avcodec_find_encoder(static_cast<AVCodecID>(initParam.encodeParam.codecId));
        }
        else
        {
            AV_LOG_E("you must set find encode by codec name or codec id");
            return;
        }

        if(checkSupport(encodeCodec, initParam, true))
        {
            m_encodeCodecCtx = avcodec_alloc_context3(encodeCodec);
            if(m_encodeCodecCtx == nullptr)
            {
                AV_LOG_E("Failed to alloc AVCodecContext with codec(%s)",
                         initParam.encodeParam.codecName.c_str());
                return;
            }
            if(m_codecMediaType == MediaType::MEDIA_AUDIO)
            {
                // set for audio
                m_encodeCodecCtx->sample_fmt     = (AVSampleFormat)initParam.encodeParam.sampleFmt;
                m_encodeCodecCtx->channel_layout = initParam.encodeParam.channelLayout;
                m_encodeCodecCtx->channels =
                    av_get_channel_layout_nb_channels(m_encodeCodecCtx->channel_layout);
                m_encodeCodecCtx->sample_rate = initParam.encodeParam.sampleRate;
                m_encodeCodecCtx->bit_rate    = initParam.encodeParam.bitRate;
                m_encodeCodecCtx->profile     = initParam.encodeParam.profile;
            }
            else if(m_codecMediaType == MediaType::MEDIA_VIDEO)
            {
                // set for video
                // SPS&PPS
                m_encodeCodecCtx->profile = initParam.encodeParam.profile;
                m_encodeCodecCtx->level   = initParam.encodeParam.level;

                m_encodeCodecCtx->width    = initParam.encodeParam.width;
                m_encodeCodecCtx->height   = initParam.encodeParam.height;
                m_encodeCodecCtx->pix_fmt  = (AVPixelFormat)initParam.encodeParam.pixFmt;
                m_encodeCodecCtx->gop_size = initParam.encodeParam.gopSize;
                // 最小多少帧插入一个I帧
                m_encodeCodecCtx->keyint_min   = initParam.encodeParam.keyintMin; //option
                m_encodeCodecCtx->max_b_frames = initParam.encodeParam.maxBFrame; //option
                m_encodeCodecCtx->has_b_frames = initParam.encodeParam.hasBFrame; //option
                // ref frame count(max)
                m_encodeCodecCtx->refs = initParam.encodeParam.refs; //option
                // input yuv format
                m_encodeCodecCtx->bit_rate = initParam.encodeParam.bitRate; //kbps
                // fps
                m_encodeCodecCtx->framerate = AVRational{initParam.encodeParam.framerate, 1};
                // frame duration
                m_encodeCodecCtx->time_base =
                    AVRational{m_encodeCodecCtx->framerate.den, m_encodeCodecCtx->framerate.num};
            }

            if(int ret = avcodec_open2(m_encodeCodecCtx, encodeCodec, NULL); ret < 0)
            {
                char errors[1024];
                av_strerror(ret, errors, sizeof(errors));
                AV_LOG_E("Failed to open codec (%s)\nerror:%s",
                         initParam.encodeParam.codecName.c_str(),
                         errors);
                return;
            }

            m_encodeCodec  = encodeCodec;
            m_encodeEnable = true;
            AV_LOG_D("init encode success");
        }
    }

    // decoder
    if(initParam.decodeParam.needDecode)
    {
        AVCodec* decodeCodec = nullptr;
        if(initParam.decodeParam.byName)
        {
            decodeCodec = avcodec_find_decoder_by_name(initParam.decodeParam.codecName.c_str());
        }
        else if(initParam.decodeParam.byId)
        {
            decodeCodec =
                avcodec_find_decoder(static_cast<AVCodecID>(initParam.decodeParam.codecId));
        }
        else
        {
            AV_LOG_E("you must set find decode by codec name or codec id");
            return;
        }
        if(!decodeCodec)
        {
            AV_LOG_E("can't find video decode %d", initParam.decodeParam.codecId);
            return;
        }

        if(checkSupport(decodeCodec, initParam, false))
        {
            m_decodeCodecCtx = avcodec_alloc_context3(decodeCodec);
            if(!m_decodeCodecCtx)
            {
                AV_LOG_E("faild to alloc codec context");
                return;
            }
            if(avcodec_parameters_to_context(m_decodeCodecCtx, initParam.decodeParam.avCodecPar) <
               0)
            {
                AV_LOG_E("faild to replace context");
                return;
            }

            if(avcodec_open2(m_decodeCodecCtx, decodeCodec, nullptr) < 0)
            {
                AV_LOG_E("faild to open decode %s", decodeCodec->name);
                return;
            }

            m_decodeCodec  = decodeCodec;
            m_decodeEnable = true;
            AV_LOG_D("init decode success");
        }
    }
}

Codec::~Codec()
{
    if(m_encodeCodecCtx)
    {
        avcodec_close(m_encodeCodecCtx);
        AV_LOG_D("release encode context");
    }

    if(m_decodeCodecCtx)
    {
        avcodec_close(m_decodeCodecCtx);
        AV_LOG_D("release decode context");
    }
}

void Codec::encode(Frame& frame, AVPacket* pkt, PacketReceiveCB cb, bool isFlush)
{
    if(!m_encodeEnable)
        return;
    int res = 0;
    if(!isFlush)
    {
        AVFrame* avFrame = frame.getAVFrame();
        res              = avcodec_send_frame(m_encodeCodecCtx, avFrame);
    }
    else
    {
        res = avcodec_send_frame(m_encodeCodecCtx, nullptr);
    }

    while(res >= 0)
    {
        res = avcodec_receive_packet(m_encodeCodecCtx, pkt);
        if(res == AVERROR(EAGAIN) || res == AVERROR_EOF)
        {
            break;
        }
        else if(res < 0)
        {
            AV_LOG_E("legitimate encoding errors");
            return;
        }
        cb(pkt);
    }
}

void Codec::decode(Frame& frame, AVPacket* pkt, FrameReceiveCB cb, bool isFlush)
{
    int res = -1;
    if(isFlush)
    {
        res = avcodec_send_packet(m_decodeCodecCtx, nullptr);
    }
    else
    {
        res = avcodec_send_packet(m_decodeCodecCtx, pkt);
    }
    while(res >= 0)
    {
        res = avcodec_receive_frame(m_decodeCodecCtx, frame.getAVFrame());
        if(res == AVERROR(EAGAIN) || res == AVERROR_EOF)
        {
            break;
        }
        else if(res < 0)
        {
            AV_LOG_E("legitimate decoding errors");
            return;
        }
        cb(frame);
    }
}

bool Codec::checkSupport(AVCodec* codec, const CodecParam& initParam, bool isEncode)
{
    bool isSupport = false;
    if(m_codecMediaType == MediaType::MEDIA_AUDIO)
    {
        if(isEncode)
        {
            isSupport = checkAudioSupport(codec,
                                          initParam.encodeParam.sampleFmt,
                                          initParam.encodeParam.channelLayout,
                                          initParam.encodeParam.sampleRate);
        }
        else
        {
            isSupport = checkAudioSupport(codec,
                                          initParam.decodeParam.avCodecPar->format,
                                          initParam.decodeParam.avCodecPar->channel_layout,
                                          initParam.decodeParam.avCodecPar->sample_rate);
        }
    }
    else if(m_codecMediaType == MediaType::MEDIA_VIDEO)
    {
        isSupport = true;
    }
    else
    {
        AV_LOG_E("don't know media type %d", m_codecMediaType);
    }

    return isSupport;
}

bool Codec::checkAudioSupport(AVCodec* codec,
                              int      format,
                              uint64_t channelLayout,
                              int64_t  sampleRate)
{
    int                   isFind     = false;
    const AVSampleFormat* supportFmt = codec->sample_fmts;
    if(supportFmt == nullptr)
    {
        AV_LOG_D("codec don't set support format, just skip check");
        isFind = true;
    }
    else
    {
        while(supportFmt && *supportFmt != AV_SAMPLE_FMT_NONE)
        {
            if(*supportFmt == format)
            {
                isFind = true;
                break;
            }
            supportFmt++;
        }
    }

    if(!isFind)
    {
        AV_LOG_E("codec %s don't support format %d", codec->name, format);
        return false;
    }
    isFind = false;

    const uint64_t* supportChannelLayout = codec->channel_layouts;
    if(supportChannelLayout == nullptr)
    {
        AV_LOG_D("codec don't set support channel layout, just skip check");
        isFind = true;
    }
    else
    {
        while(supportChannelLayout && *supportChannelLayout != 0)
        {
            if(*supportChannelLayout == channelLayout)
            {
                isFind = true;
                break;
            }
            supportChannelLayout++;
        }
    }

    if(!isFind)
    {
        AV_LOG_E("codec %s don't support channel layout %ld", codec->name, channelLayout);
        return false;
    }
    isFind = false;

    const int* supportSampleRate = codec->supported_samplerates;
    if(supportSampleRate == nullptr)
    {
        AV_LOG_D("codec don't set support sample rate, just skip check");
        isFind = true;
    }
    else
    {
        while(supportSampleRate && *supportSampleRate != 0)
        {
            if(*supportSampleRate == sampleRate)
            {
                isFind = true;
                break;
            }
            supportSampleRate++;
        }
    }

    if(!isFind)
    {
        AV_LOG_E("codec %s don't support sample rate %ld", codec->name, sampleRate);
        return false;
    }

    return true;
}

// -------------------------- Codec utils function --------------------------
AVCodecContext* Codec::getCodecCtx(bool isEncode) const
{
    if(isEncode)
    {
        return m_encodeCodecCtx;
    }
    return m_decodeCodecCtx;
}

AVCodec* Codec::getCodec(bool isEncode) const
{
    if(isEncode)
    {
        return m_encodeCodec;
    }
    return m_decodeCodec;
}

int Codec::format(bool isEncode) const
{
    auto* ctx = getCodecCtx(isEncode);
    if(ctx == nullptr)
        return -1;
    if(m_codecMediaType == MediaType::MEDIA_AUDIO)
    {
        return ctx->sample_fmt;
    }
    else if(m_codecMediaType == MediaType::MEDIA_VIDEO)
    {
        return ctx->pix_fmt;
    }
    return -1;
}

int Codec::frameSize(bool isEncode) const
{
    auto* ctx = getCodecCtx(isEncode);
    if(ctx == nullptr)
        return -1;
    return ctx->frame_size;
}

uint64_t Codec::channelLayout(bool isEncode) const
{
    auto* ctx = getCodecCtx(isEncode);
    if(ctx == nullptr)
        return 0;
    return ctx->channel_layout;
}

int Codec::sampleRate(bool isEncode) const
{
    auto* ctx = getCodecCtx(isEncode);
    if(ctx == nullptr)
        return -1;
    return ctx->sample_rate;
}

const char* Codec::codecName(bool isEncode) const
{
    auto* codec = getCodec(isEncode);
    if(codec == nullptr)
        return nullptr;
    return codec->name;
}

// -------------------------- Audio Codec --------------------------

AudioCodec::AudioCodec(const CodecParam& initParam)
    : Codec(initParam, MediaType::MEDIA_AUDIO)
{
    if(encodeEnable())
    {
        AV_LOG_D("[encode] channel layout %ld", channelLayout(true));
        AV_LOG_D("[encode] channel %d", av_get_channel_layout_nb_channels(channelLayout(true)));
        AV_LOG_D("[encode] format %d", format(true));
        AV_LOG_D("[encode] codec name %s", codecName(true));
        AV_LOG_D("[encode] sample rate %d", sampleRate(true));
    }

    if(decodeEnable())
    {
        AV_LOG_D("[decode] channel layout %ld", channelLayout(false));
        AV_LOG_D("[decode] channel %d", av_get_channel_layout_nb_channels(channelLayout(false)));
        AV_LOG_D("[decode] format %d", format(false));
        AV_LOG_D("[decode] codec name %s", codecName(false));
        AV_LOG_D("[decode] sample rate %d", sampleRate(false));
    }
}

AudioCodec::~AudioCodec() { }

// -------------------------- Video Codec --------------------------

VideoCodec::VideoCodec(const CodecParam& initParam)
    : Codec(initParam, MediaType::MEDIA_VIDEO)
{

    if(encodeEnable())
    {
        AV_LOG_D("[encode] video w/h %d/%d", width(true), height(true));
        AV_LOG_D("[encode] video codec name %s", codecName(true));
        AV_LOG_D("[encode] video AVPixelFormat %d", pixFormat(true));
    }

    if(decodeEnable())
    {
        AV_LOG_D("[decode] video w/h %d/%d", width(false), height(false));
        AV_LOG_D("[decode] video codec name %s", codecName(false));
        AV_LOG_D("[decode] video AVPixelFormat %d", pixFormat(false));
    }
}

VideoCodec::~VideoCodec() { }

int VideoCodec::width(bool isEncode) const
{
    auto* ctx = getCodecCtx(isEncode);
    if(ctx == nullptr)
        return -1;
    return ctx->width;
}

int VideoCodec::height(bool isEncode) const
{
    auto* ctx = getCodecCtx(isEncode);
    if(ctx == nullptr)
        return -1;
    return ctx->height;
}

int VideoCodec::pixFormat(bool isEncode) const
{
    auto* ctx = getCodecCtx(isEncode);
    if(ctx == nullptr)
        return -1;
    return ctx->pix_fmt;
}
