#pragma once

class AVFormatContext;
class SwrContext;

#include <string>

class SwrContextParam;
class AudioEncoderParam;

class Device
{
public:
    Device(const std::string& deviceName);
    ~Device();

    void audioRecord(const std::string& outFilename, const SwrContextParam& swrParam, const AudioEncoderParam& audioEncodeParam);

private:
    std::string m_deviceName;
    AVFormatContext* m_fmtCtx = nullptr;
};