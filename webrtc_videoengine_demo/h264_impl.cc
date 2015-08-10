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
      inited_(false),
      first_frame_encoded_(false),
      timestamp_(0)
#ifdef USEOPENH264
      ,encoder_(NULL)
#elif USEX264
	  ,h(NULL),
	  nal(NULL)
#endif
{
#ifdef USEX264
	fopen_s(&outfile, "outfile.h264", "wb+");
#endif
  memset(&codec_, 0, sizeof(codec_));
  uint32_t seed = static_cast<uint32_t>(TickTime::MillisecondTimestamp());
  srand(seed);
}

H264EncoderImpl::~H264EncoderImpl() {
  Release();
}

int H264EncoderImpl::Release() {
  if (encoded_image_._buffer != NULL) {
    delete [] encoded_image_._buffer;
    encoded_image_._buffer = NULL;
  }
#ifdef USEOPENH264
  if (encoder_ != NULL) {
    WelsDestroySVCEncoder(encoder_);
    encoder_ = NULL;
  }
#elif USEX264
  /*if (h != NULL) {
	  x264_encoder_close(h);
	  h = NULL;
  }*/
  if (inited_)
  {
	  while (x264_encoder_delayed_frames(h))
	  {
		  i_frame_size = x264_encoder_encode(h, &nal, &i_nal, NULL, &pic_out);
		  if (i_frame_size < 0){
			  WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
				  "H264EncoderImpl::Release() fails to flush encoder  %d",
				  i_frame_size);
			  x264_encoder_close(h);
			  h = NULL;
			  return WEBRTC_VIDEO_CODEC_ERROR;
		  }
		  else if (i_frame_size)
		  {
			  if (!fwrite(nal->p_payload, i_frame_size, 1, outfile))
			  {
				  WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
					  "H264EncoderImpl::Release() fails to flush encoder  %d",
					  i_frame_size);
				  x264_encoder_close(h);
				  h = NULL;
				  return WEBRTC_VIDEO_CODEC_ERROR;
			  }
		  }
	  }
	  x264_encoder_close(h);
  }
  else{
	  if (h != NULL) {
		  x264_encoder_close(h);
		  h = NULL;
	  }
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

  int ret_val= Release();
  if (ret_val < 0) {
    return ret_val;
  }
#ifdef USEOPENH264
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
  memset (&param, 0, sizeof(SEncParamBase));

  param.iUsageType = CAMERA_VIDEO_REAL_TIME;
  param.iRCMode = RC_QUALITY_MODE;
  param.fMaxFrameRate = inst->maxFramerate;
  param.iPicWidth = inst->width;
  param.iPicHeight = inst->height;
  param.iTargetBitrate = inst->maxBitrate;
//  param.iInputCsp = videoFormatI420;
  ret_val =  encoder_->Initialize(&param);
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
  ret_val = x264_param_default_preset(&param, "medium", NULL);
	  if (ret_val != 0) {
		  WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
			  "H264EncoderImpl::InitEncode() fails to initialize encoder ret_val %d",
			  ret_val);
		  x264_encoder_close(h);
		  h = NULL;
		  return WEBRTC_VIDEO_CODEC_ERROR;
	  }
	  /* Configure non-default params */
	  param.i_csp = X264_CSP_I420;
	  param.i_width = inst->width;
	  param.i_height = inst->height;
	  param.b_vfr_input = 0;
	  param.b_repeat_headers = 1;
	  param.b_annexb = 1;

	  /* Apply profile restrictions. */
	  ret_val = x264_param_apply_profile(&param, "high");
	  if (ret_val != 0) {
		  WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
			  "H264EncoderImpl::InitEncode() fails to initialize encoder ret_val %d",
			  ret_val);
		  x264_encoder_close(h);
		  h = NULL;
		  return WEBRTC_VIDEO_CODEC_ERROR;
	  }

	  ret_val = x264_picture_alloc(&pic, param.i_csp, param.i_width, param.i_height);
	  if (ret_val != 0) {
		  WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
			  "H264EncoderImpl::InitEncode() fails to initialize encoder ret_val %d",
			  ret_val);
		  x264_encoder_close(h);
		  h = NULL;
		  return WEBRTC_VIDEO_CODEC_ERROR;
	  }

	  h = x264_encoder_open(&param);
	  if (!h){
		  WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
			  "H264EncoderImpl::InitEncode() fails to initialize encoder ret_val %d",
			  ret_val);
		  x264_encoder_close(h);
		  x264_picture_clean(&pic);
		  h = NULL;
		  return WEBRTC_VIDEO_CODEC_ERROR;
	  }
#endif
  timestamp_ = 0;

  if (&codec_ != inst) {
    codec_ = *inst;
  }

  if (encoded_image_._buffer != NULL) {
    delete [] encoded_image_._buffer;
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
#ifdef USEOPENH264
  bool send_keyframe = (frame_type == kKeyFrame);
  if (send_keyframe) {
    encoder_->ForceIntraFrame(true);
    WEBRTC_TRACE(webrtc::kTraceApiCall, webrtc::kTraceVideoCoding, -1,
                 "H264EncoderImpl::EncodeKeyFrame(width:%d, height:%d)",
                 input_image.width(), input_image.height());
  }
#endif
  // Check for change in frame size.
  if (input_image.width() != codec_.width ||
      input_image.height() != codec_.height) {
    int ret = UpdateCodecFrameSize(input_image);
    if (ret < 0) {
      return ret;
    }
  }

#ifdef USEX264
  
	  /* Read input frame */
	 pic.img.plane[0]=const_cast<uint8_t*>(input_image.buffer(kYPlane));
		  
	  pic.img.plane[1]= const_cast<uint8_t*>(input_image.buffer(kUPlane));
		 
	  pic.img.plane[2] = const_cast<uint8_t*>(input_image.buffer(kVPlane));
		
	  pic.i_pts = i_frame;
	  i_frame_size = x264_encoder_encode(h, &nal, &i_nal, &pic, &pic_out);
	  if (i_frame_size < 0)
	  {
		  WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
			  "H264EncoderImpl::Encode() fails to encode %d",
			  i_frame_size);
		  x264_encoder_close(h);
		  x264_picture_clean(&pic);
		  h = NULL;
		  return WEBRTC_VIDEO_CODEC_ERROR;
	  }
	  if (i_frame_size)
	  {
		  if (!fwrite(nal->p_payload, i_frame_size, 1, outfile))
			  return WEBRTC_VIDEO_CODEC_ERROR;
	  }
	  
	  i_frame++;
#elif USEOPENH264  
  SFrameBSInfo info;
  memset(&info, 0, sizeof(SFrameBSInfo));

  SSourcePicture pic;
  memset(&pic,0,sizeof(SSourcePicture));
  pic.iPicWidth = input_image.width();
  pic.iPicHeight = input_image.height();
  pic.iColorFormat = videoFormatI420;

  pic.iStride[0] = input_image.stride(kYPlane);
  pic.iStride[1] = input_image.stride(kUPlane);
  pic.iStride[2] = input_image.stride(kVPlane);

  pic.pData[0]   = const_cast<uint8_t*>(input_image.buffer(kYPlane));
  pic.pData[1]   = const_cast<uint8_t*>(input_image.buffer(kUPlane));
  pic.pData[2]   = const_cast<uint8_t*>(input_image.buffer(kVPlane));

  int retVal = encoder_->EncodeFrame(&pic, &info);
  if (retVal == videoFrameTypeSkip) {
    return WEBRTC_VIDEO_CODEC_OK;
  }
#endif

#ifdef USEOPENH264
  int layer = 0;
  //uint8_t* pbuff = encoded_image_._buffer;

  //add by ray. 2015.5.25 support RTPFragmentationHeader.
  RTPFragmentationHeader frag_info;
  frag_info.VerifyAndAllocateFragmentationHeader(1);
  uint32_t totalNaluCount = 0;
  while (layer < info.iLayerNum) {
	  const SLayerBSInfo* layer_bs_info = &info.sLayerInfo[layer];
	  if (layer_bs_info != NULL) {
		  int layer_size = 0;
		  //int nal_begin = 0;
		  int nal_begin = 4;
		  uint8_t* nal_buffer = NULL;
		  char nal_type = 0;
		  for (int nal_index = 0; nal_index < layer_bs_info->iNalCount; nal_index++) {
			  nal_buffer = layer_bs_info->pBsBuf + nal_begin;
			  nal_type = (nal_buffer[0] & 0x1F);
			  //nal_type    = (nal_buffer[4] & 0x1F);
			  layer_size += layer_bs_info->pNalLengthInByte[nal_index];
			  nal_begin += layer_size;
			  if (nal_type == 14) {
				  continue;
			  }
			  totalNaluCount++;
		  }
	  }
	  layer++;
  }
  if (totalNaluCount == 0) {
	  return WEBRTC_VIDEO_CODEC_OK;
  }
  frag_info.VerifyAndAllocateFragmentationHeader(totalNaluCount);
  layer = 0;

  encoded_image_._frameType = frame_type;
  encoded_image_._timeStamp = input_image.timestamp();
  encoded_image_.capture_time_ms_ = input_image.render_time_ms();
  encoded_image_._length = 0;
  uint32_t totalNaluIndex = 0;
  //end add by ray.

  while (layer < info.iLayerNum) {
	  const SLayerBSInfo* layer_bs_info = &info.sLayerInfo[layer];
	  if (layer_bs_info != NULL) {
		  int layer_size = 0;
		  //int nal_begin = 0;
		  int nal_begin = 4;
		  uint8_t* nal_buffer = NULL;
		  char nal_type = 0;
		  for (int nal_index = 0; nal_index < layer_bs_info->iNalCount; nal_index++) {
			  nal_buffer = layer_bs_info->pBsBuf + nal_begin;
			  nal_type = (nal_buffer[0] & 0x1F);
			  //nal_type    = (nal_buffer[4] & 0x1F);
			  layer_size += layer_bs_info->pNalLengthInByte[nal_index];
			  nal_begin += layer_size;
			  if (nal_type == 14) {
				  continue;
			  }
			  uint32_t currentNaluSize = layer_bs_info->pNalLengthInByte[nal_index] - 4;
			  //encoded_image_._length        = layer_bs_info->iNalLengthInByte[nal_index];
			  memcpy(encoded_image_._buffer + encoded_image_._length, nal_buffer, currentNaluSize);
			  encoded_image_._length += currentNaluSize;
			  // encoded_image_._encodedHeight
			  // encoded_image_._encodedWidth

			  //        WEBRTC_TRACE(webrtc::kTraceApiCall, webrtc::kTraceVideoCoding, -1,
			  //                     "H264EncoderImpl::Encode() nal_type %d, length:%d",
			  //                     nal_type, encoded_image_._length);

			  frag_info.fragmentationOffset[totalNaluIndex] = encoded_image_._length - currentNaluSize;
			  frag_info.fragmentationLength[totalNaluIndex] = currentNaluSize;
			  frag_info.fragmentationPlType[totalNaluIndex] = nal_type;
			  frag_info.fragmentationTimeDiff[totalNaluIndex] = 0;
			  totalNaluIndex++;
		  } // for
	  }
	  layer++;
  }

#elif USEX264
		  RTPFragmentationHeader frag_info;
		  frag_info.VerifyAndAllocateFragmentationHeader(1);
		  uint32_t totalNaluCount = i_nal;
		  if (totalNaluCount == 0) {
			  return WEBRTC_VIDEO_CODEC_OK;
		  }
		  frag_info.VerifyAndAllocateFragmentationHeader(totalNaluCount);

		  encoded_image_._frameType = frame_type;
		  encoded_image_._timeStamp = input_image.timestamp();
		  encoded_image_.capture_time_ms_ = input_image.render_time_ms();
		  encoded_image_._length = 0;
		  uint32_t totalNaluIndex = 0;

		  //x264
		  uint32_t currentNaluSize = nal->i_payload - 4; //i_frame_size
		  //encoded_image_._length        = layer_bs_info->iNalLengthInByte[nal_index];
		  memcpy(encoded_image_._buffer + encoded_image_._length, nal->p_payload, currentNaluSize);
		  encoded_image_._length += currentNaluSize;
		  // encoded_image_._encodedHeight
		  // encoded_image_._encodedWidth

		  //        WEBRTC_TRACE(webrtc::kTraceApiCall, webrtc::kTraceVideoCoding, -1,
		  //                     "H264EncoderImpl::Encode() nal_type %d, length:%d",
		  //                     nal_type, encoded_image_._length);
		  frag_info.fragmentationVectorSize = 1;
		  frag_info.fragmentationOffset[totalNaluIndex] = encoded_image_._length - currentNaluSize;
		  frag_info.fragmentationLength[totalNaluIndex] = currentNaluSize;
		  frag_info.fragmentationPlType[totalNaluIndex] = nal->i_type;
		  frag_info.fragmentationTimeDiff[totalNaluIndex] = 0;
#endif

#if USEOPENH264 || USEX264
		  // call back
		  encoded_complete_callback_->Encoded(encoded_image_, NULL, &frag_info);
#endif
		  if (!first_frame_encoded_) {
			  first_frame_encoded_ = true;
		  }
  return WEBRTC_VIDEO_CODEC_OK;
}

int H264EncoderImpl::RegisterEncodeCompleteCallback(
    EncodedImageCallback* callback) {
  encoded_complete_callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

int H264EncoderImpl::SetChannelParameters(uint32_t packet_loss, int rtt) {
// ffs
	return WEBRTC_VIDEO_CODEC_OK;
}

int H264EncoderImpl::UpdateCodecFrameSize(const I420VideoFrame& input_image) {
  codec_.width = input_image.width();
  codec_.height = input_image.height();
  // ffs
  return WEBRTC_VIDEO_CODEC_OK;
}

#ifdef USEOPENH264
H264Decoder* H264Decoder::Create() {
  return new H264DecoderImpl();
}

H264DecoderImpl::H264DecoderImpl()
    : decode_complete_callback_(NULL),
      inited_(false),
      //feedback_mode_(false),
      decoder_(NULL),
      last_keyframe_(),
      key_frame_required_(true),
      buffer_with_start_code_(NULL) {
  memset(&codec_, 0, sizeof(codec_));
  buffer_with_start_code_ = new unsigned char [MAX_ENCODED_IMAGE_SIZE];
}

H264DecoderImpl::~H264DecoderImpl() {
  inited_ = true;  // in order to do the actual release
  Release();
  delete [] buffer_with_start_code_;
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
//  dec_param.uiEcActiveFlag = 1;
    dec_param.eEcActiveIdc = ERROR_CON_FRAME_COPY_CROSS_IDR;
  dec_param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;
  ret_val = decoder_->Initialize(&dec_param);
  if (ret_val != 0) {
    decoder_->Uninitialize();
    WelsDestroyDecoder(decoder_);
    decoder_ = NULL;
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  if (&codec_ != inst) {
    // Save VideoCodec instance for later; mainly for duplicating the decoder.
    codec_ = *inst;
  }

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

  unsigned char nalu_header = codec_specific_info->codecSpecific.H264.nalu_header;
  unsigned char nal_type = nalu_header & 0x1F;
  bool single_nalu = codec_specific_info->codecSpecific.H264.single_nalu;

//  WEBRTC_TRACE(webrtc::kTraceApiCall, webrtc::kTraceVideoCoding, -1,
//	           "H264DecoderImpl::Decode(frame_type:%d, length:%d, nal_type:%d",
//	            input_image._frameType, input_image._length, nal_type);

#if 0
  // Always start with a complete key frame.
  if (key_frame_required_) {
    if (input_image._frameType != kKeyFrame)
      return WEBRTC_VIDEO_CODEC_ERROR;
    // We have a key frame - is it complete?
    if (input_image._completeFrame) {
      key_frame_required_ = false;
    } else {
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
  }
#endif

  void* data[3];
  SBufferInfo buffer_info;
  memset(data, 0, sizeof(data));
  memset(&buffer_info, 0, sizeof(SBufferInfo));

  unsigned char start_code[] = {0, 0, 0, 1};
  memset(buffer_with_start_code_, 0, MAX_ENCODED_IMAGE_SIZE);
  int encoded_image_size = 0;
  if (single_nalu) {
    memcpy(buffer_with_start_code_, start_code, 4);
    memcpy(buffer_with_start_code_ + 4, input_image._buffer, input_image._length);
    encoded_image_size = 4 + input_image._length;
  } else {
    // need add nalu header
    memcpy(buffer_with_start_code_, start_code, 4);
    memcpy(buffer_with_start_code_ + 4, &nalu_header, 1);
    memcpy(buffer_with_start_code_ + 5, input_image._buffer, input_image._length);
    encoded_image_size = 5 + input_image._length;
  }
    //modify by ray.
//  DECODING_STATE rv = decoder_->DecodeFrame2(buffer_with_start_code_, encoded_image_size, data, &buffer_info);
    DECODING_STATE rv = decoder_->DecodeFrame2(buffer_with_start_code_, encoded_image_size, (unsigned char**)data, &buffer_info);

  if (rv != dsErrorFree) {
    WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
                 "H264DecoderImpl::Decode, openH264 decoding fails with error %d", rv);
    return WEBRTC_VIDEO_CODEC_ERROR;
  }

  if (buffer_info.iBufferStatus == 1) {
    int size_y = buffer_info.UsrData.sSystemBuffer.iStride[0] * buffer_info.UsrData.sSystemBuffer.iHeight;
    int size_u = buffer_info.UsrData.sSystemBuffer.iStride[1] * (buffer_info.UsrData.sSystemBuffer.iHeight/2);
    int size_v = buffer_info.UsrData.sSystemBuffer.iStride[1] * (buffer_info.UsrData.sSystemBuffer.iHeight/2);

      //modify by ray.
    decoded_image_.CreateFrame(size_y, static_cast<uint8_t*>(data[0]),
                               size_u, static_cast<uint8_t*>(data[1]),
                               size_v, static_cast<uint8_t*>(data[2]),
                               buffer_info.UsrData.sSystemBuffer.iWidth,
                               buffer_info.UsrData.sSystemBuffer.iHeight,
                               buffer_info.UsrData.sSystemBuffer.iStride[0],
                               buffer_info.UsrData.sSystemBuffer.iStride[1],
                               buffer_info.UsrData.sSystemBuffer.iStride[1]);
     /* decoded_image_.CreateFrame(static_cast<uint8_t*>(data[0]),
                                 static_cast<uint8_t*>(data[1]),
                                 static_cast<uint8_t*>(data[2]),
                                 buffer_info.UsrData.sSystemBuffer.iWidth,
                                 buffer_info.UsrData.sSystemBuffer.iHeight,
                                 buffer_info.UsrData.sSystemBuffer.iStride[0],
                                 buffer_info.UsrData.sSystemBuffer.iStride[1],
                                 buffer_info.UsrData.sSystemBuffer.iStride[1]);*/

    decoded_image_.set_timestamp(input_image._timeStamp);
    decode_complete_callback_->Decoded(decoded_image_);
    return WEBRTC_VIDEO_CODEC_OK;
  } else {
    WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
                 "H264DecoderImpl::Decode, buffer status:%d", buffer_info.iBufferStatus);
    return WEBRTC_VIDEO_CODEC_OK;
  }
}

int H264DecoderImpl::RegisterDecodeCompleteCallback(
    DecodedImageCallback* callback) {
  decode_complete_callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

int H264DecoderImpl::Release() {
  if (last_keyframe_._buffer != NULL) {
    delete [] last_keyframe_._buffer;
    last_keyframe_._buffer = NULL;
  }
  if (decoder_ != NULL) {
    decoder_->Uninitialize();
    WelsDestroyDecoder(decoder_);
    decoder_ = NULL;
  }
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
  if (last_keyframe_._buffer == NULL) {
    // Cannot clone if we have no key frame to start with.
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
#endif
}  // namespace webrtc
