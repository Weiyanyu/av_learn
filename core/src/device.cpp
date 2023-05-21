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

Device::Device()
    :m_deviceName(""),
     m_deviceType(DeviceType::PCM_FILE)
{

}

Device::Device(const std::string& deviceName, DeviceType deviceType)
    :m_deviceName(deviceName),
     m_deviceType(deviceType)
{
    // get format
    AVInputFormat* inputFormat = nullptr;
    std::string url = deviceName;
    switch (m_deviceType)
    {
    case DeviceType::AUDIO:
        inputFormat = av_find_input_format("alsa");
        break;
    case DeviceType::VIDEO:
        inputFormat = av_find_input_format("Video4Linux2");
        break;
    case DeviceType::PCM_FILE:
    case DeviceType::ENCAPSULATE_FILE:
        inputFormat = nullptr;
        break;
    default:
        AV_LOG_E("unkown device type %d", (int)m_deviceType);
        break;
    }

    AVDictionary* options = nullptr;
    // open device
    if (auto ret = avformat_open_input(&m_fmtCtx, url.c_str(), inputFormat, &options); ret < 0)
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

    if (deviceType == DeviceType::ENCAPSULATE_FILE)
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

void Device::readAudio(const std::string& inFilename, const std::string& outFilename, SwrContextParam& swrParam, const AudioCodecParam& audioEncodeParam)
{
    // 0. decied if is read from stream(file)
    bool readFromStream = false;
    if (inFilename != "")
    {
        readFromStream = true;
    }
    AV_LOG_D("read from stream %d", readFromStream);

    // 1. init param
    std::ofstream ofs(outFilename, std::ios::out);
    AVPacket audioPacket;
    int frameSize = 0;
    av_init_packet(&audioPacket);

    //2. codec
    AudioCodec audioCodec(audioEncodeParam);

    //3. calc frame size
    if (!readFromStream)
    {
        // need pre-read a frame if read from hw
        if (av_read_frame(m_fmtCtx, &audioPacket) >= 0)
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
        if (audioCodec.encodeEnable())
        {
            // need encode
            frameSize = 
                audioCodec.getCodecCtx(true)->frame_size * 
                av_get_channel_layout_nb_channels(audioEncodeParam.encodeParam.channelLayout) * 
                av_get_bytes_per_sample((AVSampleFormat)audioEncodeParam.encodeParam.sampleFmt);
        }
        else
        {
            // don't need encode
            // Note: PCM -> PCM，em...just read and than write, frame size is not important
            frameSize = 8192;
        }

    }
    AV_LOG_D("frameSize %d", frameSize);

    // 4. create dst buffer, input samples, output samples, etc...
    int inSampleSize = av_get_bytes_per_sample(swrParam.in_sample_fmt);
    int inChannels = av_get_channel_layout_nb_channels(swrParam.in_ch_layout);
    int outSampleSize = av_get_bytes_per_sample(swrParam.out_sample_fmt);
    int outChannels = av_get_channel_layout_nb_channels(swrParam.out_ch_layout);
    int inSamples = std::ceil(frameSize / inChannels / inSampleSize);
    int outSamples = std::ceil(frameSize / outChannels / outSampleSize);

    int outputBufferSize = outSampleSize * outChannels * outSamples;
    uint8_t* dstData = static_cast<uint8_t*>(av_malloc(outputBufferSize));
    if (dstData == nullptr)
    {
        AV_LOG_D("alloc dst buffer error");
        return;
    }
    AV_LOG_D("outputBufferSize %d inSamples %d outSamples %d", outputBufferSize, inSamples, outSamples);

    // 5. create swr 
    swrParam.fullOutputBufferSize = outputBufferSize;
    SwrConvertor swrConvertor(swrParam);

    // 6. create a frame
    FrameParam frameParam = 
    {
        .enable = audioCodec.encodeEnable(),
        .frameSize = frameSize,
        .channelLayout = audioEncodeParam.encodeParam.channelLayout,
        .format = audioEncodeParam.encodeParam.sampleFmt,
    };
    Frame frame(frameParam);

    // 7. create a packet
    AVPacket* newPkt = av_packet_alloc();
    if (!newPkt)
    {
        AV_LOG_E("Failed to alloc packet");
        return;
    }

    // 8. read and write/encode audio data
    if (!readFromStream)
    {
        // from hw device
        std::ifstream ifs;
        AudioReaderParam param
        {
            .ifs = ifs,
            .ofs = ofs,
            .dstData = dstData,
            .inSamples = inSamples,
            .outSamples = outSamples,
            .swrConvertor = swrConvertor,
            .audioCodec = audioCodec,
            .frame = frame,
            .pkt = newPkt
        };
        readAudioDataFromHWDevice(param);
    }
    else
    {
        // from stream/file
        std::ifstream ifs(inFilename, std::ios::in);
        uint8_t* srcBuffer = static_cast<uint8_t*>(av_malloc(frameSize));
        if (!srcBuffer)
        {
            AV_LOG_E("Failed to src buffer");
            return;
        }

        AudioReaderParam param
        {
            .ifs = ifs,
            .ofs = ofs,
            .srcData = srcBuffer,
            .dstData = dstData,
            .frameSize = frameSize,
            .inSamples = inSamples,
            .outSamples = outSamples,
            .swrConvertor = swrConvertor,
            .audioCodec = audioCodec,
            .frame = frame,
            .pkt = newPkt
        };

        readAndWriteAudioDataFromStream(param);

        // release src buffer
        if (srcBuffer)
        {
            av_freep(&srcBuffer);
        }
    }

    // 9. release resource
    if (newPkt)
    {
        av_packet_free(&newPkt);
    }

    if (dstData)
    {
        av_freep(&dstData);
    }
}


void Device::readAudioDataToPCM(const std::string outputFilename, int64_t outChannelLayout, int outSampleFmt, int64_t outSampleRate)
{
    if (m_deviceType != DeviceType::ENCAPSULATE_FILE)
    {
        AV_LOG_E("don't support read audio datat to pcm.");
        return;
    }
    std::ofstream ofs(outputFilename, std::ios::out);

    // 1. find audio stream
    int audioStreamIdx = findStreamIdxByMediaType(AVMediaType::AVMEDIA_TYPE_AUDIO);
    if (audioStreamIdx == -1)
    {
        AV_LOG_E("can't find audio stream.");
        return;
    }
    AV_LOG_D("nb stream %d", m_fmtCtx->nb_streams);
    AV_LOG_D("bit rate %d", m_fmtCtx->bit_rate);

    // 2. create audio codec
    AudiodecoderParam decodeParam 
    {
        .needDecode = true,
        .codecId = m_fmtCtx->streams[audioStreamIdx]->codecpar->codec_id,
        .avCodecPar = m_fmtCtx->streams[audioStreamIdx]->codecpar,
        .byId = true
    };
    AudioCodecParam codecParam =
    {
        .decodeParam = decodeParam
    };
    AudioCodec audioCodec(codecParam);

    // 3. create packet
    AVPacket packet;
    av_init_packet(&packet);

    // 4. create a frame
    Frame frame;

    // 5. calc out Buffer size
    int outSampleSize = av_get_bytes_per_sample(static_cast<AVSampleFormat>(outSampleFmt));
    int outChannels = av_get_channel_layout_nb_channels(outChannelLayout);
    int outputBufferSize = outSampleSize * outChannels * outSampleRate;
    uint8_t* dstData = static_cast<uint8_t*>(av_malloc(outputBufferSize));
    AV_LOG_D("outputBufferSize %d", outputBufferSize);

    // 6. create swrConvetro
    SwrContextParam swrCtxParam = 
    {
        .out_ch_layout = outChannelLayout,
        .out_sample_fmt = static_cast<AVSampleFormat>(outSampleFmt),
        .out_sample_rate = outSampleRate,
        .in_ch_layout = (int64_t)audioCodec.getCodecCtx(false)->channel_layout,
        .in_sample_fmt = audioCodec.getCodecCtx(false)->sample_fmt,
        .in_sample_rate = audioCodec.getCodecCtx(false)->sample_rate,
        .log_offset = 0,
        .log_ctx = nullptr,
        .fullOutputBufferSize = outputBufferSize
    };
    SwrConvertor swrConvertor(swrCtxParam);

    // 7. decodec callback
    auto decodecCB = [&](Frame& frame)
    {
        if (swrConvertor.enable())
        {
            int64_t dst_nb_samples = swrConvertor.calcNBSample(frame.getAVFrame()->sample_rate, frame.getAVFrame()->nb_samples, outSampleRate);
            auto [outputData, outputSize] = swrConvertor.convert(frame.getAVFrame()->data, frame.getAVFrame()->linesize[0], &dstData, frame.getAVFrame()->nb_samples, dst_nb_samples);
            
            if (outputData)
            {
                AV_LOG_D("write audio success!!!, outputSize %d", outputSize);
                ofs.write(reinterpret_cast<char*>(outputData[0]), outputSize);
            }
        }
        else
        {
            ofs.write(reinterpret_cast<char*>(frame.getAVFrame()->data[0]), frame.getAVFrame()->linesize[0]);
        }

    };

    // 8. read and decode audio data
    while (av_read_frame(m_fmtCtx, &packet) >= 0)
    {
        if (packet.stream_index == audioStreamIdx)
        {
            audioCodec.decode(frame, &packet, decodecCB, false);
        }
        av_packet_unref(&packet);
    }

    // 9. flush frame
    if (audioCodec.decodeEnable() && frame.isValid())
    {
        AV_LOG_D("flush remain frame");
        audioCodec.decode(frame, &packet, decodecCB, true);
    }
    
    // 10. flush swrConvetor remain
    while (swrConvertor.enable() && swrConvertor.hasRemain())
    {
        AV_LOG_D("start flush");
        auto [outputData, outputSize] = swrConvertor.flushRemain(&dstData);
        if (outputData)
        {
            ofs.write(reinterpret_cast<char*>(outputData[0]), outputSize);
            AV_LOG_D("write audio success!!!, nb samples %d", outputSize);
        }
    }

    // 11. release buffer
    if (dstData)
    {
        av_freep(&dstData);
    }

}

void Device::readVideoData()
{
    if (m_deviceType != DeviceType::ENCAPSULATE_FILE)
    {
        return;
    }
    int videoStreamIdx = findStreamIdxByMediaType(AVMediaType::AVMEDIA_TYPE_VIDEO);
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


void Device::readAudioDataFromHWDevice(AudioReaderParam& param)
{
    if (m_deviceType != DeviceType::AUDIO)
    {
        AV_LOG_E("can't support read from hw device");
    }
    AVPacket audioPacket;
    av_init_packet(&audioPacket);

    int recordCnt = 5000;
    auto encodeCB = [&](AVPacket* pkt)
    {
        param.ofs.write(reinterpret_cast<char*>(pkt->data), pkt->size);
    };

    do
    {
        recordCnt--;
        if (param.swrConvertor.enable())
        {
            auto [outputData, outputSize] = param.swrConvertor.convert(&audioPacket.data, audioPacket.size, &param.dstData, param.inSamples, param.outSamples);
            if (outputData && outputSize)
            {
                if (param.audioCodec.encodeEnable() && param.frame.isValid())
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
            if (param.audioCodec.encodeEnable() && param.frame.isValid())
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
    } while (av_read_frame(m_fmtCtx, &audioPacket) == 0 && recordCnt > 0);

    // flush swr
    while (param.swrConvertor.hasRemain())
    {
        auto [remainData, remainBufferSize] = param.swrConvertor.flushRemain(&param.dstData);
        AV_LOG_D("flush remain buffer size %d", remainBufferSize);
        if (remainData && remainBufferSize)
        {
            if (param.audioCodec.encodeEnable() && param.frame.isValid())
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
    if (param.audioCodec.encodeEnable() && param.frame.isValid())
    {
        param.audioCodec.encode(param.frame, param.pkt, encodeCB, true);
    }
}

void Device::readAndWriteAudioDataFromStream(AudioReaderParam& param)
{
    auto cb = [&](AVPacket* pkt)
    {
        param.ofs.write(reinterpret_cast<char*>(pkt->data), pkt->size);
    };
    int n = 0;
    while ((n = param.ifs.readsome((char*)param.srcData, param.frameSize)) > 0)
    {
        if (param.swrConvertor.enable())
        {
            auto [outputData, outputSize] = param.swrConvertor.convert(&param.srcData, n, &param.dstData, param.inSamples, param.outSamples);
            if (outputData && outputSize)
            {
                if (param.audioCodec.encodeEnable())
                {
                    if (param.frame.writeAudioData(outputData, outputSize) == false)
                    {
                        return;
                    }
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
            if (param.audioCodec.encodeEnable())
            {
                if (param.frame.writeAudioData(&param.srcData, n) == false)
                {
                    return;
                }
                param.audioCodec.encode(param.frame, param.pkt, cb, false);
            }
            else
            {
                param.ofs.write(reinterpret_cast<char*>(param.srcData), n);
            }
        }
    }

    // flush swr 
    while (param.swrConvertor.enable() && param.swrConvertor.hasRemain())
    {
        AV_LOG_D("start flush");
        auto [remainData, remainBufferSize] = param.swrConvertor.flushRemain(&param.dstData);
        if (remainData && remainBufferSize)
        {
            if (param.audioCodec.encodeEnable())
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
    if (param.audioCodec.encodeEnable())
    {
        param.audioCodec.encode(param.frame, param.pkt, cb, true);
    }
}

int Device::findStreamIdxByMediaType(int mediaType)
{
    if (!m_fmtCtx) return -1;
    int streamIdx = -1;
    for (size_t i = 0; i < m_fmtCtx->nb_streams; i++)
    {
        if (m_fmtCtx->streams[i]->codecpar->codec_type == static_cast<AVMediaType>(mediaType))
        {
            streamIdx = i;
            break;
        }
    }
    return streamIdx;
}