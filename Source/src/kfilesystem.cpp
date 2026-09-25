/**
 * @file kfilesystem.cpp
 * @brief Implementation of the virtual file system.
 */

#include "kfilesystem.h"
#include "kpackage.h"

#include <filesystem>
#include <fstream>
#include <cstdio>

namespace fs = std::filesystem;

namespace kemena
{

bool kFileSystem::s_initialized = false;
bool kFileSystem::s_packaged = false;
kString kFileSystem::s_dataRoot;
std::unique_ptr<kPackageReader> kFileSystem::s_package;

void kFileSystem::init(const kString& exeDir, const kString& dataDir)
{
    if (s_initialized)
        return;

    fs::path exePath(exeDir);
    fs::path dataPath = exePath / dataDir;

    // 1. Look for a .kpak file next to the executable
    //    e.g. MyGame.exe → MyGame.kpak  or  data.kpak
    std::error_code ec;

    // Try <exeName>.kpak
    fs::path exeFile = exePath.filename();
    fs::path pakPath1 = exePath / (exeFile.stem().string() + ".kpak");

    // Try data.kpak
    fs::path pakPath2 = exePath / "data.kpak";

    // Try <dataDir>.kpak
    fs::path pakPath3 = exePath / (dataDir + ".kpak");

    fs::path chosenPak;
    if (fs::exists(pakPath1, ec))       chosenPak = pakPath1;
    else if (fs::exists(pakPath2, ec))  chosenPak = pakPath2;
    else if (fs::exists(pakPath3, ec))  chosenPak = pakPath3;

    if (!chosenPak.empty())
    {
        // Mount the package
        s_package = std::make_unique<kPackageReader>();
        if (s_package->open(chosenPak.string()))
        {
            s_packaged = true;
            s_dataRoot = chosenPak.string();
            s_initialized = true;
            return;
        }
        else
        {
            s_package.reset();
        }
    }

    // 2. Fall back to directory-based access
    if (fs::exists(dataPath, ec))
    {
        s_dataRoot = dataPath.string();
        s_packaged = false;
        s_initialized = true;
        return;
    }

    // 3. Last resort: use exe dir itself
    s_dataRoot = exePath.string();
    s_packaged = false;
    s_initialized = true;
}

void kFileSystem::shutdown()
{
    if (s_package)
    {
        s_package->close();
        s_package.reset();
    }
    s_initialized = false;
    s_packaged = false;
    s_dataRoot.clear();
}

bool kFileSystem::isPackaged()
{
    return s_packaged;
}

std::vector<uint8_t> kFileSystem::readFile(const kString& relativePath)
{
    std::vector<uint8_t> result;

    if (!s_initialized)
        return result;

    if (s_packaged && s_package)
    {
        s_package->readFile(relativePath, result);
        return result;
    }

    // Direct filesystem read
    fs::path fullPath = fs::path(s_dataRoot) / relativePath;
    std::error_code ec;
    if (!fs::exists(fullPath, ec))
        return result;

    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return result;

    size_t size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    result.resize(size);
    file.read(reinterpret_cast<char*>(result.data()), size);
    file.close();

    return result;
}

kString kFileSystem::readFileString(const kString& relativePath)
{
    auto data = readFile(relativePath);
    if (data.empty())
        return {};
    return kString(reinterpret_cast<const char*>(data.data()), data.size());
}

bool kFileSystem::fileExists(const kString& relativePath)
{
    if (!s_initialized)
        return false;

    if (s_packaged && s_package)
        return s_package->fileExists(relativePath);

    fs::path fullPath = fs::path(s_dataRoot) / relativePath;
    std::error_code ec;
    return fs::exists(fullPath, ec);
}

size_t kFileSystem::getFileSize(const kString& relativePath)
{
    if (!s_initialized)
        return 0;

    if (s_packaged && s_package)
        return s_package->getFileSize(relativePath);

    fs::path fullPath = fs::path(s_dataRoot) / relativePath;
    std::error_code ec;
    if (!fs::exists(fullPath, ec))
        return 0;
    return static_cast<size_t>(fs::file_size(fullPath, ec));
}

kString kFileSystem::resolveOSPath(const kString& relativePath)
{
    if (!s_initialized)
        return {};

    if (s_packaged && s_package)
    {
        // Extract the file to a temporary location for APIs that need an OS path.
        // We use the system temp directory, preserving the relative path structure.
        fs::path tempDir = fs::temp_directory_path() / "Kemena3D_extracted";
        std::error_code ec;
        fs::create_directories(tempDir, ec);

        fs::path extractedPath = tempDir / relativePath;

        // Avoid re-extracting if already done this session
        if (!fs::exists(extractedPath, ec))
        {
            std::vector<uint8_t> data;
            if (s_package->readFile(relativePath, data))
            {
                fs::create_directories(extractedPath.parent_path(), ec);
                std::ofstream out(extractedPath, std::ios::binary);
                if (out.is_open())
                {
                    out.write(reinterpret_cast<const char*>(data.data()), data.size());
                    out.close();
                }
            }
        }

        return extractedPath.string();
    }

    // Direct filesystem: return the absolute path
    return (fs::path(s_dataRoot) / relativePath).string();
}

const kString& kFileSystem::getDataRoot()
{
    return s_dataRoot;
}

} // namespace kemena
