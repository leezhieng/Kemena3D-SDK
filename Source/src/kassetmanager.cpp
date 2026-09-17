#include "kassetmanager.h"
#include "kgl_internal.h"

#ifndef KEMENA_NO_ASSIMP
// Public header is now Assimp-free; the importer paths below still need
// Assimp directly, so include it locally along with the private helpers.
#include "kassimp_internal.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STBI_MSC_SECURE_CRT
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <fstream>
#include <vector>
#include <cstring>
#include <cstdint>

// S3TC / sRGB-S3TC tokens (from GL_EXT_texture_compression_s3tc and
// GL_EXT_texture_sRGB). Guarded in case the GL loader doesn't expose them.
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
#define GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT 0x8C4F
#endif

namespace kemena
{
    kAssetManager::kAssetManager()
    {
    }

    kString kAssetManager::getFileExtension(const kString &fileName)
    {
        kString::size_type idx;
        idx = fileName.rfind('.');

        if (idx != kString::npos)
        {
            kString extension = fileName.substr(idx + 1);
            return extension;
        }

        // No extension found
        return "";
    }

    bool kAssetManager::fileExists(const kString &fileName)
    {
        // When running from a package, try the VFS first with a relative path.
        if (kFileSystem::isPackaged())
        {
            if (kFileSystem::fileExists(fileName))
                return true;
        }

        bool ret;
        FILE *fp = fopen(fileName.c_str(), "rb");
        if (fp)
        {
            ret = true;
            fclose(fp);
        }
        else
        {
            ret = false;
        }
        return ret;
    }

    kString kAssetManager::getBaseDir(const kString &filePath)
    {
        if (filePath.find_last_of("/\\") != kString::npos)
            return filePath.substr(0, filePath.find_last_of("/\\"));
        return "";
    }

    kString kAssetManager::getBaseFilename(const kString &filePath)
    {
        if (filePath.find_last_of("/\\") != kString::npos)
            return filePath.substr(filePath.find_last_of("/\\") + 1, filePath.size());
        return "";
    }

    kString kAssetManager::getExecDir()
    {
        return kString(SDL_GetBasePath());
    }

    kString kAssetManager::popDir(const kString &filePath)
    {
        if (filePath.find_last_of("/\\") != kString::npos)
            return filePath.substr(0, filePath.find_last_of("/\\"));
        return "";
    }

// -----------------------------------------------------------------------
// Portable resource file helper — loads a file from the Resources/
// directory next to the executable (macOS) or from the executable's
// base path (all platforms).  Returns nullptr on failure.
// -----------------------------------------------------------------------
static kString resourcePath(const kString &resourceName)
{
    const char *base = SDL_GetBasePath();
    if (!base) return kString();
    kString path = kString(base) + "Resources/" + resourceName;
    SDL_free(const_cast<char *>(base));
    return path;
}

static bool readResourceFile(const kString &resourceName, std::vector<char> &out)
{
    kString path = resourcePath(resourceName);
    if (path.empty()) return false;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamsize sz = f.tellg();
    f.seekg(0);
    out.resize(static_cast<size_t>(sz));
    f.read(out.data(), sz);
    return f.good();
}

// -----------------------------------------------------------------------
// Resource-loading implementations
// -----------------------------------------------------------------------

 unsigned char* kAssetManager::loadImageFromResource(const char* resourceName, int& width, int& height, int& channels)
 {
#ifdef _WIN32
  // Find the resource
  HRSRC hRes = FindResource(NULL, resourceName, RT_RCDATA);
  if (!hRes) return nullptr;
  HGLOBAL hData = LoadResource(NULL, hRes);
  if (!hData) return nullptr;
  DWORD size = SizeofResource(NULL, hRes);
  void* pData = LockResource(hData);
  unsigned char* data = stbi_load_from_memory(
   reinterpret_cast<unsigned char*>(pData), size,
   &width, &height, &channels, 0);
  return data;
#else
  std::vector<char> buf;
  if (!readResourceFile(resourceName, buf)) return nullptr;
  return stbi_load_from_memory(
   reinterpret_cast<unsigned char*>(buf.data()),
   static_cast<int>(buf.size()),
   &width, &height, &channels, 0);
#endif
 }

    kTexture2D *kAssetManager::loadTexture2D(const kString fileName, const kString textureName, const kTextureFormat format, const bool flipVertical, const bool keepData)
    {
        // std::cout << textureName << std::endl;

        int width;
        int height;
        int channels;

        // Set the flip state explicitly every call — it's a sticky global, so
        // leaving it on would silently flip every later load.
        stbi_set_flip_vertically_on_load(flipVertical);

        // Force stb to decode the exact channel count the GL upload expects.
        // Decoding native channels (0) and then uploading as a fixed format
        // (e.g. a 3-channel RGB image uploaded as GL_RGBA) reads past the pixel
        // buffer and corrupts the texture, which crashes the GL driver on draw.
        int reqChannels = (format == kTextureFormat::TEX_FORMAT_RGB ||
                           format == kTextureFormat::TEX_FORMAT_SRGB) ? 3 : 4;
        unsigned char *data = stbi_load(fileName.c_str(), &width, &height, &channels, reqChannels);

        kDriver *driver = kDriver::getCurrent();

        uint32_t textureID = 0;
        if (data)
        {
            textureID = driver->createTexture2D(width, height, format, data,
                                                kTextureWrap::REPEAT,
                                                kTextureFilter::LINEAR_MIPMAP_LINEAR,
                                                kTextureFilter::LINEAR, true);
        }
        else
        {
            std::cout << "Failed to load texture:" << fileName << std::endl;
        }

        kTexture2D *texture = new kTexture2D();

        texture->setType(TEX_TYPE_2D);
        texture->setTextureID(textureID);
        texture->setWidth(width);
        texture->setHeight(height);
        texture->setChannels(channels);
        texture->setTextureName(textureName);

        // Normally no need keep the data unless you need it to modify or export the texture again later
        if (keepData)
            texture->setData(data);
        else
            stbi_image_free(data);

        return texture;
    }

    kTexture2D *kAssetManager::loadTexture2DDDS(const kString fileName, const kString textureName, const bool sRGB, const int wrapMode, const int filterMode)
    {
        std::ifstream f(fileName.c_str(), std::ios::binary | std::ios::ate);
        if (!f.is_open())
        {
            std::cout << "Failed to open DDS: " << fileName << std::endl;
            return nullptr;
        }
        std::streamsize sz = f.tellg();
        if (sz < 128) { std::cout << "DDS too small: " << fileName << std::endl; return nullptr; }
        f.seekg(0, std::ios::beg);
        std::vector<unsigned char> buf((size_t)sz);
        f.read(reinterpret_cast<char *>(buf.data()), sz);
        f.close();

        auto rd = [&](size_t off) -> uint32_t {
            return  (uint32_t)buf[off]
                 | ((uint32_t)buf[off + 1] << 8)
                 | ((uint32_t)buf[off + 2] << 16)
                 | ((uint32_t)buf[off + 3] << 24);
        };

        if (rd(0) != 0x20534444) { std::cout << "Not a DDS file: " << fileName << std::endl; return nullptr; } // "DDS "

        int      width    = (int)rd(16);
        int      height   = (int)rd(12);
        uint32_t mipCount = rd(28);
        uint32_t pfFlags  = rd(80);
        uint32_t fourCC   = rd(84);
        int      levels   = mipCount > 0 ? (int)mipCount : 1;

        const bool compressed = (pfFlags & 0x4) != 0;            // DDPF_FOURCC
        const uint32_t DXT5   = ('D') | ('X' << 8) | ('T' << 16) | ('5' << 24);
        if (compressed && fourCC != DXT5)
        {
            std::cout << "Unsupported DDS FourCC in " << fileName << " (only DXT5 supported)" << std::endl;
            return nullptr;
        }

        kDriver *driver = kDriver::getCurrent();

        kTextureWrap kwrap = (wrapMode == 1) ? kTextureWrap::CLAMP_TO_EDGE
                           : (wrapMode == 2) ? kTextureWrap::MIRRORED_REPEAT
                                             : kTextureWrap::REPEAT;

        const bool hasMips = levels > 1;
        kTextureFilter magFil = (filterMode == 0) ? kTextureFilter::NEAREST : kTextureFilter::LINEAR;
        kTextureFilter minFil;
        if (filterMode == 0)       minFil = hasMips ? kTextureFilter::NEAREST_MIPMAP_NEAREST : kTextureFilter::NEAREST;
        else if (filterMode == 2)  minFil = hasMips ? kTextureFilter::LINEAR_MIPMAP_LINEAR  : kTextureFilter::LINEAR;
        else /* bilinear */        minFil = hasMips ? kTextureFilter::LINEAR_MIPMAP_NEAREST : kTextureFilter::LINEAR;

        kTextureFormat texFmt = sRGB ? kTextureFormat::TEX_FORMAT_SRGBA : kTextureFormat::TEX_FORMAT_RGBA;
        uint32_t textureID = driver->createTexture2D(width, height, texFmt, nullptr,
                                                      kwrap, minFil, magFil, false);

        const unsigned char *p = buf.data() + 128;
        size_t remaining = buf.size() - 128;
        int w = width, h = height;
        int uploaded = 0;
        for (int level = 0; level < levels; ++level)
        {
            size_t levelSize = compressed
                ? (size_t)((w + 3) / 4) * ((h + 3) / 4) * 16
                : (size_t)w * h * 4;
            if (levelSize == 0 || levelSize > remaining) break;

            if (compressed)
                driver->uploadCompressedTexture2D(textureID, level, w, h, texFmt, p, levelSize);
            else
                driver->uploadTexture2D(textureID, level, w, h, texFmt, p);

            p += levelSize;
            remaining -= levelSize;
            ++uploaded;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }

        if (uploaded == 0)
        {
            std::cout << "DDS had no usable levels: " << fileName << std::endl;
            driver->deleteTexture(textureID);
            return nullptr;
        }

        kTexture2D *texture = new kTexture2D();
        texture->setType(TEX_TYPE_2D);
        texture->setTextureID(textureID);
        texture->setWidth(width);
        texture->setHeight(height);
        texture->setChannels(4);
        texture->setTextureName(textureName);

        return texture;
    }

#ifndef KEMENA_NO_ASSIMP
    kTexture2D *kAssetManager::loadTexture2DFromMemory(const aiTexture *rawData, const kString textureName, const kTextureFormat format, const bool flipVertical, const bool keepData)
    {
        unsigned char *data = nullptr;

        int width;
        int height;
        int channels;

        if (flipVertical)
            stbi_set_flip_vertically_on_load(true);

        if (rawData->mHeight == 0)
        {
            data = stbi_load_from_memory(reinterpret_cast<unsigned char *>(rawData->pcData), rawData->mWidth, &width, &height, &channels, 0);
        }
        else
        {
            data = stbi_load_from_memory(reinterpret_cast<unsigned char *>(rawData->pcData), rawData->mWidth * rawData->mHeight, &width, &height, &channels, 0);
        }

        kDriver *driver = kDriver::getCurrent();

        uint32_t textureID = 0;
        if (data)
        {
            textureID = driver->createTexture2D(width, height, format, data,
                                                kTextureWrap::REPEAT,
                                                kTextureFilter::LINEAR_MIPMAP_LINEAR,
                                                kTextureFilter::LINEAR, true);
        }
        else
        {
            std::cout << "Failed to load texture from memory" << std::endl;
        }

        kTexture2D *texture = new kTexture2D();
        texture->setTextureID(textureID);

        texture->setWidth(width);
        texture->setHeight(height);
        texture->setChannels(channels);
        texture->setTextureName(textureName);

        // Normally no need keep the data unless you need it to modify or export the texture again later
        if (keepData)
            texture->setData(data);
        else
            stbi_image_free(data);

        return texture;
    }
#endif // KEMENA_NO_ASSIMP

	kTexture2D *kAssetManager::loadTexture2DFromResource(const kString resourceName, const kString textureName, const kTextureFormat format, const bool flipVertical, const bool keepData)
    {
        int width;
        int height;
        int channels;

        if (flipVertical)
            stbi_set_flip_vertically_on_load(true);
		
		unsigned char* data = loadImageFromResource(resourceName.c_str(), width, height, channels);

        kDriver *driver = kDriver::getCurrent();

        uint32_t textureID = 0;
        if (data)
        {
            textureID = driver->createTexture2D(width, height, format, data,
                                                kTextureWrap::REPEAT,
                                                kTextureFilter::LINEAR_MIPMAP_LINEAR,
                                                kTextureFilter::LINEAR, true);
        }
        else
        {
            std::cout << "Failed to load texture from memory" << std::endl;
        }

        kTexture2D *texture = new kTexture2D();
        texture->setTextureID(textureID);

        texture->setWidth(width);
        texture->setHeight(height);
        texture->setChannels(channels);
        texture->setTextureName(textureName);

        // Normally no need keep the data unless you need it to modify or export the texture again later
        if (keepData)
            texture->setData(data);
        else
            stbi_image_free(data);

        return texture;
    }

    void kAssetManager::saveTexture2D(kTexture2D *texture, const kString fileName, kString format)
    {
        if (format == "png")
        {
            stbi_write_png(fileName.c_str(), texture->getWidth(), texture->getHeight(), texture->getChannels(), texture->getData(), texture->getWidth() * texture->getChannels());
        }
        else if (format == "jpg")
        {
            stbi_write_jpg(fileName.c_str(), texture->getWidth(), texture->getHeight(), texture->getChannels(), texture->getData(), 12);
        }
        else if (format == "tga")
        {
            stbi_write_tga(fileName.c_str(), texture->getWidth(), texture->getHeight(), texture->getChannels(), texture->getData());
        }
        else if (format == "bmp")
        {
            stbi_write_bmp(fileName.c_str(), texture->getWidth(), texture->getHeight(), texture->getChannels(), texture->getData());
        }
        else
        {
            std::cout << "Failed to save texture, invalid format" << std::endl;
        }
    }

    kTextureCube *kAssetManager::loadTextureCube(const kString fileNameRight, const kString fileNameLeft, const kString fileNameTop, const kString fileNameBottom, const kString fileNameFront, const kString fileNameBack, const kString textureName)
    {
        std::vector<kString> faces;
        faces.push_back(fileNameRight);
        faces.push_back(fileNameLeft);
        faces.push_back(fileNameTop);
        faces.push_back(fileNameBottom);
        faces.push_back(fileNameFront);
        faces.push_back(fileNameBack);

        kDriver *driver = kDriver::getCurrent();

        // Load all 6 faces into memory first
        int width = 0, height = 0, nrChannels = 0;
        const void *faceData[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
        for (unsigned int i = 0; i < faces.size(); i++)
        {
            unsigned char *data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
            if (data)
            {
                faceData[i] = data;
            }
            else
            {
                std::cout << "Cubemap tex failed to load at path: " << faces[i] << std::endl;
            }
        }

        unsigned int textureID = driver->createTextureCube(width, height, faceData, true);

        // Free face data
        for (int i = 0; i < 6; ++i)
            if (faceData[i])
                stbi_image_free((unsigned char *)faceData[i]);

        kTextureCube *newTexture = new kTextureCube();
        newTexture->setType(kTextureType::TEX_TYPE_CUBE);
        newTexture->setTextureID(textureID);
        newTexture->setTextureName(textureName);

        return newTexture;
    }
	
	kTextureCube* kAssetManager::loadTextureCubeFromResource(const kString resRight, const kString resLeft, const kString resTop, const kString resBottom, const kString resFront, const kString resBack, const kString textureName)
	{
		std::vector<kString> faces;
		faces.push_back(resRight);
		faces.push_back(resLeft);
		faces.push_back(resTop);
		faces.push_back(resBottom);
		faces.push_back(resFront);
		faces.push_back(resBack);

		kDriver *driver = kDriver::getCurrent();

		int width = 0, height = 0, nrChannels = 0;
		const void *faceData[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
		for (unsigned int i = 0; i < faces.size(); i++)
		{
			width = height = nrChannels = -1;
			unsigned char* data = loadImageFromResource(faces[i].c_str(), width, height, nrChannels);
			if (data)
			{
				faceData[i] = data;
			}
			else
			{
				std::cout << "Cubemap tex failed to load from resource: " << faces[i] << std::endl;
			}
		}

		unsigned int textureID = driver->createTextureCube(width, height, faceData, true);

		// Free face data
		for (int i = 0; i < 6; ++i)
			if (faceData[i])
				stbi_image_free((unsigned char *)faceData[i]);

		kTextureCube* newTexture = new kTextureCube();
		newTexture->setType(kTextureType::TEX_TYPE_CUBE);
		newTexture->setTextureID(textureID);
		newTexture->setTextureName(textureName);

		return newTexture;
	}

	// You need to tell what's the extension of the file (obj, fbx, gltf, glb, etc.)
	kMesh *kAssetManager::loadMeshFromResource(const kString resourceName, const kString extention)
    {
		std::cout << "Load mesh from resource: " << resourceName << std::endl;
#ifndef KEMENA_NO_ASSIMP
		return loadMeshResourceAssimp(resourceName, extention);
#else
		std::cout << "loadMeshFromResource: not available in slim runtime build "
		             "(Assimp disabled). Only filesystem .gltf / .glb supported." << std::endl;
		return nullptr;
#endif
    }

    kMesh *kAssetManager::loadMesh(const kString fileName)
    {
        std::cout << "Load mesh: " << fileName << std::endl;

        kString ext = getFileExtension(fileName);
        kMesh *mesh = nullptr;

#ifndef KEMENA_NO_ASSIMP
        // Full editor build: assimp reads every supported format, including
        // glTF/GLB. The tinygltf path (loadMeshGltf) is only a stub right now,
        // so routing .glb through it here returned empty meshes — use assimp.
        if (ext == "obj" || ext == "fbx" || ext == "gltf" || ext == "glb")
        {
            mesh = loadMeshFileAssimp(fileName);
        }
        else
        {
            std::cout << "loadMesh: unsupported extension '" << ext << "'" << std::endl;
        }
#else
        // Slim runtime build (no assimp): glTF/GLB only, via tinygltf.
        if (ext == "gltf" || ext == "glb")
        {
            mesh = loadMeshGltf(fileName);
        }
        else
        {
            std::cout << "loadMesh: unsupported extension '" << ext << "'"
                      << " (slim runtime build accepts only .gltf / .glb)" << std::endl;
        }
#endif

        if (mesh)
        {
            mesh->setLoaded(true);
            mesh->setFileName(fileName);
        }
        return mesh;
    }

    kMesh *kAssetManager::loadMeshGltf(const kString fileName)
    {
        // TODO: actual tinygltf decode. This stub keeps the slim-build link
        // succeeding; replace with the tinygltf importer in the next pass.
        std::cout << "loadMeshGltf: tinygltf importer not yet wired — "
                  << "returning empty mesh for '" << fileName << "'\n";
        kMesh *m = new kMesh();
        m->setFileName(fileName);
        return m;
    }

#ifndef KEMENA_NO_ASSIMP
    kMesh *kAssetManager::loadMeshFileAssimp(const kString fileName)
    {
        // kMesh* rootMesh = new kMesh();
        kMesh *rootMesh;

        unsigned int assimpReadFlag = aiProcess_Triangulate |
                                      aiProcess_FlipUVs |
                                      aiProcess_GenSmoothNormals |
                                      aiProcess_CalcTangentSpace |
                                      aiProcess_JoinIdenticalVertices |
                                      aiProcess_LimitBoneWeights |
                                      aiProcess_ImproveCacheLocality |
                                      aiProcess_RemoveRedundantMaterials |
                                      aiProcess_FixInfacingNormals |
                                      aiProcess_TransformUVCoords |
                                      aiProcess_SortByPType;

        Assimp::Importer import;
        import.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

        const aiScene *scene = import.ReadFile(fileName, assimpReadFlag);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
        {
            std::cout << "ERROR::ASSIMP::" << import.GetErrorString() << std::endl;
            return nullptr;
        }
        kString directory;
        directory = fileName.substr(0, fileName.find_last_of('/'));

        // kMesh* model = processNode(scene->mRootNode, scene, rootMesh);
        std::map<kString, kBoneInfo> globalBoneMap;
        rootMesh = processNode(scene->mRootNode, scene, nullptr, globalBoneMap);

        /*std::cout << "Meshes: " << scene->mNumMeshes << std::endl;
        std::cout << "Materials: " << scene->mNumMaterials << std::endl;
        std::cout << "Textures: " << scene->mNumTextures << std::endl;
        std::cout << "Skeletons: " << scene->mNumSkeletons << std::endl;
        std::cout << "Animations:" << scene->mNumAnimations << std::endl;
        std::cout << "Cameras: " << scene->mNumCameras << std::endl;
        std::cout << "Lights: " << scene->mNumLights << std::endl;

        if (scene->mNumMeshes > 0)
        {
            for (int i = 0; i < scene->mNumMeshes; ++i)
            {
                std::cout << "Mesh: " << scene->mMeshes[i]->mName.C_Str() << std::endl;
                std::cout << "-- Anim Meshes: " << scene->mMeshes[i]->mNumAnimMeshes << std::endl;
                std::cout << "-- Bones: " << scene->mMeshes[i]->mNumBones << std::endl;
                std::cout << "-- Faces: " << scene->mMeshes[i]->mNumFaces << std::endl;
                std::cout << "-- UV Components: " << scene->mMeshes[i]->mNumUVComponents << std::endl;
                std::cout << "-- Vertices: " << scene->mMeshes[i]->mNumVertices << std::endl;
            }
        }

        if (scene->mNumMaterials > 0)
        {
            for (int i = 0; i < scene->mNumMaterials; ++i)
            {
                std::cout << "Material: " << scene->mMaterials[i]->GetName().C_Str() << std::endl;

                if (scene->mMaterials[i]->mNumProperties > 0)
                {
                    for (int j = 0; j < scene->mMaterials[i]->mNumProperties; ++j)
                    {
                        kString typeName = "";
                        aiPropertyTypeInfo type = scene->mMaterials[i]->mProperties[j]->mType;

                        if (type == aiPropertyTypeInfo::aiPTI_Buffer)
                            typeName = "buffer";
                        else if (type == aiPropertyTypeInfo::aiPTI_Double)
                            typeName = "double";
                        else if (type == aiPropertyTypeInfo::aiPTI_Float)
                            typeName = "float";
                        else if (type == aiPropertyTypeInfo::aiPTI_Integer)
                            typeName = "integer";
                        else if (type == aiPropertyTypeInfo::aiPTI_String)
                            typeName = "kString";
                        else if (type == aiPropertyTypeInfo::_aiPTI_Force32Bit)
                            typeName = "force 32 bit";

                        std::cout << "-- Property: " << scene->mMaterials[i]->mProperties[j]->mKey.C_Str() << " : " << scene->mMaterials[i]->mProperties[j]->mData << " (" << typeName << ")" << std::endl;
                    }
                }
            }
        }*/

        /*if (scene->mNumTextures > 0)
        {
            for (int i = 0; i < scene->mNumTextures; ++i)
            {
                std::cout << "Texture: " << getBaseFilename(scene->mTextures[i]->mFilename.C_Str()) << std::endl;

                kTexture2D* texture = loadTextureFromMemory(scene->mTextures[i], getBaseFilename(scene->mTextures[i]->mFilename.C_Str()));

                //saveTexture(texture, getBaseFilename(scene->mTextures[i]->mFilename.C_Str()), getFileExtension(getBaseFilename(scene->mTextures[i]->mFilename.C_Str())));
            }
        }*/

        // return model;
        return rootMesh;
    }
	
	kMesh* kAssetManager::loadMeshResourceAssimp(const kString resourceName, const kString extention)
	{
		kMesh *rootMesh = nullptr;

		unsigned int assimpReadFlag = aiProcess_Triangulate |
	                                     aiProcess_FlipUVs |
	                                     aiProcess_GenSmoothNormals |
	                                     aiProcess_CalcTangentSpace |
	                                     aiProcess_JoinIdenticalVertices |
	                                     aiProcess_LimitBoneWeights |
	                                     aiProcess_ImproveCacheLocality |
	                                     aiProcess_RemoveRedundantMaterials |
	                                     aiProcess_FixInfacingNormals |
	                                     aiProcess_TransformUVCoords |
	                                     aiProcess_SortByPType;

		void *pResData = nullptr;
		size_t dataSize = 0;
		std::vector<char> fileBuf;

#ifdef _WIN32
		// Windows: load from embedded resource
		HRSRC hRes = FindResource(NULL, resourceName.c_str(), RT_RCDATA);
		if (!hRes) { std::cerr << "Failed to find resource " << resourceName << std::endl; return nullptr; }
		HGLOBAL hResData = LoadResource(NULL, hRes);
		dataSize = SizeofResource(NULL, hRes);
		pResData = LockResource(hResData);
#else
		// macOS / Linux: load from Resources/ directory
		if (!readResourceFile(resourceName, fileBuf)) return nullptr;
		pResData = fileBuf.data();
		dataSize = fileBuf.size();
#endif
		if (!pResData || dataSize == 0)
		{
			std::cerr << "Failed to load resource " << resourceName << std::endl;
			return nullptr;
		}
		
		std::cout << "Loaded resource: " << resourceName << " (size=" << dataSize << " bytes)" << std::endl;

		Assimp::Importer import;
		import.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);

		const aiScene* scene = import.ReadFileFromMemory(pResData, dataSize, assimpReadFlag, extention.c_str());
		if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
		{
			std::cout << "ERROR::ASSIMP::" << import.GetErrorString() << std::endl;
			return nullptr;
		}
		
		if (scene && scene->HasMeshes())
		{
			std::cout << "Loaded mesh count: " << scene->mNumMeshes << std::endl;
			for (unsigned int i = 0; i < scene->mNumMeshes; i++)
			{
				std::cout << "Mesh " << i << ": vertices=" << scene->mMeshes[i]->mNumVertices
						  << " faces=" << scene->mMeshes[i]->mNumFaces << std::endl;
			}
		}
		else
		{
			std::cout << "Scene has no meshes!" << std::endl;
		}

		std::map<kString, kBoneInfo> globalBoneMap;
		rootMesh = processNode(scene->mRootNode, scene, nullptr, globalBoneMap);
		if (rootMesh)
			rootMesh->setLoaded(true);
		return rootMesh;
	}

		  kMesh *kAssetManager::processNode(aiNode *node, const aiScene *scene, kMesh *parent,
		                                    std::map<kString, kBoneInfo> &globalBoneMap)
		  {
        kMesh *newMesh;

        //std::cout << "Mesh count:" << node->mNumMeshes << std::endl;

        if (node->mNumMeshes > 0)
        {
            // process all the node's meshes (if any)
            for (size_t i = 0; i < node->mNumMeshes; i++)
            {
                aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];

                newMesh = processMesh(mesh, scene, globalBoneMap);
                if (newMesh != nullptr && parent != nullptr)
                {
                    newMesh->setParent(parent);
                }
            }
        }
        else
        {
            newMesh = new kMesh(parent);
        }

        if (node->mNumChildren > 0)
        {
            // then do the same for each of its children
            for (unsigned int i = 0; i < node->mNumChildren; i++)
            {
                if (node->mChildren[i] != nullptr && scene != nullptr && newMesh != nullptr)
                {
                    processNode(node->mChildren[i], scene, newMesh, globalBoneMap);
                }
            }
        }
		
		//std::cout << newMesh->getVertices().size() << std::endl;

        return newMesh;
    }

    kMesh *kAssetManager::processMesh(aiMesh *mesh, const aiScene *scene,
                                      std::map<kString, kBoneInfo> &globalBoneMap)
    {
        kMesh *newMesh = new kMesh();
        newMesh->setName(kString(mesh->mName.C_Str()));

        if (mesh->mNumVertices > 0)
        {
            // Reserve space for bone data (bone IDs and weights)
            newMesh->reserveBoneData(mesh->mNumVertices);

            // Reserve space for vertices, normals, UVs, bone IDs, and weights
            newMesh->reserveSpace(mesh->mNumVertices);

            for (int i = 0; i < (int)mesh->mNumVertices; ++i)
            {
                // Vertex
                kVec3 position = kAssimpInternal::toVec3(mesh->mVertices[i]);
                newMesh->addVertex(position);

                // Normal
                kVec3 normal = kAssimpInternal::toVec3(mesh->mNormals[i]);
                newMesh->addNormal(normal);

                // UV
                if (mesh->HasTextureCoords(0))
                {
                    kVec2 texCoord = kAssimpInternal::toVec2(mesh->mTextureCoords[0][i]);
                    newMesh->addUV(texCoord);
                }
                else
                {
                    newMesh->addUV(kVec2(0.0f, 0.0f));
                }

                // Tangent and Bitangent
                kVec3 tangent = kVec3(0.0f);
                kVec3 bitangent = kVec3(0.0f);
                if (mesh->HasTangentsAndBitangents())
                {
                    tangent = kVec3(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z);
                    bitangent = kVec3(mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z);
                }
                newMesh->addTangent(tangent);
                newMesh->addBitangent(bitangent);

                // Initialize bone IDs and weights
                newMesh->addBoneID(kIvec4(-1, -1, -1, -1));
                newMesh->addWeight(kVec4(0.0f, 0.0f, 0.0f, 0.0f));
            }

            // Debug: Print bone data after initialization
            // std::cout << "Bone data after initialization:" << std::endl;
            /*
            for (int i = 0; i < newMesh->getVertexCount(); ++i)
            {
                auto boneID = newMesh->getBoneID(i);
                auto weight = newMesh->getWeight(i);
                //std::cout << "Vertex " << i << ": BoneID = " << glm::to_string(boneID) << ", Weight = " << glm::to_string(weight) << std::endl;
            }
            */

            // Extract bone weights for vertices
            extractBoneWeightForVertices(newMesh, mesh, scene, globalBoneMap);

            // Debug: Print bone data after extraction
            // std::cout << "Bone data after extraction:" << std::endl;
            /*
            for (int i = 0; i < newMesh->getVertexCount(); ++i)
            {
                auto boneID = newMesh->getBoneID(i);
                auto weight = newMesh->getWeight(i);
                //std::cout << "Vertex " << i << ": BoneID = " << glm::to_string(boneID) << ", Weight = " << glm::to_string(weight) << std::endl;
            }
            */
        }

        if (mesh->mNumFaces > 0)
        {
            for (int i = 0; i < (int)mesh->mNumFaces; ++i)
            {
                // Indices
                if (mesh->mFaces[i].mNumIndices > 0)
                {
                    for (int j = 0; j < (int)mesh->mFaces[i].mNumIndices; ++j)
                    {
                        newMesh->addIndex((uint32_t)mesh->mFaces[i].mIndices[j]);
                    }
                }
            }
        }

        newMesh->generateVbo();
        newMesh->setLoaded(true);

        return newMesh;
    }
#endif // KEMENA_NO_ASSIMP

    void kAssetManager::calculateNormal(float N[3], float v0[3], float v1[3], float v2[3])
    {
        float v10[3];
        v10[0] = v1[0] - v0[0];
        v10[1] = v1[1] - v0[1];
        v10[2] = v1[2] - v0[2];

        float v20[3];
        v20[0] = v2[0] - v0[0];
        v20[1] = v2[1] - v0[1];
        v20[2] = v2[2] - v0[2];

        N[0] = v10[1] * v20[2] - v10[2] * v20[1];
        N[1] = v10[2] * v20[0] - v10[0] * v20[2];
        N[2] = v10[0] * v20[1] - v10[1] * v20[0];

        float len2 = N[0] * N[0] + N[1] * N[1] + N[2] * N[2];
        if (len2 > 0.0f)
        {
            float len = sqrtf(len2);

            N[0] /= len;
            N[1] /= len;
            N[2] /= len;
        }
    }

    void kAssetManager::normalizeVector(kVec3 &v)
    {
        float len2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
        if (len2 > 0.0f)
        {
            float len = sqrtf(len2);

            v[0] /= len;
            v[1] /= len;
            v[2] /= len;
        }
    }

#ifndef KEMENA_NO_ASSIMP
    void kAssetManager::extractBoneWeightForVertices(kMesh *mesh, aiMesh *meshData, const aiScene *scene,
                                                     std::map<kString, kBoneInfo> &globalBoneMap)
    {
        if (meshData->mNumBones > 0)
        {
            std::map<kString, kBoneInfo> &boneInfoMap = mesh->getBoneInfoMap();

            for (unsigned int boneIndex = 0; boneIndex < meshData->mNumBones; ++boneIndex)
            {
                kString boneName = meshData->mBones[boneIndex]->mName.C_Str();
                kMat4 offset = kAssimpInternal::toMat4(meshData->mBones[boneIndex]->mOffsetMatrix);

                // Assign one globally unique palette index per bone name across
                // every submesh.  Without this, each submesh numbers its bones
                // from 0 again and the shared finalBoneMatrices palette gets
                // overwritten with the wrong submesh's transforms.
                int boneID;
                auto git = globalBoneMap.find(boneName);
                if (git == globalBoneMap.end())
                {
                    kBoneInfo info;
                    info.id     = (int)globalBoneMap.size();
                    info.offset = offset;
                    globalBoneMap[boneName] = info;
                    boneID = info.id;
                }
                else
                {
                    boneID = git->second.id;
                }

                // Store the global id alongside this mesh's own offset matrix.
                kBoneInfo local;
                local.id     = boneID;
                local.offset = offset;
                boneInfoMap[boneName] = local;

                // Assign bone weights to vertices
                auto weights = meshData->mBones[boneIndex]->mWeights;
                int numWeights = meshData->mBones[boneIndex]->mNumWeights;

                for (int weightIndex = 0; weightIndex < numWeights; ++weightIndex)
                {
                    size_t vertexID = weights[weightIndex].mVertexId;
                    float weight = weights[weightIndex].mWeight;

                    // Assign bone ID and weight to the vertex
                    mesh->setVertexBoneData(vertexID, boneID, weight);
                }
            }

            mesh->setBoneCount((int)boneInfoMap.size());
        }
    }
#endif // KEMENA_NO_ASSIMP

    kShader *kAssetManager::loadShaderFromFile(kString vertexShaderPath, kString fragmentShaderPath)
    {
        kShader *shader = new kShader();
        shader->loadShadersFile(vertexShaderPath, fragmentShaderPath);

        shaders.push_back(shader);

        return shader;
    }

    kShader *kAssetManager::loadShaderFromCode(kString vertexShaderCode, kString fragmentShaderCode)
    {
        kShader *shader = new kShader();
        shader->loadShadersCode(vertexShaderCode.c_str(), fragmentShaderCode.c_str());

        shaders.push_back(shader);

        return shader;
    }
	
	kShader *kAssetManager::loadShaderFromResource(kString vertexShaderName, kString fragmentShaderName)
	{
		kString vertexCode, fragmentCode;

#ifdef _WIN32
		// --- Vertex shader ---
		{ HRSRC vRes = FindResource(NULL, vertexShaderName.c_str(), RT_RCDATA);
		if (!vRes) return nullptr;
		HGLOBAL vhData = LoadResource(NULL, vRes);
		if (!vhData) return nullptr;
		DWORD vsize = SizeofResource(NULL, vRes);
		void* vpData = LockResource(vhData);
		vertexCode = kString(reinterpret_cast<const char*>(vpData), vsize); }

		// --- Fragment shader ---
		{ HRSRC fRes = FindResource(NULL, fragmentShaderName.c_str(), RT_RCDATA);
		if (!fRes) return nullptr;
		HGLOBAL fhData = LoadResource(NULL, fRes);
		if (!fhData) return nullptr;
		DWORD fsize = SizeofResource(NULL, fRes);
		void* fpData = LockResource(fhData);
		fragmentCode = kString(reinterpret_cast<const char*>(fpData), fsize); }
#else
		std::vector<char> buf;
		if (readResourceFile(vertexShaderName, buf))
			vertexCode = kString(buf.data(), buf.size());
		if (readResourceFile(fragmentShaderName, buf))
			fragmentCode = kString(buf.data(), buf.size());
		if (vertexCode.empty() || fragmentCode.empty()) return nullptr;
#endif

		kShader *shader = new kShader();
		shader->loadShadersCode(vertexCode.c_str(), fragmentCode.c_str());
		shaders.push_back(shader);
		return shader;
	}

	   /// @brief Reads a text resource (RCDATA on Windows, Resources/<name> elsewhere).
	   static bool readTextResource(const kString &resourceName, kString &out)
	   {
#ifdef _WIN32
	       HRSRC hRes = FindResource(NULL, resourceName.c_str(), RT_RCDATA);
	       if (!hRes) return false;
	       HGLOBAL hData = LoadResource(NULL, hRes);
	       if (!hData) return false;
	       DWORD  size = SizeofResource(NULL, hRes);
	       void  *data = LockResource(hData);
	       if (!data || size == 0) return false;
	       out = kString(reinterpret_cast<const char *>(data), static_cast<size_t>(size));
	       return true;
#else
	       std::vector<char> buf;
	       if (!readResourceFile(resourceName, buf)) return false;
	       out = kString(buf.data(), buf.size());
	       return true;
#endif
	   }

	   kShader *kAssetManager::loadGlslFromResource(kString resourceName)
	   {
#ifdef KEMENA_D3D11
	       // Shader source is never translated by the engine: each backend loads its
	       // own variant of the shader.  When the active renderer is DirectX 11 the
	       // <NAME>_HLSL resource is used, and the GLSL entry is only a fallback for
	       // shaders that have no HLSL counterpart yet.  The whole branch is compiled
	       // out of non-D3D11 builds, so OpenGL behaviour is unchanged.
	       kDriver *activeDriver = kDriver::getCurrent();
	       if (activeDriver != nullptr &&
	           activeDriver->getRendererType() == kRendererType::RENDERER_D3D11)
	       {
	           kString hlslSrc;
	           const kString hlslName = resourceName + "_HLSL";
	           if (readTextResource(hlslName, hlslSrc) && !hlslSrc.empty())
	           {
	               kShader *shader = new kShader();
	               shader->loadHlslCodeDX11(hlslSrc);
	               shaders.push_back(shader);
	               return shader;
	           }

	           std::cout << "[kAssetManager] No HLSL variant for resource '" << resourceName
	                     << "' (expected '" << hlslName
	                     << "'); falling back to the GLSL source, which the D3D11 "
	                        "backend cannot compile." << std::endl;
	       }
#endif

	       kString src;
	       if (!readTextResource(resourceName, src))
	           return nullptr;

	       kShader *shader = new kShader();
	       shader->loadGlslCode(src);
	       shaders.push_back(shader);
	       return shader;
	   }

    kMaterial *kAssetManager::createMaterial(kShader *shader)
    {
        kMaterial *material = new kMaterial();
        material->setShader(shader);

        materials.push_back(material);

        return material;
    }

    kSkeletalAnimation *kAssetManager::loadAnimation(const kString fileName, kMesh *mesh)
    {
#ifndef KEMENA_NO_ASSIMP
        return new kSkeletalAnimation(fileName, mesh);
#else
        // tinygltf-based skeletal animation loading is a follow-up — for now
        // the slim runtime build just refuses the call.
        (void)fileName; (void)mesh;
        std::cout << "loadAnimation: not yet supported in slim runtime build "
                     "(tinygltf animation importer TODO)." << std::endl;
        return nullptr;
#endif
    }
}
