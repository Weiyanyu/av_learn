#pragma once

class AVFormatContext;
class SwrContext;

#include <string>

class SwrContextParam;
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