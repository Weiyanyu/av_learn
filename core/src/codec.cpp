#include "codec.h"
#include "../../utils/include/log.h"
#include "frame.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

#include <ostream>


AudioCodec::AudioCodec(const AudioCodecParam& initParam)
    :m_codecCtx(nullptr),
     m_enable(false)
{
    if (!initParam.needEncode) 
    {
        AV_LOG_D("don't need encode");
        return;
    }
    AVCodec* codec = avcodec_find_encoder_by_name(initParam.codecName.c_str());
    m_codecCtx = avcodec_alloc_context3(codec);
    if (m_codecCtx == nullptr)
    { 
        AV_LOG_E("Failed to alloc AVCodecContext with codec(%s)", initParam.codecName.c_str());
        return;
    }

    m_codecCtx->sample_fmt = (AVSampleFormat)initParam.sampleFmt;
    m_codecCtx->channel_layout = initParam.channelLayout;
    m_codecCtx->channels = av_get_channel_layout_nb_channels(m_codecCtx->channel_layout);
    m_codecCtx->sample_rate = initParam.sampleRate;
    m_codecCtx->bit_rate = initParam.bitRate;
    m_codecCtx->profile = initParam.profile;

    if (int ret = avcodec_open2(m_codecCtx, codec, NULL); ret < 0)
    {
        char errors[1024];
        av_strerror(ret, errors, sizeof(errors));
        AV_LOG_E("Failed to open codec (%s)\nerror:%s", initParam.codecName.c_str(), errors);
        return;
    }

    m_enable = true;
}

AudioCodec::~AudioCodec()
{
    if (m_codecCtx)
    {
        avcodec_close(m_codecCtx);
    }
}

void AudioCodec::encode(Frame& frame, AVPacket* pkt, std::ostream& os, bool isFlush)
{
    if (!m_enable) return;
    int res = 0;
    if (!isFlush)
    {
        AVFrame* avFrame = frame.getAVFrame();
        res = avcodec_send_frame(m_codecCtx, avFrame);
    }
    else
    {
        res = avcodec_send_frame(m_codecCtx, nullptr);
    }

    while (res >= 0)
    {                
        res = avcodec_receive_packet(m_codecCtx, pkt);
        if (res == AVERROR(EAGAIN) || res == AVERROR_EOF)
        {
            break;
        }
        else if (res < 0)
        {
            AV_LOG_E("legitimate encoding errors");
            return;
        }

        os.write(reinterpret_cast<char*>(pkt->data), pkt->size);
    }
}