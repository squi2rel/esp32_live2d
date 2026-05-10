/**
 * Copyright(c) Live2D Inc. All rights reserved.
 *
 * Use of this source code is governed by the Live2D Open Software license
 * that can be found at https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html.
 */

#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <vector>
#include <Rendering/PortableGL/CubismPortableGL.hpp>
#include <Type/csmVector.hpp>

#include "LAppTextureManager_Common.hpp"

/**
* @brief テクスチャ管理クラス
*
* 画像読み込み、管理を行うクラス。
*/
class LAppTextureManager : public LAppTextureManager_Common
{
public:
    enum TextureUsage
    {
        TextureUsage_Other = 0,
        TextureUsage_Model = 1,
        TextureUsage_Ui = 2,
    };

    struct TextureEntryStats
    {
        std::string FileName;
        std::size_t Bytes;
        std::size_t RefCount;
        TextureUsage Usage;
    };

    struct TextureStats
    {
        std::size_t TotalBytes;
        std::size_t TotalCount;
        std::size_t ModelBytes;
        std::size_t ModelCount;
        std::size_t UiBytes;
        std::size_t UiCount;
        std::size_t OtherBytes;
        std::size_t OtherCount;
        std::vector<TextureEntryStats> Entries;
    };

    /**
    * @brief コンストラクタ
    */
    LAppTextureManager();

    /**
    * @brief デストラクタ
    *
    */
    ~LAppTextureManager();

    /**
    * @brief 画像読み込み
    *
    * @param[in] fileName  読み込む画像ファイルパス名
    * @return 画像情報。読み込み失敗時はNULLを返す
    */
    TextureInfo* CreateTextureFromPngFile(std::string fileName, TextureUsage usage = TextureUsage_Other);

    /**
    * @brief 画像の解放
    *
    * 配列に存在する画像全てを解放する
    */
    void ReleaseTextures();

    /**
     * @brief 画像の解放
     *
     * 指定したテクスチャIDの画像を解放する
     * @param[in] textureId  解放するテクスチャID
     **/
    void ReleaseTexture(Csm::csmUint32 textureId);

    /**
    * @brief 画像の解放
    *
    * 指定した名前の画像を解放する
    * @param[in] fileName  解放する画像ファイルパス名
    **/
    void ReleaseTexture(std::string fileName);

    std::size_t GetTextureMemoryUsageBytes() const;
    std::size_t GetTextureCount() const;
    TextureStats CollectTextureStats() const;

private:
    struct TextureRefInfo
    {
        std::size_t RefCount;
        TextureUsage Usage;
    };

    void ReleaseTextureAt(Csm::csmUint32 index);

    std::map<std::string, TextureRefInfo> _textureRefInfo;
};
