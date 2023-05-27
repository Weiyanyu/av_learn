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

VideoDevice::VideoDevice()
    : Device()
{ }

VideoDevice::VideoDevice(const std::string& deviceName, DeviceType deviceType)
    : Device(deviceName, deviceType)
{ }

VideoDevice::~VideoDevice() { }

void VideoDevice::readData(const std::string& inFilename,
                           const std::string& outFilename,
                           SwrContextParam&   swrParam,
                           const CodecParam&  encodeParam)
{ }

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
    Frame frame;
    Frame frameYUV;

    int outputBufferSize =
        av_image_get_buffer_size((AVPixelFormat)outPixFormat, outWidth, outHeight, 1);
    AV_LOG_D("outputBufferSize %d", outputBufferSize);
    uint8_t* outBuffer = (uint8_t*)av_malloc(outputBufferSize);
    frameYUV.writeImageData(outBuffer, videoCodec.pixFormat(false), outWidth, outHeight);

    SwsContext* swsCtx = sws_getContext(videoCodec.width(false),
                                        videoCodec.height(false),
                                        (AVPixelFormat)videoCodec.pixFormat(false),
                                        outWidth,
                                        outHeight,
                                        (AVPixelFormat)outPixFormat,
                                        SWS_BICUBIC,
                                        nullptr,
                                        nullptr,
                                        nullptr);

    int picCnt = 0;
    // create dir
    if(access(outFilename.c_str(), F_OK) == -1)
    {
        mkdir(outFilename.c_str(), S_IRWXO | S_IRWXG | S_IRWXU);
    }

    // 7. decodec callback
    auto decodecCB = [&](Frame& frame) {
        std::string   outName = outFilename + "/" + std::to_string(picCnt) + ".yuv";
        std::ofstream ofs(outName, std::ios::out);
        sws_scale(swsCtx,
                  frame.data(),
                  frame.lineSize(),
                  0,
                  videoCodec.height(false),
                  frameYUV.data(),
                  frameYUV.lineSize());
        if(outPixFormat == AVPixelFormat::AV_PIX_FMT_YUV420P)
        {
            int y_size = outWidth * outHeight;
            int u_size = y_size / 4;
            int v_size = y_size / 4;
            ofs.write(reinterpret_cast<char*>(frameYUV.data()[0]), y_size);
            ofs.write(reinterpret_cast<char*>(frameYUV.data()[1]), u_size);
            ofs.write(reinterpret_cast<char*>(frameYUV.data()[2]), v_size);
            AV_LOG_D("write yuv: %s  w/h = %d/%d", outName.c_str(), outWidth, outHeight);
        }
        else
        {
            AV_LOG_E("don't support format %d yet", outPixFormat);
        }

        picCnt++;
    };

    while(av_read_frame(fmtCtx, packet) >= 0)
    {
        if(packet->stream_index == videoStreamIdx)
        {
            videoCodec.decode(frame, packet, decodecCB);
        }
        av_packet_unref(packet);
    }

    av_packet_free(&packet);

    videoCodec.decode(frame, packet, decodecCB, true);
}