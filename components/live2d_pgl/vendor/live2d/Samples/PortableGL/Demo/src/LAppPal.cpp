/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppPal.hpp"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#if !defined(CSM_TARGET_ESP_PGL)
#include <time.h>
#endif
#include <iostream>
#include <Model/CubismMoc.hpp>
#if defined(CSM_TARGET_ESP_PGL)
#include <esp_timer.h>
#endif

#include "LAppAssetProvider.hpp"
#include "LAppDefine.hpp"

using std::endl;
using namespace Csm;
using namespace std;
using namespace LAppDefine;

double LAppPal::s_currentFrame = 0.0;
double LAppPal::s_lastFrame = 0.0;
double LAppPal::s_deltaTime = 0.0;

namespace {
double GetMonotonicTimeSeconds()
{
#if defined(CSM_TARGET_ESP_PGL)
    return static_cast<double>(esp_timer_get_time()) / 1000000.0;
#else
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1000000000.0;
#endif
}
}

csmByte* LAppPal::LoadFileAsBytes(const string filePath, csmSizeInt* outSize)
{
    csmByte* bytes = LAppAssetProvider::GetInstance().LoadFileAsBytes(filePath, outSize);
    if (bytes == NULL && DebugLogEnable)
    {
        PrintLogLn("File load failed. path:%s", filePath.c_str());
    }
    return bytes;
}

void LAppPal::ReleaseBytes(csmByte* byteData)
{
    LAppAssetProvider::GetInstance().ReleaseBytes(byteData);
}

csmFloat32  LAppPal::GetDeltaTime()
{
    return static_cast<csmFloat32>(s_deltaTime);
}

void LAppPal::UpdateTime()
{
    s_currentFrame = GetMonotonicTimeSeconds();
    if (s_lastFrame == 0.0)
    {
        s_lastFrame = s_currentFrame;
    }
    s_deltaTime = s_currentFrame - s_lastFrame;
    s_lastFrame = s_currentFrame;
}

void LAppPal::PrintLog(const csmChar* format, ...)
{
    va_list args;
    csmChar buf[256];
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args); // 標準出力でレンダリング
    std::cout << buf;
    va_end(args);
}

void LAppPal::PrintLogLn(const csmChar* format, ...)
{
    va_list args;
    csmChar buf[256];
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args); // 標準出力でレンダリング
    std::cout << buf << std::endl;
    va_end(args);
}

void LAppPal::PrintMessage(const csmChar* message)
{
    PrintLog("%s", message);
}

void LAppPal::PrintMessageLn(const csmChar* message)
{
    PrintLogLn("%s", message);
}
