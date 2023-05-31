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

VideoDevice::VideoDevice()
    : Device()
{ }

VideoDevice::VideoDevice(const std::string& deviceName, DeviceType deviceType)
    : Device(deviceName, deviceType)
{ }

VideoDevice::~VideoDevice() { }

void VideoDevice::readData(const std::string& inFilename,
                           const std::string& outFilename,
                           ReampleParam&      reampleParam,
                           const CodecParam&  encodeParam)
{

    bool isReadFromStream = inFilename != "";

    // 1. create video codec
    VideoCodec videoCodec(encodeParam);

    // 2. create packet
    AVPacket* packet = av_packet_alloc();
    if(!packet)
    {
        AV_LOG_E("can't alloct packet");
        return;
    }
    // 3. creat frame
    VideoFrameParam vFrameParam{
        .enable    = true,
        .width     = reampleParam.inWidth,
        .height    = reampleParam.inHeight,
        .pixFormat = reampleParam.inPixFmt,
    };
    Frame frame(vFrameParam);
    int   frameBufferSize = av_image_get_buffer_size(
        (AVPixelFormat)vFrameParam.pixFormat, vFrameParam.width, vFrameParam.height, 32);
    AV_LOG_D("frameBufferSize %d", frameBufferSize);

    std::ifstream ifs(inFilename, std::ios::in);
    std::ofstream ofs(outFilename, std::ios::out);

    // 4. read from stream
    if(isReadFromStream)
    {
        uint8_t* srcBuffer = static_cast<uint8_t*>(av_malloc(frameBufferSize));

        VideoReaderParam vReaderParam{
            .ifs        = ifs,
            .ofs        = ofs,
            .srcData    = srcBuffer,
            .frameSize  = frameBufferSize,
            .inWidth    = reampleParam.inWidth,
            .inHeight   = reampleParam.inHeight,
            .inPixFmt   = reampleParam.inPixFmt,
            .outWidth   = reampleParam.outWidth,
            .outHeight  = reampleParam.outHeight,
            .outPixFmt  = reampleParam.outPixFmt,
            .videoCodec = videoCodec,
            .frame      = frame,
            .pkt        = packet,
        };
        readVideoFromStream(vReaderParam);

        // 4.1 release buffer
        if(srcBuffer)
        {
            av_freep(&srcBuffer);
        }
    }

    // 5. release resource
    if(packet)
    {
        av_packet_free(&packet);
    }
}

void VideoDevice::readVideoDataToYUV(const std::string& inFilename,
                                     const std::string& outFilename,
                                     const CodecParam&  videoEncodeParam,
                                     int                outWidth,
                                     int                outHeight,
                                     int                outPixFormat)
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

    int outputBufferSize =
        av_image_get_buffer_size((AVPixelFormat)outPixFormat, outWidth, outHeight, 1);
    AV_LOG_D("outputBufferSize %d", outputBufferSize);

    bool        isNeedSws = false;
    SwsContext* swsCtx    = nullptr;
    if(inWidth != outWidth || inHeight != outHeight || inPixFmt != outPixFormat)
    {
        swsCtx = sws_getContext(inWidth,
                                inHeight,
                                (AVPixelFormat)inPixFmt,
                                outWidth,
                                outHeight,
                                (AVPixelFormat)outPixFormat,
                                SWS_BICUBIC,
                                nullptr,
                                nullptr,
                                nullptr);
        if(swsCtx != nullptr)
        {
            VideoFrameParam swsOutVfp{
                .enable    = true,
                .width     = outWidth,
                .height    = outHeight,
                .pixFormat = outPixFormat,
            };
            swsOutFrame = std::make_unique<Frame>(swsOutVfp);
            isNeedSws   = true;
        }
        AV_LOG_D("sws in w/h %d/%d out w/h %d/%d", inWidth, inHeight, outWidth, outHeight);
    }
    if(!isNeedSws)
    {
        AV_LOG_D("don't need sws");
    }

    std::ofstream ofs(outFilename);

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

void VideoDevice::writeImageToFile(std::ofstream& ofs, Frame& frame)
{
    if(frame.format() == AVPixelFormat::AV_PIX_FMT_YUV420P)
    {
        int y_size = frame.width() * frame.heigt();
        int u_size = y_size / 4;
        int v_size = y_size / 4;
        ofs.write(reinterpret_cast<char*>(frame.data()[0]), y_size);
        ofs.write(reinterpret_cast<char*>(frame.data()[1]), u_size);
        ofs.write(reinterpret_cast<char*>(frame.data()[2]), v_size);
    }
    else
    {
        AV_LOG_E("don't support format %d yet", frame.format());
    }
}