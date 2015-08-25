/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 * This file contains the WEBRTC H264 wrapper implementation
 *
 */

#include "h264_impl.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>

#include "webrtc/common.h"
#include "webrtc/common_video/libyuv/include/webrtc_libyuv.h"
#include "webrtc/modules/interface/module_common_types.h"
#include "webrtc/system_wrappers/interface/trace.h"
#include "webrtc/system_wrappers/interface/tick_util.h"
#include "webrtc/system_wrappers/interface/trace_event.h"

namespace webrtc {

	H264Encoder* H264Encoder::Create() {
		return new H264EncoderImpl();
	}

	H264EncoderImpl::H264EncoderImpl()
		: encoded_image_(),
		encoded_complete_callback_(NULL),
		inited_(false)
#if USEOPENH264
		,encoder_(NULL)
#elif USEX264
		,encoder_(NULL)
		, nal(NULL)
#endif
	{
		memset(&codec_, 0, sizeof(codec_));
	}

	H264EncoderImpl::~H264EncoderImpl() {
		Release();
	}

	int H264EncoderImpl::Release() {
		if (encoded_image_._buffer != NULL) {
			delete[] encoded_image_._buffer;
			encoded_image_._buffer = NULL;
		}
#if USEOPENH264
		if (encoder_ != NULL) {
			encoder_->Uninitialize();
			WelsDestroySVCEncoder(encoder_);
			encoder_ = NULL;
		}
#elif USEX264
			if (encoder_ != NULL) {
				x264_encoder_close(encoder_);
				encoder_ = NULL;
			}
#endif
		inited_ = false;
		return WEBRTC_VIDEO_CODEC_OK;
	}

	int H264EncoderImpl::SetRates(uint32_t new_bitrate_kbit,
		uint32_t new_framerate) {
		WEBRTC_TRACE(webrtc::kTraceApiCall, webrtc::kTraceVideoCoding, -1,
			"H264EncoderImpl::SetRates(%d, %d)", new_bitrate_kbit, new_framerate);
		if (!inited_) {
			return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
		}
		if (new_framerate < 1) {
			return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
		}
		// update bit rate
		if (codec_.maxBitrate > 0 && new_bitrate_kbit > codec_.maxBitrate) {
			new_bitrate_kbit = codec_.maxBitrate;
		}
		return WEBRTC_VIDEO_CODEC_OK;
	}

	int H264EncoderImpl::InitEncode(const VideoCodec* inst,
		int number_of_cores,
		size_t max_payload_size) {
		if (inst == NULL) {
			return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
		}
		if (inst->maxFramerate < 1) {
			return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
		}
		// allow zero to represent an unspecified maxBitRate
		if (inst->maxBitrate > 0 && inst->startBitrate > inst->maxBitrate) {
			return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
		}
		if (inst->width < 1 || inst->height < 1) {
			return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
		}
		if (number_of_cores < 1) {
			return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
		}

		int ret_val = Release();
		if (ret_val < 0) {
			return ret_val;
		}
#if USEOPENH264
		if (encoder_ == NULL) {
			ret_val = WelsCreateSVCEncoder(&encoder_);

			if (ret_val != 0) {
				WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
					"H264EncoderImpl::InitEncode() fails to create encoder ret_val %d",
					ret_val);
				return WEBRTC_VIDEO_CODEC_ERROR;
			}
		}
		SEncParamBase param;
		memset(&param, 0, sizeof(SEncParamBase));
		param.iUsageType = CAMERA_VIDEO_REAL_TIME;
		param.iRCMode = RC_QUALITY_MODE;
		param.fMaxFrameRate = inst->maxFramerate;
		param.iPicWidth = inst->width;
		param.iPicHeight = inst->height;
		param.iTargetBitrate = inst->maxBitrate;

		ret_val = encoder_->Initialize(&param);
		int videoFormat = videoFormatI420;
		encoder_->SetOption(ENCODER_OPTION_DATAFORMAT, &videoFormat);

		if (ret_val != 0) {
			WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
				"H264EncoderImpl::InitEncode() fails to initialize encoder ret_val %d",
				ret_val);
			WelsDestroySVCEncoder(encoder_);
			encoder_ = NULL;
			return WEBRTC_VIDEO_CODEC_ERROR;
		}
#elif USEX264
		/* Get default params for preset/tuning */
		x264_param_t param;
		ret_val = x264_param_default_preset(&param, "medium", NULL);
		if (ret_val != 0) {
			WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
				"H264EncoderImpl::InitEncode() fails to initialize encoder ret_val %d",
				ret_val);
			x264_encoder_close(encoder_);
			encoder_ = NULL;
			return WEBRTC_VIDEO_CODEC_ERROR;
		}
		/* Configure non-default params */
		param.i_csp = X264_CSP_I420;
		param.i_width = inst->width;
		param.i_height = inst->height;
		param.b_vfr_input = 0;
		param.b_repeat_headers = 1;
		param.b_annexb = 0;
		param.i_fps_num = 1;
		param.i_fps_num = codec_.maxFramerate;
		param.rc.i_bitrate = codec_.maxBitrate;
		/* Apply profile restrictions. */
		ret_val = x264_param_apply_profile(&param, "high");
		if (ret_val != 0) {
			WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
				"H264EncoderImpl::InitEncode() fails to initialize encoder ret_val %d",
				ret_val);
			x264_encoder_close(encoder_);
			encoder_ = NULL;
			return WEBRTC_VIDEO_CODEC_ERROR;
		}

		ret_val = x264_picture_alloc(&pic, param.i_csp, param.i_width, param.i_height);
		if (ret_val != 0) {
			WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
				"H264EncoderImpl::InitEncode() fails to initialize encoder ret_val %d",
				ret_val);
			x264_encoder_close(encoder_);
			encoder_ = NULL;
			return WEBRTC_VIDEO_CODEC_ERROR;
		}

		encoder_ = x264_encoder_open(&param);
		if (!encoder_){
			WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
				"H264EncoderImpl::InitEncode() fails to initialize encoder ret_val %d",
				ret_val);
			x264_encoder_close(encoder_);
			x264_picture_clean(&pic);
			encoder_ = NULL;
			return WEBRTC_VIDEO_CODEC_ERROR;
		}
#endif

		if (&codec_ != inst) {
			codec_ = *inst;
		}

		if (encoded_image_._buffer != NULL) {
			delete[] encoded_image_._buffer;
		}
		encoded_image_._size = CalcBufferSize(kI420, codec_.width, codec_.height);
		encoded_image_._buffer = new uint8_t[encoded_image_._size];
		encoded_image_._completeFrame = true;

		inited_ = true;
		WEBRTC_TRACE(webrtc::kTraceApiCall, webrtc::kTraceVideoCoding, -1,
			"H264EncoderImpl::InitEncode(width:%d, height:%d, framerate:%d, start_bitrate:%d, max_bitrate:%d)",
			inst->width, inst->height, inst->maxFramerate, inst->startBitrate, inst->maxBitrate);

		return WEBRTC_VIDEO_CODEC_OK;
	}

	int H264EncoderImpl::Encode(const I420VideoFrame& input_image,
		const CodecSpecificInfo* codec_specific_info,
		const std::vector<VideoFrameType>* frame_types) {
		if (!inited_) {
			return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
		}
		if (input_image.IsZeroSize()) {
			return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
		}
		if (encoded_complete_callback_ == NULL) {
			return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
		}

		VideoFrameType frame_type = kDeltaFrame;
		// We only support one stream at the moment.
		if (frame_types && frame_types->size() > 0) {
			frame_type = (*frame_types)[0];
		}

		bool send_keyframe = (frame_type == kKeyFrame);
		if (send_keyframe) {
#if USEOPENH264
			encoder_->ForceIntraFrame(true);
#elif USEX264
			pic.b_keyframe = TRUE;
#endif
			WEBRTC_TRACE(webrtc::kTraceApiCall, webrtc::kTraceVideoCoding, -1,
				"H264EncoderImpl::EncodeKeyFrame(width:%d, height:%d)",
				input_image.width(), input_image.height());
		}

		// Check for change in frame size.
		if (input_image.width() != codec_.width ||
			input_image.height() != codec_.height) {
			int ret = UpdateCodecFrameSize(input_image);
			if (ret < 0) {
				return ret;
			}
		}

#if USEX264
		/* Read input frame */
		pic.img.plane[0] = const_cast<uint8_t*>(input_image.buffer(kYPlane));
		pic.img.plane[1] = const_cast<uint8_t*>(input_image.buffer(kUPlane));
		pic.img.plane[2] = const_cast<uint8_t*>(input_image.buffer(kVPlane));
		pic.i_pts = i_frame;

		int i_nal = 0;
		int i_frame_size = x264_encoder_encode(encoder_, &nal, &i_nal, &pic, &pic_out);
		if (i_frame_size < 0)
		{
			WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
				"H264EncoderImpl::Encode() fails to encode %d",
				i_frame_size);
			x264_encoder_close(encoder_);
			x264_picture_clean(&pic);
			encoder_ = NULL;
			return WEBRTC_VIDEO_CODEC_ERROR;
		}

#elif USEOPENH264  
		SFrameBSInfo info;
		memset(&info, 0, sizeof(SFrameBSInfo));

		SSourcePicture pic;
		memset(&pic, 0, sizeof(SSourcePicture));
		pic.iPicWidth = input_image.width();
		pic.iPicHeight = input_image.height();
		pic.iColorFormat = videoFormatI420;

		pic.iStride[0] = input_image.stride(kYPlane);
		pic.iStride[1] = input_image.stride(kUPlane);
		pic.iStride[2] = input_image.stride(kVPlane);

		pic.pData[0] = const_cast<uint8_t*>(input_image.buffer(kYPlane));
		pic.pData[1] = const_cast<uint8_t*>(input_image.buffer(kUPlane));
		pic.pData[2] = const_cast<uint8_t*>(input_image.buffer(kVPlane));

		int retVal = encoder_->EncodeFrame(&pic, &info);
		if (retVal == videoFrameTypeSkip) {
			return WEBRTC_VIDEO_CODEC_OK;
		}
#endif
		RTPFragmentationHeader frag_info;
#if USEOPENH264
		int layer = 0;

		uint32_t totalNaluCount = 0;
		while (layer < info.iLayerNum) {
			const SLayerBSInfo* layer_bs_info = &info.sLayerInfo[layer];
			if (layer_bs_info != NULL) {
				totalNaluCount += layer_bs_info->iNalCount;
			}
			layer++;
		}
		if (totalNaluCount == 0) {
			return WEBRTC_VIDEO_CODEC_OK;
		}

		frag_info.VerifyAndAllocateFragmentationHeader(totalNaluCount);

		encoded_image_._length = 0;
		layer = 0;
		uint32_t totalNaluIndex = 0;

		while (layer < info.iLayerNum) {
			const SLayerBSInfo* layer_bs_info = &info.sLayerInfo[layer];
			if (layer_bs_info != NULL) {
				int layer_size = 0;
				int nal_begin = 4;
				uint8_t* nal_buffer = NULL;
				char nal_type = 0;
				for (int nal_index = 0; nal_index < layer_bs_info->iNalCount; nal_index++) {
					nal_buffer = layer_bs_info->pBsBuf + nal_begin;
					nal_type = (nal_buffer[0] & 0x1F);
					layer_size += layer_bs_info->pNalLengthInByte[nal_index];
					nal_begin += layer_size;
					if (nal_type == 14) {
						continue;
					}
					uint32_t currentNaluSize = layer_bs_info->pNalLengthInByte[nal_index] - 4;
					memcpy(encoded_image_._buffer + encoded_image_._length, nal_buffer, currentNaluSize);//no start code in nal_buffer
					encoded_image_._length += currentNaluSize;

					WEBRTC_TRACE(webrtc::kTraceApiCall, webrtc::kTraceVideoCoding, -1,
						"H264EncoderImpl::Encode() nal_type %d, length:%d",
						nal_type, encoded_image_._length);

					// Offset of pointer to data for each fragm.
					frag_info.fragmentationOffset[totalNaluIndex] = encoded_image_._length - currentNaluSize;
					// Data size for each fragmentation
					frag_info.fragmentationLength[totalNaluIndex] = currentNaluSize;
					// Payload type of each fragmentation
					frag_info.fragmentationPlType[totalNaluIndex] = nal_type;
					// Timestamp difference relative "now" for
					// each fragmentation
					frag_info.fragmentationTimeDiff[totalNaluIndex] = 0;
					totalNaluIndex++;
				} // for
			}
			layer++;
		}

#elif USEX264	
		
		if (i_frame_size)
		{
			if (i_nal == 0) {
				return WEBRTC_VIDEO_CODEC_OK;
			}
			frag_info.VerifyAndAllocateFragmentationHeader(i_nal);

			encoded_image_._length = 0;

			uint32_t totalNaluIndex = 0;
			for (int nal_index = 0; nal_index < i_nal; nal_index++)
			{
				uint32_t currentNaluSize = 0;
				currentNaluSize = nal[nal_index].i_payload - 4; //i_frame_size
				memcpy(encoded_image_._buffer + encoded_image_._length, nal[nal_index].p_payload + 4, currentNaluSize);//will add start code automatically
				encoded_image_._length += currentNaluSize;

				WEBRTC_TRACE(webrtc::kTraceApiCall, webrtc::kTraceVideoCoding, -1,
					"H264EncoderImpl::Encode() nal_type %d, length:%d",
					nal[nal_index].i_type, encoded_image_._length);

				frag_info.fragmentationOffset[totalNaluIndex] = encoded_image_._length - currentNaluSize;
				frag_info.fragmentationLength[totalNaluIndex] = currentNaluSize;
				frag_info.fragmentationPlType[totalNaluIndex] = nal[nal_index].i_type;
				frag_info.fragmentationTimeDiff[totalNaluIndex] = 0;
				totalNaluIndex++;
			}
		}
		i_frame++;
#endif
		if (encoded_image_._length > 0) {
			encoded_image_._timeStamp = input_image.timestamp();
			encoded_image_.capture_time_ms_ = input_image.render_time_ms();
			encoded_image_._encodedHeight = codec_.height;
			encoded_image_._encodedWidth = codec_.width;
			encoded_image_._frameType = frame_type;
			// call back
			encoded_complete_callback_->Encoded(encoded_image_, NULL, &frag_info);
		}
		return WEBRTC_VIDEO_CODEC_OK;
	}

	int H264EncoderImpl::RegisterEncodeCompleteCallback(
		EncodedImageCallback* callback) {
		encoded_complete_callback_ = callback;
		return WEBRTC_VIDEO_CODEC_OK;
	}

	int H264EncoderImpl::SetChannelParameters(uint32_t packet_loss, int rtt) {
		return WEBRTC_VIDEO_CODEC_OK;
	}

	int H264EncoderImpl::UpdateCodecFrameSize(const I420VideoFrame& input_image) {
		codec_.width = input_image.width();
		codec_.height = input_image.height();
		return WEBRTC_VIDEO_CODEC_OK;
	}


	H264Decoder* H264Decoder::Create() {
		return new H264DecoderImpl();
	}

	H264DecoderImpl::H264DecoderImpl()
		: decode_complete_callback_(NULL),
		inited_(false),
		key_frame_required_(true)
#if USEOPENH264
		,buffer_with_start_code_(NULL)
		,decoder_(NULL)
#elif USEX264
		, decode_buffer(NULL)
		, out_buffer(NULL)
#endif
	{
		memset(&codec_, 0, sizeof(codec_));
#if USEOPENH264
		buffer_with_start_code_ = new unsigned char[MAX_ENCODED_IMAGE_SIZE];
#elif USEX264
		av_register_all();
		avformat_network_init();
		decode_buffer = (unsigned char *)av_malloc(MAX_ENCODED_IMAGE_SIZE);
#endif
	}

	H264DecoderImpl::~H264DecoderImpl() {
		inited_ = true;  // in order to do the actual release
		Release();
#if USEOPEN264
		delete[] buffer_with_start_code_;
#elif USEX264
		delete[] decode_buffer;
#endif
	}

	int H264DecoderImpl::Reset() {
		if (!inited_) {
			return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
		}
		InitDecode(&codec_, 1);
		return WEBRTC_VIDEO_CODEC_OK;
	}

	int H264DecoderImpl::InitDecode(const VideoCodec* inst, int number_of_cores) {
		if (inst == NULL) {
			return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
		}
		int ret_val = Release();
		if (ret_val < 0) {
			return ret_val;
		}

		if (&codec_ != inst) {
			// Save VideoCodec instance for later; mainly for duplicating the decoder.
			codec_ = *inst;
		}
#if USEOPENH264
		if (decoder_ == NULL) {
			ret_val = WelsCreateDecoder(&decoder_);
			if (ret_val != 0) {
				decoder_ = NULL;
				return WEBRTC_VIDEO_CODEC_ERROR;
			}
		}
		SDecodingParam dec_param;
		memset(&dec_param, 0, sizeof(SDecodingParam));
		dec_param.eOutputColorFormat = videoFormatI420;
		dec_param.uiTargetDqLayer = UCHAR_MAX;
		dec_param.eEcActiveIdc = ERROR_CON_FRAME_COPY_CROSS_IDR;
		dec_param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;
		ret_val = decoder_->Initialize(&dec_param);
		if (ret_val != 0) {
			decoder_->Uninitialize();
			WelsDestroyDecoder(decoder_);
			decoder_ = NULL;
			return WEBRTC_VIDEO_CODEC_ERROR;
		}
#elif USEX264
		pCodec = avcodec_find_decoder(AV_CODEC_ID_H264);
		pCodecCtx = avcodec_alloc_context3(pCodec);
		pCodecCtx->pix_fmt = PIX_FMT_YUV420P;
		pCodecCtx->width = codec_.width;
		pCodecCtx->height = codec_.height;
		//pCodecCtx->bit_rate = codec_.targetBitrate*1000;
		pCodecCtx->time_base.num = 1;
		pCodecCtx->time_base.den = codec_.maxFramerate;

		if (pCodec == NULL){
			WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
				"H264DecoderImpl::InitDecode, Codec not found.");
			return WEBRTC_VIDEO_CODEC_ERROR;
		}
		if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0){
			WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
				"H264DecoderImpl::InitDecode, Could not open codec.");
			return WEBRTC_VIDEO_CODEC_ERROR;
		}
#endif
		inited_ = true;

		// Always start with a complete key frame.
		key_frame_required_ = true;
		WEBRTC_TRACE(webrtc::kTraceApiCall, webrtc::kTraceVideoCoding, -1,
			"H264DecoderImpl::InitDecode(width:%d, height:%d, framerate:%d, start_bitrate:%d, max_bitrate:%d)",
			inst->width, inst->height, inst->maxFramerate, inst->startBitrate, inst->maxBitrate);
		return WEBRTC_VIDEO_CODEC_OK;
	}

	int H264DecoderImpl::Decode(const EncodedImage& input_image,
		bool missing_frames,
		const RTPFragmentationHeader* fragmentation,
		const CodecSpecificInfo* codec_specific_info,
		int64_t /*render_time_ms*/) {
		if (!inited_) {
			WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
				"H264DecoderImpl::Decode, decoder is not initialized");
			return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
		}

		if (decode_complete_callback_ == NULL) {
			WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
				"H264DecoderImpl::Decode, decode complete call back is not set");
			return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
		}

		if (input_image._buffer == NULL) {
			WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
				"H264DecoderImpl::Decode, null buffer");
			return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
		}
		if (!codec_specific_info) {
			WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
				"H264EncoderImpl::Decode, no codec info");
			return WEBRTC_VIDEO_CODEC_ERROR;
		}
		if (codec_specific_info->codecType != kVideoCodecH264) {
			WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
				"H264EncoderImpl::Decode, non h264 codec %d", codec_specific_info->codecType);
			return WEBRTC_VIDEO_CODEC_ERROR;
		}

		WEBRTC_TRACE(webrtc::kTraceApiCall, webrtc::kTraceVideoCoding, -1,
			"H264DecoderImpl::Decode(frame_type:%d, length:%d",
			input_image._frameType, input_image._length);

#if 0
		// Always start with a complete key frame.
		if (key_frame_required_) {
			if (input_image._frameType != kKeyFrame)
				return WEBRTC_VIDEO_CODEC_ERROR;
			// We have a key frame - is it complete?
			if (input_image._completeFrame) {
				key_frame_required_ = false;
			}
			else {
				return WEBRTC_VIDEO_CODEC_ERROR;
			}
		}
#endif
#if USEOPENH264
		void* data[3];
		SBufferInfo buffer_info;
		memset(data, 0, sizeof(data));
		memset(&buffer_info, 0, sizeof(SBufferInfo));

		memset(buffer_with_start_code_, 0, MAX_ENCODED_IMAGE_SIZE);
		int encoded_image_size = 0;
			memcpy(buffer_with_start_code_ , input_image._buffer, input_image._length);
			encoded_image_size =  input_image._length;

		DECODING_STATE rv = decoder_->DecodeFrame2(buffer_with_start_code_, encoded_image_size, (unsigned char**)data, &buffer_info);

		if (rv != dsErrorFree) {
			WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
				"H264DecoderImpl::Decode, openH264 decoding fails with error %d", rv);
			return WEBRTC_VIDEO_CODEC_ERROR;
		}

		if (buffer_info.iBufferStatus == 1) {
			int size_y = buffer_info.UsrData.sSystemBuffer.iStride[0] * buffer_info.UsrData.sSystemBuffer.iHeight;
			int size_u = buffer_info.UsrData.sSystemBuffer.iStride[1] * (buffer_info.UsrData.sSystemBuffer.iHeight / 2);
			int size_v = buffer_info.UsrData.sSystemBuffer.iStride[1] * (buffer_info.UsrData.sSystemBuffer.iHeight / 2);

			decoded_image_.CreateFrame(size_y, static_cast<uint8_t*>(data[0]),
				size_u, static_cast<uint8_t*>(data[1]),
				size_v, static_cast<uint8_t*>(data[2]),
				buffer_info.UsrData.sSystemBuffer.iWidth,
				buffer_info.UsrData.sSystemBuffer.iHeight,
				buffer_info.UsrData.sSystemBuffer.iStride[0],
				buffer_info.UsrData.sSystemBuffer.iStride[1],
				buffer_info.UsrData.sSystemBuffer.iStride[1]);

			decoded_image_.set_timestamp(input_image._timeStamp);
			decode_complete_callback_->Decoded(decoded_image_);
			return WEBRTC_VIDEO_CODEC_OK;
		}else {
			WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
				"H264DecoderImpl::Decode, buffer status:%d", buffer_info.iBufferStatus);
			return WEBRTC_VIDEO_CODEC_OK;
		}
#elif USEX264	
		if (framecnt < 2)
		{
			memcpy(decode_buffer + encoded_length, input_image._buffer, input_image._length);
			encoded_length += input_image._length;
			framecnt++;
		}
		else
		{
			pFrame = av_frame_alloc();
			pFrameYUV = av_frame_alloc();
			out_buffer = (uint8_t *)av_malloc(avpicture_get_size(PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));
			avpicture_fill((AVPicture *)pFrameYUV, out_buffer, PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);
			img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
				pCodecCtx->width, pCodecCtx->height, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
			
			if (framecnt == 2)
			{
				packet = (AVPacket *)av_malloc(sizeof(AVPacket));
				av_new_packet(packet, encoded_length);
				memcpy(packet->data, decode_buffer, encoded_length);
				av_free(decode_buffer);
				framecnt++;
				printf("\n\nLoading");
			}
			else
			{
				packet = (AVPacket *)av_malloc(sizeof(AVPacket));
				av_new_packet(packet, input_image._length);
				memcpy(packet->data, input_image._buffer, input_image._length);
			}
			
			int got_picture = 0;
			int ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
			if (ret < 0){
				WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
					"H264DecoderImpl::Decode, Decode Error.");
				return WEBRTC_VIDEO_CODEC_ERROR;
			}
			if (got_picture){
				sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height,
					pFrameYUV->data, pFrameYUV->linesize);

				int size_y = pFrameYUV->linesize[0] * pCodecCtx->height;
				int size_u = pFrameYUV->linesize[1] * pCodecCtx->height / 2;
				int size_v = pFrameYUV->linesize[2] * pCodecCtx->height / 2;

				decoded_image_.CreateFrame(size_y, static_cast<uint8_t*>(pFrameYUV->data[0]),
					size_u, static_cast<uint8_t*>(pFrameYUV->data[1]),
					size_v, static_cast<uint8_t*>(pFrameYUV->data[2]),
					pCodecCtx->width,
					pCodecCtx->height,
					pFrameYUV->linesize[0],
					pFrameYUV->linesize[1],
					pFrameYUV->linesize[2]);

				decoded_image_.set_timestamp(input_image._timeStamp);
				decode_complete_callback_->Decoded(decoded_image_);
				return WEBRTC_VIDEO_CODEC_OK;
			}
			else
				printf(".");
			av_free_packet(packet);
		}
		return WEBRTC_VIDEO_CODEC_OK;
#endif	
	}

	int H264DecoderImpl::RegisterDecodeCompleteCallback(
		DecodedImageCallback* callback) {
		decode_complete_callback_ = callback;
		return WEBRTC_VIDEO_CODEC_OK;
	}

	int H264DecoderImpl::Release() {
#if USEOPENH264
		if (decoder_ != NULL) {
			decoder_->Uninitialize();
			WelsDestroyDecoder(decoder_);
			decoder_ = NULL;
		}
#elif USEX264
		avcodec_close(pCodecCtx);
#endif
		inited_ = false;
		return WEBRTC_VIDEO_CODEC_OK;
	}

	VideoDecoder* H264DecoderImpl::Copy() {
		// Sanity checks.
		if (!inited_) {
			// Not initialized.
			assert(false);
			return NULL;
		}
		if (decoded_image_.IsZeroSize()) {
			// Nothing has been decoded before; cannot clone.
			return NULL;
		}
		// Create a new VideoDecoder object
		H264DecoderImpl *copy = new H264DecoderImpl;

		// Initialize the new decoder
		if (copy->InitDecode(&codec_, 1) != WEBRTC_VIDEO_CODEC_OK) {
			delete copy;
			return NULL;
		}

		return static_cast<VideoDecoder*>(copy);
	}
}  // namespace webrtc
