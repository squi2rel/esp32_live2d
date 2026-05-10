/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#include "LAppTextureManager.hpp"

#include <algorithm>
#include <iostream>

#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include "stb_image.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include "LAppPal.hpp"

LAppTextureManager::LAppTextureManager() : LAppTextureManager_Common()
{
}

LAppTextureManager::~LAppTextureManager()
{
    ReleaseTextures();
}

LAppTextureManager::TextureInfo* LAppTextureManager::CreateTextureFromPngFile(std::string fileName, TextureUsage usage)
{
    //search loaded texture already.
    for (Csm::csmUint32 i = 0; i < _texturesInfo.GetSize(); i++)
    {
        if (_texturesInfo[i]->fileName == fileName)
        {
            TextureRefInfo& refInfo = _textureRefInfo[fileName];
            ++refInfo.RefCount;
            if (refInfo.Usage == TextureUsage_Other)
            {
                refInfo.Usage = usage;
            }
            else if (usage != TextureUsage_Other && refInfo.Usage != usage)
            {
                refInfo.Usage = TextureUsage_Other;
            }
            return _texturesInfo[i];
        }
    }

    GLuint textureId;
    int width, height, channels;
    unsigned int size;
    unsigned char* png;
    unsigned char* address;

    address = LAppPal::LoadFileAsBytes(fileName, &size);
    if (address == NULL || size == 0)
    {
        return NULL;
    }

    // png情報を取得する
    png = stbi_load_from_memory(
        address,
        static_cast<int>(size),
        &width,
        &height,
        &channels,
        STBI_rgb_alpha);
    if (png == NULL)
    {
        LAppPal::ReleaseBytes(address);
        return NULL;
    }
    {

#ifdef PREMULTIPLIED_ALPHA_ENABLE
        unsigned int* fourBytes = reinterpret_cast<unsigned int*>(png);
        for (int i = 0; i < width * height; i++)
        {
            unsigned char* p = png + i * 4;
            fourBytes[i] = LAppTextureManager_Common::Premultiply(p[0], p[1], p[2], p[3]);
        }
#endif
    }

    // OpenGL用のテクスチャを生成する
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, png);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 解放処理
    stbi_image_free(png);
    LAppPal::ReleaseBytes(address);

    LAppTextureManager::TextureInfo* textureInfo = new LAppTextureManager::TextureInfo();
    if (textureInfo != NULL)
    {
        textureInfo->fileName = fileName;
        textureInfo->width = width;
        textureInfo->height = height;
        textureInfo->id = textureId;

        _texturesInfo.PushBack(textureInfo);
        _textureRefInfo[fileName] = {1u, usage};
    }

    return textureInfo;

}

void LAppTextureManager::ReleaseTextures()
{
    for (Csm::csmUint32 i = 0; i < _texturesInfo.GetSize(); i++)
    {
        const GLuint textureId = static_cast<GLuint>(_texturesInfo[i]->id);
        glDeleteTextures(1, &textureId);
    }

    ReleaseTexturesInfo();
    _textureRefInfo.clear();
}

void LAppTextureManager::ReleaseTexture(Csm::csmUint32 textureId)
{
    for (Csm::csmUint32 i = 0; i < _texturesInfo.GetSize(); i++)
    {
        if (_texturesInfo[i]->id != textureId)
        {
            continue;
        }
        ReleaseTextureAt(i);
        break;
    }
}

void LAppTextureManager::ReleaseTexture(std::string fileName)
{
    for (Csm::csmUint32 i = 0; i < _texturesInfo.GetSize(); i++)
    {
        if (_texturesInfo[i]->fileName == fileName)
        {
            ReleaseTextureAt(i);
            break;
        }
    }
}

void LAppTextureManager::ReleaseTextureAt(Csm::csmUint32 index)
{
    if (index >= _texturesInfo.GetSize() || _texturesInfo[index] == NULL)
    {
        return;
    }

    TextureInfo* textureInfo = _texturesInfo[index];
    std::map<std::string, TextureRefInfo>::iterator refInfo = _textureRefInfo.find(textureInfo->fileName);
    if (refInfo != _textureRefInfo.end() && refInfo->second.RefCount > 1)
    {
        --refInfo->second.RefCount;
        return;
    }

    if (refInfo != _textureRefInfo.end())
    {
        _textureRefInfo.erase(refInfo);
    }

    const GLuint textureId = static_cast<GLuint>(textureInfo->id);
    glDeleteTextures(1, &textureId);
    delete textureInfo;
    _texturesInfo.Remove(index);
}

std::size_t LAppTextureManager::GetTextureMemoryUsageBytes() const
{
    std::size_t totalBytes = 0;
    for (Csm::csmUint32 i = 0; i < _texturesInfo.GetSize(); ++i)
    {
        if (_texturesInfo[i] == NULL)
        {
            continue;
        }

        totalBytes += static_cast<std::size_t>(_texturesInfo[i]->width) *
            static_cast<std::size_t>(_texturesInfo[i]->height) * 4u;
    }

    return totalBytes;
}

std::size_t LAppTextureManager::GetTextureCount() const
{
    return static_cast<std::size_t>(_texturesInfo.GetSize());
}

LAppTextureManager::TextureStats LAppTextureManager::CollectTextureStats() const
{
    TextureStats stats = {};

    for (Csm::csmUint32 i = 0; i < _texturesInfo.GetSize(); ++i)
    {
        if (_texturesInfo[i] == NULL)
        {
            continue;
        }

        const TextureInfo& textureInfo = *(_texturesInfo[i]);
        const std::size_t bytes = static_cast<std::size_t>(textureInfo.width) *
            static_cast<std::size_t>(textureInfo.height) * 4u;

        TextureUsage usage = TextureUsage_Other;
        std::size_t refCount = 0;
        std::map<std::string, TextureRefInfo>::const_iterator refInfo = _textureRefInfo.find(textureInfo.fileName);
        if (refInfo != _textureRefInfo.end())
        {
            usage = refInfo->second.Usage;
            refCount = refInfo->second.RefCount;
        }

        TextureEntryStats entry = {};
        entry.FileName = textureInfo.fileName;
        entry.Bytes = bytes;
        entry.RefCount = refCount;
        entry.Usage = usage;
        stats.Entries.push_back(entry);

        stats.TotalBytes += bytes;
        ++stats.TotalCount;

        switch (usage)
        {
        case TextureUsage_Model:
            stats.ModelBytes += bytes;
            ++stats.ModelCount;
            break;
        case TextureUsage_Ui:
            stats.UiBytes += bytes;
            ++stats.UiCount;
            break;
        default:
            stats.OtherBytes += bytes;
            ++stats.OtherCount;
            break;
        }
    }

    std::sort(stats.Entries.begin(), stats.Entries.end(), [](const TextureEntryStats& lhs, const TextureEntryStats& rhs) {
        if (lhs.Bytes != rhs.Bytes)
        {
            return lhs.Bytes > rhs.Bytes;
        }

        return lhs.FileName < rhs.FileName;
    });

    return stats;
}
