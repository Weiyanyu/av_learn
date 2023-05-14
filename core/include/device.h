#pragma once

class AVFormatContext;
class SwrContext;

#include <string>

class SwrContextParam;
class AudioCodecParam;

enum class DeviceType : int
{
    AUDIO,
    VIDEO,
    FILE,
};

class Device
{
public:
    Device(const std::string& deviceName, DeviceType deviceType);
    // dsiable copy-ctor and move-ctor
    Device(const Device&) = delete;
    Device& operator=(const Device) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    ~Device();

    void audioRecord(const std::string& outFilename, const SwrContextParam& swrParam, const AudioCodecParam& audioEncodeParam);
    void readVideoData();

private:
    std::string m_deviceName;
    DeviceType m_deviceType;
    AVFormatContext* m_fmtCtx = nullptr;
};