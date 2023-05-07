#pragma once

class AVFormatContext;
class SwrContext;


extern "C"
{
#include <libavutil/samplefmt.h>
}


#include <string>

struct SwrContextParam
{
    bool enable = false;
    int64_t out_ch_layout;
    enum AVSampleFormat out_sample_fmt;
    int out_sample_rate;
    int64_t in_ch_layout;
    enum AVSampleFormat in_sample_fmt;
    int  in_sample_rate;
    int log_offset;
    void *log_ctx;
};

class Device
{
public:
    Device(const std::string& deviceName);
    ~Device();

    void audioRecord(const std::string& outFilename, const SwrContextParam& swrParam);

private:
    std::string m_deviceName;
    AVFormatContext* m_fmtCtx = nullptr;
};