#include "media-decoder.h"
#include "watermark.h"

bool decoder_open(struct irl_source_data *data)
{
	data->video_stream_idx = -1;
	data->audio_stream_idx = -1;

	for (unsigned i = 0; i < data->fmt_ctx->nb_streams; i++) {
		AVCodecParameters *par = data->fmt_ctx->streams[i]->codecpar;

		if (par->codec_type == AVMEDIA_TYPE_VIDEO &&
		    data->video_stream_idx < 0) {
			const AVCodec *codec =
				avcodec_find_decoder(par->codec_id);
			if (!codec)
				continue;

			AVCodecContext *ctx = avcodec_alloc_context3(codec);
			if (!ctx)
				continue;

			avcodec_parameters_to_context(ctx, par);
			ctx->thread_count = 2;

			if (avcodec_open2(ctx, codec, NULL) < 0) {
				avcodec_free_context(&ctx);
				continue;
			}

		data->video_dec_ctx = ctx;
		data->video_stream_idx = (int)i;
		snprintf(data->stats_video_codec,
			 sizeof(data->stats_video_codec), "%s",
			 codec->name);
		data->stats_video_width = par->width;
		data->stats_video_height = par->height;
		data->stats_video_pixfmt[0] = '\0';
		dbg_log(LOG_DEBUG,
		     "[%s] Video stream #%u: %s %dx%d",
		     PLUGIN_NAME, i, codec->name, par->width,
		     par->height);
		} else if (par->codec_type == AVMEDIA_TYPE_AUDIO &&
			   data->audio_stream_idx < 0) {
			const AVCodec *codec =
				avcodec_find_decoder(par->codec_id);
			if (!codec)
				continue;

			AVCodecContext *ctx = avcodec_alloc_context3(codec);
			if (!ctx)
				continue;

			avcodec_parameters_to_context(ctx, par);

			if (avcodec_open2(ctx, codec, NULL) < 0) {
				avcodec_free_context(&ctx);
				continue;
			}

		data->audio_dec_ctx = ctx;
		data->audio_stream_idx = (int)i;
		snprintf(data->stats_audio_codec,
			 sizeof(data->stats_audio_codec), "%s",
			 codec->name);
		data->stats_audio_sample_rate = par->sample_rate;
		dbg_log(LOG_DEBUG,
		     "[%s] Audio stream #%u: %s %dHz",
		     PLUGIN_NAME, i, codec->name,
		     par->sample_rate);
		}
	}

	if (data->video_stream_idx < 0) {
		dbg_log(LOG_WARNING, "[%s] No video stream found", PLUGIN_NAME);
		return false;
	}

	return true;
}

void decoder_close(struct irl_source_data *data)
{
	if (data->video_dec_ctx) {
		avcodec_free_context(&data->video_dec_ctx);
		data->video_dec_ctx = NULL;
	}
	if (data->audio_dec_ctx) {
		avcodec_free_context(&data->audio_dec_ctx);
		data->audio_dec_ctx = NULL;
	}
	if (data->sws_ctx) {
		sws_freeContext(data->sws_ctx);
		data->sws_ctx = NULL;
	}
	if (data->swr_ctx) {
		swr_free(&data->swr_ctx);
		data->swr_ctx = NULL;
	}
	if (data->video_dst_data[0]) {
		av_freep(&data->video_dst_data[0]);
		memset(data->video_dst_data, 0, sizeof(data->video_dst_data));
		memset(data->video_dst_linesize, 0,
		       sizeof(data->video_dst_linesize));
	}

	data->video_stream_idx = -1;
	data->audio_stream_idx = -1;
	data->sws_width = 0;
	data->sws_height = 0;
	data->swr_sample_rate = 0;
	data->swr_channels = 0;
}

static enum video_format ffmpeg_to_obs_format(enum AVPixelFormat fmt,
					       bool *full_range)
{
	*full_range = false;
	switch (fmt) {
	case AV_PIX_FMT_YUV420P:
		return VIDEO_FORMAT_I420;
	case AV_PIX_FMT_YUVJ420P:
		*full_range = true;
		return VIDEO_FORMAT_I420;
	case AV_PIX_FMT_NV12:
		return VIDEO_FORMAT_NV12;
	case AV_PIX_FMT_YUV422P:
		return VIDEO_FORMAT_I422;
	case AV_PIX_FMT_YUVJ422P:
		*full_range = true;
		return VIDEO_FORMAT_I422;
	case AV_PIX_FMT_YUV444P:
		return VIDEO_FORMAT_I444;
	case AV_PIX_FMT_YUVJ444P:
		*full_range = true;
		return VIDEO_FORMAT_I444;
	case AV_PIX_FMT_UYVY422:
		return VIDEO_FORMAT_UYVY;
	case AV_PIX_FMT_YUYV422:
		return VIDEO_FORMAT_YUY2;
	case AV_PIX_FMT_RGBA:
		return VIDEO_FORMAT_RGBA;
	case AV_PIX_FMT_BGRA:
		return VIDEO_FORMAT_BGRA;
	default:
		return VIDEO_FORMAT_NONE;
	}
}

static void output_video_frame(struct irl_source_data *data, AVFrame *frame)
{
	int w = frame->width;
	int h = frame->height;
	enum AVPixelFormat src_fmt = (enum AVPixelFormat)frame->format;
	bool full_range = false;

	enum video_format obs_fmt = ffmpeg_to_obs_format(src_fmt, &full_range);

	if (!data->stats_video_pixfmt[0])
		snprintf(data->stats_video_pixfmt,
			 sizeof(data->stats_video_pixfmt), "%s",
			 av_get_pix_fmt_name(src_fmt));

	if (obs_fmt != VIDEO_FORMAT_NONE) {
		if (w != data->sws_width || h != data->sws_height ||
		    src_fmt != data->sws_src_fmt) {
			data->sws_width = w;
			data->sws_height = h;
			data->sws_src_fmt = src_fmt;
			dbg_log(LOG_DEBUG,
			     "[%s] Video: %s %dx%d -> direct output (fmt=%d, full_range=%d)",
			     PLUGIN_NAME,
			     av_get_pix_fmt_name(src_fmt), w, h,
			     obs_fmt, full_range);
		}

		if (data->show_watermark &&
		    av_frame_make_writable(frame) >= 0)
			watermark_draw(frame);

		enum video_colorspace cs = (h >= 720) ? VIDEO_CS_709
						      : VIDEO_CS_601;

		struct obs_source_frame obs_frame = {0};
		for (int i = 0; i < MAX_AV_PLANES && frame->data[i]; i++) {
			obs_frame.data[i] = frame->data[i];
			obs_frame.linesize[i] =
				(uint32_t)frame->linesize[i];
		}
		obs_frame.width = (uint32_t)w;
		obs_frame.height = (uint32_t)h;
		obs_frame.format = obs_fmt;
		obs_frame.full_range = full_range;
		obs_frame.timestamp = os_gettime_ns();

		video_format_get_parameters_for_format(
			cs, obs_fmt, full_range, obs_frame.color_matrix,
			obs_frame.color_range_min,
			obs_frame.color_range_max);

		obs_source_output_video(data->source, &obs_frame);
		data->last_frame_time_ns = obs_frame.timestamp;
		return;
	}

	if (w != data->sws_width || h != data->sws_height ||
	    src_fmt != data->sws_src_fmt) {
		sws_freeContext(data->sws_ctx);
		data->sws_ctx = sws_getContext(w, h, src_fmt, w, h,
					       AV_PIX_FMT_NV12,
					       SWS_BILINEAR, NULL, NULL,
					       NULL);

		av_freep(&data->video_dst_data[0]);
		memset(data->video_dst_data, 0, sizeof(data->video_dst_data));
		av_image_alloc(data->video_dst_data, data->video_dst_linesize,
			       w, h, AV_PIX_FMT_NV12, 32);

		data->sws_width = w;
		data->sws_height = h;
		data->sws_src_fmt = src_fmt;

		dbg_log(LOG_DEBUG,
		     "[%s] Video: %s %dx%d -> NV12 sws conversion",
		     PLUGIN_NAME,
		     av_get_pix_fmt_name(src_fmt), w, h);
	}

	if (!data->sws_ctx || !data->video_dst_data[0])
		return;

	sws_scale(data->sws_ctx, (const uint8_t *const *)frame->data,
		  frame->linesize, 0, h, data->video_dst_data,
		  data->video_dst_linesize);

	if (data->show_watermark) {
		AVFrame tmp = {0};
		tmp.data[0] = data->video_dst_data[0];
		tmp.data[1] = data->video_dst_data[1];
		tmp.linesize[0] = data->video_dst_linesize[0];
		tmp.linesize[1] = data->video_dst_linesize[1];
		tmp.width = w;
		tmp.height = h;
		tmp.format = AV_PIX_FMT_NV12;
		watermark_draw(&tmp);
	}

	enum video_colorspace cs = (h >= 720) ? VIDEO_CS_709 : VIDEO_CS_601;

	struct obs_source_frame obs_frame = {0};
	obs_frame.data[0] = data->video_dst_data[0];
	obs_frame.data[1] = data->video_dst_data[1];
	obs_frame.linesize[0] = (uint32_t)data->video_dst_linesize[0];
	obs_frame.linesize[1] = (uint32_t)data->video_dst_linesize[1];
	obs_frame.width = (uint32_t)w;
	obs_frame.height = (uint32_t)h;
	obs_frame.format = VIDEO_FORMAT_NV12;
	obs_frame.timestamp = os_gettime_ns();

	video_format_get_parameters_for_format(
		cs, VIDEO_FORMAT_NV12, false, obs_frame.color_matrix,
		obs_frame.color_range_min, obs_frame.color_range_max);

	obs_source_output_video(data->source, &obs_frame);
	data->last_frame_time_ns = obs_frame.timestamp;
}

static void output_audio_frame(struct irl_source_data *data, AVFrame *frame)
{
	int in_rate = frame->sample_rate;
	int in_ch = frame->ch_layout.nb_channels;

	if (!data->swr_ctx || in_rate != data->swr_sample_rate ||
	    in_ch != data->swr_channels) {
		swr_free(&data->swr_ctx);

		AVChannelLayout out_layout = AV_CHANNEL_LAYOUT_STEREO;

		int ret = swr_alloc_set_opts2(
			&data->swr_ctx, &out_layout, AV_SAMPLE_FMT_FLTP,
			48000, &frame->ch_layout,
			(enum AVSampleFormat)frame->format, in_rate, 0, NULL);
		if (ret < 0 || swr_init(data->swr_ctx) < 0) {
			swr_free(&data->swr_ctx);
			return;
		}

		data->swr_sample_rate = in_rate;
		data->swr_channels = in_ch;
	}

	int out_samples =
		swr_get_out_samples(data->swr_ctx, frame->nb_samples);
	if (out_samples <= 0)
		return;

	uint8_t *out_buf[2] = {NULL, NULL};
	av_samples_alloc(out_buf, NULL, 2, out_samples, AV_SAMPLE_FMT_FLTP, 0);

	out_samples = swr_convert(data->swr_ctx, out_buf, out_samples,
				  (const uint8_t **)frame->extended_data,
				  frame->nb_samples);
	if (out_samples <= 0) {
		av_freep(&out_buf[0]);
		return;
	}

	struct obs_source_audio obs_audio = {0};
	obs_audio.data[0] = out_buf[0];
	obs_audio.data[1] = out_buf[1];
	obs_audio.frames = (uint32_t)out_samples;
	obs_audio.speakers = SPEAKERS_STEREO;
	obs_audio.format = AUDIO_FORMAT_FLOAT_PLANAR;
	obs_audio.samples_per_sec = 48000;
	obs_audio.timestamp = os_gettime_ns();

	obs_source_output_audio(data->source, &obs_audio);

	av_freep(&out_buf[0]);
}

bool decoder_decode_packet(struct irl_source_data *data, AVPacket *pkt)
{
	if (pkt->stream_index == data->video_stream_idx &&
	    data->video_dec_ctx) {
		int send_ret =
			avcodec_send_packet(data->video_dec_ctx, pkt);
		data->dec_vid_pkt_count++;

		if (send_ret < 0) {
			if (data->dec_vid_pkt_count <= 5)
				dbg_log(LOG_WARNING,
				     "[%s] avcodec_send_packet failed: %d (pkt #%d, size=%d)",
				     PLUGIN_NAME, send_ret,
				     data->dec_vid_pkt_count, pkt->size);
			return true;
		}

		AVFrame *frame = av_frame_alloc();
		while (avcodec_receive_frame(data->video_dec_ctx, frame) == 0) {
			data->dec_vid_frame_count++;
			if (data->dec_vid_frame_count <= 3 ||
			    (data->dec_vid_frame_count % 300 == 0))
				dbg_log(LOG_DEBUG,
				     "[%s] Video frame #%d decoded (fmt=%d %dx%d)",
				     PLUGIN_NAME,
				     data->dec_vid_frame_count,
				     frame->format,
				     frame->width, frame->height);
			output_video_frame(data, frame);
			data->stats_total_frames++;
			av_frame_unref(frame);
		}
		av_frame_free(&frame);

		if (data->dec_vid_pkt_count == 30 &&
		    data->dec_vid_frame_count == 0)
			dbg_log(LOG_WARNING,
			     "[%s] 30 video packets sent but 0 frames decoded",
			     PLUGIN_NAME);

		return true;

	} else if (pkt->stream_index == data->audio_stream_idx &&
		   data->audio_dec_ctx) {
		if (avcodec_send_packet(data->audio_dec_ctx, pkt) < 0)
			return true;

		AVFrame *frame = av_frame_alloc();
		while (avcodec_receive_frame(data->audio_dec_ctx, frame) ==
		       0) {
			output_audio_frame(data, frame);
			av_frame_unref(frame);
		}
		av_frame_free(&frame);
		return true;
	}

	return false;
}
