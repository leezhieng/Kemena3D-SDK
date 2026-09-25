#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

// Custom decoding backends — libvorbis and libopus replace the built-in
// stb_vorbis backend, giving us full Vorbis + Opus support via external
// libraries linked from Kemena3D-SDK/Dependencies/{ogg,vorbis,opus,opusfile}.
#include "miniaudio_libvorbis.h"
#include "miniaudio_libopus.h"

#include "kaudiomanager.h"

#include <iostream>
#include <vector>
#include <algorithm>

namespace kemena
{
    struct kAudioManager::Impl
    {
        ma_engine              engine          = {};
        ma_resource_manager    resourceManager = {};
        bool                   initialized     = false;
        std::vector<kAudio *>  sounds;
    };

    kAudioManager::kAudioManager()
        : m_impl(new Impl())
    {
    }

    kAudioManager::~kAudioManager()
    {
        shutdown();
        delete m_impl;
    }

    // --- Lifecycle -----------------------------------------------------------

    bool kAudioManager::init()
    {
        if (m_impl->initialized)
            return true;

        // --- Resource manager with custom decoding backends -----------------
        // libvorbis + libopus replace miniaudio's built-in stb_vorbis backend,
        // giving us full Vorbis and Opus support via the external libraries
        // linked from Kemena3D-SDK/Dependencies/.
        {
            ma_decoding_backend_vtable* customBackends[] = {
                ma_decoding_backend_libvorbis,
                ma_decoding_backend_libopus,
            };

            ma_resource_manager_config rmConfig = ma_resource_manager_config_init();
            rmConfig.ppCustomDecodingBackendVTables = customBackends;
            rmConfig.customDecodingBackendCount     = sizeof(customBackends) / sizeof(customBackends[0]);
            rmConfig.decodedFormat                  = ma_format_f32;
            rmConfig.decodedChannels                = 0;
            rmConfig.decodedSampleRate              = 0;

            ma_result result = ma_resource_manager_init(&rmConfig, &m_impl->resourceManager);
            if (result != MA_SUCCESS)
            {
                std::cout << "[kAudioManager] Failed to init resource manager "
                          << "(ma_result = " << result << ")." << std::endl;
                return false;
            }
        }

        // --- Engine ---------------------------------------------------------
        {
            ma_engine_config engineConfig = ma_engine_config_init();
            engineConfig.pResourceManager = &m_impl->resourceManager;

            ma_result result = ma_engine_init(&engineConfig, &m_impl->engine);
            if (result != MA_SUCCESS)
            {
                std::cout << "[kAudioManager] Failed to initialise audio engine "
                          << "(ma_result = " << result << ")." << std::endl;
                ma_resource_manager_uninit(&m_impl->resourceManager);
                return false;
            }
        }

        // Log the actual backend that miniaudio selected.  If the backend is
        // "null" the audio device could not be opened and all playback will
        // be silent — that's the most common cause of "no audio" on Windows.
        ma_device *device = ma_engine_get_device(&m_impl->engine);
        if (device != nullptr && device->pContext != nullptr)
        {
            std::cout << "[kAudioManager] Audio engine initialised."
                      << " backend=" << ma_get_backend_name(device->pContext->backend)
                      << "." << std::endl;
        }
        else
        {
            std::cout << "[kAudioManager] Audio engine initialised (no device info)."
                      << std::endl;
        }

        m_impl->initialized = true;
        return true;
    }

    void kAudioManager::shutdown()
    {
        if (!m_impl->initialized)
            return;

        // Unload every tracked kAudio
        for (kAudio *audio : m_impl->sounds)
        {
            audio->uninit();
            delete audio;
        }
        m_impl->sounds.clear();

        ma_engine_uninit(&m_impl->engine);
        ma_resource_manager_uninit(&m_impl->resourceManager);
        m_impl->initialized = false;
    }

    // --- Global controls -----------------------------------------------------

    void kAudioManager::stopAll()
    {
        for (kAudio *audio : m_impl->sounds)
            audio->stop();
    }

    void kAudioManager::setMasterVolume(float volume)
    {
        if (m_impl->initialized)
            ma_engine_set_volume(&m_impl->engine, volume);
    }

    float kAudioManager::getMasterVolume() const
    {
        if (!m_impl->initialized)
            return 1.0f;
        return ma_engine_get_volume(&m_impl->engine);
    }

    // --- 3D listener ---------------------------------------------------------

    void kAudioManager::setListenerPosition(const kVec3 &position)
    {
        if (m_impl->initialized)
            ma_engine_listener_set_position(&m_impl->engine, 0,
                                            position.x, position.y, position.z);
    }

    void kAudioManager::setListenerDirection(const kVec3 &forward, const kVec3 &up)
    {
        if (m_impl->initialized)
        {
            ma_engine_listener_set_direction(&m_impl->engine, 0,
                                             forward.x, forward.y, forward.z);
            ma_engine_listener_set_world_up(&m_impl->engine, 0,
                                            up.x, up.y, up.z);
        }
    }

    void kAudioManager::setListenerVelocity(const kVec3 &velocity)
    {
        if (m_impl->initialized)
            ma_engine_listener_set_velocity(&m_impl->engine, 0,
                                            velocity.x, velocity.y, velocity.z);
    }

    // --- Audio clip factory --------------------------------------------------

    kAudio *kAudioManager::loadAudio(const kString &filePath)
    {
        if (!m_impl->initialized)
        {
            std::cout << "[kAudioManager] loadAudio called before init()." << std::endl;
            return nullptr;
        }

        kAudio *audio = new kAudio();
        if (!audio->initFromFile(&m_impl->engine, filePath))
        {
            delete audio;
            return nullptr;
        }

        m_impl->sounds.push_back(audio);
        return audio;
    }

    void kAudioManager::unloadAudio(kAudio *audio)
    {
        if (!audio)
            return;

        auto it = std::find(m_impl->sounds.begin(), m_impl->sounds.end(), audio);
        if (it != m_impl->sounds.end())
            m_impl->sounds.erase(it);

        audio->uninit();
        delete audio;
    }

} // namespace kemena
