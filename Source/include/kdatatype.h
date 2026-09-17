/**
 * @file kdatatype.h
 * @brief Core type aliases, enumerations, structs, and utility functions used throughout the engine.
 */

#ifndef KDATATYPE_H
#define KDATATYPE_H

#include "kmd5.h"

#include <string>
#include <vector>
#include <iostream>
#include <random>
#include <sstream>
#include <fstream>
#include <iomanip>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

// Tell SDL not to mess with main()
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

namespace kemena
{
    /// @name Math type aliases
    /// @{
    typedef glm::vec2 kVec2;   ///< 2-component float vector.
    typedef glm::vec3 kVec3;   ///< 3-component float vector.
    typedef glm::vec4 kVec4;   ///< 4-component float vector.
    typedef glm::ivec2 kIvec2; ///< 2-component integer vector.
    typedef glm::ivec3 kIvec3; ///< 3-component integer vector.
    typedef glm::ivec4 kIvec4; ///< 4-component integer vector.
    typedef glm::mat3 kMat3;   ///< 3x3 float matrix.
    typedef glm::mat4 kMat4;   ///< 4x4 float matrix.
    typedef glm::quat kQuat;   ///< Unit quaternion.

    typedef std::string kString; ///< Standard string alias.
/// @}

/// Maximum number of bones a single vertex can be influenced by.
#define MAX_BONE_INFLUENCE 4
/// Maximum number of bones in a skeleton.
#define MAX_BONES 128

    /**
     * @brief Identifies the graphics backend used by kRenderer.
     */
    enum kRendererType
    {
        RENDERER_GL,   ///< OpenGL 3.3+ renderer (desktop).
        RENDERER_GLES, ///< OpenGL ES 3.0 renderer (Android / mobile).
        RENDERER_D3D11 ///< DirectX 11 renderer (Windows desktop).
    };

    /**
     * @brief Selects the debug visualization mode for the scene viewport.
     */
    enum class kRenderMode
    {
        RENDER_MODE_FULL = 0,           ///< Normal fully-lit render.
        RENDER_MODE_ALBEDO = 1,         ///< Albedo / diffuse texture only, no lighting.
        RENDER_MODE_NORMALS = 2,        ///< World normals visualized as RGB.
        RENDER_MODE_WIREFRAME = 3,      ///< Flat-color wireframe only.
        RENDER_MODE_DEPTH = 4,          ///< Linearized depth as greyscale.
        RENDER_MODE_OBJECT_IDS = 5,     ///< Color-coded object ID picking buffer.
        RENDER_MODE_FULL_WIREFRAME = 6, ///< Full lit render with wireframe overlay.
    };

    /**
     * @brief Window creation mode.
     */
    enum kWindowType
    {
        WINDOW_DEFAULT,    ///< Normal windowed mode.
        WINDOW_FULLSCREEN, ///< Exclusive fullscreen.
        WINDOW_BORDERLESS, ///< Borderless/windowed fullscreen.
    };

    /**
     * @brief Scene-graph node type tag.
     */
    enum kNodeType
    {
        NODE_TYPE_OBJECT, ///< Generic empty object.
        NODE_TYPE_MESH,   ///< Renderable mesh.
        NODE_TYPE_CAMERA, ///< Camera.
        NODE_TYPE_LIGHT,  ///< Light source.
        NODE_TYPE_AUDIO,  ///< Audio emitter object.
        NODE_TYPE_TERRAIN,///< Terrain tile object.
        NODE_TYPE_DECAL   ///< Flat sticker-quad decal.
    };

    /**
     * @brief Camera behaviour mode.
     */
    enum kCameraType
    {
        CAMERA_TYPE_FREE,  ///< Free-fly; position and orientation are set independently.
        CAMERA_TYPE_LOCKED ///< Orbit; always looks at a fixed target point.
    };

    /**
     * @brief Transparency/blending mode for a material.
     */
    enum kTransparentType
    {
        TRANSP_TYPE_NONE, ///< Opaque — no blending.
        TRANSP_TYPE_BLEND ///< Alpha blending (src-alpha / one-minus-src-alpha).
    };

    /**
     * @brief Light source variety.
     */
    enum kLightType
    {
        LIGHT_TYPE_SUN,   ///< Directional (infinite-distance) light.
        LIGHT_TYPE_POINT, ///< Omnidirectional point light.
        LIGHT_TYPE_SPOT   ///< Cone-shaped spotlight.
    };

    /**
     * @brief Texture upload format (internal precision / colour-space).
     *
     * The first four values are uncompressed formats available on every
     * backend.  The ETC2 / EAC and ASTC values are block-compressed formats
     * intended for the OpenGL ES (mobile / embedded) backend, where the
     * desktop-only S3TC / DXT family is normally unavailable.
     *
     * ETC2/EAC is mandatory in OpenGL ES 3.0, so a GLES device that reports
     * ES 3.0 always supports TEX_FORMAT_ETC2_*.  ASTC is an extension
     * (GL_KHR_texture_compression_astc_ldr) and must be queried through
     * kDriver::supportsCompressedFormat().
     */
    enum kTextureFormat
    {
        TEX_FORMAT_RGB,   ///< 8-bit RGB, linear colour-space.
        TEX_FORMAT_RGBA,  ///< 8-bit RGBA, linear colour-space.
        TEX_FORMAT_SRGB,  ///< 8-bit RGB, sRGB colour-space (gamma-corrected).
        TEX_FORMAT_SRGBA, ///< 8-bit RGBA, sRGB colour-space.

        // --- Mobile / embedded block-compressed formats ---------------------
        TEX_FORMAT_ETC2_RGB,    ///< ETC2 RGB8 (4x4 blocks, 8 bytes each) — core in ES 3.0.
        TEX_FORMAT_ETC2_SRGB,   ///< ETC2 RGB8 sRGB — core in ES 3.0.
        TEX_FORMAT_ETC2_RGBA,   ///< ETC2 RGBA8 (EAC alpha) — core in ES 3.0.
        TEX_FORMAT_ETC2_SRGBA,  ///< ETC2 RGBA8 sRGB — core in ES 3.0.
        TEX_FORMAT_ETC2_RGB_A1, ///< ETC2 RGB8 with 1-bit punch-through alpha.
        TEX_FORMAT_ASTC_4x4,    ///< ASTC LDR 4x4 (16 bytes per block).
        TEX_FORMAT_ASTC_6x6,    ///< ASTC LDR 6x6 (16 bytes per block).
        TEX_FORMAT_ASTC_8x8,    ///< ASTC LDR 8x8 (16 bytes per block).
        TEX_FORMAT_ASTC_12x12,  ///< ASTC LDR 12x12 (16 bytes per block).
    };

    /**
     * @brief Distinguishes 2D from cube-map textures.
     */
    enum kTextureType
    {
        TEX_TYPE_2D,  ///< Standard 2-dimensional texture.
        TEX_TYPE_CUBE ///< Cube-map texture (six faces).
    };

    /**
     * @brief Texture wrap mode for UV coordinates.
     */
    enum class kTextureWrap
    {
        REPEAT,          ///< Repeat the texture (tiling).
        CLAMP_TO_EDGE,   ///< Clamp UV to edge texel colour.
        CLAMP_TO_BORDER, ///< Clamp UV to a solid border colour.
        MIRRORED_REPEAT  ///< Repeat with mirroring each tile.
    };

    /**
     * @brief Texture minification / magnification filter.
     */
    enum class kTextureFilter
    {
        NEAREST,                ///< Nearest-neighbour sampling.
        LINEAR,                 ///< Bilinear interpolation.
        NEAREST_MIPMAP_NEAREST, ///< Nearest mip level, nearest sampling.
        LINEAR_MIPMAP_NEAREST,  ///< Nearest mip level, bilinear sampling.
        NEAREST_MIPMAP_LINEAR,  ///< Linear mip blend, nearest sampling.
        LINEAR_MIPMAP_LINEAR,   ///< Trilinear filtering (linear mip blend, bilinear).
    };

/// @name Event type constants (mirror SDL3 values)
/// @{
#define K_EVENT_QUIT 256
#define K_EVENT_KEYDOWN 768
#define K_EVENT_KEYUP 769
#define K_EVENT_MOUSEMOTION 1024
#define K_EVENT_MOUSEBUTTONDOWN 1025
#define K_EVENT_MOUSEBUTTONUP 1026
#define K_EVENT_MOUSEWHEEL 1027
/// @}

/// @name Mouse button identifiers
/// @{
#define K_MOUSEBUTTON_LEFT 1
#define K_MOUSEBUTTON_MIDDLE 2
#define K_MOUSEBUTTON_RIGHT 3
/// @}

/// @name Gamepad button identifiers (mirror SDL3 SDL_GamepadButton values)
/// @{
#define K_GAMEPAD_BUTTON_SOUTH 0             // Bottom face button (A / Cross)
#define K_GAMEPAD_BUTTON_EAST 1              // Right face button (B / Circle)
#define K_GAMEPAD_BUTTON_WEST 2              // Left face button (X / Square)
#define K_GAMEPAD_BUTTON_NORTH 3             // Top face button (Y / Triangle)
#define K_GAMEPAD_BUTTON_BACK 4
#define K_GAMEPAD_BUTTON_GUIDE 5
#define K_GAMEPAD_BUTTON_START 6
#define K_GAMEPAD_BUTTON_LEFT_STICK 7
#define K_GAMEPAD_BUTTON_RIGHT_STICK 8
#define K_GAMEPAD_BUTTON_LEFT_SHOULDER 9
#define K_GAMEPAD_BUTTON_RIGHT_SHOULDER 10
#define K_GAMEPAD_BUTTON_DPAD_UP 11
#define K_GAMEPAD_BUTTON_DPAD_DOWN 12
#define K_GAMEPAD_BUTTON_DPAD_LEFT 13
#define K_GAMEPAD_BUTTON_DPAD_RIGHT 14
/// @}

/// @name Gamepad axis identifiers (mirror SDL3 SDL_GamepadAxis values)
/// @{
#define K_GAMEPAD_AXIS_LEFTX 0
#define K_GAMEPAD_AXIS_LEFTY 1
#define K_GAMEPAD_AXIS_RIGHTX 2
#define K_GAMEPAD_AXIS_RIGHTY 3
#define K_GAMEPAD_AXIS_LEFT_TRIGGER 4
#define K_GAMEPAD_AXIS_RIGHT_TRIGGER 5
/// @}

// Key Code (OS key location)
#define K_KEY_UNKNOWN 0
#define K_KEY_BACKSPACE 8
#define K_KEY_TAB 9
#define K_KEY_RETURN 13
#define K_KEY_ESCAPE 27
#define K_KEY_SPACE 32
#define K_KEY_EXCLAIM 33    // !
#define K_KEY_QUOTEDBL 34   // "
#define K_KEY_HASH 35       // #
#define K_KEY_DOLLAR 36     // $
#define K_KEY_PERCENT 37    // %
#define K_KEY_AMPERSAND 38  // &
#define K_KEY_QUOTE 39      // '
#define K_KEY_LEFTPAREN 40  // (
#define K_KEY_RIGHTPAREN 41 // )
#define K_KEY_ASTERISK 42   // *
#define K_KEY_PLUS 43       // +
#define K_KEY_COMMA 44      // ,
#define K_KEY_MINUS 45      // -
#define K_KEY_PERIOD 46     // .
#define K_KEY_SLASH 47      // /
#define K_KEY_0 48
#define K_KEY_1 49
#define K_KEY_2 50
#define K_KEY_3 51
#define K_KEY_4 52
#define K_KEY_5 53
#define K_KEY_6 54
#define K_KEY_7 55
#define K_KEY_8 56
#define K_KEY_9 57
#define K_KEY_A 97
#define K_KEY_B 98
#define K_KEY_C 99
#define K_KEY_D 100
#define K_KEY_E 101
#define K_KEY_F 102
#define K_KEY_G 103
#define K_KEY_H 104
#define K_KEY_I 105
#define K_KEY_J 106
#define K_KEY_K 107
#define K_KEY_L 108
#define K_KEY_M 109
#define K_KEY_N 110
#define K_KEY_O 111
#define K_KEY_P 112
#define K_KEY_Q 113
#define K_KEY_R 114
#define K_KEY_S 115
#define K_KEY_T 116
#define K_KEY_U 117
#define K_KEY_V 118
#define K_KEY_W 119
#define K_KEY_X 120
#define K_KEY_Y 121
#define K_KEY_Z 122

// Function keys
#define K_KEY_F1 1073741882
#define K_KEY_F2 1073741883
#define K_KEY_F3 1073741884
#define K_KEY_F4 1073741885
#define K_KEY_F5 1073741886
#define K_KEY_F6 1073741887
#define K_KEY_F7 1073741888
#define K_KEY_F8 1073741889
#define K_KEY_F9 1073741890
#define K_KEY_F10 1073741891
#define K_KEY_F11 1073741892
#define K_KEY_F12 1073741893

// Modifier keys
#define K_KEY_CAPSLOCK 1073741881
#define K_KEY_LSHIFT 1073742049
#define K_KEY_RSHIFT 1073742053
#define K_KEY_LCTRL 1073742048
#define K_KEY_RCTRL 1073742052
#define K_KEY_LALT 1073742050
#define K_KEY_RALT 1073742054
#define K_KEY_LGUI 1073742051 // Windows / Command key
#define K_KEY_RGUI 1073742055

// Navigation / Editing
#define K_KEY_INSERT 1073741897
#define K_KEY_DELETE 127
#define K_KEY_HOME 1073741898
#define K_KEY_END 1073741901
#define K_KEY_PAGEUP 1073741899
#define K_KEY_PAGEDOWN 1073741902

// Arrow keys
#define K_KEY_RIGHT 1073741903
#define K_KEY_LEFT 1073741904
#define K_KEY_DOWN 1073741905
#define K_KEY_UP 1073741906

// Keypad (Numpads)
#define K_KEY_NUMLOCKCLEAR 1073741907
#define K_KEY_KP_DIVIDE 1073741908
#define K_KEY_KP_MULTIPLY 1073741909
#define K_KEY_KP_MINUS 1073741910
#define K_KEY_KP_PLUS 1073741911
#define K_KEY_KP_ENTER 1073741912
#define K_KEY_KP_1 1073741913
#define K_KEY_KP_2 1073741914
#define K_KEY_KP_3 1073741915
#define K_KEY_KP_4 1073741916
#define K_KEY_KP_5 1073741917
#define K_KEY_KP_6 1073741918
#define K_KEY_KP_7 1073741919
#define K_KEY_KP_8 1073741920
#define K_KEY_KP_9 1073741921
#define K_KEY_KP_0 1073741922
#define K_KEY_KP_PERIOD 1073741923

// Others
#define K_KEY_PRINTSCREEN 1073741894
#define K_KEY_SCROLLLOCK 1073741895
#define K_KEY_PAUSE 1073741896

// Scan Code (physical key location)
#define K_SCANCODE_UNKNOWN 0
#define K_SCANCODE_A 4
#define K_SCANCODE_B 5
#define K_SCANCODE_C 6
#define K_SCANCODE_D 7
#define K_SCANCODE_E 8
#define K_SCANCODE_F 9
#define K_SCANCODE_G 10
#define K_SCANCODE_H 11
#define K_SCANCODE_I 12
#define K_SCANCODE_J 13
#define K_SCANCODE_K 14
#define K_SCANCODE_L 15
#define K_SCANCODE_M 16
#define K_SCANCODE_N 17
#define K_SCANCODE_O 18
#define K_SCANCODE_P 19
#define K_SCANCODE_Q 20
#define K_SCANCODE_R 21
#define K_SCANCODE_S 22
#define K_SCANCODE_T 23
#define K_SCANCODE_U 24
#define K_SCANCODE_V 25
#define K_SCANCODE_W 26
#define K_SCANCODE_X 27
#define K_SCANCODE_Y 28
#define K_SCANCODE_Z 29
#define K_SCANCODE_1 30
#define K_SCANCODE_2 31
#define K_SCANCODE_3 32
#define K_SCANCODE_4 33
#define K_SCANCODE_5 34
#define K_SCANCODE_6 35
#define K_SCANCODE_7 36
#define K_SCANCODE_8 37
#define K_SCANCODE_9 38
#define K_SCANCODE_0 39
#define K_SCANCODE_RETURN 40
#define K_SCANCODE_ESCAPE 41
#define K_SCANCODE_BACKSPACE 42
#define K_SCANCODE_TAB 43
#define K_SCANCODE_SPACE 44
#define K_SCANCODE_MINUS 45
#define K_SCANCODE_EQUALS 46
#define K_SCANCODE_LEFTBRACKET 47
#define K_SCANCODE_RIGHTBRACKET 48
#define K_SCANCODE_BACKSLASH 49
#define K_SCANCODE_NONUSHASH 50
#define K_SCANCODE_SEMICOLON 51
#define K_SCANCODE_APOSTROPHE 52
#define K_SCANCODE_GRAVE 53
#define K_SCANCODE_COMMA 54
#define K_SCANCODE_PERIOD 55
#define K_SCANCODE_SLASH 56
#define K_SCANCODE_CAPSLOCK 57
#define K_SCANCODE_F1 58
#define K_SCANCODE_F2 59
#define K_SCANCODE_F3 60
#define K_SCANCODE_F4 61
#define K_SCANCODE_F5 62
#define K_SCANCODE_F6 63
#define K_SCANCODE_F7 64
#define K_SCANCODE_F8 65
#define K_SCANCODE_F9 66
#define K_SCANCODE_F10 67
#define K_SCANCODE_F11 68
#define K_SCANCODE_F12 69
#define K_SCANCODE_PRINTSCREEN 70
#define K_SCANCODE_SCROLLLOCK 71
#define K_SCANCODE_PAUSE 72
#define K_SCANCODE_INSERT 73
#define K_SCANCODE_HOME 74
#define K_SCANCODE_PAGEUP 75
#define K_SCANCODE_DELETE 76
#define K_SCANCODE_END 77
#define K_SCANCODE_PAGEDOWN 78
#define K_SCANCODE_RIGHT 79
#define K_SCANCODE_LEFT 80
#define K_SCANCODE_DOWN 81
#define K_SCANCODE_UP 82
#define K_SCANCODE_NUMLOCKCLEAR 83
#define K_SCANCODE_KP_DIVIDE 84
#define K_SCANCODE_KP_MULTIPLY 85
#define K_SCANCODE_KP_MINUS 86
#define K_SCANCODE_KP_PLUS 87
#define K_SCANCODE_KP_ENTER 88
#define K_SCANCODE_KP_1 89
#define K_SCANCODE_KP_2 90
#define K_SCANCODE_KP_3 91
#define K_SCANCODE_KP_4 92
#define K_SCANCODE_KP_5 93
#define K_SCANCODE_KP_6 94
#define K_SCANCODE_KP_7 95
#define K_SCANCODE_KP_8 96
#define K_SCANCODE_KP_9 97
#define K_SCANCODE_KP_0 98
#define K_SCANCODE_KP_PERIOD 99
#define K_SCANCODE_NONUSBACKSLASH 100
#define K_SCANCODE_APPLICATION 101
#define K_SCANCODE_KP_EQUALS 103
#define K_SCANCODE_F13 104
#define K_SCANCODE_F14 105
#define K_SCANCODE_F15 106
#define K_SCANCODE_F16 107
#define K_SCANCODE_F17 108
#define K_SCANCODE_F18 109
#define K_SCANCODE_F19 110
#define K_SCANCODE_F20 111
#define K_SCANCODE_F21 112
#define K_SCANCODE_F22 113
#define K_SCANCODE_F23 114
#define K_SCANCODE_F24 115
#define K_SCANCODE_EXECUTE 116
#define K_SCANCODE_HELP 117
#define K_SCANCODE_MENU 118
#define K_SCANCODE_SELECT 119
#define K_SCANCODE_STOP 120
#define K_SCANCODE_AGAIN 121
#define K_SCANCODE_UNDO 122
#define K_SCANCODE_CUT 123
#define K_SCANCODE_COPY 124
#define K_SCANCODE_PASTE 125
#define K_SCANCODE_FIND 126
#define K_SCANCODE_MUTE 127
#define K_SCANCODE_VOLUMEUP 128
#define K_SCANCODE_VOLUMEDOWN 129
#define K_SCANCODE_KP_COMMA 133
#define K_SCANCODE_KP_EQUALSAS400 134
#define K_SCANCODE_LCTRL 224
#define K_SCANCODE_LSHIFT 225
#define K_SCANCODE_LALT 226
#define K_SCANCODE_LGUI 227
#define K_SCANCODE_RCTRL 228
#define K_SCANCODE_RSHIFT 229
#define K_SCANCODE_RALT 230
#define K_SCANCODE_RGUI 231

    /**
     * @brief Wraps an SDL_Event and provides helper accessors for input polling.
     *
     * Pass one of these to your main loop and call hasEvent() each frame to drain
     * the SDL event queue. Key state (which keys are currently held) is tracked
     * automatically inside the struct.
     */
    struct kSystemEvent
    {
        SDL_Event event;                ///< Underlying SDL event.
        std::vector<unsigned int> keys; ///< Currently-held key codes.

        /** @brief Returns a pointer to the internal SDL_Event. */
        SDL_Event *getSdlEvent()
        {
            return &event;
        }

        /**
         * @brief Polls one event from the SDL queue.
         * @return true if an event was available, false if the queue was empty.
         */
        bool hasEvent()
        {
            if (SDL_PollEvent(&event))
            {
                return true;
            }
            return false;
        }

        /**
         * @brief Returns the event type and maintains the held-key list.
         * @return SDL event type value (matches K_EVENT_* constants).
         */
        unsigned int getType()
        {
            if (event.type == K_EVENT_KEYDOWN)
            {
                unsigned int key = getKeyButton();

                // Put key into array
                bool found = false;
                if (keys.size() > 0)
                {
                    for (size_t i = 0; i < keys.size(); ++i)
                    {
                        if (keys.at(i) == key)
                        {
                            found = true;
                            break;
                        }
                    }
                }
                if (!found)
                {
                    keys.push_back(key);
                }
            }
            else if (event.type == K_EVENT_KEYUP)
            {
                unsigned int key = getKeyButton();

                // Remove key from array
                if (keys.size() > 0)
                {
                    for (size_t i = keys.size(); i-- > 0;)
                    {
                        if (keys[i] == key)
                        {
                            keys.erase(keys.begin() + i);
                            break;
                        }
                    }
                }
            }

            return event.type;
        }

        /** @brief Returns the mouse button index from the last button event. */
        unsigned int getMouseButton()
        {
            return event.button.button;
        }

        /** @brief Returns the X position from the last mouse-button event. */
        float getMouseX()
        {
            return event.button.x;
        }

        /** @brief Returns the Y position from the last mouse-button event. */
        float getMouseY()
        {
            return event.button.y;
        }

        /** @brief Returns the absolute X position from the last motion event. */
        float getMouseMoveX()
        {
            return event.motion.x;
        }

        /** @brief Returns the absolute Y position from the last motion event. */
        float getMouseMoveY()
        {
            return event.motion.y;
        }

        /** @brief Returns the key code from the last key event. */
        unsigned int getKeyButton()
        {
            return event.key.key;
        }

        /**
         * @brief Returns true if the given key is currently held down.
         * @param key Key code to test (use K_KEY_* constants).
         */
        bool getKeyDown(unsigned int key)
        {
            if (keys.empty())
                return false;

            for (size_t i = 0; i < keys.size(); ++i)
            {
                if (keys[i] == key)
                {
                    return true;
                }
            }

            return false;
        }

        /** @brief Returns horizontal scroll delta from the last wheel event. */
        float getMouseWheelX()
        {
            return event.wheel.x;
        }

        /** @brief Returns vertical scroll delta from the last wheel event. */
        float getMouseWheelY()
        {
            return event.wheel.y;
        }
    };

    /**
     * @brief Axis-aligned bounding box (min/max in the same space).
     */
    struct kAABB
    {
        kVec3 min = kVec3(1e30f);  ///< Minimum corner; initialised to +inf so the box starts empty/invalid.
        kVec3 max = kVec3(-1e30f); ///< Maximum corner; initialised to -inf so the box starts empty/invalid.

        /** @brief Constructs an empty (invalid) bounding box. */
        kAABB() = default;

        /**
         * @brief Constructs a bounding box from explicit corners.
         * @param min Minimum corner.
         * @param max Maximum corner.
         */
        kAABB(kVec3 min, kVec3 max) : min(min), max(max) {}

        /** @brief Returns the geometric centre of the box. */
        kVec3 center() const { return (min + max) * 0.5f; }

        /** @brief Returns half the size of the box along each axis. */
        kVec3 halfExtents() const { return (max - min) * 0.5f; }

        /** @brief Returns true if min is component-wise less than or equal to max (i.e. the box is non-degenerate). */
        bool isValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }

        /**
         * @brief Grows the box so that it encloses the given point.
         * @param point Point to include.
         */
        void expandBy(const kVec3 &point)
        {
            min = glm::min(min, point);
            max = glm::max(max, point);
        }

        /**
         * @brief Grows the box so that it encloses another box.
         * @param other Box to merge into this one.
         */
        void merge(const kAABB &other)
        {
            min = glm::min(min, other.min);
            max = glm::max(max, other.max);
        }

        /**
         * @brief Returns a copy of the box uniformly expanded outward by the given amount on every side.
         * @param amount Distance to push each face outward.
         * @return Expanded bounding box.
         */
        kAABB expanded(float amount) const
        {
            return {min - kVec3(amount), max + kVec3(amount)};
        }

        /**
         * @brief Tests whether this box fully contains another box.
         * @param other Box to test for containment.
         * @return true if @p other lies entirely within this box.
         */
        bool contains(const kAABB &other) const
        {
            return other.min.x >= min.x && other.max.x <= max.x && other.min.y >= min.y && other.max.y <= max.y && other.min.z >= min.z && other.max.z <= max.z;
        }

        /**
         * @brief Tests whether this box intersects another box.
         * @param other Box to test against.
         * @return true if the two boxes overlap (touching counts as overlap).
         */
        bool overlaps(const kAABB &other) const
        {
            return min.x <= other.max.x && max.x >= other.min.x && min.y <= other.max.y && max.y >= other.min.y && min.z <= other.max.z && max.z >= other.min.z;
        }
    };

    /**
     * @brief Full per-vertex data layout used internally by the asset manager.
     */
    struct kVertexInfo
    {
        kVec3 position;  ///< Vertex position in model space.
        kVec3 normal;    ///< Vertex normal.
        kVec2 texCoords; ///< UV texture coordinates.
        kVec3 tangent;   ///< Tangent vector (for normal mapping).
        kVec3 bitangent; ///< Bitangent vector (for normal mapping).

        int boneIDs[MAX_BONE_INFLUENCE];   ///< Indices of influencing bones.
        float weights[MAX_BONE_INFLUENCE]; ///< Blend weights for each bone.
    };

    /**
     * @brief Stores the per-bone offset matrix and its index in the palette.
     */
    struct kBoneInfo
    {
        int id;       ///< Index into the final bone-matrix palette.
        kMat4 offset; ///< Transforms from model space to bone-local space.
    };

    /** @brief A single position keyframe for skeletal animation. */
    struct kKeyPosition
    {
        kVec3 position;  ///< World-space position at this keyframe.
        float timeStamp; ///< Time (in ticks) at which this keyframe occurs.
    };

    /** @brief A single rotation keyframe for skeletal animation. */
    struct kKeyRotation
    {
        kQuat orientation; ///< Rotation quaternion at this keyframe.
        float timeStamp;   ///< Time (in ticks) at which this keyframe occurs.
    };

    /** @brief A single scale keyframe for skeletal animation. */
    struct kKeyScale
    {
        kVec3 scale;     ///< Non-uniform scale at this keyframe.
        float timeStamp; ///< Time (in ticks) at which this keyframe occurs.
    };

    /**
     * @brief Node in a skeletal-animation hierarchy (one per scene-graph joint).
     *
     * The skeletal animation loader walks the source asset's node tree and
     * builds an equivalent kNodeData tree; kAnimator then traverses this tree
     * each frame to accumulate world-space bone matrices. The fields mirror
     * what a typical skinned-mesh asset format provides without referencing
     * any specific import library.
     */
    struct kNodeData
    {
        kMat4 transformation;            ///< Local-space transform for this node.
        kString name;                    ///< Node name (used to look up bones by name).
        int childrenCount;               ///< Number of child nodes.
        std::vector<kNodeData> children; ///< Child nodes.
    };

    /**
     * @brief Glyph metrics for a single character in a bitmap font atlas.
     */
    struct kFontGlyph
    {
        float ax;       ///< Horizontal advance (pen advance after drawing the glyph).
        float lsb;      ///< Left side bearing.
        float tx, ty;   ///< UV coordinates of the glyph within the atlas texture.
        int w, h;       ///< Width and height of the glyph in pixels.
        int xoff, yoff; ///< Pixel offset from the glyph origin.
    };

    /**
     * @brief Computes an MD5 checksum of the given file.
     * @param fileName Path to the file.
     * @return Lowercase hex MD5 digest kString, or empty kString if the file cannot be opened.
     */
    inline kString generateFileChecksum(const kString &fileName)
    {
        std::ifstream file(fileName, std::ios::binary);
        if (!file.is_open())
            return "";

        const size_t bufferSize = 8192;
        std::vector<uint8_t> buffer(bufferSize);

        kMD5 md5;

        while (file)
        {
            file.read(reinterpret_cast<char *>(buffer.data()), bufferSize);
            std::streamsize bytesRead = file.gcount();
            if (bytesRead > 0)
            {
                md5.update(buffer.data(), static_cast<size_t>(bytesRead));
            }
        }

        return md5.final();
    }

    /**
     * @brief Generates a cryptographically random alphanumeric kString.
     * @param stringLength Number of characters to generate.
     * @return Random kString of the requested length.
     */
    inline kString generateRandomString(int stringLength)
    {
        const kString possibleCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");

        kString randomString;
        randomString.reserve(stringLength);

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(0, (int)possibleCharacters.length() - 1);

        for (int i = 0; i < stringLength; ++i)
        {
            char nextChar = possibleCharacters.at(dist(gen));
            randomString.push_back(nextChar);
        }

        return randomString;
    }

    /**
     * @brief Generates a random UUID v4 kString.
     * @return UUID formatted as @c xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx.
     */
    inline kString generateUuid()
    {
        // Generates UUID v4
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dist;

        // Generate 128 bits (UUID is 128-bit)
        uint64_t high = dist(gen);
        uint64_t low = dist(gen);

        // Set version (UUID v4 -> bits 12–15 of time_hi_and_version)
        high &= 0xFFFFFFFFFFFF0FFFULL;
        high |= 0x0000000000004000ULL;

        // Set variant (10xx -> bits 62–63)
        low &= 0x3FFFFFFFFFFFFFFFULL;
        low |= 0x8000000000000000ULL;

        std::stringstream ss;
        ss << std::hex << std::setfill('0');

        ss << std::setw(8) << (uint32_t)(high >> 32);
        ss << "-";
        ss << std::setw(4) << (uint16_t)(high >> 16);
        ss << "-";
        ss << std::setw(4) << (uint16_t)(high);
        ss << "-";
        ss << std::setw(4) << (uint16_t)(low >> 48);
        ss << "-";
        ss << std::setw(12) << (low & 0x0000FFFFFFFFFFFFULL);

        return ss.str();
    }

}; // namespace kemena

#endif // KDATATYPE_H
