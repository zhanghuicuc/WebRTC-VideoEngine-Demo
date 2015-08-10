#ifndef WEBRTC_MODULES_VIDEO_CODING_CODECS_H264_INCLUDE_H264_H_
#define WEBRTC_MODULES_VIDEO_CODING_CODECS_H264_INCLUDE_H264_H_

#include "webrtc/modules/video_coding/codecs/interface/video_codec_interface.h"

namespace webrtc {

class H264Encoder : public VideoEncoder {
 public:
  static H264Encoder* Create();

  virtual ~H264Encoder() {};
};  // end of H264Encoder class

#ifdef USEOPENH264
class H264Decoder : public VideoDecoder {
 public:
  static H264Decoder* Create();

  virtual ~H264Decoder() {};
};  // end of H264Decoder class
#endif
}  // namespace webrtc

#endif // WEBRTC_MODULES_VIDEO_CODING_CODECS_H264_INCLUDE_H264_H_
