LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := DuckerNative
LOCAL_SRC_FILES := DuckerNative.cpp

LOCAL_CFLAGS    := -D__ANDROID__ -std=c++17 -Wall -Wextra -O2 -fexceptions
LOCAL_LDLIBS    := -landroid -lGLESv3 -llog -lc++_shared

LOCAL_C_INCLUDES := $(LOCAL_PATH)

include $(BUILD_SHARED_LIBRARY)