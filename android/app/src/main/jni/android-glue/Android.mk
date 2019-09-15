LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := android-glue
LOCAL_SRC_FILES = \
	SWIGWrapper.cpp

LOCAL_C_INCLUDES :=\
	$(LOCAL_PATH) \
	$(LOCAL_PATH)/../dtnd/src

LOCAL_WHOLE_STATIC_LIBRARIES:=\
	libibrcommon \
	libibrdtn \
	libdtnd

LOCAL_LDLIBS:=\
	-lz \
	-llog

include $(BUILD_SHARED_LIBRARY)
