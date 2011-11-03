#ifndef _AVR_VIDEORECORDER_H_
#define _AVR_VIDEORECORDER_H_

// Encodes video to H.264
// Encodes audio to AAC-LC
// Outputs to MP4 file

namespace AVR {

enum VideoFrameFormat {
	VideoFrameFormatYUV420P=0,
	VideoFrameFormatNV12,
	VideoFrameFormatNV21,
	VideoFrameFormatRGB24,
	VideoFrameFormatBGR24,
	VideoFrameFormatARGB,
	VideoFrameFormatRGBA,
	VideoFrameFormatABGR,
	VideoFrameFormatBGRA,
	VideoFrameFormatRGB565LE,
	VideoFrameFormatRGB565BE,
	VideoFrameFormatBGR565LE,
	VideoFrameFormatBGR565BE,
	VideoFrameFormatMax
};

enum AudioSampleFormat {
	AudioSampleFormatU8=0,
	AudioSampleFormatS16,
	AudioSampleFormatS32,
	AudioSampleFormatFLT,
	AudioSampleFormatDBL,
	AudioSampleFormatMax
};

class VideoRecorder {
public:
	VideoRecorder();
	virtual ~VideoRecorder();
	
	// Use this to get an instance of VideoRecorder. Use delete operator to delete it.
	static VideoRecorder* New();
	
	// Return true on success, false on failure

	// Call first
	virtual bool Open(const char* mp4file,bool hasAudio,bool dbg)=0;
	// Call last
	virtual bool Close()=0;
	
	// Call SetVideoOptions and SetAudioOptions before calling Start
	virtual bool SetVideoOptions(VideoFrameFormat fmt,unsigned long bitrate)=0;
	virtual bool SetAudioOptions(AudioSampleFormat fmt,unsigned long bitrate)=0;

	// After this succeeds, you can call SupplyVideoFrame and SupplyAudioSamples
	virtual bool Start()=0;
	
	// Supply a video frame
	virtual void SupplyVideoFrame(const void* frame,unsigned long numBytes)=0;
	// Supply audio samples
	virtual void SupplyAudioSamples(const void* samples,unsigned long numSamples)=0;
};

} // namespace AVR

#endif // _AVR_VIDEORECORDER_H_