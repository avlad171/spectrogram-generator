#ifndef FFMPEG_ENCODER_H_INCLUDED
#define FFMPEG_ENCODER_H_INCLUDED

#include <inttypes.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libswresample/swresample.h>
}

class ffmpeg_encoder
{
    const char * filename = nullptr;

    AVFrame* videoFrame = nullptr;
    AVFrame* audioFrame = nullptr;
    AVCodecContext* cctx = nullptr;
    AVCodecContext* acctx = nullptr;
    SwsContext* swsCtx = nullptr;
    AVFormatContext* ofctx = nullptr;
    AVOutputFormat* oformat = nullptr;
    AVStream* vst = nullptr;
    AVStream* ast = nullptr;

    //video
    int frameCounter;
    int fps;
    int width;
    int height;
    int bitrate;

    //audio
    int audio_bitrate;
    int audio_samplerate;
    int next_pts;
    int samples_count;

public:
    bool encode_video_frame(const uint8_t *);
    bool encode_audio_frame(const float *);
    bool init(AVCodecID video_codec_id, AVCodecID audio_codec_id);
    int get_audio_frame_size();
    bool finish();
    ffmpeg_encoder(const char * filename, int width, int height, int video_bitrate, int fps, int audio_bitrate, int audio_samplerate);
    ~ffmpeg_encoder();
};

#endif // FFMPEG_ENCODER_H_INCLUDED
