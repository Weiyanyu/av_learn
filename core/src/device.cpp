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
}


#include "device.h"
#include "resample.h"
#include "audiocodec.h"
#include "frame.h"
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

void Device::audioRecord(const std::string& outFilename, const SwrContextParam& swrParam, const AudioEncoderParam& audioEncodeParam)
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
    AudioEncoder audioEncoder(audioEncodeParam);

    //3. create a frame
    // outChannel = 
    // frameNbSample = std::ceil(audioPacketSize / m_outChannel / m_outSampleSize);
    FrameParam frameParam = 
    {
        .enable = audioEncoder.enable(),
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
                if (audioEncoder.enable())
                {
                    frame.writeAudioData(outputData, outputSize);
                    audioEncoder.encode(frame, newPkt, ofs);
                }
                else
                {
                    ofs.write(reinterpret_cast<char*>(outputData[0]), outputSize);
                }

            }
        }
        else
        {
            if (audioEncoder.enable())
            {
                frame.writeAudioData(&audioPacket.data, audioPacket.size);
                audioEncoder.encode(frame, newPkt, ofs);
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
            if (audioEncoder.enable())
            {
                frame.writeAudioData(remainData, remainBufferSize);
                audioEncoder.encode(frame, newPkt, ofs);
            }
            else
            {
                ofs.write(reinterpret_cast<char*>(remainData[0]), remainBufferSize);
            }
        }
    }

    if (audioEncoder.enable())
    {
        // flush frame
        audioEncoder.encode(frame, newPkt, ofs, true);
    }


    av_packet_free(&newPkt);
    
}