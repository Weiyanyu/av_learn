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

}
Device::~Device()
{
    if (m_fmtCtx)
    {
        avformat_close_input(&m_fmtCtx);
    }
}

void Device::audioRecord(const std::string& outFilename, const SwrContextParam& swrParam, const AudioCodecParam& audioEncodeParam)
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

    //1. resample
    SwrConvertor swrConvertor(swrParam, audioPacketSize);
    
    //2. codec
    AudioCodec audioCodec(audioEncodeParam);

    //3. create a frame
    // outChannel = 
    // frameNbSample = std::ceil(audioPacketSize / m_outChannel / m_outSampleSize);
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

    do
    {
        recordCnt--;
        if (swrConvertor.enable())
        {
            auto [outputData, outputSize] = swrConvertor.convert(audioPacket.data, audioPacket.size);
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
        auto [remainData, remainBufferSize] = swrConvertor.flushRemain();
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
    
}

void Device::readVideoData()
{
    if (avformat_find_stream_info(m_fmtCtx,NULL) < 0)
    {
        AV_LOG_E("can't read video stream info");
        return;
    }

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