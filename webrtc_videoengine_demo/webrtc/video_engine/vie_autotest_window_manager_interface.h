/*
 *  Copyright (c) 2011 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

/*
 *  vie_autotest_window_manager_interface.h
 */

//#include "webrtc/video_engine/vie_autotest_defines.h"

#ifndef WEBRTC_VIDEO_ENGINE_MAIN_TEST_AUTOTEST_INTERFACE_VIE_AUTOTEST_WINDOW_MANAGER_INTERFACE_H_
#define WEBRTC_VIDEO_ENGINE_MAIN_TEST_AUTOTEST_INTERFACE_VIE_AUTOTEST_WINDOW_MANAGER_INTERFACE_H_

struct AutoTestSize {
	unsigned int width;
	unsigned int height;
	AutoTestSize() :
		width(0), height(0) {
	}
	AutoTestSize(unsigned int iWidth, unsigned int iHeight) :
		width(iWidth), height(iHeight) {
	}
};

struct AutoTestOrigin {
	unsigned int x;
	unsigned int y;
	AutoTestOrigin() :
		x(0), y(0) {
	}
	AutoTestOrigin(unsigned int iX, unsigned int iY) :
		x(iX), y(iY) {
	}
};

struct AutoTestRect {
	AutoTestSize size;
	AutoTestOrigin origin;
	AutoTestRect() :
		size(), origin() {
	}

	AutoTestRect(unsigned int iX, unsigned int iY, unsigned int iWidth, unsigned int iHeight) :
		size(iX, iY), origin(iWidth, iHeight) {
	}

	void Copy(AutoTestRect iRect) {
		origin.x = iRect.origin.x;
		origin.y = iRect.origin.y;
		size.width = iRect.size.width;
		size.height = iRect.size.height;
	}
};

class ViEAutoTestWindowManagerInterface
{
public:
    virtual int CreateWindows(AutoTestRect window1Size,
                              AutoTestRect window2Size, void* window1Title,
                              void* window2Title) = 0;
    virtual int TerminateWindows() = 0;
    virtual void* GetWindow1() = 0;
    virtual void* GetWindow2() = 0;
    virtual bool SetTopmostWindow() = 0;
    virtual ~ViEAutoTestWindowManagerInterface() {}
};

#endif  // WEBRTC_VIDEO_ENGINE_MAIN_TEST_AUTOTEST_INTERFACE_VIE_AUTOTEST_WINDOW_MANAGER_INTERFACE_H_
