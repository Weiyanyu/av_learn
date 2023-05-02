#pragma once

class AVFormatContext;
#include <string>

class Device
{
public:
    Device(const std::string& deviceName);
    ~Device();

    void record(const std::string& outFilename);

private:
    std::string m_deviceName;
    AVFormatContext* m_fmtCtx = nullptr;
};