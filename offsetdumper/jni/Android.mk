LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := offsetdumper

# Compila seu codigo + os arquivos .c do xDL direto
LOCAL_SRC_FILES := module.cpp Il2Cpp.cpp \
                  xdl/xdl.c \
                  xdl/xdl_iterate.c \
                  xdl/xdl_linker.c \
                  xdl/xdl_lzma.c \
                  xdl/xdl_util.c

# Inclui a pasta jni/ e a pasta jni/xdl/include
LOCAL_C_INCLUDES := $(LOCAL_PATH) $(LOCAL_PATH)/xdl/include

LOCAL_LDLIBS := -llog

LOCAL_CPPFLAGS := -std=c++17 -fno-exceptions -fno-rtti -fvisibility=hidden
LOCAL_CFLAGS := -Wall -Wextra -fvisibility=hidden -fno-unwind-tables -Wno-error=format-security -w

LOCAL_LDFLAGS := -Wl,--exclude-libs,ALL -Wl,--gc-sections -Wl,--version-script=$(LOCAL_PATH)/version_script.txt

include $(BUILD_SHARED_LIBRARY)