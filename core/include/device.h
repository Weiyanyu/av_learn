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
    ENCAPSULATE_FILE,
    PCM_FILE,
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

    // audido
    void audioRecord(const std::string& outFilename, SwrContextParam& swrParam, const AudioCodecParam& audioEncodeParam);
    void readAudioDataToPCM(const std::string outputFilename, int64_t outChannelLayout, int outSampleFmt, int64_t outSampleRate);
    void encodePCM(const std::string outputFilename);

    void readVideoData();

private:
    int findStreamIdxByMediaType(int mediaType);

private:
    std::string m_deviceName;
    DeviceType m_deviceType;
    AVFormatContext* m_fmtCtx = nullptr;
};