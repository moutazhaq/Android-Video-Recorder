// compiles on MacOS X with: g++ VideoRecorder.cpp -o v -lavcodec -lavformat -lavutil -lswscale -lx264 -g
//#include <android/log.h>
#include "VideoRecorder.h"
#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

// Use LOG() macro to output debug statements if dbg==true in Open()
//#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"VideoRecorder",__VA_ARGS__)
#define LOG(...) printf(__VA_ARGS__)
// Do not use C++ exceptions, templates, or RTTI

namespace AVR {

class VideoRecorderImpl : public VideoRecorder {
public:
	VideoRecorderImpl();
	~VideoRecorderImpl();
	
	bool SetVideoOptions(VideoFrameFormat fmt, int width, int height, unsigned long bitrate);
	bool SetAudioOptions(AudioSampleFormat fmt, int channels, unsigned long samplerate, unsigned long bitrate);

	bool Open(const char *mp4file, bool hasAudio, bool dbg);
	bool Close();
	
	bool Start();

	void SupplyVideoFrame(const void *frame, unsigned long numBytes, unsigned long timestamp);
	void SupplyAudioSamples(const void *samples, unsigned long numSamples);

private:	
	AVStream *add_audio_stream(enum CodecID codec_id);
	void open_audio();	
	void write_audio_frame(AVStream *st);
	
	AVStream *add_video_stream(enum CodecID codec_id);
	AVFrame *alloc_picture(enum PixelFormat pix_fmt, int width, int height);
	void open_video();
	void write_video_frame(AVStream *st);
	
	// audio related vars
	int16_t *samples;
	uint8_t *audio_outbuf;
	int audio_outbuf_size;
	int audio_input_frame_size;
	AVStream *audio_st;
	
	unsigned long audio_input_leftover_samples;
	
	int audio_channels;				// number of channels (2)
	unsigned long audio_bit_rate;		// codec's output bitrate
	unsigned long audio_sample_rate;		// number of samples per second
	int audio_sample_size;					// size of each sample in bytes (16-bit = 2)
	AVSampleFormat audio_sample_format;
		
	// video related vars
	uint8_t *video_outbuf;
	int video_outbuf_size;
	AVStream *video_st;
	
	int video_width;
	int video_height;
	unsigned long video_bitrate;
	PixelFormat video_pixfmt;
	AVFrame *picture;			// video frame after being converted to x264-friendly YUV420P
	AVFrame *tmp_picture;		// video frame before conversion (RGB565)
	
	unsigned long timestamp_base;
	
	// common
	AVFormatContext *oc;
};

VideoRecorder::VideoRecorder()
{
	
}

VideoRecorder::~VideoRecorder()
{
	
}

VideoRecorderImpl::VideoRecorderImpl()
{
	
}

VideoRecorderImpl::~VideoRecorderImpl()
{
	
}

bool VideoRecorderImpl::Open(const char *mp4file, bool hasAudio, bool dbg)
{	
	av_register_all();
	
	avformat_alloc_output_context2(&oc, NULL, NULL, mp4file);
	if (!oc) {
		printf("Could not deduce output format from file extension\n");
		return false;
	}
	
	video_st = add_video_stream(CODEC_ID_H264);
	audio_st = add_audio_stream(CODEC_ID_AAC);
	
	if(dbg)
		av_dump_format(oc, 0, mp4file, 1);
	
	open_video();
	open_audio();
	
	if (avio_open(&oc->pb, mp4file, AVIO_FLAG_WRITE) < 0) {
		fprintf(stderr, "Could not open '%s'\n", mp4file);
		exit(1);
	}
	
	av_write_header(oc);
}

AVStream *VideoRecorderImpl::add_audio_stream(enum CodecID codec_id)
{
	AVCodecContext *c;
	AVStream *st;

	st = av_new_stream(oc, 1);
	if (!st) {
		fprintf(stderr, "Could not alloc stream\n");
		exit(1);
	}

	c = st->codec;
	c->codec_id = codec_id;
	c->codec_type = AVMEDIA_TYPE_AUDIO;
	c->sample_fmt = audio_sample_format;
	c->bit_rate = audio_bit_rate;
	c->sample_rate = audio_sample_rate;
	c->channels = audio_channels;
	c->profile = FF_PROFILE_AAC_LOW;

	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;

	return st;
}

void VideoRecorderImpl::open_audio()
{
	AVCodecContext *c;
	AVCodec *codec;
	
	c = audio_st->codec;

	codec = avcodec_find_encoder(c->codec_id);
	if (!codec) {
		fprintf(stderr, "audio codec not found\n");
		exit(1);
	}

	if (avcodec_open(c, codec) < 0) {
		fprintf(stderr, "could not open audio codec\n");
		exit(1);
	}

	audio_outbuf_size = 10000;
	audio_outbuf = (uint8_t *)av_malloc(audio_outbuf_size);

	audio_input_frame_size = c->frame_size;
	samples = (int16_t *)av_malloc(audio_input_frame_size * audio_sample_size * c->channels);
	
	audio_input_leftover_samples = 0;
}

AVStream *VideoRecorderImpl::add_video_stream(enum CodecID codec_id)
{
	AVCodecContext *c;
	AVStream *st;

	st = avformat_new_stream(oc, NULL);
	if (!st) {
		fprintf(stderr, "Could not alloc stream\n");
		exit(1);
	}

	c = st->codec;
	c->codec_id = codec_id;
	c->codec_type = AVMEDIA_TYPE_VIDEO;

	/* put sample parameters */
	c->bit_rate = video_bitrate;
	c->width = video_width;
	c->height = video_height;
	c->time_base.num = 1;
	c->time_base.den = 90000;
	c->pix_fmt = PIX_FMT_YUV420P;		// we convert everything to PIX_FMT_YUV420P

	/* h264 specific stuff */
	c->coder_type = 0;	// coder = 1
	c->me_cmp |= 1;	// cmp=+chroma, where CHROMA = 1
	c->partitions |= X264_PART_I8X8 + X264_PART_I4X4 + X264_PART_P8X8 + X264_PART_B8X8; // partitions=+parti8x8+parti4x4+partp8x8+partb8x8
	c->me_method = ME_HEX;	// me_method=hex
	c->me_subpel_quality = 7;	// subq=7
	c->me_range = 16;	// me_range=16
	c->gop_size = 250;	// g=250
	c->keyint_min = 25; // keyint_min=25
	c->scenechange_threshold = 40;	// sc_threshold=40
	c->i_quant_factor = 0.71; // i_qfactor=0.71
	c->b_frame_strategy = 1;  // b_strategy=1
	c->qcompress = 0.6; // qcomp=0.6
	c->qmin = 10;	// qmin=10
	c->qmax = 51;	// qmax=51
	c->max_qdiff = 4;	// qdiff=4
	c->max_b_frames = 0;	// bf=3
	c->refs = 3;	// refs=3
	c->directpred = 1;	// directpred=1
	c->trellis = 1; // trellis=1
	c->weighted_p_pred = 2; // wpredp=2

	c->flags |= CODEC_FLAG_LOOP_FILTER + CODEC_FLAG_GLOBAL_HEADER;
	c->flags2 |= CODEC_FLAG2_BPYRAMID + CODEC_FLAG2_MIXED_REFS + CODEC_FLAG2_WPRED + CODEC_FLAG2_8X8DCT + CODEC_FLAG2_FASTPSKIP;	// flags2=+bpyramid+mixed_refs+wpred+dct8x8+fastpskip
	c->flags2 |= CODEC_FLAG2_8X8DCT;
	c->flags2 ^= CODEC_FLAG2_8X8DCT;

	c->profile = FF_PROFILE_H264_BASELINE;
	c->level = 30;

	if (oc->oformat->flags & AVFMT_GLOBALHEADER)
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;

	return st;
}

AVFrame *VideoRecorderImpl::alloc_picture(enum PixelFormat pix_fmt, int width, int height)
{
	AVFrame *pict;
	uint8_t *picture_buf;
	int size;

	pict = avcodec_alloc_frame();
	if (!pict)
		return NULL;
	size = avpicture_get_size(pix_fmt, width, height);
	picture_buf = (uint8_t *)av_malloc(size);
	if (!picture_buf) {
		av_free(pict);
		return NULL;
	}
	avpicture_fill((AVPicture *)pict, picture_buf,
				   pix_fmt, width, height);
	return pict;
}

void VideoRecorderImpl::open_video()
{
	AVCodec *codec;
	AVCodecContext *c;

	timestamp_base = 0;
	
	c = video_st->codec;

	codec = avcodec_find_encoder(c->codec_id);
	if (!codec) {
		fprintf(stderr, "codec not found\n");
		exit(1);
	}

	if (avcodec_open(c, codec) < 0) {
		fprintf(stderr, "could not open codec\n");
		exit(1);
	}

	video_outbuf = NULL;
	if (!(oc->oformat->flags & AVFMT_RAWPICTURE)) {
		video_outbuf_size = 200000; // XXX TODO ???
		video_outbuf = (uint8_t *)av_malloc(video_outbuf_size);
	}

	// the AVFrame the YUV frame is stored after conversion
	picture = alloc_picture(c->pix_fmt, c->width, c->height);
	if (!picture) {
		fprintf(stderr, "Could not allocate picture\n");
		exit(1);
	}

	// the src AVFrame before conversion
	tmp_picture = alloc_picture(video_pixfmt, c->width, c->height);
	if (!tmp_picture) {
		fprintf(stderr, "Could not allocate temporary picture\n");
		exit(1);
	}
	
	if(video_pixfmt != PIX_FMT_RGB565LE) {
		fprintf(stderr, "We've hardcoded linesize in tmp_picture for PIX_FMT_RGB565LE only!!");
		exit(1);
	}
	tmp_picture->linesize[0] = c->width * 2;	// fix the linesize for tmp_picture (assuming RGB565)
	
}

bool VideoRecorderImpl::Close()
{
	av_write_trailer(oc);
	
	avcodec_close(video_st->codec);
	av_free(picture->data[0]);
	av_free(picture);
	
	av_free(tmp_picture->data[0]);
	av_free(tmp_picture);
	
	av_free(video_outbuf);
	avcodec_close(audio_st->codec);
	av_free(samples);
	av_free(audio_outbuf);
	
	for(int i = 0; i < oc->nb_streams; i++) {
		av_freep(&oc->streams[i]->codec);
		av_freep(&oc->streams[i]);
	}
	
	avio_close(oc->pb);
	av_free(oc);
}


bool VideoRecorderImpl::SetVideoOptions(VideoFrameFormat fmt, int width, int height, unsigned long bitrate)
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
	video_width = width;
	video_height = height;
	video_bitrate = bitrate;
	return true;
}

bool VideoRecorderImpl::SetAudioOptions(AudioSampleFormat fmt, int channels, unsigned long samplerate, unsigned long bitrate)
{
	switch(fmt) {
		case AudioSampleFormatU8: audio_sample_format=AV_SAMPLE_FMT_U8; audio_sample_size=1; break;
		case AudioSampleFormatS16: audio_sample_format=AV_SAMPLE_FMT_S16; audio_sample_size=2; break;
		case AudioSampleFormatS32: audio_sample_format=AV_SAMPLE_FMT_S32; audio_sample_size=4; break;
		case AudioSampleFormatFLT: audio_sample_format=AV_SAMPLE_FMT_FLT; audio_sample_size=4; break;
		case AudioSampleFormatDBL: audio_sample_format=AV_SAMPLE_FMT_DBL; audio_sample_size=8; break;
		default: LOG("Unknown sample format passed to SetAudioOptions!"); return false;
	}
	audio_channels = channels;
	audio_bit_rate = bitrate;
	audio_sample_rate = samplerate;
	return true;
}

bool VideoRecorderImpl::Start()
{
	
}

void VideoRecorderImpl::SupplyAudioSamples(const void *sampleData, unsigned long numSamples)
{
	AVCodecContext *c = audio_st->codec;

	uint8_t *samplePtr = (uint8_t *)sampleData;		// using a byte pointer
	
	// numSamples is supplied by the codec.. should be c->frame_size (1024 for AAC)
	// if it's more we go through it c->frame_size samples at a time
	while(numSamples) {
		AVPacket pkt;
		av_init_packet(&pkt);	// need to init packet every time so all the values (such as pts) are re-initialized
		
		// if we have enough samples for a frame, we write out c->frame_size number of samples (ie: one frame) to the output context
		if( (numSamples + audio_input_leftover_samples) >= c->frame_size) {
			// audio_input_leftover_samples contains the number of samples already in our "samples" array, left over from last time
			// we copy the remaining samples to fill up the frame to the complete frame size
			int num_new_samples = c->frame_size - audio_input_leftover_samples;
			
			memcpy((uint8_t *)samples + (audio_input_leftover_samples * audio_sample_size * audio_channels), samplePtr, num_new_samples * audio_sample_size * audio_channels);
			numSamples -= num_new_samples;
			samplePtr += (num_new_samples * audio_sample_size * audio_channels);
			audio_input_leftover_samples = 0;
			
			pkt.flags |= AV_PKT_FLAG_KEY;
			pkt.stream_index = audio_st->index;
			pkt.data = audio_outbuf;
			pkt.size = avcodec_encode_audio(c, audio_outbuf, audio_outbuf_size, samples);

			if (c->coded_frame && c->coded_frame->pts != AV_NOPTS_VALUE)
				pkt.pts = av_rescale_q(c->coded_frame->pts, c->time_base, audio_st->time_base);

			if(av_interleaved_write_frame(oc, &pkt) != 0) {
				fprintf(stderr, "Error while writing audio frame\n");
				exit(1);
			}
		}
		else {
			// if we didn't have enough samples for a frame, we copy over however many we had and update audio_input_leftover_samples
			int num_new_samples = c->frame_size - audio_input_leftover_samples;
			if(numSamples < num_new_samples)
				num_new_samples = numSamples;
				
			memcpy((uint8_t *)samples + (audio_input_leftover_samples * audio_sample_size * audio_channels), samplePtr, num_new_samples * audio_sample_size * audio_channels);
			numSamples -= num_new_samples;
			samplePtr += (num_new_samples * audio_sample_size * audio_channels);
			audio_input_leftover_samples += num_new_samples;
		}
	}
}

void VideoRecorderImpl::SupplyVideoFrame(const void *frameData, unsigned long numBytes, unsigned long timestamp)
{
	AVCodecContext *c = video_st->codec;
	
	memcpy(tmp_picture->data[0], frameData, numBytes);
	
	// if the input pixel format is not YUV420P, we'll assume
	// it's stored in tmp_picture, so we'll convert it to YUV420P
	// and store it in "picture"
	// if it's already in YUV420P format we'll assume it's stored in
	// "picture" from before
	if(video_pixfmt != PIX_FMT_YUV420P) {
		static struct SwsContext *img_convert_ctx;
		if (img_convert_ctx == NULL) {
			// convert whatever our current format is to YUV420P which x264 likes
			img_convert_ctx = sws_getContext(video_width, video_height, video_pixfmt, c->width, c->height, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
			if(img_convert_ctx == NULL) {
				fprintf(stderr, "Cannot initialize the conversion context\n");
				exit(1);
			}
		}

		sws_scale(img_convert_ctx, tmp_picture->data, tmp_picture->linesize, 0, video_height, picture->data, picture->linesize);
	}

	if(timestamp_base == 0)
		timestamp_base = timestamp;
	
	picture->pts = 90 * (timestamp - timestamp_base);	// assuming millisecond timestamp and 90 kHz timebase
	
	int out_size = avcodec_encode_video(c, video_outbuf, video_outbuf_size, picture);
	
	if(out_size > 0) {
		AVPacket pkt;
		
		av_init_packet(&pkt);
		
		if (c->coded_frame->pts != AV_NOPTS_VALUE)
			pkt.pts = av_rescale_q(c->coded_frame->pts, c->time_base, video_st->time_base);

		if(c->coded_frame->key_frame)
			pkt.flags |= AV_PKT_FLAG_KEY;

		pkt.stream_index = video_st->index;
		pkt.data = video_outbuf;
		pkt.size = out_size;
		
		if(av_interleaved_write_frame(oc, &pkt) != 0) {
			fprintf(stderr, "Unable to write video frame");
			exit(1);
		}
	}
}

VideoRecorder* VideoRecorder::New()
{
	return (VideoRecorder*)(new VideoRecorderImpl);
}

} // namespace AVR

float t = 0;
float tincr = 2 * M_PI * 110.0 / 44100;
float tincr2 = 2 * M_PI * 110.0 / 44100 / 44100;

void fill_audio_frame(int16_t *samples, int frame_size, int nb_channels)
{
	int j, i, v;
	int16_t *q;

	q = samples;
	for (j = 0; j < frame_size; j++) {
		v = (int)(sin(t) * 10000);
		for(i = 0; i < nb_channels; i++)
			*q++ = v;
		t += tincr;
		tincr += tincr2;
	}
}

void fill_yuv_image(AVFrame *pict, int frame_index, int width, int height)
{
	int x, y, i;

	i = frame_index;

	/* Y */
	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;
		}
	}

	/* Cb and Cr */
	for (y = 0; y < height/2; y++) {
		for (x = 0; x < width/2; x++) {
			pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
			pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
		}
	}
}

#define RGB565(r,g,b) (uint16_t)( ((red & 0x1F) << 11) | ((green & 0x3F) << 5) | (blue & 0x1F) ) 

void fill_rgb_image(uint8_t *pixels, int i, int width, int height)
{	
	int x, y;
	
	for(y = 0; y < height; y++) {
		for(x = 0; x < width; x++) {
			
			uint8_t red = x + y + i * 3;
			uint8_t green = x + y + i * 3;
			uint8_t blue = x + y + i * 3;

			uint16_t pixel = RGB565(red, green, blue);

			// assume linesize is width*2
			pixels[y * (width*2) + x*2 + 0] = (uint8_t)(pixel);		// lower order bits
			pixels[y * (width*2) + x*2 + 1] = (uint8_t)(pixel >> 8);	// higher order bits
		}
	}
}

int main()
{
	AVR::VideoRecorder *recorder = new AVR::VideoRecorderImpl();

	recorder->SetAudioOptions(AVR::AudioSampleFormatS16, 2, 44100, 64000);
	recorder->SetVideoOptions(AVR::VideoFrameFormatRGB565LE, 640, 480, 400000);
	recorder->Open("testing.mp4", true, true);

	int16_t *sound_buffer = new int16_t[2048 * 2];
	uint8_t *video_buffer = new uint8_t[640 * 480 * 2];
	for(int i = 0; i < 200; i++) {
		fill_audio_frame(sound_buffer, 900, 2);
		recorder->SupplyAudioSamples(sound_buffer, 900);

		fill_rgb_image(video_buffer, i, 640, 480);
		recorder->SupplyVideoFrame(video_buffer, 640*480*2, (25 * i)+1);
	}
	
	delete video_buffer;
	delete sound_buffer;

	recorder->Close();

	std::cout << "Sup" << std::endl;

	delete recorder;
	
	return 0;
}
