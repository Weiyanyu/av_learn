#pragma once



#include <string>

class AVFormatContext;
class SwrContext;
class SwrContextParam;
class AudioCodecParam;
class Frame;
class AudioCodec;
class AVPacket;
class SwrConvertor;

enum class DeviceType : int
{
    AUDIO,
    VIDEO,
    ENCAPSULATE_FILE,
    PCM_FILE,
};

struct AudioReaderParam
{
    std::ifstream& ifs;
    std::ofstream& ofs;
    uint8_t* srcData;
    uint8_t* dstData;
    int frameSize;
    int inSamples;
    int outSamples;
    SwrConvertor& swrConvertor;
    AudioCodec& audioCodec;
    Frame& frame;
    AVPacket* pkt;
};

class Device
{
public:
    Device();
    Device(const std::string& deviceName, DeviceType deviceType);
    // dsiable copy-ctor and move-ctor
    Device(const Device&) = delete;
    Device& operator=(const Device) = delete;
    Device(Device&&) = delete;
    Device& operator=(Device&&) = delete;

    ~Device();

    // audido
    void readAudioDataToPCM(const std::string outputFilename, int64_t outChannelLayout, int outSampleFmt, int64_t outSampleRate);
    void readAudio(const std::string& inFilename, const std::string& outFilename, SwrContextParam& swrParam, const AudioCodecParam& audioEncodeParam);
    void readVideoData();

private:
    int findStreamIdxByMediaType(int mediaType);
    // util func
    void readAudioDataFromHWDevice(AudioReaderParam& param);
    void readAndWriteAudioDataFromStream(AudioReaderParam& param);
private:
    std::string m_deviceName;
    DeviceType m_deviceType;
    AVFormatContext* m_fmtCtx = nullptr;
};