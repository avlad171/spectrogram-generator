#include "ffmpeg_encoder.h"
#include <iostream>

using namespace std;


ffmpeg_encoder::ffmpeg_encoder(const char * filename, int width = 720, int height = 576, int bitrate = 2000, int fps = 60, int audio_bitrate = 64000, int audio_samplerate = 44100)
{
    this->filename = filename;
    this->width = width;
    this->height = height;
    this->bitrate = bitrate;
    this->fps = fps;
    this->frameCounter = 0;
    this->next_pts = 0;
    this->samples_count = 0;
    this->audio_bitrate = audio_bitrate;
    this->audio_samplerate = audio_samplerate;
    //cout<<"[CTOR] "<<filename<<" "<<width<<" "<<height<<" "<<bitrate<<" "<<fps<<"\n";
}

bool ffmpeg_encoder::init(AVCodecID video_codec_id, AVCodecID audio_codec_id)
{
    //av_register_all();
    //avcodec_register_all();

    //select format from filename extension
    oformat = av_guess_format(NULL, "a.mp4", NULL);
    if (!oformat)
    {
        cout << "Error av_guess_format()" << endl;
        return 0;
    }

    if (avformat_alloc_output_context2(&ofctx, oformat, NULL, filename) < 0)
    {
        cout << "Error avformat_alloc_output_context2()" << endl;
        return 0;
    }

    //create video stream
    vst = avformat_new_stream(ofctx, 0);
    if (!vst)
    {
        cout << "Error avformat_new_stream() video" << endl;
        return 0;
    }
    vst->id = ofctx->nb_streams - 1;
    cout<<"Video stream ID: "<<vst->id<<endl;
    //find encoder for the required video codec
    AVCodec *pvCodec = avcodec_find_encoder(video_codec_id);
    if (!pvCodec)
    {
        cout << "Error avcodec_find_encoder() video" << endl;
        return 0;
    }

    //alloc context for video codec
    cctx = avcodec_alloc_context3(pvCodec);
    if (!cctx)
    {
        cout << "Error avcodec_alloc_context3() video" << endl;
        return 0;
    }

    //setup video parameters 2
    cctx->codec_id = video_codec_id;
    cctx->bit_rate = bitrate;
    cctx->width = width;
    cctx->height = height;
    vst->time_base = (AVRational){ 1, 90000 };
    //vst->avg_frame_rate = (AVRational){fps, 1};
    cctx->time_base = vst->time_base;
    cctx->gop_size = 12;
    cctx->pix_fmt = AV_PIX_FMT_YUV420P;
    cctx->max_b_frames = 1;
    cctx->framerate = (AVRational){ fps, 1 };


    // Some formats want stream headers to be separate
    if (ofctx->oformat->flags & AVFMT_GLOBALHEADER)
        cctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;



    //create audio stream
    ast = avformat_new_stream(ofctx, 0);
    if(!ast)
    {
        cout << "Error avformat_new_stream() audio" << endl;
        return 0;
    }
    ast->id = ofctx->nb_streams - 1;
    cout<<"Audio stream ID: "<<ast->id<<endl;

    //find encoder for the required audio codec
    AVCodec *paCodec = avcodec_find_encoder(audio_codec_id);
    if (!paCodec)
    {
        cout << "Error avcodec_find_encoder() audio" << endl;
        return 0;
    }


    //alloc context for audio codec
    acctx = avcodec_alloc_context3(paCodec);
    if (!acctx)
    {
        cout << "Error avcodec_alloc_context3() audio" << endl;
        return 0;
    }

    acctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    acctx->bit_rate = audio_bitrate;
    acctx->sample_rate = audio_samplerate;
    acctx->channel_layout = AV_CH_LAYOUT_MONO;
    acctx->channels = av_get_channel_layout_nb_channels(acctx->channel_layout);
    ast->time_base = (AVRational){ 1, audio_samplerate };
    acctx->time_base = ast->time_base;

    // Some formats want stream headers to be separate
    if (ofctx->oformat->flags & AVFMT_GLOBALHEADER)
        acctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;


    //open video codec
    if (avcodec_open2(cctx, pvCodec, NULL) < 0)
    {
        cout << "Error avcodec_open2() video" << endl;
        return 0;
    }

    //copy stream parameters to the muxer
    if(avcodec_parameters_from_context(vst->codecpar, cctx) < 0)
    {
        cout<<"Could not copy video stream parameters! "<<endl;
        return 0;
    }

    //open audio codec
    if (avcodec_open2(acctx, paCodec, NULL) < 0)
    {
        cout << "Error avcodec_open2() audio" << endl;
        return 0;
    }

    //copy stream parameters to the muxer
    if(avcodec_parameters_from_context(ast->codecpar, acctx) < 0)
    {
        cout<<"Could not copy video stream parameters! "<<endl;
        return 0;
    }

    av_dump_format(ofctx, 0, filename, 1);

    //open file
    if (!(oformat->flags & AVFMT_NOFILE))
    {
        if (avio_open(&ofctx->pb, filename, AVIO_FLAG_WRITE) < 0)
        {
            cout << "Error avio_open()" << endl;
            return 0;
        }
    }

    if (avformat_write_header(ofctx, NULL) < 0)
    {
        cout << "Error avformat_write_header()" << endl;
        return 0;
    }

    return 1;
}

bool ffmpeg_encoder::encode_video_frame(const uint8_t * data)
{
    int err;
    if (!videoFrame)
    {
        videoFrame = av_frame_alloc();
        videoFrame->format = AV_PIX_FMT_YUV420P;
        videoFrame->width = cctx->width;
        videoFrame->height = cctx->height;
        if ((err = av_frame_get_buffer(videoFrame, 32)) < 0)
        {
            cout << "Failed to allocate picture" << err << endl;
            return 0;
        }
    }
    if (!swsCtx)
    {
        swsCtx = sws_getContext(cctx->width, cctx->height, AV_PIX_FMT_RGB24, cctx->width, cctx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, 0, 0, 0);
    }
    int inLinesize[1] = { 3 * cctx->width };
    // From RGB to YUV
    sws_scale(swsCtx, (const uint8_t* const*)&data, inLinesize, 0, cctx->height, videoFrame->data, videoFrame->linesize);
    videoFrame->pts = (1.0 / (float)fps) * 90000 * (frameCounter++);
    //videoFrame->pts = (frameCounter++) * st->time_base.den / (st->time_base.num * fps);
    //videoFrame->pts = frameCounter++;
    //cout << videoFrame->pts << " " << cctx->time_base.num << " " << cctx->time_base.den << " " << frameCounter << endl;

    if ((err = avcodec_send_frame(cctx, videoFrame)) < 0)
    {
        cout << "Failed to send video frame" << err << endl;
        return 0;
    }

    AVPacket pkt = {0};
    av_init_packet(&pkt);
    if (avcodec_receive_packet(cctx, &pkt) == 0)
    {
        pkt.stream_index = vst->index;
        av_interleaved_write_frame(ofctx, &pkt);
        av_packet_unref(&pkt);
    }
    return 1;
}

int ffmpeg_encoder::get_audio_frame_size()
{
    if(acctx)
    {
        return acctx->frame_size;
    }

    else
    {
        return 0;
    }
}
bool ffmpeg_encoder::encode_audio_frame(const float * samples)
{
    //frame holding input audio
    int err;
    if (!audioFrame)
    {
        audioFrame = av_frame_alloc();
        audioFrame->nb_samples = acctx->frame_size;
        audioFrame->format = acctx->sample_fmt;
        audioFrame->channel_layout = acctx->channel_layout;
        audioFrame->sample_rate = acctx->sample_rate;

        if ((err = av_frame_get_buffer(audioFrame, 0)) < 0)
        {
            cout << "Failed to allocate buffer for audio frame" << err << endl;
            return 0;
        }
    }

    //add data to the frame, which is acctx->frame_size samples
    float * frameSamples = (float *)audioFrame->extended_data[0];
    for(int i = 0; i < audioFrame->nb_samples; ++i)
        frameSamples[i] = samples[i];


    audioFrame->pts = next_pts;
    next_pts += audioFrame->nb_samples;
    cout<<"Pts: "<<audioFrame->pts<<" ";

    //audioFrame->pts = av_rescale_q(samples_count, (AVRational){1, acctx->sample_rate}, acctx->time_base);
    //samples_count += audioFrame->nb_samples;

    if ((err = avcodec_send_frame(acctx, audioFrame)) < 0)
    {
        cout << "Failed to send video frame" << err << endl;
        return 0;
    }

    AVPacket pkt = {0};
    av_init_packet(&pkt);
    while(err >= 0)
    {
        err = avcodec_receive_packet(acctx, &pkt);
        if (err == AVERROR(EAGAIN) || err == AVERROR_EOF)
            break;
        else if (err < 0)
        {
            cout<<"Error encoding audio frame "<<err<<"\n";
        }

        pkt.stream_index = ast->index;      //i spent 2 days debugging until i figured out this was missing
        av_interleaved_write_frame(ofctx, &pkt);
        av_packet_unref(&pkt);
    }
    return 1;
}


bool ffmpeg_encoder::finish()
{
    //DELAYED FRAMES
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);

    while(true)
    {
        avcodec_send_frame(cctx, NULL);
        if (avcodec_receive_packet(cctx, &pkt) == 0)
        {
            av_interleaved_write_frame(ofctx, &pkt);
            av_packet_unref(&pkt);
        }
        else
        {
            break;
        }
    }

    while(true)
    {
        avcodec_send_frame(acctx, NULL);
        if (avcodec_receive_packet(acctx, &pkt) == 0)
        {
            av_interleaved_write_frame(ofctx, &pkt);
            av_packet_unref(&pkt);
        }
        else
        {
            break;
        }
    }

    av_write_trailer(ofctx);
    if (!(oformat->flags & AVFMT_NOFILE))
    {
        int err = avio_close(ofctx->pb);
        if (err < 0)
        {
            cout << "Failed to close file" << err << endl;
            return 0;
        }
    }
    return 1;
}

ffmpeg_encoder::~ffmpeg_encoder()
{
    if (videoFrame)
    {
        av_frame_free(&videoFrame);
    }

    if (audioFrame)
    {
        av_frame_free(&audioFrame);
    }

    if (cctx)
    {
        avcodec_free_context(&cctx);
    }

    if(acctx)
    {
        avcodec_free_context(&acctx);
    }

    if (ofctx)
    {
        avformat_free_context(ofctx);
    }

    if (swsCtx)
    {
        sws_freeContext(swsCtx);
    }
}

/*
0 1 1 1
3000 1 1 2
6000 1 1 3
9000 1 1 4
12000 1 1 5
15000 1 1 6
18000 1 1 7
21000 1 1 8
24000 1 1 9
27000 1 1 10
30000 1 1 11
33000 1 1 12
36000 1 1 13
39000 1 1 14
42000 1 1 15
45000 1 1 16
48000 1 1 17
51000 1 1 18
54000 1 1 19
57000 1 1 20
60000 1 1 21
63000 1 1 22
66000 1 1 23
69000 1 1 24
72000 1 1 25
75000 1 1 26
78000 1 1 27
81000 1 1 28
84000 1 1 29
87000 1 1 30
90000 1 1 31
93000 1 1 32
96000 1 1 33
99000 1 1 34
102000 1 1 35
105000 1 1 36
108000 1 1 37
111000 1 1 38
114000 1 1 39
117000 1 1 40
120000 1 1 41
123000 1 1 42
126000 1 1 43
129000 1 1 44
132000 1 1 45
135000 1 1 46
138000 1 1 47
141000 1 1 48
144000 1 1 49
147000 1 1 50
150000 1 1 51
153000 1 1 52
156000 1 1 53
159000 1 1 54
162000 1 1 55
165000 1 1 56
168000 1 1 57
171000 1 1 58
174000 1 1 59
177000 1 1 60
180000 1 1 61
183000 1 1 62
186000 1 1 63
189000 1 1 64
192000 1 1 65
195000 1 1 66
198000 1 1 67
201000 1 1 68
204000 1 1 69
207000 1 1 70
210000 1 1 71
213000 1 1 72
216000 1 1 73
219000 1 1 74
222000 1 1 75
225000 1 1 76
228000 1 1 77
231000 1 1 78
234000 1 1 79
237000 1 1 80
240000 1 1 81
243000 1 1 82
246000 1 1 83
249000 1 1 84
252000 1 1 85
255000 1 1 86
258000 1 1 87
261000 1 1 88
264000 1 1 89
267000 1 1 90
270000 1 1 91
273000 1 1 92
276000 1 1 93
279000 1 1 94
282000 1 1 95
285000 1 1 96
288000 1 1 97
291000 1 1 98
294000 1 1 99
297000 1 1 100
*/
