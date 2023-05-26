#pragma once

#include <cstdint>

struct FrameParam
{
	bool	 enable = false;
	int		 frameSize;
	uint64_t channelLayout;
	int		 format;
};

class AVFrame;
class Frame
{
public:
	Frame();
	Frame(const FrameParam& initParam);
	// dsiable copy-ctor and move-ctor
	Frame(const Frame&) = delete;
	Frame& operator=(const Frame) = delete;
	Frame(Frame&&)				  = delete;
	Frame& operator=(Frame&&) = delete;

	~Frame();

public:
	AVFrame* getAVFrame() const
	{
		return m_avFrame;
	}

	// util func
public:
	bool writeAudioData(uint8_t** audioData, int32_t audioDataSize);
	bool writeImageData(uint8_t* imageData, int pixFormat, int width, int height);

	bool isValid() const
	{
		return m_valid;
	}
	int32_t	  lineSize(int idx) const;
	int*	  lineSize() const;
	uint8_t** data() const;

	int format() const;
	int channels() const;
	int nbSamples() const;

private:
	AVFrame* m_avFrame;
	bool	 m_valid;
};