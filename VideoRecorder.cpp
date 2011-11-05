#include <android/log.h>
#include "VideoRecorder.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

// Use LOG() macro to output debug statements if dbg==true in Open()
#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"VideoRecorder",__VA_ARGS__)
// Do not use C++ exceptions, templates, or RTTI

namespace AVR {

class VideoRecorderImpl : public VideoRecorder {
public:
	VideoRecorderImpl();
	~VideoRecorderImpl();
	
	bool SetVideoOptions(VideoFrameFormat fmt,int width,int height,int fps,unsigned long bitrate);
	bool SetAudioOptions(AudioSampleFormat fmt,int channels,unsigned long samplerate,unsigned long bitrate);

	bool Open(const char* mp4file,bool hasAudio,bool dbg);
	bool Close();
	
	bool Start();

	void SupplyVideoFrame(const void* frame,unsigned long numBytes);
	void SupplyAudioSamples(const void* samples,unsigned long numSamples);

private:
	bool InitVideoStream(PixelFormat pixfmt,int width,int height,int fps,unsigned long bitrate);
	bool InitAudioStream(AVSampleFormat fmt,int samplesz,int channels,unsigned long samplerate,unsigned long bitrate);
	
	AVFormatContext *fc;
	
    AVStream *video_stream;
	AVFrame *frame;
	uint8_t *video_outbuf;
	int video_outbuf_size;
	int video_width,video_height,video_fps;
	unsigned long video_bitrate;
	PixelFormat video_pixfmt;
	
	AVStream *audio_stream;
	uint16_t *samples;
	uint8_t *audio_outbuf;
	int audio_outbuf_size;
	int audio_channels;
	unsigned long audio_bitrate,audio_samplerate;
	AVSampleFormat audio_sample_format;
	int audio_sample_size;
	
};

VideoRecorderImpl::VideoRecorderImpl()
{
	avcodec_init();
	avcodec_register_all();

	fc = NULL;
}

VideoRecorderImpl::~VideoRecorderImpl()
{
	if(fc) {
		av_free(fc);
		fc = NULL;
	}
}

bool VideoRecorderImpl::Open(const char* mp4file,bool hasAudio,bool dbg)
{
	// prevent Open unless it's new or previously been Close()'d
	if(fc) {
		LOG("Tried to open twice");
		return false;
	}

	// initialize AVFormatContext
	avformat_alloc_output_context2(&fc, NULL, NULL, mp4file);
	if(!fc) {
		LOG("avformat_alloc_output_context2 failed");
		return false;
	}
	
	if(!InitVideoStream(video_pixfmt,video_width,video_height,video_fps,video_bitrate)) {
		LOG("InitVideoStream failed");
		return false;
	}
	
	if(hasAudio) {
		if(!InitAudioStream(audio_sample_format,audio_sample_size,audio_channels,audio_samplerate,audio_bitrate)) {
			LOG("InitAudioStream failed");
			return false;
		}
	}

	// open output file
	if(avio_open(&fc->pb, mp4file, AVIO_FLAG_WRITE) < 0) {
		LOG("avio_open failed");
		return false;
	}

	return true;
}

bool VideoRecorderImpl::InitVideoStream(PixelFormat pixfmt,int width,int height,int fps,unsigned long bitrate)
{
	AVCodecContext *cc;
	
	// create a new video stream
	video_stream = avformat_new_stream(fc, NULL);
	if(!video_stream) {
		LOG("avformat_new_stream for video failed");
		return false;
	}

	// set AVCodecContext pointer
	cc = video_stream->codec;

	// configure codec
	cc->codec_id = CODEC_ID_H264;
	cc->codec_type = AVMEDIA_TYPE_VIDEO;

	cc->pix_fmt = pixfmt;
	cc->bit_rate = bitrate;
	cc->time_base.den = fps;
	cc->time_base.num = 1;
	cc->width = width;
	cc->height = height;

	AVCodec *codec = avcodec_find_encoder(cc->codec_id);
	if(!codec) {
		LOG("could not find video codec");
		return false;
	}

	if(avcodec_open(cc, codec) < 0) {
		LOG("could not open video codec");
		return false;
	}

	// initialize frame
	frame = avcodec_alloc_frame();
	if(!frame) {
		LOG("could not allocate avframe");
		return false;
	}

	// initialize picture AVFrame's data
	int size = avpicture_get_size(cc->pix_fmt, cc->width, cc->height);
	uint8_t *frame_buf = (uint8_t*)av_malloc(size);

	avpicture_fill((AVPicture *)frame, frame_buf, cc->pix_fmt, cc->width, cc->height);

	// initialize video output buffer
	video_outbuf_size = size;
	video_outbuf = (uint8_t*)av_malloc(video_outbuf_size);
	if(!video_outbuf) {
		LOG("could not malloc video_outbuf");
		return false;
	}
	
	return true;
}

bool VideoRecorderImpl::InitAudioStream(AVSampleFormat fmt,int samplesz,int channels,unsigned long samplerate,unsigned long bitrate)
{
	audio_stream = av_new_stream(fc, 1);
	if(!audio_stream) {
		LOG("could not create audio stream");
		return false;
	}
	
	AVCodecContext *cc;
	
	cc = audio_stream->codec;
	cc->codec_id = CODEC_ID_AAC;
	cc->codec_type = AVMEDIA_TYPE_AUDIO;
	cc->sample_fmt = fmt;
	cc->bit_rate = bitrate;
	cc->sample_rate = samplerate;
	cc->channels = channels;
	
	AVCodec *codec = avcodec_find_encoder(cc->codec_id);
	if(!codec) {
		LOG("could not find audio codec");
		return false;
	}
	
	if(avcodec_open(cc, codec) < 0) {
		LOG("could not open audio codec");
		return false;
	}
	
	audio_outbuf_size = 10000;
    audio_outbuf = (uint8_t*)av_malloc(audio_outbuf_size);

	samples = (uint16_t*)av_malloc(cc->frame_size * samplesz * cc->channels);
}

bool VideoRecorderImpl::SetVideoOptions(VideoFrameFormat fmt,int width,int height,int fps,unsigned long bitrate)
{
	switch(fmt) {
	case VideoFrameFormatYUV420P: video_pixfmt=PIX_FMT_YUV420P; break;
	case VideoFrameFormatNV12: video_pixfmt=PIX_FMT_NV12; break;
	case VideoFrameFormatNV21: video_pixfmt=PIX_FMT_NV21; break;
	case VideoFrameFormatRGB24: video_pixfmt=PIX_FMT_RGB24; break;
	case VideoFrameFormatBGR24: video_pixfmt=PIX_FMT_BGR24; break;
	case VideoFrameFormatARGB: video_pixfmt=PIX_FMT_ARGB; break;
	case VideoFrameFormatRGBA: video_pixfmt=PIX_FMT_RGBA; break;
	case VideoFrameFormatABGR: video_pixfmt=PIX_FMT_ABGR; break;
	case VideoFrameFormatBGRA: video_pixfmt=PIX_FMT_BGRA; break;
	case VideoFrameFormatRGB565LE: video_pixfmt=PIX_FMT_RGB565LE; break;
	case VideoFrameFormatRGB565BE: video_pixfmt=PIX_FMT_RGB565BE; break;
	case VideoFrameFormatBGR565LE: video_pixfmt=PIX_FMT_BGR565LE; break;
	case VideoFrameFormatBGR565BE: video_pixfmt=PIX_FMT_BGR565BE; break;
	default: LOG("Unknown frame format passed to SetVideoOptions!"); return false;
	}
	video_width=width;
	video_height=height;
	video_fps=fps;
	video_bitrate=bitrate;
	return true;
}

bool VideoRecorderImpl::SetAudioOptions(AudioSampleFormat fmt,int channels,unsigned long samplerate,unsigned long bitrate)
{
	switch(fmt) {
	case AudioSampleFormatU8: audio_sample_format=AV_SAMPLE_FMT_U8; audio_sample_size=1; break;
	case AudioSampleFormatS16: audio_sample_format=AV_SAMPLE_FMT_S16; audio_sample_size=2; break;
	case AudioSampleFormatS32: audio_sample_format=AV_SAMPLE_FMT_S32; audio_sample_size=4; break;
	case AudioSampleFormatFLT: audio_sample_format=AV_SAMPLE_FMT_FLT; audio_sample_size=4; break;
	case AudioSampleFormatDBL: audio_sample_format=AV_SAMPLE_FMT_DBL; audio_sample_size=8; break;
	default: LOG("Unknown sample format passed to SetAudioOptions!"); return false;
	}
	audio_channels=channels;
	audio_bitrate=bitrate;
	audio_samplerate=samplerate;
	return true;
}

bool VideoRecorderImpl::Start()
{
	av_write_header(fc);
}

void VideoRecorderImpl::SupplyVideoFrame(const void* frameData,unsigned long numBytes)
{
	memcpy(frame->data[0], frameData, numBytes);
	
	AVPacket pkt;
	av_init_packet(&pkt);
	
	pkt.stream_index = video_stream->index;
	pkt.data = video_outbuf;
	pkt.size = avcodec_encode_video(video_stream->codec, video_outbuf, video_outbuf_size, frame);

	av_interleaved_write_frame(fc, &pkt);
}

void VideoRecorderImpl::SupplyAudioSamples(const void* sampleData,unsigned long numSamples)
{
	memcpy(samples, sampleData, numSamples * audio_sample_size);
	
	AVPacket pkt;
	av_init_packet(&pkt);

	pkt.stream_index = audio_stream->index;
	pkt.data = video_outbuf;	
	pkt.size = avcodec_encode_audio(audio_stream->codec, audio_outbuf, audio_outbuf_size, (const short*)samples);
	
	av_interleaved_write_frame(fc, &pkt);
}

bool VideoRecorderImpl::Close()
{
	av_write_trailer(fc);

	avcodec_close(video_stream->codec);
	avcodec_close(audio_stream->codec);

	av_free(frame->data[0]);
	av_free(frame);
	
	av_free(audio_outbuf);
	av_free(samples);

	int i;
	for( i = 0; i < fc->nb_streams; i++ ) {
		av_freep(&fc->streams[i]->codec);
		av_freep(&fc->streams[i]);
	}

	avio_close(fc->pb);

	av_free(fc);
	fc = NULL;
}

VideoRecorder* VideoRecorder::New()
{
	return (VideoRecorder*)(new VideoRecorderImpl);
}

} // namespace AVR
