extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/common.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/frame.h>

#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}


#include "device.h"
#include "resample.h"
#include "codec.h"
#include "frame.h"
#include "../../utils/include/log.h"

#include <iostream>
#include <fstream>
#include <stdio.h>


Device::Device(const std::string& deviceName, DeviceType deviceType)
    :m_deviceName(deviceName),
     m_deviceType(deviceType)
{
    // get format
    AVInputFormat* inputFormat = nullptr;
    switch (m_deviceType)
    {
    case DeviceType::AUDIO:
        inputFormat = av_find_input_format("alsa");
        break;
    case DeviceType::VIDEO:
        inputFormat = av_find_input_format("Video4Linux2");
        break;
    case DeviceType::FILE:
        inputFormat = nullptr;
        break;
    default:
        AV_LOG_E("unkown device type %d", (int)m_deviceType);
        break;
    }

    AVDictionary* options = nullptr;
    // open device
    if (auto ret = avformat_open_input(&m_fmtCtx, m_deviceName.c_str(), inputFormat, &options); ret < 0)
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

    if (deviceType == DeviceType::FILE)
    {
        if (avformat_find_stream_info(m_fmtCtx,NULL) < 0)
        {
            AV_LOG_E("can't read audio stream info");
            return;
        }
    }

}
Device::~Device()
{
    if (m_fmtCtx)
    {
        avformat_close_input(&m_fmtCtx);
    }
}

void Device::audioRecord(const std::string& outFilename, SwrContextParam& swrParam, const AudioCodecParam& audioEncodeParam)
{
    if (m_deviceType != DeviceType::AUDIO)
    {
        AV_LOG_E("only DeviceType::AUDIO support audio record. please check it");
        return;
    }
    std::ofstream ofs(outFilename, std::ios::out);
    int recordCnt = 5000;
    AVPacket audioPacket;
    int audioPacketSize = 0;

    av_init_packet(&audioPacket);

    // pre-read a frame
    if (av_read_frame(m_fmtCtx, &audioPacket) >= 0)
    {
        audioPacketSize = audioPacket.size;
        AV_LOG_D("audioPacketSize %d", audioPacketSize);
    }
    else
    {
        AV_LOG_E("can't read any frame");
        return;
    }

    int inSampleSize = av_get_bytes_per_sample(swrParam.in_sample_fmt);
    int inChannels = av_get_channel_layout_nb_channels(swrParam.in_ch_layout);
    int outSampleSize = av_get_bytes_per_sample(swrParam.out_sample_fmt);
    int outChannels = av_get_channel_layout_nb_channels(swrParam.out_ch_layout);
    int inSamples = std::ceil(audioPacketSize / inChannels / inSampleSize);
    int outSamples = std::ceil(audioPacketSize / outChannels / outSampleSize);

    int outputBufferSize = outSampleSize * outChannels * outSamples;
    uint8_t* dstData = static_cast<uint8_t*>(av_malloc(outputBufferSize));
    if (dstData == nullptr)
    {
        AV_LOG_D("alloc dst buffer error");
        return;
    }


    AV_LOG_D("outputBufferSize %d inSamples %d outSamples %d", outputBufferSize, inSamples, outSamples);
    swrParam.fullOutputBufferSize = outputBufferSize;
    //1. resample
    SwrConvertor swrConvertor(swrParam, audioPacketSize);
    
    //2. codec
    AudioCodec audioCodec(audioEncodeParam);

    //3. create a frame
    FrameParam frameParam = 
    {
        .enable = audioCodec.enable(),
        .pktSize = audioPacketSize,
        .channelLayout = audioEncodeParam.channelLayout,
        .format = audioEncodeParam.sampleFmt,
    };
    Frame frame(frameParam);

    AVPacket* newPkt = av_packet_alloc();
    if (!newPkt)
    {
        AV_LOG_E("Failed to alloc packet");
        return;
    }
    AV_LOG_D("frame line size %d", frame.getAVFrame()->linesize[0]);

    do
    {
        recordCnt--;
        if (swrConvertor.enable())
        {
            auto [outputData, outputSize] = swrConvertor.convert(&audioPacket.data, audioPacket.size, &dstData, inSamples, outSamples);
            if (outputData && outputSize)
            {
                if (audioCodec.enable() && frame.isValid())
                {
                    frame.writeAudioData(outputData, outputSize);
                    audioCodec.encode(frame, newPkt, ofs);
                }
                else
                {
                    ofs.write(reinterpret_cast<char*>(outputData[0]), outputSize);
                }

            }
        }
        else
        {
            if (audioCodec.enable() && frame.isValid())
            {
                frame.writeAudioData(&audioPacket.data, audioPacket.size);
                audioCodec.encode(frame, newPkt, ofs);
            }
            else
            {
                ofs.write(reinterpret_cast<char*>(audioPacket.data), audioPacket.size);
            }
        }

        av_packet_unref(&audioPacket);
    } while (av_read_frame(m_fmtCtx, &audioPacket) == 0 && recordCnt > 0);


    while (swrConvertor.hasRemain())
    {
        auto [remainData, remainBufferSize] = swrConvertor.flushRemain(&dstData);
        AV_LOG_D("flush remain buffer size %d", remainBufferSize);
        if (remainData && remainBufferSize)
        {
            if (audioCodec.enable() && frame.isValid())
            {
                frame.writeAudioData(remainData, remainBufferSize);
                audioCodec.encode(frame, newPkt, ofs);
            }
            else
            {
                ofs.write(reinterpret_cast<char*>(remainData[0]), remainBufferSize);
            }
        }
    }

    // flush frame
    if (audioCodec.enable() && frame.isValid())
    {
        audioCodec.encode(frame, newPkt, ofs, true);
    }

    av_packet_free(&newPkt);
    if (dstData)
    {
        av_freep(&dstData);
    }
}

void Device::readAudioData()
{
    int audioStreamIdx = -1;
    for (size_t i = 0; i < m_fmtCtx->nb_streams; i++)
    {
        if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audioStreamIdx = i;
            break;
        }
    }
    if (audioStreamIdx == -1)
    {
        AV_LOG_E("can't find audio stream.");
        return;
    }


    AVCodec* codec = avcodec_find_decoder(m_fmtCtx->streams[audioStreamIdx]->codecpar->codec_id);
    if (!codec)
    {
        AV_LOG_E("can't find video decode %d", m_fmtCtx->streams[audioStreamIdx]->codecpar->codec_id);
        return;
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx)
    {
        AV_LOG_E("faild to alloc codec context");
        return;
    }
    if (avcodec_parameters_to_context(codecCtx, m_fmtCtx->streams[audioStreamIdx]->codecpar) < 0)
    {
        AV_LOG_E("faild to replace context");
        return;
    }


    if (avcodec_open2(codecCtx, codec, nullptr) < 0)
    {
        AV_LOG_D("faild to open decode %s", codec->name);
        return;
    }

    AV_LOG_D("nb stream %d", m_fmtCtx->nb_streams);
    AV_LOG_D("bit rate %d", m_fmtCtx->bit_rate);
    AV_LOG_D("channel layout %ld", codecCtx->channel_layout);
    AV_LOG_D("channel %d", codecCtx->channels);
    AV_LOG_D("format %d", codecCtx->sample_fmt);
    AV_LOG_D("codec name %s", codec->name);
    AV_LOG_D("codec profile %s", codec->profiles->name);
    AV_LOG_D("sample rate %d", codecCtx->sample_rate);


    AVPacket* packet = av_packet_alloc();
    if (!packet)
    {
        AV_LOG_E("can't alloct packet");
        return;
    }

    AVFrame* frame = av_frame_alloc();
    if (!frame)
    {
        AV_LOG_E("can't alloct frame");
        return;
    }

    // pre-read a frame
    int packetSize = 0;
    if (av_read_frame(m_fmtCtx, packet) >= 0)
    {
        packetSize = packet->size;
        AV_LOG_D("audioPacketSize %d", packetSize);
    }
    else
    {
        AV_LOG_E("can't read any frame");
        return;
    }

    int outSampleSize = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
    int outChannels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
    int outputBufferSize = outSampleSize * outChannels * 44100;
    uint8_t* dstData = static_cast<uint8_t*>(av_malloc(outputBufferSize));
    AV_LOG_D("outputBufferSize %d", outputBufferSize);

    SwrContextParam swrCtxParam = 
    {
        .out_ch_layout = AV_CH_LAYOUT_STEREO,
        .out_sample_fmt = AV_SAMPLE_FMT_S16,
        .out_sample_rate = 44100,
        .in_ch_layout = codecCtx->channel_layout,
        .in_sample_fmt = codecCtx->sample_fmt,
        .in_sample_rate = codecCtx->sample_rate,
        .log_offset = 0,
        .log_ctx = nullptr,
        .fullOutputBufferSize = outputBufferSize
    };
    SwrConvertor swrConvertor(swrCtxParam, packetSize);

    std::ofstream ofs("out2.pcm", std::ios::out);
    do
    {
        if (packet->stream_index == audioStreamIdx)
        {
            int res = avcodec_send_packet(codecCtx, packet);
            while (res >= 0)
            {
                res = avcodec_receive_frame(codecCtx, frame);

                if (res == AVERROR(EAGAIN) || res == AVERROR_EOF)
                {
                    break;
                }
                else if (res < 0)
                {
                    AV_LOG_E("legitimate encoding errors");
                    return;
                }

                int64_t dst_nb_samples = swrConvertor.calcNBSample(frame->sample_rate, frame->nb_samples, 44100);
                auto [outputData, outputSize] = swrConvertor.convert(frame->data, frame->linesize[0], &dstData, frame->nb_samples, dst_nb_samples);
                if (outputData)
                {
                    AV_LOG_D("write audio success!!!, outputSize %d", outputSize);
                    ofs.write(reinterpret_cast<char*>(outputData[0]), outputSize);
                }

            }
        }
        av_packet_unref(packet);
    } while (av_read_frame(m_fmtCtx, packet) >= 0);
    
    if (swrConvertor.hasRemain())
    {
        AV_LOG_D("start flush");
        auto [outputData, outputSize] = swrConvertor.flushRemain(&dstData);
        if (outputData)
        {
            ofs.write(reinterpret_cast<char*>(outputData[0]), outputSize);
            AV_LOG_D("write audio success!!!, nb samples %d", outputSize);
        }
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_close(codecCtx);
    if (dstData)
    {
        av_freep(&dstData);
    }

}

void Device::readVideoData()
{
    // if (avformat_find_stream_info(m_fmtCtx,NULL) < 0)
    // {
    //     AV_LOG_E("can't read video stream info");
    //     return;
    // }

    int videoStreamIdx = -1;
    for (size_t i = 0; i < m_fmtCtx->nb_streams; i++)
    {
        if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStreamIdx = i;
            break;
        }
    }
    if (videoStreamIdx == -1)
    {
        AV_LOG_E("can't find video stream.");
        return;
    }

    AVCodec* codec = avcodec_find_decoder(m_fmtCtx->streams[videoStreamIdx]->codecpar->codec_id);
    if (!codec)
    {
        AV_LOG_E("can't find video decode %d", m_fmtCtx->streams[videoStreamIdx]->codecpar->codec_id);
        return;
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx)
    {
        AV_LOG_E("faild to alloc codec context");
        return;
    }
    if (avcodec_parameters_to_context(codecCtx, m_fmtCtx->streams[videoStreamIdx]->codecpar) < 0)
    {
        AV_LOG_E("faild to replace context");
        return;
    }


    if (avcodec_open2(codecCtx, codec, nullptr) < 0)
    {
        AV_LOG_D("faild to open decode %s", codec->name);
        return;
    }

    AV_LOG_D("video format %s",m_fmtCtx->iformat->name);
    AV_LOG_D("video time %ld", (m_fmtCtx->duration)/1000000);
    AV_LOG_D("video w/h %d/%d",codecCtx->width,codecCtx->height);
    AV_LOG_D("video codec name %s",codec->name);
    AV_LOG_D("video AVPixelFormat %d",codecCtx->pix_fmt);

    



    AVPacket* packet = av_packet_alloc();
    if (!packet)
    {
        AV_LOG_E("can't alloct packet");
        return;
    }

    AVFrame* frame = av_frame_alloc();
    if (!frame)
    {
        AV_LOG_E("can't alloct frame");
        return;
    }

    AVFrame* frameYUV = av_frame_alloc();
    if (!frameYUV)
    {
        AV_LOG_E("can't alloct frame yuv");
        return;
    }

    int picBufferSize = av_image_get_buffer_size(codecCtx->pix_fmt, 1280,720, 1);
    uint8_t* outBuffer = (uint8_t*)av_malloc(picBufferSize);
    av_image_fill_arrays(frameYUV->data, frameYUV->linesize, outBuffer, codecCtx->pix_fmt, 1280,720, 1);

    SwsContext* swsCtx = sws_getContext(codecCtx->width,codecCtx->height, codecCtx->pix_fmt,
                                    1280,720, codecCtx->pix_fmt,
                                    SWS_BICUBIC, nullptr, nullptr, nullptr);
    // AV_LOG_D("frame line size %d yuv frame line size %d", frame->linesize[0],frameYUV->linesize[0]);
    std::ofstream ofs("out.yuv", std::ios::out);
    int got_pic = 0;
    while (av_read_frame(m_fmtCtx, packet) >= 0 && got_pic == 0)
    {
        AV_LOG_D("packet size %d", packet->size);
        if (packet->stream_index == videoStreamIdx)
        {
            int res = avcodec_send_packet(codecCtx, packet);
            while (res >= 0)
            {
                res = avcodec_receive_frame(codecCtx, frame);
                AV_LOG_D("res = %d", res);

                if (res == AVERROR(EAGAIN) || res == AVERROR_EOF)
                {
                    break;
                }
                else if (res < 0)
                {
                    AV_LOG_E("legitimate encoding errors");
                    return;
                }
                int outH = sws_scale(swsCtx, frame->data, frame->linesize, 0, codecCtx->height,
                          frameYUV->data, frameYUV->linesize);
                int y_size = 1280 * 720;
                int u_size = y_size / 4;
                int v_size = y_size / 4;
                ofs.write(reinterpret_cast<char*>(frameYUV->data[0]), y_size);
                ofs.write(reinterpret_cast<char*>(frameYUV->data[1]), u_size);
                ofs.write(reinterpret_cast<char*>(frameYUV->data[2]), v_size);
                AV_LOG_D("write yuv success!!!, out h = %d", outH);
                got_pic = 1;
            }
        }
        av_packet_unref(packet);
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_close(codecCtx);
}