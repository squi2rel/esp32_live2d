#include "LAppAssetProvider.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "LAppDefine.hpp"

#if !defined(CSM_TARGET_ESP_PGL)
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#endif

namespace {
std::string EnsureTrailingSlash(const std::string& path)
{
    if (path.empty() || path[path.size() - 1] == '/')
    {
        return path;
    }

    return path + "/";
}

#if defined(CSM_TARGET_ESP_PGL)
extern const uint8_t big_squirrel_moc3_start[] asm("_binary_big_squirrel_moc3_start");
extern const uint8_t big_squirrel_moc3_end[] asm("_binary_big_squirrel_moc3_end");
extern const uint8_t big_squirrel_cdi3_json_start[] asm("_binary_big_squirrel_cdi3_json_start");
extern const uint8_t big_squirrel_cdi3_json_end[] asm("_binary_big_squirrel_cdi3_json_end");
extern const uint8_t big_squirrel_texture_00_png_start[] asm("_binary_big_squirrel_texture_00_png_start");
extern const uint8_t big_squirrel_texture_00_png_end[] asm("_binary_big_squirrel_texture_00_png_end");

struct EmbeddedAsset
{
    const char* Path;
    const uint8_t* Start;
    const uint8_t* End;
};

constexpr const char* kEmbeddedBigSquirrelDirectory = "A";
constexpr const char* kEmbeddedBigSquirrelModelJsonName = "big_squirrel.model3.json";
constexpr char kEmbeddedBigSquirrelModelJson[] =
    "{\n"
    "  \"Version\": 3,\n"
    "  \"FileReferences\": {\n"
    "    \"Moc\": \"big_squirrel.moc3\",\n"
    "    \"Textures\": [\n"
    "      \"big_squirrel.512/texture_00.png\"\n"
    "    ],\n"
    "    \"DisplayInfo\": \"big_squirrel.cdi3.json\"\n"
    "  },\n"
    "  \"Groups\": [\n"
    "    {\n"
    "      \"Target\": \"Parameter\",\n"
    "      \"Name\": \"EyeBlink\",\n"
    "      \"Ids\": []\n"
    "    },\n"
    "    {\n"
    "      \"Target\": \"Parameter\",\n"
    "      \"Name\": \"LipSync\",\n"
    "      \"Ids\": []\n"
    "    }\n"
    "  ]\n"
    "}\n";

const EmbeddedAsset kEmbeddedBigSquirrelAssets[] = {
    {
        "A/big_squirrel.moc3",
        big_squirrel_moc3_start,
        big_squirrel_moc3_end,
    },
    {
        "A/big_squirrel.cdi3.json",
        big_squirrel_cdi3_json_start,
        big_squirrel_cdi3_json_end,
    },
    {
        "A/big_squirrel.512/texture_00.png",
        big_squirrel_texture_00_png_start,
        big_squirrel_texture_00_png_end,
    },
};

bool TryLoadEmbeddedBigSquirrelAsset(const std::string& filePath, Csm::csmSizeInt* outSize, Csm::csmByte** outBytes)
{
    if (outSize == NULL || outBytes == NULL)
    {
        return false;
    }

    if (filePath == "A/big_squirrel.model3.json")
    {
        *outSize = static_cast<Csm::csmSizeInt>(sizeof(kEmbeddedBigSquirrelModelJson) - 1);
        *outBytes = reinterpret_cast<Csm::csmByte*>(const_cast<char*>(kEmbeddedBigSquirrelModelJson));
        return true;
    }

    for (std::size_t i = 0; i < (sizeof(kEmbeddedBigSquirrelAssets) / sizeof(kEmbeddedBigSquirrelAssets[0])); ++i)
    {
        const EmbeddedAsset& asset = kEmbeddedBigSquirrelAssets[i];
        if (filePath != asset.Path)
        {
            continue;
        }

        *outSize = static_cast<Csm::csmSizeInt>(asset.End - asset.Start);
        *outBytes = const_cast<Csm::csmByte*>(reinterpret_cast<const Csm::csmByte*>(asset.Start));
        return true;
    }

    return false;
}
#endif
}

LAppAssetProvider& LAppAssetProvider::GetInstance()
{
    static LAppAssetProvider instance;
    return instance;
}

LAppAssetProvider::LAppAssetProvider()
    : _baseDirectory()
{
}

void LAppAssetProvider::SetBaseDirectory(const std::string& baseDirectory)
{
    _baseDirectory = EnsureTrailingSlash(baseDirectory);
}

std::string LAppAssetProvider::GetResourceRootPath() const
{
#if defined(CSM_TARGET_ESP_PGL)
    return std::string();
#else
    if (_baseDirectory.empty())
    {
        return std::string();
    }

    return _baseDirectory + LAppDefine::ResourcesPath;
#endif
}

std::string LAppAssetProvider::BuildResourcePath(const std::string& relativePath) const
{
    const std::string rootPath = GetResourceRootPath();
    if (rootPath.empty())
    {
        return relativePath;
    }

    return rootPath + relativePath;
}

std::vector<LAppAssetProvider::ModelEntry> LAppAssetProvider::EnumerateModelEntries() const
{
    std::vector<ModelEntry> entries;

#if defined(CSM_TARGET_ESP_PGL)
    ModelEntry entry = {};
    entry.Directory = kEmbeddedBigSquirrelDirectory;
    entry.JsonFileName = kEmbeddedBigSquirrelModelJsonName;
    entries.push_back(entry);
    return entries;
#else
    const std::string resourceRoot = GetResourceRootPath();
    if (resourceRoot.empty())
    {
        return entries;
    }

    DIR* resourceDirectory = opendir(resourceRoot.c_str());
    if (resourceDirectory == NULL)
    {
        return entries;
    }

    struct dirent* modelDirectory = NULL;
    while ((modelDirectory = readdir(resourceDirectory)) != NULL)
    {
        if ((modelDirectory->d_type & DT_DIR) == 0
            || std::strcmp(modelDirectory->d_name, ".") == 0
            || std::strcmp(modelDirectory->d_name, "..") == 0)
        {
            continue;
        }

        const std::string modelDirectoryName = modelDirectory->d_name;
        const std::string preferredModelJsonName = modelDirectoryName + ".model3.json";
        const std::string modelPath = resourceRoot + modelDirectoryName + "/";

        DIR* modelFiles = opendir(modelPath.c_str());
        if (modelFiles == NULL)
        {
            continue;
        }

        std::string discoveredModelJsonName;
        struct dirent* modelFile = NULL;
        while ((modelFile = readdir(modelFiles)) != NULL)
        {
            const char* entryName = modelFile->d_name;
            const char* suffix = ".model3.json";
            const std::size_t entryLength = std::strlen(entryName);
            const std::size_t suffixLength = std::strlen(suffix);
            if (entryLength < suffixLength
                || std::strcmp(entryName + entryLength - suffixLength, suffix) != 0)
            {
                continue;
            }

            if (preferredModelJsonName == entryName)
            {
                discoveredModelJsonName = entryName;
                break;
            }

            if (discoveredModelJsonName.empty())
            {
                discoveredModelJsonName = entryName;
            }
        }

        closedir(modelFiles);

        if (!discoveredModelJsonName.empty())
        {
            ModelEntry entry = {};
            entry.Directory = modelDirectoryName;
            entry.JsonFileName = discoveredModelJsonName;
            entries.push_back(entry);
        }
    }

    closedir(resourceDirectory);
    return entries;
#endif
}

Csm::csmByte* LAppAssetProvider::LoadFileAsBytes(const std::string& filePath, Csm::csmSizeInt* outSize) const
{
    if (outSize == NULL)
    {
        return NULL;
    }

#if defined(CSM_TARGET_ESP_PGL)
    Csm::csmByte* bytes = NULL;
    if (TryLoadEmbeddedBigSquirrelAsset(filePath, outSize, &bytes))
    {
        return bytes;
    }

    static bool loggedMissingAssetProvider = false;
    if (!loggedMissingAssetProvider)
    {
        std::fprintf(stdout, "[APP]asset provider not configured for ESP target.\n");
        std::fflush(stdout);
        loggedMissingAssetProvider = true;
    }
    *outSize = 0;
    (void)filePath;
    return NULL;
#else
    const std::string resolvedPath = ResolvePath(filePath);
    if (resolvedPath.empty())
    {
        *outSize = 0;
        return NULL;
    }

    struct stat fileStat = {};
    if (stat(resolvedPath.c_str(), &fileStat) != 0 || fileStat.st_size <= 0)
    {
        *outSize = 0;
        return NULL;
    }

    std::ifstream file(resolvedPath.c_str(), std::ios::in | std::ios::binary);
    if (!file.is_open())
    {
        *outSize = 0;
        return NULL;
    }

    const Csm::csmSizeInt size = static_cast<Csm::csmSizeInt>(fileStat.st_size);
    char* bytes = new char[size];
    file.read(bytes, size);
    file.close();

    *outSize = size;
    return reinterpret_cast<Csm::csmByte*>(bytes);
#endif
}

void LAppAssetProvider::ReleaseBytes(Csm::csmByte* byteData) const
{
#if defined(CSM_TARGET_ESP_PGL)
    (void)byteData;
#else
    delete[] byteData;
#endif
}

std::string LAppAssetProvider::ResolvePath(const std::string& filePath) const
{
    if (filePath.empty())
    {
        return std::string();
    }

    if (filePath[0] == '/' || _baseDirectory.empty())
    {
        return filePath;
    }

    return _baseDirectory + filePath;
}
