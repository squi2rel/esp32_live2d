#pragma once

#include <string>
#include <vector>

#include <CubismFramework.hpp>

class LAppAssetProvider
{
public:
    struct ModelEntry
    {
        std::string Directory;
        std::string JsonFileName;
    };

    static LAppAssetProvider& GetInstance();

    void SetBaseDirectory(const std::string& baseDirectory);
    std::string GetResourceRootPath() const;
    std::string BuildResourcePath(const std::string& relativePath) const;
    std::vector<ModelEntry> EnumerateModelEntries() const;

    Csm::csmByte* LoadFileAsBytes(const std::string& filePath, Csm::csmSizeInt* outSize) const;
    void ReleaseBytes(Csm::csmByte* byteData) const;

private:
    LAppAssetProvider();

    std::string ResolvePath(const std::string& filePath) const;

    std::string _baseDirectory;
};
