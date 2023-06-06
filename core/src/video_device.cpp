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
#include <memory>

#include <sys/stat.h>
#include <unistd.h>

#include <thread>

int convertDeprecatedFormat(int format);

VideoDevice::VideoDevice()
    : Device()
{ }

VideoDevice::VideoDevice(const std::string& deviceName,
                         DeviceType         deviceType,
                         AVDictionary*      options)
    : Device(deviceName, deviceType, options)
{ }

VideoDevice::~VideoDevice() { }

void VideoDevice::readAndEncode(ReadDeviceDataParam& params)
{

    bool isReadFromStream = params.inFilename != "";

    // 1. create video codec
    if(!isReadFromStream)
    {
        auto* fmtCtx = getFmtCtx();

        int videoStreamIdx = findStreamIdxByMediaType(AVMediaType::AVMEDIA_TYPE_VIDEO);
        if(videoStreamIdx == -1)
        {
            AV_LOG_E("can't find video stream.");
            return;
        }

        params.resampleParam.inWidth  = fmtCtx->streams[videoStreamIdx]->codecpar->width;
        params.resampleParam.inHeight = fmtCtx->streams[videoStreamIdx]->codecpar->height;
        params.resampleParam.inPixFmt = fmtCtx->streams[videoStreamIdx]->codecpar->format;

        if(convertDeprecatedFormat(params.resampleParam.inPixFmt) != params.resampleParam.inPixFmt)
        {
            AV_LOG_D("input format %d is deprecated format. need decoder",
                     params.resampleParam.inPixFmt);
            DecoderParam decodeParam{.needDecode = true,
                                     .codecId = fmtCtx->streams[videoStreamIdx]->codecpar->codec_id,
                                     .avCodecPar = fmtCtx->streams[videoStreamIdx]->codecpar,
                                     .byId       = true};
            params.codecParam.decodeParam = decodeParam;
        }
    }

    VideoCodec videoCodec(params.codecParam);

    // 2. create packet
    AVPacket* packet = av_packet_alloc();
    if(!packet)
    {
        AV_LOG_E("can't alloct packet");
        return;
    }

    // 3 creat frame for origin video data
    VideoFrameParam vFrameParam{
        .enable    = true,
        .width     = params.resampleParam.inWidth,
        .height    = params.resampleParam.inHeight,
        .pixFormat = params.resampleParam.inPixFmt,
    };
    Frame frame(vFrameParam);
    int   frameBufferSize = av_image_get_buffer_size(
        (AVPixelFormat)vFrameParam.pixFormat, vFrameParam.width, vFrameParam.height, 32);
    AV_LOG_D("frameBufferSize %d", frameBufferSize);

    std::ifstream ifs(params.inFilename, std::ios::in);
    std::ofstream ofs(params.outFilename, std::ios::out);


    VideoReaderParam vReaderParam{
        .ifs        = ifs,
        .ofs        = ofs,
        .srcData    = nullptr,
        .frameSize  = frameBufferSize,
        .inWidth    = params.resampleParam.inWidth,
        .inHeight   = params.resampleParam.inHeight,
        .inPixFmt   = params.resampleParam.inPixFmt,
        .outWidth   = params.resampleParam.outWidth,
        .outHeight  = params.resampleParam.outHeight,
        .outPixFmt  = params.resampleParam.outPixFmt,
        .videoCodec = videoCodec,
        .frame      = frame,
        .pkt        = packet,
    };
    // 4. read from stream
    if(isReadFromStream)
    {
        uint8_t* srcBuffer = static_cast<uint8_t*>(av_malloc(frameBufferSize));
        vReaderParam.srcData = srcBuffer;
        readVideoFromStream(vReaderParam);
        // 4.1 release buffer
        if(srcBuffer)
        {
            av_freep(&srcBuffer);
        }
    }
    else
    {
        readVideoFromHWDevice(vReaderParam);
    }

    // 5. release resource
    if(packet)
    {
        av_packet_free(&packet);
    }
}

void VideoDevice::readAndDecode(ReadDeviceDataParam& params)
{
    if(getDeviceType() != DeviceType::ENCAPSULATE_FILE)
    {
        return;
    }
    int videoStreamIdx = findStreamIdxByMediaType(AVMediaType::AVMEDIA_TYPE_VIDEO);
    if(videoStreamIdx == -1)
    {
        AV_LOG_E("can't find video stream.");
        return;
    }
    auto* fmtCtx = getFmtCtx();

    DecoderParam decodeParam{.needDecode = true,
                             .codecId    = fmtCtx->streams[videoStreamIdx]->codecpar->codec_id,
                             .avCodecPar = fmtCtx->streams[videoStreamIdx]->codecpar,
                             .byId       = true};
    CodecParam   codecParam = {.decodeParam = decodeParam};
    VideoCodec   videoCodec(codecParam);

    AV_LOG_D("video format %s", fmtCtx->iformat->name);
    AV_LOG_D("video time %lds", (fmtCtx->duration) / 1000000);

    AVPacket* packet = av_packet_alloc();
    if(!packet)
    {
        AV_LOG_E("can't alloct packet");
        return;
    }
    int inWidth  = videoCodec.width(false);
    int inHeight = videoCodec.height(false);
    int inPixFmt = videoCodec.pixFormat(false);

    VideoFrameParam vfp{
        .enable    = true,
        .width     = inWidth,
        .height    = inHeight,
        .pixFormat = inPixFmt,
    };
    Frame                  frame(vfp);
    std::unique_ptr<Frame> swsOutFrame;

    int outputBufferSize = av_image_get_buffer_size(
        (AVPixelFormat)params.outPixFormat, params.outWidth, params.outHeight, 1);
    AV_LOG_D("outputBufferSize %d", outputBufferSize);

    bool        isNeedSws = false;
    SwsContext* swsCtx    = nullptr;
    if(inWidth != params.outWidth || inHeight != params.outHeight ||
       inPixFmt != params.outPixFormat)
    {
        swsCtx = sws_getContext(inWidth,
                                inHeight,
                                (AVPixelFormat)inPixFmt,
                                params.outWidth,
                                params.outHeight,
                                (AVPixelFormat)params.outPixFormat,
                                SWS_BICUBIC,
                                nullptr,
                                nullptr,
                                nullptr);
        if(swsCtx != nullptr)
        {
            VideoFrameParam swsOutVfp{
                .enable    = true,
                .width     = params.outWidth,
                .height    = params.outHeight,
                .pixFormat = params.outPixFormat,
            };
            swsOutFrame = std::make_unique<Frame>(swsOutVfp);
            isNeedSws   = true;
        }
        AV_LOG_D(
            "sws in w/h %d/%d out w/h %d/%d", inWidth, inHeight, params.outWidth, params.outHeight);
    }
    if(!isNeedSws)
    {
        AV_LOG_D("don't need sws");
    }

    std::ofstream ofs(params.outFilename);

    // 7. decodec callback
    auto decodecCB = [&](Frame& frame) {
        if(isNeedSws)
        {
            sws_scale(swsCtx,
                      frame.data(),
                      frame.lineSize(),
                      0,
                      inHeight,
                      swsOutFrame->data(),
                      swsOutFrame->lineSize());
            writeImageToFile(ofs, (*swsOutFrame.get()));
        }
        else
        {
            writeImageToFile(ofs, frame);
        }
    };

    while(av_read_frame(fmtCtx, packet) >= 0)
    {
        if(packet->stream_index == videoStreamIdx)
        {
            if(videoCodec.decodeEnable())
            {
                videoCodec.decode(frame, packet, decodecCB);
            }
        }
        av_packet_unref(packet);
    }

    if(videoCodec.decodeEnable())
    {
        videoCodec.decode(frame, packet, decodecCB, true);
    }

    // release resource
    if(packet)
    {
        av_packet_free(&packet);
    }

    if(swsCtx)
    {
        sws_freeContext(swsCtx);
    }
}

void VideoDevice::readVideoFromStream(VideoReaderParam& param)
{
    int  n              = 0;
    auto encodeCallback = [&](AVPacket* pkt) {
        param.ofs.write(reinterpret_cast<char*>(pkt->data), pkt->size);
        AV_LOG_D("write data %d", pkt->size);
    };

    SwsContext*            swsCtx = nullptr;
    std::unique_ptr<Frame> pSwrOutFrame;
    bool                   isNeedSws = false;
    if(param.inWidth != param.outWidth || param.inHeight != param.outHeight ||
       param.inPixFmt != param.outPixFmt)
    {
        swsCtx = sws_getContext(param.inWidth,
                                param.inHeight,
                                (AVPixelFormat)param.inPixFmt,
                                param.outWidth,
                                param.outHeight,
                                (AVPixelFormat)param.outPixFmt,
                                SWS_BICUBIC,
                                nullptr,
                                nullptr,
                                nullptr);
        if(swsCtx != nullptr)
        {
            VideoFrameParam vFrameParam{
                .enable    = true,
                .width     = param.outWidth,
                .height    = param.outHeight,
                .pixFormat = param.outPixFmt,
            };
            pSwrOutFrame = std::make_unique<Frame>(vFrameParam);
            isNeedSws    = true;
        }
        AV_LOG_D("sws in w/h %d/%d out w/h %d/%d",
                 param.inWidth,
                 param.inHeight,
                 param.outWidth,
                 param.outHeight);
    }
    if(!isNeedSws)
    {
        AV_LOG_D("don't need sws");
    }

    while((n = param.ifs.readsome((char*)param.srcData, param.frameSize)) > 0)
    {
        param.frame.writeImageData(
            param.srcData, (AVPixelFormat)param.inPixFmt, param.inWidth, param.inHeight);
        if(isNeedSws)
        {
            sws_scale(swsCtx,
                      param.frame.data(),
                      param.frame.lineSize(),
                      0,
                      param.inHeight,
                      pSwrOutFrame->data(),
                      pSwrOutFrame->lineSize());
            pSwrOutFrame->getAVFrame()->pts++;

            if(param.videoCodec.encodeEnable())
            {
                param.videoCodec.encode((*pSwrOutFrame.get()), param.pkt, encodeCallback);
            }
            else
            {
                writeImageToFile(param.ofs, param.frame);
            }
        }
        else
        {
            param.frame.getAVFrame()->pts++;
            if(param.videoCodec.encodeEnable())
            {
                param.videoCodec.encode(param.frame, param.pkt, encodeCallback);
            }
            else
            {
                writeImageToFile(param.ofs, param.frame);
            }
        }
    }

    if(param.videoCodec.encodeEnable())
    {
        param.videoCodec.encode(param.frame, param.pkt, encodeCallback, true);
    }

    if(swsCtx)
    {
        sws_freeContext(swsCtx);
    }
}

void VideoDevice::readVideoFromHWDevice(VideoReaderParam& param)
{
    if(getDeviceType() != DeviceType::VIDEO)
    {
        AV_LOG_E("can't support read from hw device");
        return;
    }
    int recordCnt = 30;

    //1. init param
    auto*                  fmtCtx = getFmtCtx();
    SwsContext*            swsCtx = nullptr;
    std::unique_ptr<Frame> pSwrOutFrame;
    bool                   isNeedSws    = false;
    bool                   isNeedDecode = param.videoCodec.decodeEnable();
    param.inPixFmt                      = convertDeprecatedFormat(param.inPixFmt);

    //2. check if need sws
    if(param.inWidth != param.outWidth || param.inHeight != param.outHeight ||
       param.inPixFmt != param.outPixFmt)
    {
        swsCtx = sws_getContext(param.inWidth,
                                param.inHeight,
                                (AVPixelFormat)param.inPixFmt,
                                param.outWidth,
                                param.outHeight,
                                (AVPixelFormat)param.outPixFmt,
                                SWS_BICUBIC,
                                nullptr,
                                nullptr,
                                nullptr);
        if(swsCtx != nullptr)
        {
            VideoFrameParam vFrameParam{
                .enable    = true,
                .width     = param.outWidth,
                .height    = param.outHeight,
                .pixFormat = param.outPixFmt,
            };
            pSwrOutFrame = std::make_unique<Frame>(vFrameParam);
            isNeedSws    = true;
        }
        AV_LOG_D("sws in w/h %d/%d fmt %d out w/h %d/%d fmt %d",
                 param.inWidth,
                 param.inHeight,
                 param.inPixFmt,
                 param.outWidth,
                 param.outHeight,
                 param.outPixFmt);
    }
    if(!isNeedSws)
    {
        AV_LOG_D("don't need sws");
    }

    // 3. check if need decode
    std::unique_ptr<Frame> pDecodeFrame;
    if(isNeedDecode)
    {
        VideoFrameParam vDecodeFrameParam{
            .enable    = true,
            .width     = param.inWidth,
            .height    = param.inHeight,
            .pixFormat = param.inPixFmt,
        };
        pDecodeFrame = std::make_unique<Frame>(vDecodeFrameParam);
    }

    // 4. create packet
    AVPacket* newPkt = av_packet_alloc();
    if(!newPkt)
    {
        AV_LOG_E("alloct packet error");
        return;
    }
    int  basePts        = 0;
    auto encodeCallback = [&](AVPacket* pkt) {
        param.ofs.write(reinterpret_cast<char*>(pkt->data), pkt->size);
        AV_LOG_D("write data %d", pkt->size);
        recordCnt--;
    };

    // 5. encode process
    auto encodeProcess = [&](Frame& frame) {
        if(isNeedSws)
        {
            sws_scale(swsCtx,
                      frame.data(),
                      frame.lineSize(),
                      0,
                      param.inHeight,
                      pSwrOutFrame->data(),
                      pSwrOutFrame->lineSize());
            pSwrOutFrame->getAVFrame()->pts = basePts++;
            if(param.videoCodec.encodeEnable())
            {
                param.videoCodec.encode(*pSwrOutFrame, newPkt, encodeCallback);
            }
            else
            {
                writeImageToFile(param.ofs, *pSwrOutFrame);
            }
        }
        else
        {
            param.frame.getAVFrame()->pts = basePts++;
            if(param.videoCodec.encodeEnable())
            {
                param.videoCodec.encode(frame, newPkt, encodeCallback);
            }
            else
            {
                writeImageToFile(param.ofs, frame);
            }
        }
        param.frame.setComplete(false);
    };

    // 6. decodec callback
    auto decodecCB = [&](Frame& frame) {
        av_frame_copy_props(param.frame.getAVFrame(), frame.getAVFrame());
        av_frame_copy(param.frame.getAVFrame(), frame.getAVFrame());
    };

    // 7. start recieve data from hw device
    while(av_read_frame(fmtCtx, param.pkt) >= 0 && recordCnt > 0)
    {
        if(isNeedDecode)
        {
            param.videoCodec.decode(*pDecodeFrame, param.pkt, decodecCB);
            encodeProcess(param.frame);
        }
        else
        {
            param.frame.writeImageData(
                param.pkt->data, param.inPixFmt, param.inWidth, param.inHeight);
            encodeProcess(param.frame);
        }
    }

    // 8. flush encoder
    if(param.videoCodec.encodeEnable())
    {
        param.videoCodec.encode(param.frame, param.pkt, encodeCallback, true);
    }

    // 9. release resource
    if(swsCtx)
    {
        sws_freeContext(swsCtx);
    }

    if(newPkt)
    {
        av_packet_free(&newPkt);
    }
}

void VideoDevice::writeImageToFile(std::ofstream& ofs, Frame& frame)
{
    int format = convertDeprecatedFormat(frame.format());
    if(format == AVPixelFormat::AV_PIX_FMT_YUV420P)
    {
        int y_size = frame.width() * frame.heigt();
        int u_size = y_size / 4;
        int v_size = y_size / 4;
        ofs.write(reinterpret_cast<char*>(frame.data()[0]), y_size);
        ofs.write(reinterpret_cast<char*>(frame.data()[1]), u_size);
        ofs.write(reinterpret_cast<char*>(frame.data()[2]), v_size);
    }
    else if(format == AVPixelFormat::AV_PIX_FMT_YUV422P)
    {
        int y_size = frame.width() * frame.heigt();
        int u_size = y_size / 2;
        int v_size = y_size / 2;
        ofs.write(reinterpret_cast<char*>(frame.data()[0]), y_size);
        ofs.write(reinterpret_cast<char*>(frame.data()[1]), u_size);
        ofs.write(reinterpret_cast<char*>(frame.data()[2]), v_size);
    }
    else
    {
        AV_LOG_E("don't support format %d yet", frame.format());
    }
}

int convertDeprecatedFormat(int format)
{
    switch(format)
    {
    case AV_PIX_FMT_YUVJ420P:
        return AV_PIX_FMT_YUV420P;
    case AV_PIX_FMT_YUVJ422P:
        return AV_PIX_FMT_YUV422P;
    case AV_PIX_FMT_YUVJ444P:
        return AV_PIX_FMT_YUV444P;
    case AV_PIX_FMT_YUVJ440P:
        return AV_PIX_FMT_YUV440P;
    case AV_PIX_FMT_YUVJ411P:
        return AV_PIX_FMT_YUV411P;
    default:
        return format;
    }
}