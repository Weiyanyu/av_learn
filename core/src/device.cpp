extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}


#include "device.h"
#include "resample.h"
#include "../../utils/include/log.h"

#include <iostream>
#include <fstream>
#include <stdio.h>


Device::Device(const std::string& deviceName)
    :m_deviceName(deviceName)
{
    // get format
    AVInputFormat* audioInputFormat = av_find_input_format("alsa");
    AVDictionary* options = nullptr;
    // open audio device
    if (auto ret = avformat_open_input(&m_fmtCtx, m_deviceName.c_str(), audioInputFormat, &options); ret < 0)
    {
        char errors[1024];
        av_strerror(ret, errors, sizeof(errors));
        AV_LOG_E("Failed to open audio device(%s)\nerror:%s", m_deviceName.c_str(), errors);
        return;
    }
    else
    {
        AV_LOG_D("success to open audio device(%s)", m_deviceName.c_str());
    }

}
Device::~Device()
{
    if (m_fmtCtx)
    {
        avformat_close_input(&m_fmtCtx);
    }
}

void Device::audioRecord(const std::string& outFilename, const SwrContextParam& swrParam)
{
    std::ofstream ofs(outFilename, std::ios::out);
    int recordCnt = 5000;
    AVPacket audioPacket;
    int audioPacketSize = 0;

    av_init_packet(&audioPacket);

    // pre-read a frame
    if (av_read_frame(m_fmtCtx, &audioPacket) >= 0)
    {
        audioPacketSize = audioPacket.size;
    }
    else
    {
        AV_LOG_E("can't read any frame");
        return;
    }

    //1. resample
    SwrConvertor swrConvertor(swrParam, audioPacketSize);

    //2. codec
    // AVCodec* codec = avcodec_find_encoder(AVCodecID::AV_CODEC_ID_AAC);
    AVCodec* codec = avcodec_find_encoder_by_name("libfdk_aac");
    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if (codecCtx == nullptr)
    {
        AV_LOG_E("Failed to alloc AVCodecContext with codec(%s)", avcodec_get_name(AVCodecID::AV_CODEC_ID_AAC));
        return;
    }

    codecCtx->sample_fmt = AV_SAMPLE_FMT_S16;
    codecCtx->channel_layout = AV_CH_LAYOUT_STEREO;
    codecCtx->channels = av_get_channel_layout_nb_channels(codecCtx->channel_layout);
    codecCtx->sample_rate = 44100;
    codecCtx->bit_rate = 0;
    codecCtx->profile = FF_PROFILE_AAC_HE_V2;

    if (int ret = avcodec_open2(codecCtx, codec, NULL); ret < 0)
    {
        char errors[1024];
        av_strerror(ret, errors, sizeof(errors));
        AV_LOG_E("Failed to open codec (%s)\nerror:%s", avcodec_get_name(AVCodecID::AV_CODEC_ID_AAC), errors);
        return;
    }

    do
    {
        recordCnt--;

        if (swrConvertor.enable())
        {
            auto [outputData, outputSize] = swrConvertor.convert(audioPacket.data, audioPacket.size);
            ofs.write(reinterpret_cast<char*>(outputData[0]), outputSize);
        }
        else
        {
            ofs.write(reinterpret_cast<char*>(audioPacket.data), audioPacket.size);
        }

        av_packet_unref(&audioPacket);
    } while (av_read_frame(m_fmtCtx, &audioPacket) == 0 && recordCnt > 0);


    avcodec_close(codecCtx);
    
}