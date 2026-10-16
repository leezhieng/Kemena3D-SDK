/**
 * @file kterrain.h
 * @brief Grid-based terrain system with polygon-based sculpting, layered materials,
 *        seamless neighbor stitching, and load/unload based on player position.
 *
 * Sculpting edits vertex Y positions (polygons) directly — no height map texture
 * or vertex shader displacement is used.  The shader only handles material splatting.
 */

#ifndef KTERRAIN_H
#define KTERRAIN_H

#include "kexport.h"
#include "kdatatype.h"
#include "kobject.h"
#include "kmesh.h"
#include "kmaterial.h"
#include "ktexture2d.h"
#include "ktexture.h"

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace kemena
{

    class kScene;
    class kAssetManager;
    class kRenderer;

    // ---------------------------------------------------------------------------
    // Terrain Material Layer
    // ---------------------------------------------------------------------------

    /**
     * @brief Describes one material layer in the terrain's layered material system.
     *
     * Each layer has a base material (albedo/normal/etc.), a tile size for UV
     * scaling, and a height range that controls where the layer is applied.
     */
    struct KEMENA3D_API kTerrainLayer
    {
        kString name;                  ///< Display name (e.g. "Grass", "Rock", "Snow").
        kMaterial *material = nullptr; ///< Material asset for this layer (owned externally).
        float tileSize = 1.0f;         ///< World-space size of one texture repeat.
        float minHeight = 0.0f;        ///< Minimum height (world units) where this layer appears.
        float maxHeight = 1.0f;        ///< Maximum height (world units) where this layer appears.
        float slopeMin = 0.0f;         ///< Minimum slope angle in degrees for this layer.
        float slopeMax = 90.0f;        ///< Maximum slope angle in degrees for this layer.

        // Optional per-layer textures. These are packed into the terrain's
        // sampler2DArray objects (albedo / normal / ORM / mask) keyed by layer
        // index, mirroring Cartographer's texture-array terrain material.
        kString albedoPath;            ///< Albedo image (sRGB) -> u_AlbedoArray[layer].
        kString normalPath;            ///< Normal-map image -> u_NormalArray[layer].
        kString ormPath;               ///< ORM image (R=AO, G=Roughness, B=Metallic) -> u_ORMArray.
        kString maskPath;              ///< Masked-blend mask image -> u_MaskArray[layer].

        /// Which RGBA channel of the terrain splat map masks this layer:
        /// 0 = R, 1 = G, 2 = B, 3 = A, -1 = "auto" (use the layer's own index).
        int maskChannel = -1;

        void serialize(json &j) const;
        void deserialize(const json &j);
    };

    // ---------------------------------------------------------------------------
    // Non-destructive height modifier layer
    // ---------------------------------------------------------------------------

    /** @brief Kind of height modifier stacked on top of the sculpted base. */
    enum class KEMENA3D_API kTerrainHeightLayerType
    {
        Noise,            ///< Fractal noise (fBm / ridged / billowed).
        HydraulicErosion, ///< Droplet erosion (weigert/SimpleErosion).
        ThermalErosion,   ///< Talus-angle slope relaxation.
        WindErosion,      ///< Directional material advection.
        Hydrology         ///< D8 stream-power river carving.
    };

    /**
     * @brief A non-destructive height modifier layer.
     *
     * Sculpting and height painting write to the tile's *base* height field.
     * Generated operators (noise, erosion) do not touch the base: they keep a
     * full-resolution @ref offsets buffer holding their delta over the composite
     * of everything below them.  The rendered height is
     * `base + Σ(enabled ? offsets * weight : 0)`, so a layer can be disabled,
     * re-weighted or deleted without re-running its generator.
     */
    struct KEMENA3D_API kTerrainHeightLayer
    {
        kString name = "Layer";                              ///< Display name.
        kTerrainHeightLayerType type = kTerrainHeightLayerType::Noise; ///< Generator kind.
        bool enabled = true;                                 ///< Composited when true.
        float weight = 1.0f;                                 ///< Strength multiplier on the delta.

        // --- Noise parameters -------------------------------------------------
        float noiseScale = 50.0f;      ///< Samples per noise cell.
        float noiseAmplitude = 10.0f;  ///< World-unit amplitude.
        int noiseOctaves = 4;          ///< fBm octave count.
        int noiseSeed = 42;            ///< Lattice seed.
        int noiseType = 0;             ///< 0 = fBm, 1 = ridged, 2 = billowed.
        bool noiseAdditive = false;    ///< Add to the layer input instead of replacing it.

        // --- Erosion parameters -----------------------------------------------
        int droplets = 50000;          ///< Hydraulic droplet count.
        float erodeRadius = 4.0f;      ///< Droplet brush radius (samples).
        float erodeInertia = 0.05f;    ///< Droplet inertia.
        float erodeCapacity = 4.0f;    ///< Sediment capacity factor.
        float erodeDeposit = 0.3f;     ///< Deposition factor.
        float erodeErode = 0.3f;       ///< Erosion factor.
        float erodeEvaporate = 0.01f;  ///< Water evaporation per step.
        float erodeGravity = 4.0f;     ///< Gravity constant.

        int iterations = 8;            ///< Thermal iterations.
        float talus = 0.4f;            ///< Talus threshold (height units).
        float strength = 0.5f;         ///< Per-iteration relaxation strength.

        float windDirection = 0.0f;    ///< Wind direction in radians.

        float hydroK = 0.02f;          ///< Stream-power coefficient.
        float hydroM = 0.5f;           ///< Area exponent.
        float hydroN = 1.0f;           ///< Slope exponent.
        float hydroDeposit = 0.1f;     ///< Deposition fraction.

        /// Per-sample delta over the composite below this layer (heightRes*heightRes).
        std::vector<float> offsets;

        /** @brief Serializes the generator parameters (offsets are stored separately). */
        void serialize(json &j) const;
        /** @brief Deserializes the generator parameters (offsets are loaded separately). */
        void deserialize(const json &j);
    };

    // ---------------------------------------------------------------------------
    // Terrain Tile
    // ---------------------------------------------------------------------------

    /**
     * @brief A single terrain tile — a heightfield mesh with layered materials.
     *
     * Each tile occupies a fixed-size world-space region and is identified by
     * its integer grid coordinate (gridX, gridZ).  The tile generates a GPU mesh
     * from its height data.
     *
     * Sculpting edits vertex Y positions directly (polygon editing) — no height
     * map texture or vertex shader displacement is used.  The shader only
     * performs material splatting.
     *
     * The mesh is registered with the owning kScene so it participates in
     * frustum culling and rendering like any other mesh.
     *
     * Neighbor references are stored so that adjacent tiles can be stitched
     * together (seamless edges) by adjusting the outer ring of vertices.
     */
    class KEMENA3D_API kTerrain : public kObject
    {
    public:
        /**
         * @brief Constructs an empty terrain tile.
         * @param scene         Owning scene (for mesh registration).
         * @param assetManager  Asset manager for loading textures/materials.
         * @param gridX         Integer X coordinate in the terrain grid.
         * @param gridZ         Integer Z coordinate in the terrain grid.
         * @param worldSize     World-space size of the tile (assumed square; e.g. 256 units).
         * @param heightRes     Number of height samples per side (e.g. 513 produces 512 quads).
         */
        kTerrain(kScene *scene, kAssetManager *assetManager,
                 int gridX, int gridZ, float worldSize = 256.0f, int heightRes = 513);
        ~kTerrain();

        // --- Sculpting brush types -----------------------------------------------

        /** @brief Brush operation types for terrain sculpting. */
        enum class BrushMode
        {
            Raise,   ///< Increase height at brush centre.
            Lower,   ///< Decrease height at brush centre.
            Flatten, ///< Pull heights toward the initial sample height under the brush centre.
            Smooth   ///< Average heights within the brush radius.
        };

        // --- Grid identity -------------------------------------------------------

        /** @brief Returns the X coordinate in the terrain grid. */
        int getGridX() const { return m_gridX; }

        /** @brief Returns the Z coordinate in the terrain grid. */
        int getGridZ() const { return m_gridZ; }

        /** @brief Returns the world-space size of the tile (square). */
        float getWorldSize() const { return m_worldSize; }

        /** @brief Returns the number of height samples per side. */
        int getHeightRes() const { return m_heightRes; }

        /** @brief Returns the world-space position of the tile's corner (min X, min Z). */
        kVec3 getWorldPosition() const;

        // --- Height data ---------------------------------------------------------

        /**
         * @brief Returns a pointer to the raw height data (float array, heightRes*heightRes).
         *
         * The data is stored row-major; index = z * heightRes + x.
         */
        const float *getHeightData() const { return m_heightData.data(); }
        float *getHeightData() { return m_heightData.data(); }

        /** @brief Returns the number of height samples (heightRes * heightRes). */
        size_t getHeightDataSize() const { return m_heightData.size(); }

        /**
         * @brief Samples the height at a world-space position (bilinear interpolation).
         * @param worldPos World-space XZ position (Y is ignored).
         * @return Interpolated height, or 0 if outside the tile.
         */
        float sampleHeight(const kVec3 &worldPos) const;

        /**
         * @brief Sets the height at a given heightmap coordinate.
         * @param x      X index [0, heightRes-1].
         * @param z      Z index [0, heightRes-1].
         * @param value  New height value.
         */
        void setHeight(int x, int z, float value);

        /**
         * @brief Loads height data from a raw float binary file.
         * @param path Path to the .raw height file.
         * @return true on success.
         */
        bool loadHeightData(const kString &path);

        /**
         * @brief Saves height data to a raw float binary file.
         * @param path Path to the .raw height file.
         * @return true on success.
         */
        bool saveHeightData(const kString &path) const;

        /**
         * @brief Applies a sculpting brush stamp at a world-space position.
         *
         * Modifies height values within the brush radius using the specified
         * operation (raise, lower, flatten, smooth).  After modifying height
         * data, call updateMesh() (fast) or rebuildMesh() (full rebuild)
         * to update the GPU vertex buffer.
         *
         * @param worldPos  World-space XZ position of the brush centre.
         * @param radius    World-space brush radius.
         * @param strength  Brush opacity [0, 1]. 1.0 = full effect.
         * @param mode      Brush operation (Raise, Lower, Flatten, Smooth).
         * @param flattenTargetHeight  Heights are pulled toward this value in Flatten mode.
         */
        void applyBrush(const kVec3 &worldPos, float radius, float strength,
                        BrushMode mode, float flattenTargetHeight = 0.0f);

        /**
         * @brief Converts a world-space position to the nearest heightmap coordinates.
         * @param worldPos World-space XZ position.
         * @param outX     Output heightmap X index.
         * @param outZ     Output heightmap Z index.
         * @return true if the position is inside this tile.
         */
        bool worldToHeightmap(const kVec3 &worldPos, int &outX, int &outZ) const;

        /**
         * @brief Converts heightmap coordinates to a world-space XZ position.
         * @param x Heightmap X index.
         * @param z Heightmap Z index.
         * @return World-space position (Y = 0).
         */
        kVec3 heightmapToWorld(int x, int z) const;

        /**
         * @brief Fills the entire heightmap with a flat value and rebuilds the mesh.
         * @param value Height value.
         */
        void fillHeight(float value);

        // --- Weight map (material blending, up to 16 layers) ---------------------

        /** @brief Maximum number of material layers a terrain tile supports. */
        static constexpr int kMaxTerrainLayers = 16;

        /**
         * @brief Returns a pointer to the raw weight data.
         *
         * Layout: heightRes * heightRes * 16 bytes, packed per sample as
         * kMaxTerrainLayers consecutive layer weights (sample index = z*res+x).
         * Layers are uploaded as four RGBA weight-map textures: layers 0..3 ->
         * u_WeightMap0, 4..7 -> u_WeightMap1, 8..11 -> u_WeightMap2,
         * 12..15 -> u_WeightMap3.
         */
        const unsigned char *getWeightData() const { return m_weightData.data(); }
        unsigned char *getWeightData() { return m_weightData.data(); }

        /** @brief Returns the weight data size in bytes. */
        size_t getWeightDataSize() const { return m_weightData.size(); }

        /** @brief Backward-compatible accessor returning the weight data (layers 0..3 are the legacy RGBA splat). */
        const unsigned char *getSplatData() const { return m_weightData.data(); }
        unsigned char *getSplatData() { return m_weightData.data(); }
        size_t getSplatDataSize() const { return m_weightData.size(); }

        /** @brief Per-sample weight (0..255) of a layer at heightmap (x, z). */
        unsigned char getWeight(int x, int z, int layer) const;

        /** @brief Sets the per-sample weight (0..255) of a layer at heightmap (x, z). */
        void setWeight(int x, int z, int layer, unsigned char v);

        /** @brief GPU weight-map texture handle (w in 0..3), or 0 if not created. */
        uint32_t getWeightTexture(int w) const { return (w >= 0 && w < 4) ? m_weightTextures[w] : 0; }

        /** @brief Rebuilds the layer texture arrays from the current layer image paths. */
        void rebuildLayerArrays();

        /**
         * @brief Loads a splat map from a PNG file (RGBA8).
         * @param path Path to the PNG splat map.
         * @return true on success.
         */
        bool loadSplatMap(const kString &path);

        /**
         * @brief Saves the splat map to a PNG file (RGBA8).
         * @param path Output path for the PNG splat map.
         * @return true on success.
         */
        bool saveSplatMap(const kString &path) const;

        /**
         * @brief Saves the height map as a normalized grayscale PNG.
         * @param path Output path for the PNG file.
         * @return true on success.
         */
        bool saveHeightMapPng(const kString &path) const;

        // --- Material layers ---------------------------------------------------

        /** @brief Returns the list of terrain material layers. */
        const std::vector<kTerrainLayer> &getLayers() const { return m_layers; }
        std::vector<kTerrainLayer> &getLayers() { return m_layers; }

        /**
         * @brief Uploads the per-layer splat-mask channel selection to the material.
         *
         * Writes `u_MaskChannel[l]` (0..3 = R/G/B/A) for every layer from
         * @ref kTerrainLayer::maskChannel, where -1 ("auto") falls back to the
         * layer's own index. Call after editing the layers so the shader picks the
         * change up.
         */
        void syncLayerMaskChannels();

        /**
         * @brief Sets the material for a specific layer.
         * @param index    Layer index [0, 3].
         * @param material Material to assign.
         */
        void setLayerMaterial(int index, kMaterial *material);

        /**
         * @brief Adds or updates a terrain layer.
         * @param layer Layer descriptor.
         */
        void setLayer(const kTerrainLayer &layer);

        // --- Height field: sculpted base vs. generated modifier layers ----------

        /**
         * @brief Returns the raw sculpted base height field (sculpt + height paint).
         *
         * The base is what gets written to the tile's `.height` file.  The
         * rendered height is the *composite* returned by getHeightData().
         */
        const float *getBaseHeightData() const { return m_baseHeightData.data(); }
        float *getBaseHeightData() { return m_baseHeightData.data(); }

        /**
         * @brief Recomputes the composite height field.
         *
         * `m_heightData = base + Σ(enabled ? layer.offsets * layer.weight : 0)`.
         * Call after touching the base or any layer, then updateMesh().
         */
        void recomposeHeight();

        /** @brief Returns the non-destructive height modifier stack. */
        const std::vector<kTerrainHeightLayer> &getHeightLayers() const { return m_heightLayers; }
        std::vector<kTerrainHeightLayer> &getHeightLayers() { return m_heightLayers; }

        /** @brief Appends a modifier layer (offsets zeroed); returns its index. */
        int addHeightLayer(const kTerrainHeightLayer &layer);

        /** @brief Removes a modifier layer and recomposites; false if out of range. */
        bool removeHeightLayer(int index);

        /**
         * @brief (Re)generates one layer's offsets from a generator functor.
         *
         * @p generator is handed the composite of the base plus every layer below
         * @p index, and must fill its output buffer with the layer's *absolute*
         * result.  The delta over the input is stored, so weight/enabled later
         * rescale without re-running the generator.
         */
        void generateHeightLayer(int index,
                                 const std::function<void(const std::vector<float> &input,
                                                          std::vector<float> &output)> &generator);

        /** @brief Fills @p out with the composite of the base plus all layers below @p index. */
        void buildLayerInput(int index, std::vector<float> &out) const;

        // --- Resizing -----------------------------------------------------------

        /**
         * @brief Resamples the base height field and every layer to @p newHeightRes.
         *
         * Uses bilinear sampling; quality is intentionally not preserved.
         */
        void resizeHeightData(int newHeightRes);

        /** @brief Resamples the 16-layer weight (splat) map to @p newHeightRes. */
        void resizeWeightData(int newHeightRes);

        /** @brief Changes the tile's world size and refreshes its material uniform. */
        void setWorldSize(float worldSize);

        // --- Internally owned assets --------------------------------------------

        /**
         * @brief Creates the tile's own `.height`, `.splat`, `.hlayers` and `.mat`
         *        files inside @p dataDir (named after the mesh UUID).
         *
         * Safe to call repeatedly — existing files are only rewritten on save.
         */
        void createOwnedAssets(const kString &dataDir);

        /** @brief Deletes the tile's owned asset files from disk. */
        void destroyOwnedAssets();

        /** @brief Absolute path of the owned `.height` file ("" when not created). */
        const kString &getHeightFilePath() const { return m_heightFilePath; }
        /** @brief Absolute path of the owned `.splat` file ("" when not created). */
        const kString &getSplatFilePath() const { return m_splatFilePath; }
        /** @brief Absolute path of the owned `.hlayers` file ("" when not created). */
        const kString &getHeightLayersFilePath() const { return m_heightLayersFilePath; }
        /** @brief Absolute path of the owned `.mat` file ("" when not created). */
        const kString &getMaterialFilePath() const { return m_materialFilePath; }

        /**
         * @brief Adopts explicit owned-asset paths.
         *
         * Used when loading a world: the `.world` records the filenames the tile
         * was saved with, and they do not have to match the manager's current
         * naming scheme.
         */
        void adoptOwnedAssetPaths(const kString &heightFile, const kString &splatFile,
                                  const kString &heightLayersFile, const kString &materialFile);

        /**
         * @brief Returns the terrain's internally managed material.
         *
         * Never user-assignable: the Terrain panel edits its parameters, and the
         * inspector / drag-drop paths refuse to replace it.
         */
        kMaterial *getTerrainMaterial() const { return m_terrainMaterial; }

        /** @brief Writes every owned asset (height, splat, layers, material) to disk. */
        void saveOwnedAssets();

        /** @brief Reads every owned asset (height, splat, layers, material) from disk. */
        void loadOwnedAssets();

        // --- Mesh and rendering -------------------------------------------------

        /** @brief Returns the generated terrain kMesh (registered with the scene). */
        kMesh *getMesh() const { return m_mesh; }

        /**
         * @brief Rebuilds the GPU mesh from the current height data.
         *
         * Generates vertex positions with actual Y heights, computes normals
         * from geometry, and uploads to GPU.  No height map texture or vertex
         * shader displacement is used — the polygon data carries the height.
         *
         * @param stitchNeighbors If true, adjusts edge vertices to match neighbor heights.
         */
        void rebuildMesh(bool stitchNeighbors = true);

        /**
         * @brief Fast update: updates vertex Y positions and normals on the GPU
         *        using glBufferSubData, without full mesh recreation.
         *
         * Call during brush stroke for live feedback; call rebuildMesh() on
         * mouse-up for full quality (tangents, AABB, physics).
         */
        void updateMesh();

        /**
         * @brief Fast update: uploads the current splat data to the GPU splat
         *        texture via glTexSubImage2D (no geometry changes).
         */
        void updateSplatTexture();

        // --- Distance LOD --------------------------------------------------------

        /**
         * @brief Sets the mesh decimation step (1 = full resolution). Powers of two
         *        that divide (heightRes-1) keep the grid tile-aligned. Rebuilds the
         *        mesh immediately when the step changes and the tile is loaded.
         */
        void setLodStep(int step);

        /** @brief Returns the current mesh decimation step (1 = full resolution). */
        int getLodStep() const { return m_lodStep; }

        // --- Neighbor stitching --------------------------------------------------

        /**
         * @brief Sets a neighbor tile reference for edge stitching.
         * @param direction "north", "south", "east", or "west".
         * @param neighbor  Pointer to the neighboring kTerrain, or nullptr to clear.
         */
        void setNeighbor(const kString &direction, kTerrain *neighbor);

        /** @brief Returns the neighbor tile in the given direction, or nullptr. */
        kTerrain *getNeighbor(const kString &direction) const;

        // --- Load / unload -------------------------------------------------------

        /** @brief Returns true if the tile is currently loaded (has GPU mesh). */
        bool isLoaded() const { return m_loaded; }

        /**
         * @brief Unloads the tile: destroys GPU mesh and splat texture.
         *
         * Height and splat data are retained in memory so the tile can be reloaded.
         */
        void unload();

        /**
         * @brief Reloads the tile: regenerates GPU mesh.
         * @param stitchNeighbors If true, stitches edges with loaded neighbors.
         */
        void reload(bool stitchNeighbors = true);

        // --- Serialization -------------------------------------------------------

        /**
         * @brief Serializes terrain metadata to JSON.
         *
         * Height and splat data are NOT included — they live in the _data folder.
         */
        void serialize(json &j) const;

        /**
         * @brief Deserializes terrain metadata from JSON.
         *
         * Height and splat data are loaded separately from the _data folder.
         */
        void deserialize(const json &j);

    private:
        /** @brief Computes the world-space X coordinate of a heightmap sample. */
        float sampleToWorldX(int x) const;

        /** @brief Computes the world-space Z coordinate of a heightmap sample. */
        float sampleToWorldZ(int z) const;

        /** @brief Returns the height sample at (x, z) with neighbor stitching applied. */
        float getStitchedHeight(int x, int z) const;

        /** @brief Computes the normal at a heightmap sample using central differences. */
        kVec3 computeNormal(int x, int z) const;

        /** @brief Creates the four GPU weight-map textures (RGBA8) from the 16 layers. */
        void createSplatTexture();

        /** @brief Builds the per-layer albedo / normal / ORM / mask texture arrays. */
        void createLayerArrays();

        /** @brief Pixels per side of each layer texture array. */
        static constexpr int kTerrainArraySize = 256;

        kScene *m_scene = nullptr;
        kAssetManager *m_assetManager = nullptr;
        kMesh *m_mesh = nullptr;
        kMaterial *m_terrainMaterial = nullptr; ///< Combined terrain shader material.

        int m_gridX = 0;
        int m_gridZ = 0;
        float m_worldSize = 256.0f;
        int m_heightRes = 513;
        float m_sampleSpacing = 0.0f; ///< World-space distance between height samples.

        std::vector<float> m_heightData;         ///< Composite height (base + modifier layers).
        std::vector<float> m_baseHeightData;     ///< Sculpted/painted base height (persisted).
        std::vector<kTerrainHeightLayer> m_heightLayers; ///< Non-destructive modifier stack.
        std::vector<unsigned char> m_weightData; ///< heightRes * heightRes * 16 bytes (16 layer weights).
        std::vector<kTerrainLayer> m_layers;     ///< Up to kMaxTerrainLayers material layers.

        // Owned asset paths, filled by createOwnedAssets().
        kString m_heightFilePath;      ///< <dataDir>/<uuid>.height
        kString m_splatFilePath;       ///< <dataDir>/<uuid>.splat
        kString m_heightLayersFilePath;///< <dataDir>/<uuid>.hlayers
        kString m_materialFilePath;    ///< <dataDir>/<uuid>.mat

        uint32_t m_weightTextures[4] = {0, 0, 0, 0}; ///< GPU RGBA8 weight-map textures (4 x 4 layers).
        uint32_t m_albedoArray = 0; ///< GL_TEXTURE_2D_ARRAY of per-layer albedo (sRGB).
        uint32_t m_normalArray = 0; ///< GL_TEXTURE_2D_ARRAY of per-layer normals.
        uint32_t m_ormArray    = 0; ///< GL_TEXTURE_2D_ARRAY of per-layer ORM (R=AO, G=Rough, B=Metal).
        uint32_t m_maskArray   = 0; ///< GL_TEXTURE_2D_ARRAY of per-layer blend masks.

        // Neighbors for stitching (N, S, E, W)
        kTerrain *m_neighborNorth = nullptr;
        kTerrain *m_neighborSouth = nullptr;
        kTerrain *m_neighborEast = nullptr;
        kTerrain *m_neighborWest = nullptr;

        bool m_loaded = false;
        int  m_lodStep = 1; ///< Mesh decimation step for distance LOD (1 = full res).
    };

    // ---------------------------------------------------------------------------
    // Terrain Manager
    // ---------------------------------------------------------------------------

    /**
     * @brief Manages a grid of kTerrain tiles, handling load/unload based on
     *        player proximity and coordinating seamless neighbor stitching.
     *
     * Typical usage:
     * @code
     *   kTerrainManager terrainMgr;
     *   terrainMgr.init(scene, assetMgr, worldSize, heightRes);
     *
     *   // Each frame:
     *   terrainMgr.update(playerPosition, loadRadius);
     * @endcode
     */
    class KEMENA3D_API kTerrainManager
    {
    public:
        kTerrainManager();
        ~kTerrainManager();

        /**
         * @brief Initializes the terrain manager.
         * @param scene         Owning scene for mesh registration.
         * @param assetManager  Asset manager for loading textures.
         * @param worldSize     World-space size of each tile (square).
         * @param heightRes     Number of height samples per side per tile.
         */
        void init(kScene *scene, kAssetManager *assetManager,
                  float worldSize = 256.0f, int heightRes = 513);

        /**
         * @brief Updates terrain loading based on player position.
         *
         * Tiles within @p loadRadius of @p playerPos are loaded (if not already).
         * Tiles outside @p unloadRadius are unloaded.
         *
         * @param playerPos    World-space player position (XZ used, Y ignored).
         * @param loadRadius   Distance within which tiles are loaded.
         * @param unloadRadius Distance beyond which tiles are unloaded.
         */
        void update(const kVec3 &playerPos, float loadRadius = 512.0f, float unloadRadius = 768.0f);

        /**
         * @brief Creates a terrain tile at the given grid coordinate.
         *
         * If a tile already exists at that coordinate, returns the existing one.
         *
         * @param gridX Integer X coordinate.
         * @param gridZ Integer Z coordinate.
         * @return Pointer to the terrain tile.
         */
        kTerrain *createTile(int gridX, int gridZ);

        /**
         * @brief Removes a terrain tile at the given grid coordinate.
         * @param gridX Integer X coordinate.
         * @param gridZ Integer Z coordinate.
         * @return true if a tile was removed.
         */
        bool removeTile(int gridX, int gridZ);

        /**
         * @brief Returns the terrain tile at the given grid coordinate, or nullptr.
         */
        kTerrain *getTile(int gridX, int gridZ) const;

        /**
         * @brief Returns all currently existing tiles.
         */
        const std::unordered_map<uint64_t, std::unique_ptr<kTerrain>> &getTiles() const { return m_tiles; }

        /**
         * @brief Returns the grid coordinate for a world-space position.
         */
        static void worldToGrid(const kVec3 &worldPos, float worldSize, int &outGridX, int &outGridZ);

        /**
         * @brief Converts grid coordinates to the world-space position of the tile's corner.
         */
        static kVec3 gridToWorld(int gridX, int gridZ, float worldSize);

        /**
         * @brief Packs two grid coordinates into a single 64-bit key for map lookups.
         */
        static uint64_t gridKey(int gridX, int gridZ);

        /**
         * @brief Loads all terrain tiles from .terrain files in a directory.
         * @param directory Path to the folder containing .terrain files.
         */
        void loadFromDirectory(const kString &directory);

        /**
         * @brief Saves all terrain tiles to .terrain files.
         * @param directory Path to the output folder.
         */
        void saveToDirectory(const kString &directory) const;

        /**
         * @brief Unloads all tiles and clears the manager.
         */
        void clear();

        /** @brief Returns the number of currently loaded tiles. */
        int getLoadedCount() const;

        /** @brief Returns the total number of tiles. */
        int getTotalCount() const { return static_cast<int>(m_tiles.size()); }

        /** @brief Returns the world size of each tile. */
        float getWorldSize() const { return m_worldSize; }

        /** @brief Returns the height resolution of each tile. */
        int getHeightRes() const { return m_heightRes; }

        /**
         * @brief Sets the folder that owns every tile's asset files.
         *
         * Tiles created afterwards get their `.height`, `.splat`, `.hlayers` and
         * `.mat` files inside this folder, named after the mesh UUID.  Existing
         * tiles adopt the folder too, so a late project load still persists them.
         */
        void setAssetDirectory(const kString &directory);

        /** @brief Returns the folder that owns the tiles' asset files. */
        const kString &getAssetDirectory() const { return m_assetDirectory; }

        /** @brief Adopts the manager's asset directory on every existing tile. */
        void adoptAssetDirectory();

        /**
         * @brief Resizes every tile (and the manager's own world size / resolution).
         *
         * All tiles are resampled to @p heightRes; quality is intentionally not
         * preserved.
         */
        void resizeAllTiles(float worldSize, int heightRes);

    private:
        /**
         * @brief Updates neighbor references for all tiles so stitching works.
         */
        void updateNeighbors();

        /**
         * @brief Loads a tile (creates GPU mesh).
         */
        void loadTile(kTerrain *terrain);

        /**
         * @brief Unloads a tile (destroys GPU mesh).
         */
        void unloadTile(kTerrain *terrain);

        kScene *m_scene = nullptr;
        kAssetManager *m_assetManager = nullptr;
        float m_worldSize = 256.0f;
        int m_heightRes = 513;

        kString m_assetDirectory; ///< Folder owning tile .height/.splat/.hlayers/.mat files.

        std::unordered_map<uint64_t, std::unique_ptr<kTerrain>> m_tiles;
        std::unordered_set<uint64_t> m_loadedTiles; ///< Set of currently loaded tile keys.
    };

} // namespace kemena

#endif // KTERRAIN_H
