#include "device.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>

#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "../../utils/include/log.h"
#include "codec.h"
#include "device.h"
#include "frame.h"
#include "resample.h"

#include <fstream>
#include <iostream>

#include <sys/stat.h>
#include <unistd.h>

AudioDevice::AudioDevice()
    : Device()
{ }

AudioDevice::AudioDevice(const std::string& deviceName, DeviceType deviceType)
    : Device(deviceName, deviceType)
{ }

AudioDevice::~AudioDevice() { }

void AudioDevice::readData(const std::string& inFilename,
                           const std::string& outFilename,
                           ReampleParam&      reampleParam,
                           const CodecParam&  audioEncodeParam)
{
    // 0. decied if is read from stream(file)
    bool readFromStream = false;
    if(inFilename != "")
    {
        readFromStream = true;
    }
    AV_LOG_D("is read from stream %d", readFromStream);

    // 1. init param
    std::ofstream ofs(outFilename, std::ios::out);
    AVPacket      audioPacket;
    int           frameSize = 0;
    av_init_packet(&audioPacket);

    // 2. codec
    AudioCodec audioCodec(audioEncodeParam);

    // 3. calc frame size
    if(!readFromStream)
    {
        // need pre-read a frame if read from hw
        if(av_read_frame(getFmtCtx(), &audioPacket) >= 0)
        {
            frameSize = audioPacket.size;
        }
        else
        {
            AV_LOG_E("can't read any frame");
            return;
        }
    }
    else
    {
        // read from stream
        if(audioCodec.encodeEnable())
        {
            // need encode
            frameSize =
                audioCodec.frameSize(true) *
                av_get_channel_layout_nb_channels(audioEncodeParam.encodeParam.channelLayout) *
                av_get_bytes_per_sample((AVSampleFormat)audioEncodeParam.encodeParam.sampleFmt);
        }
        else
        {
            // don't need encode
            // Note: PCM -> PCM，em...just read and than write, frame size is
            // not important
            frameSize = 8192;
        }
    }
    AV_LOG_D("frameSize %d", frameSize);

    // 4. create dst buffer, input samples, output samples, etc...
    int inSampleSize  = av_get_bytes_per_sample(reampleParam.inSampleFmt);
    int inChannels    = av_get_channel_layout_nb_channels(reampleParam.inChannelLayout);
    int outSampleSize = av_get_bytes_per_sample(reampleParam.outSampleFmt);
    int outChannels   = av_get_channel_layout_nb_channels(reampleParam.outChannelLayout);
    int inSamples     = std::ceil(frameSize / inChannels / inSampleSize);
    int outSamples    = std::ceil(frameSize / outChannels / outSampleSize);

    int      outputBufferSize = outSampleSize * outChannels * outSamples;
    uint8_t* dstData          = static_cast<uint8_t*>(av_malloc(outputBufferSize));
    if(dstData == nullptr)
    {
        AV_LOG_D("alloc dst buffer error");
        return;
    }
    AV_LOG_D(
        "outputBufferSize %d inSamples %d outSamples %d", outputBufferSize, inSamples, outSamples);

    // 5. create swr
    reampleParam.fullOutputBufferSize = outputBufferSize;
    SwrConvertor swrConvertor(reampleParam);

    // 6. create a frame
    AudioFrameParam AudioFrameParam = {
        .enable        = audioCodec.encodeEnable(),
        .frameSize     = frameSize,
        .channelLayout = audioEncodeParam.encodeParam.channelLayout,
        .format        = audioEncodeParam.encodeParam.sampleFmt,
    };
    Frame frame(AudioFrameParam);

    // 7. create a packet
    AVPacket* newPkt = av_packet_alloc();
    if(!newPkt)
    {
        AV_LOG_E("Failed to alloc packet");
        return;
    }

    // 8. read and write/encode audio data
    if(!readFromStream)
    {
        // from hw device
        std::ifstream    ifs;
        AudioReaderParam param{.ifs          = ifs,
                               .ofs          = ofs,
                               .dstData      = dstData,
                               .inSamples    = inSamples,
                               .outSamples   = outSamples,
                               .swrConvertor = swrConvertor,
                               .audioCodec   = audioCodec,
                               .frame        = frame,
                               .pkt          = newPkt};
        readAudioFromHWDevice(param);
    }
    else
    {
        // from stream/file
        std::ifstream ifs(inFilename, std::ios::in);
        uint8_t*      srcBuffer = static_cast<uint8_t*>(av_malloc(frameSize));
        if(!srcBuffer)
        {
            AV_LOG_E("Failed to src buffer");
            return;
        }

        AudioReaderParam param{.ifs          = ifs,
                               .ofs          = ofs,
                               .srcData      = srcBuffer,
                               .dstData      = dstData,
                               .frameSize    = frameSize,
                               .inSamples    = inSamples,
                               .outSamples   = outSamples,
                               .swrConvertor = swrConvertor,
                               .audioCodec   = audioCodec,
                               .frame        = frame,
                               .pkt          = newPkt};

        readAudioFromStream(param);

        // release src buffer
        if(srcBuffer)
        {
            av_freep(&srcBuffer);
        }
    }

    // 9. release resource
    if(newPkt)
    {
        av_packet_free(&newPkt);
    }

    if(dstData)
    {
        av_freep(&dstData);
    }
}

void AudioDevice::readAudioDataToPCM(const std::string outputFilename,
                                     int64_t           outChannelLayout,
                                     int               outSampleFmt,
                                     int64_t           outSampleRate)
{
    if(getDeviceType() != DeviceType::ENCAPSULATE_FILE)
    {
        AV_LOG_E("don't support read audio data to pcm.");
        return;
    }
    std::ofstream ofs(outputFilename, std::ios::out);

    // 1. find audio stream
    int audioStreamIdx = findStreamIdxByMediaType(AVMediaType::AVMEDIA_TYPE_AUDIO);
    if(audioStreamIdx == -1)
    {
        AV_LOG_E("can't find audio stream.");
        return;
    }

    auto* fmtCtx = getFmtCtx();
    AV_LOG_D("nb stream %d", fmtCtx->nb_streams);
    AV_LOG_D("bit rate %ld", fmtCtx->bit_rate);

    // 2. create audio codec
    DecoderParam decodeParam{.needDecode = true,
                             .codecId    = fmtCtx->streams[audioStreamIdx]->codecpar->codec_id,
                             .avCodecPar = fmtCtx->streams[audioStreamIdx]->codecpar,
                             .byId       = true};
    CodecParam   codecParam = {.decodeParam = decodeParam};
    AudioCodec   audioCodec(codecParam);
    if(!audioCodec.decodeEnable())
    {
        AV_LOG_D("can't use decode. please check it");
        return;
    }

    // 3. create packet
    AVPacket packet;
    av_init_packet(&packet);

    // 4. create a frame
    Frame frame;

    // 5. calc out Buffer size
    int      outSampleSize    = av_get_bytes_per_sample(static_cast<AVSampleFormat>(outSampleFmt));
    int      outChannels      = av_get_channel_layout_nb_channels(outChannelLayout);
    int      outputBufferSize = outSampleSize * outChannels * outSampleRate;
    uint8_t* dstData          = static_cast<uint8_t*>(av_malloc(outputBufferSize));
    AV_LOG_D("outputBufferSize %d", outputBufferSize);

    // 6. create swrConvetro
    ReampleParam swrCtxParam = {.outChannelLayout     = outChannelLayout,
                                .outSampleFmt         = static_cast<AVSampleFormat>(outSampleFmt),
                                .outSampleRate        = outSampleRate,
                                .inChannelLayout      = (int64_t)audioCodec.channelLayout(false),
                                .inSampleFmt          = (AVSampleFormat)audioCodec.format(false),
                                .inSampleRate         = audioCodec.sampleRate(false),
                                .logOffset            = 0,
                                .logCtx               = nullptr,
                                .fullOutputBufferSize = outputBufferSize};
    SwrConvertor swrConvertor(swrCtxParam);

    // 7. decodec callback
    auto decodecCB = [&](Frame& frame) {
        if(swrConvertor.enable())
        {
            int64_t dst_nb_samples = swrConvertor.calcNBSample(
                frame.getAVFrame()->sample_rate, frame.getAVFrame()->nb_samples, outSampleRate);
            auto [outputData, outputSize] = swrConvertor.convert(frame.data(),
                                                                 frame.lineSize(0),
                                                                 &dstData,
                                                                 frame.getAVFrame()->nb_samples,
                                                                 dst_nb_samples);

            if(outputData)
            {
                AV_LOG_D("write audio success!!!, outputSize %d", outputSize);
                ofs.write(reinterpret_cast<char*>(outputData[0]), outputSize);
            }
        }
        else
        {
            ofs.write(reinterpret_cast<char*>(frame.data()[0]), frame.lineSize(0));
        }
    };

    // 8. read and decode audio data
    while(av_read_frame(fmtCtx, &packet) >= 0)
    {
        if(packet.stream_index == audioStreamIdx)
        {
            audioCodec.decode(frame, &packet, decodecCB, false);
        }
        av_packet_unref(&packet);
    }

    // 9. flush frame
    if(audioCodec.decodeEnable() && frame.isValid())
    {
        AV_LOG_D("flush remain frame");
        audioCodec.decode(frame, &packet, decodecCB, true);
    }

    // 10. flush swrConvetor remain
    while(swrConvertor.enable() && swrConvertor.hasRemain())
    {
        AV_LOG_D("start flush swr remain");
        auto [outputData, outputSize] = swrConvertor.flushRemain(&dstData);
        if(outputData)
        {
            ofs.write(reinterpret_cast<char*>(outputData[0]), outputSize);
        }
    }

    // 11. release buffer
    if(dstData)
    {
        av_freep(&dstData);
    }
}

void AudioDevice::readAudioFromHWDevice(AudioReaderParam& param)
{
    if(getDeviceType() != DeviceType::AUDIO)
    {
        AV_LOG_E("can't support read from hw device");
        return;
    }
    AVPacket audioPacket;
    av_init_packet(&audioPacket);

    int  recordCnt = 5000;
    auto encodeCB  = [&](AVPacket* pkt) {
        param.ofs.write(reinterpret_cast<char*>(pkt->data), pkt->size);
    };
    auto* fmtCtx = getFmtCtx();

    do
    {
        recordCnt--;
        if(param.swrConvertor.enable())
        {
            auto [outputData, outputSize] = param.swrConvertor.convert(&audioPacket.data,
                                                                       audioPacket.size,
                                                                       &param.dstData,
                                                                       param.inSamples,
                                                                       param.outSamples);
            if(outputData && outputSize)
            {
                if(param.audioCodec.encodeEnable() && param.frame.isValid())
                {
                    param.frame.writeAudioData(outputData, outputSize);
                    param.audioCodec.encode(param.frame, param.pkt, encodeCB);
                }
                else
                {
                    param.ofs.write(reinterpret_cast<char*>(outputData[0]), outputSize);
                }
            }
        }
        else
        {
            if(param.audioCodec.encodeEnable() && param.frame.isValid())
            {
                param.frame.writeAudioData(&audioPacket.data, audioPacket.size);
                param.audioCodec.encode(param.frame, param.pkt, encodeCB);
            }
            else
            {
                param.ofs.write(reinterpret_cast<char*>(audioPacket.data), audioPacket.size);
            }
        }

        av_packet_unref(&audioPacket);
    } while(av_read_frame(fmtCtx, &audioPacket) == 0 && recordCnt > 0);

    // flush swr
    while(param.swrConvertor.hasRemain())
    {
        auto [remainData, remainBufferSize] = param.swrConvertor.flushRemain(&param.dstData);
        AV_LOG_D("flush remain buffer size %d", remainBufferSize);
        if(remainData && remainBufferSize)
        {
            if(param.audioCodec.encodeEnable() && param.frame.isValid())
            {
                param.frame.writeAudioData(remainData, remainBufferSize);
                param.audioCodec.encode(param.frame, param.pkt, encodeCB);
            }
            else
            {
                param.ofs.write(reinterpret_cast<char*>(remainData[0]), remainBufferSize);
            }
        }
    }

    // flush encode
    if(param.audioCodec.encodeEnable() && param.frame.isValid())
    {
        param.audioCodec.encode(param.frame, param.pkt, encodeCB, true);
    }
}

void AudioDevice::readAudioFromStream(AudioReaderParam& param)
{
    auto cb = [&](AVPacket* pkt) {
        param.ofs.write(reinterpret_cast<char*>(pkt->data), pkt->size);
    };
    int n   = 0;
    int pts = 0;
    while((n = param.ifs.readsome((char*)param.srcData, param.frameSize)) > 0)
    {
        if(param.swrConvertor.enable())
        {
            auto [outputData, outputSize] = param.swrConvertor.convert(
                &param.srcData, n, &param.dstData, param.inSamples, param.outSamples);
            if(outputData && outputSize)
            {
                if(param.audioCodec.encodeEnable())
                {
                    if(param.frame.writeAudioData(outputData, outputSize) == false)
                    {
                        return;
                    }
                    pts += param.frame.getAVFrame()->nb_samples;
                    param.frame.getAVFrame()->pts = pts;
                    param.audioCodec.encode(param.frame, param.pkt, cb, false);
                }
                else
                {
                    param.ofs.write(reinterpret_cast<char*>(outputData), outputSize);
                }
            }
        }
        else
        {
            if(param.audioCodec.encodeEnable())
            {
                if(param.frame.writeAudioData(&param.srcData, n) == false)
                {
                    return;
                }
                pts += param.frame.getAVFrame()->nb_samples;
                param.frame.getAVFrame()->pts = pts;
                param.audioCodec.encode(param.frame, param.pkt, cb, false);
            }
            else
            {
                param.ofs.write(reinterpret_cast<char*>(param.srcData), n);
            }
        }
    }

    // flush swr
    while(param.swrConvertor.enable() && param.swrConvertor.hasRemain())
    {
        AV_LOG_D("start flush");
        auto [remainData, remainBufferSize] = param.swrConvertor.flushRemain(&param.dstData);
        if(remainData && remainBufferSize)
        {
            if(param.audioCodec.encodeEnable())
            {
                param.frame.writeAudioData(remainData, remainBufferSize);
                param.audioCodec.encode(param.frame, param.pkt, cb);
            }
            else
            {
                param.ofs.write(reinterpret_cast<char*>(remainData), remainBufferSize);
            }
            AV_LOG_D("write audio success!!!, nb samples %d", remainBufferSize);
        }
    }

    // flush encode
    if(param.audioCodec.encodeEnable())
    {
        param.audioCodec.encode(param.frame, param.pkt, cb, true);
    }
}