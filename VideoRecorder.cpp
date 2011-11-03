#include <android/log.h>
#include "VideoRecorder.h"

// Use LOG() macro to output debug statements if dbg==true in Open()
#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"VideoRecorder",__VA_ARGS__)
// Do not use C++ exceptions, templates, or RTTI

namespace AVR {

class VideoRecorderImpl : public VideoRecorder {
public:
	VideoRecorderImpl();
	~VideoRecorderImpl();
	
	bool Open(const char* mp4file,bool hasAudio,bool dbg);
	bool Close();
	
	bool SetVideoOptions(VideoFrameFormat fmt,unsigned long bitrate);
	bool SetAudioOptions(AudioSampleFormat fmt,unsigned long bitrate);
	
	bool Start();

	void SupplyVideoFrame(const void* frame,unsigned long numBytes);
	void SupplyAudioSamples(const void* samples,unsigned long numSamples);

private:
	// TODO
};

VideoRecorderImpl::VideoRecorderImpl()
{
	// TODO
}

VideoRecorderImpl::~VideoRecorderImpl()
{
	// TODO
}

bool VideoRecorderImpl::Open(const char* mp4file,bool hasAudio,bool dbg)
{
	// TODO
	return false;
}

bool VideoRecorderImpl::Close()
{
	// TODO
	return false;
}

bool VideoRecorderImpl::SetVideoOptions(VideoFrameFormat fmt,unsigned long bitrate)
{
	// TODO
	return false;
}

bool VideoRecorderImpl::SetAudioOptions(AudioSampleFormat fmt,unsigned long bitrate)
{
	// TODO
	return false;
}

bool VideoRecorderImpl::Start()
{
	// TODO
	return false;
}

void VideoRecorderImpl::SupplyVideoFrame(const void* frame,unsigned long numBytes)
{
	// TODO
}

void VideoRecorderImpl::SupplyAudioSamples(const void* samples,unsigned long numSamples)
{
	// TODO
}

VideoRecorder* VideoRecorder::New()
{
	return (VideoRecorder*)(new VideoRecorderImpl);
}

} // namespace AVR
