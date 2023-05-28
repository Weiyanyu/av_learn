#pragma once

#include <string>

class AVFormatContext;
class SwrContext;
class ReampleParam;
class AudioCodecParam;
class CodecParam;
class Frame;
class AudioCodec;
class VideoCodec;
class AVPacket;
class SwrConvertor;

enum class DeviceType : int
{
    AUDIO,
    VIDEO,
    ENCAPSULATE_FILE,
    PURE_FILE,
};

struct AudioReaderParam
{
    std::ifstream& ifs;
    std::ofstream& ofs;
    uint8_t*       srcData;
    uint8_t*       dstData;
    int            frameSize;
    int            inSamples;
    int            outSamples;
    SwrConvertor&  swrConvertor;
    AudioCodec&    audioCodec;
    Frame&         frame;
    AVPacket*      pkt;
};

struct VideoReaderParam
{
    std::ifstream& ifs;
    std::ofstream& ofs;
    uint8_t*       srcData;
    int            frameSize;
    int            inWidth  = 0;
    int            inHeight = 0;
    int            inPixFmt = 0;

    int outWidth  = 0;
    int outHeight = 0;
    int outPixFmt = -1;

    VideoCodec& videoCodec;
    Frame&      frame;
    AVPacket*   pkt;
};

class Device
{
public:
    Device();
    Device(const std::string& deviceName, DeviceType deviceType);
    // dsiable copy-ctor and move-ctor
    Device(const Device&) = delete;
    Device& operator=(const Device) = delete;
    Device(Device&&)                = delete;
    Device& operator=(Device&&) = delete;

    virtual ~Device();

public:
    // read data and encode
    virtual void readData(const std::string& inFilename,
                          const std::string& outFilename,
                          ReampleParam&      reampleParam,
                          const CodecParam&  encodeParam)
    { }

protected:
    int findStreamIdxByMediaType(int mediaType);

    AVFormatContext* getFmtCtx() const;
    DeviceType       getDeviceType() const
    {
        return m_deviceType;
    }
    std::string getDeviceName() const
    {
        return m_deviceName;
    }

private:
    std::string      m_deviceName;
    DeviceType       m_deviceType;
    AVFormatContext* m_fmtCtx = nullptr;
};

class AudioDevice : public Device
{
public:
    AudioDevice();
    AudioDevice(const std::string& deviceName, DeviceType deviceType);

    ~AudioDevice();

    void readData(const std::string& inFilename,
                  const std::string& outFilename,
                  ReampleParam&      reampleParam,
                  const CodecParam&  encodeParam) override;
    void readAudioDataToPCM(const std::string outputFilename,
                            int64_t           outChannelLayout,
                            int               outSampleFmt,
                            int64_t           outSampleRate);

private:
    // util func
    void readAudioFromHWDevice(AudioReaderParam& param);
    void readAudioFromStream(AudioReaderParam& param);
};

class VideoDevice : public Device
{
public:
    VideoDevice();
    VideoDevice(const std::string& deviceName, DeviceType deviceType);

    ~VideoDevice();

    void readData(const std::string& inFilename,
                  const std::string& outFilename,
                  ReampleParam&      reampleParam,
                  const CodecParam&  encodeParam) override;

    void readVideoDataToYUV(const std::string& inFilename,
                            const std::string& outFilename,
                            const CodecParam&  videoEncodeParam,
                            int                outWidth,
                            int                outHeight,
                            int                outPixFormat);

public:
    void writeImageToFile(std::ofstream& ofs, Frame& frame);

private:
    void readAudioFromStream(VideoReaderParam& param);
};