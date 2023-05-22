#include "codec.h"
#include "../../utils/include/log.h"
#include "frame.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

#include <ostream>

AudioCodec::AudioCodec(const AudioCodecParam& initParam)
	: m_encodeCodecCtx(nullptr)
	, m_decodeCodecCtx(nullptr)
	, m_encodeEnable(false)
{
	// encoder
	if(initParam.encodeParam.needEncode)
	{

		AVCodec* encodeCodec = nullptr;
		if(initParam.encodeParam.byName)
		{
			encodeCodec = avcodec_find_encoder_by_name(initParam.encodeParam.codecName.c_str());
		}
		else if(initParam.encodeParam.byId)
		{
			encodeCodec =
				avcodec_find_encoder(static_cast<AVCodecID>(initParam.encodeParam.codecId));
		}
		else
		{
			AV_LOG_E("you must set find encode by codec name or codec id");
			return;
		}

		if(isSupport(encodeCodec,
					 initParam.encodeParam.sampleFmt,
					 initParam.encodeParam.channelLayout,
					 initParam.encodeParam.sampleRate))
		{
			m_encodeCodecCtx = avcodec_alloc_context3(encodeCodec);
			if(m_encodeCodecCtx == nullptr)
			{
				AV_LOG_E("Failed to alloc AVCodecContext with codec(%s)",
						 initParam.encodeParam.codecName.c_str());
				return;
			}

			m_encodeCodecCtx->sample_fmt	 = (AVSampleFormat)initParam.encodeParam.sampleFmt;
			m_encodeCodecCtx->channel_layout = initParam.encodeParam.channelLayout;
			m_encodeCodecCtx->channels =
				av_get_channel_layout_nb_channels(m_encodeCodecCtx->channel_layout);
			m_encodeCodecCtx->sample_rate = initParam.encodeParam.sampleRate;
			m_encodeCodecCtx->bit_rate	  = initParam.encodeParam.bitRate;
			m_encodeCodecCtx->profile	  = initParam.encodeParam.profile;

			if(int ret = avcodec_open2(m_encodeCodecCtx, encodeCodec, NULL); ret < 0)
			{
				char errors[1024];
				av_strerror(ret, errors, sizeof(errors));
				AV_LOG_E("Failed to open codec (%s)\nerror:%s",
						 initParam.encodeParam.codecName.c_str(),
						 errors);
				return;
			}

			m_encodeEnable = true;
			AV_LOG_D("init encode success");
		}
	}

	// decoder
	if(initParam.decodeParam.needDecode)
	{

		AVCodec* decodeCodec = nullptr;
		if(initParam.decodeParam.byName)
		{
			decodeCodec = avcodec_find_decoder_by_name(initParam.decodeParam.codecName.c_str());
		}
		else if(initParam.decodeParam.byId)
		{
			decodeCodec =
				avcodec_find_decoder(static_cast<AVCodecID>(initParam.decodeParam.codecId));
		}
		else
		{
			AV_LOG_E("you must set find decode by codec name or codec id");
			return;
		}
		if(!decodeCodec)
		{
			AV_LOG_E("can't find video decode %d", initParam.decodeParam.codecId);
			return;
		}

		if(isSupport(decodeCodec,
					 initParam.decodeParam.avCodecPar->format,
					 initParam.decodeParam.avCodecPar->channel_layout,
					 initParam.decodeParam.avCodecPar->sample_rate))
		{
			m_decodeCodecCtx = avcodec_alloc_context3(decodeCodec);
			if(!m_decodeCodecCtx)
			{
				AV_LOG_E("faild to alloc codec context");
				return;
			}
			if(avcodec_parameters_to_context(m_decodeCodecCtx, initParam.decodeParam.avCodecPar) <
			   0)
			{
				AV_LOG_E("faild to replace context");
				return;
			}

			if(avcodec_open2(m_decodeCodecCtx, decodeCodec, nullptr) < 0)
			{
				AV_LOG_E("faild to open decode %s", decodeCodec->name);
				return;
			}

			AV_LOG_D("channel layout %ld", m_decodeCodecCtx->channel_layout);
			AV_LOG_D("channel %d", m_decodeCodecCtx->channels);
			AV_LOG_D("format %d", m_decodeCodecCtx->sample_fmt);
			AV_LOG_D("codec name %s", decodeCodec->name);
			if(decodeCodec->profiles)
				AV_LOG_D("codec profile %s", decodeCodec->profiles->name);
			AV_LOG_D("sample rate %d", m_decodeCodecCtx->sample_rate);

			m_decodeEnable = true;
			AV_LOG_D("init decode success");
		}
	}
}

AudioCodec::~AudioCodec()
{
	if(m_encodeCodecCtx)
	{
		AV_LOG_D("release encode context");

		avcodec_close(m_encodeCodecCtx);
	}

	if(m_decodeCodecCtx)
	{
		avcodec_close(m_decodeCodecCtx);
		AV_LOG_D("release decode context");
	}
}

void AudioCodec::encode(Frame& frame, AVPacket* pkt, PacketReceiveCB cb, bool isFlush)
{
	if(!m_encodeEnable)
		return;
	int res = 0;
	if(!isFlush)
	{
		AVFrame* avFrame = frame.getAVFrame();
		res				 = avcodec_send_frame(m_encodeCodecCtx, avFrame);
	}
	else
	{
		res = avcodec_send_frame(m_encodeCodecCtx, nullptr);
	}

	while(res >= 0)
	{
		res = avcodec_receive_packet(m_encodeCodecCtx, pkt);
		if(res == AVERROR(EAGAIN) || res == AVERROR_EOF)
		{
			break;
		}
		else if(res < 0)
		{
			AV_LOG_E("legitimate encoding errors");
			return;
		}

		cb(pkt);
	}
}

void AudioCodec::decode(Frame& frame, AVPacket* pkt, FrameReceiveCB cb, bool isFlush)
{
	int res = -1;
	if(isFlush)
	{
		res = avcodec_send_packet(m_decodeCodecCtx, nullptr);
	}
	else
	{
		res = avcodec_send_packet(m_decodeCodecCtx, pkt);
	}
	while(res >= 0)
	{
		res = avcodec_receive_frame(m_decodeCodecCtx, frame.getAVFrame());

		if(res == AVERROR(EAGAIN) || res == AVERROR_EOF)
		{
			break;
		}
		else if(res < 0)
		{
			AV_LOG_E("legitimate encoding errors");
			return;
		}
		cb(frame);
	}
}

bool AudioCodec::isSupport(AVCodec* codec, int format, uint64_t channelLayout, int64_t sampleRate)
{
	int					  isFind	 = false;
	const AVSampleFormat* supportFmt = codec->sample_fmts;
	if(supportFmt == nullptr)
	{
		AV_LOG_D("codec don't set support format, just skip check");
		isFind = true;
	}
	else
	{
		while(supportFmt && *supportFmt != AV_SAMPLE_FMT_NONE)
		{
			if(*supportFmt == format)
			{
				isFind = true;
				break;
			}
			supportFmt++;
		}
	}

	if(!isFind)
	{
		AV_LOG_E("codec %s don't support format %d", codec->name, format);
		return false;
	}
	isFind = false;

	const uint64_t* supportChannelLayout = codec->channel_layouts;
	if(supportChannelLayout == nullptr)
	{
		AV_LOG_D("codec don't set support channel layout, just skip check");
		isFind = true;
	}
	else
	{
		while(supportChannelLayout && *supportChannelLayout != 0)
		{
			if(*supportChannelLayout == channelLayout)
			{
				isFind = true;
				break;
			}
			supportChannelLayout++;
		}
	}

	if(!isFind)
	{
		AV_LOG_E("codec %s don't support channel layout %ld", codec->name, channelLayout);
		return false;
	}
	isFind = false;

	const int* supportSampleRate = codec->supported_samplerates;
	if(supportSampleRate == nullptr)
	{
		AV_LOG_D("codec don't set support sample rate, just skip check");
		isFind = true;
	}
	else
	{
		while(supportSampleRate && *supportSampleRate != 0)
		{
			if(*supportSampleRate == sampleRate)
			{
				isFind = true;
				break;
			}
			supportSampleRate++;
		}
	}

	if(!isFind)
	{
		AV_LOG_E("codec %s don't support sample rate %ld", codec->name, sampleRate);
		return false;
	}

	return true;
}

// util func
AVCodecContext* AudioCodec::getCodecCtx(bool isEncode) const
{
	if(isEncode)
	{
		return m_encodeCodecCtx;
	}
	return m_decodeCodecCtx;
}

int AudioCodec::format(bool isEncode) const
{
	auto* ctx = getCodecCtx(isEncode);
	if(ctx == nullptr)
		return -1;
	return ctx->sample_fmt;
}

int AudioCodec::frameSize(bool isEncode) const
{
	auto* ctx = getCodecCtx(isEncode);
	if(ctx == nullptr)
		return -1;
	return ctx->frame_size;
}

uint64_t AudioCodec::channelLayout(bool isEncode) const
{
	auto* ctx = getCodecCtx(isEncode);
	if(ctx == nullptr)
		return 0;
	return ctx->channel_layout;
}

int AudioCodec::sampleRate(bool isEncode) const
{
	auto* ctx = getCodecCtx(isEncode);
	if(ctx == nullptr)
		return -1;
	return ctx->sample_rate;
}
