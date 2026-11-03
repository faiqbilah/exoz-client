#pragma once
#include <android/log.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "BedrockClient", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "BedrockClient", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "BedrockClient", __VA_ARGS__)
