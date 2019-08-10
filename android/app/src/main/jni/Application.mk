LOCAL_PATH:=$(call my-dir)

# SUBDIR parameter in androgenizer expects <project>_TOP variables to be defined
ibrcommon_TOP:=$(abspath $(LOCAL_PATH))/ibrcommon
ibrdtn_TOP:=$(abspath $(LOCAL_PATH))/ibrdtn
dtnd_TOP:=$(abspath $(LOCAL_PATH))/dtnd

# Optimizations
#APP_OPTIM:=release
APP_OPTIM:=debug

# Build target
APP_ABI:=all

# API 9 has RW Mutex implementation in pthread lib
APP_PLATFORM:=android-24
# see /build.gradle#defaultAndroidConfig.project.android.defaultConfig.minSdkVersion

# See for documentation on Androids c++ support: $(NDK_PATH)/docs/CPLUSPLUS-SUPPORT.html
# select c++ gnu stl, because we need exception support
APP_STL := c++_shared

# enable exceptions and rtti (information about data types at runtime)
APP_CPPFLAGS+=-fexceptions -frtti
#APP_CPPFLAGS+=-Wall -Wextra -Wconversion

# ibrcommon
# openssl headers
# include openssl headers for ibrcommon/ibrcommon/ssl/gcm/gcm_aes.c, APP_CFLAGS are also used for c++
# APP_CFLAGS+=-I$(abspath $(LOCAL_PATH))/openssl/include

# ibrdtn
# include ibrcommon headers
APP_CPPFLAGS+=-I$(abspath $(LOCAL_PATH))/ibrcommon

# dtnd
# also include ibrdtn headers
APP_CPPFLAGS+=-I$(abspath $(LOCAL_PATH))/ibrdtn

# If APP_MODULES is not set, all modules are compiled!
APP_MODULES:=ibrcommon ibrdtn dtnd android-glue
#APP_MODULES:=all
