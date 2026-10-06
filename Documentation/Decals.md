# Decals

Decals in Kemena3D are **projected**. Instead of placing a flat quad against a
surface, a decal defines a projection volume anchored at its pivot. When it
overlaps scene geometry, the engine clips the covered triangles and rebuilds a
small polygon mesh from them, generating texture coordinates automatically from
each fragment's position inside the volume. Whatever material (and albedo/alpha
texture) is assigned to the decal is then mapped onto that surface.

This makes it possible to stamp logos, bullet holes, blood splatters, road
markings and similar artwork onto arbitrary meshes without editing their UVs or
geometry.

## Creating a decal

Use **GameObject ▸ Decal** (or the equivalent editor menu). A new decal is
created as a dynamic, unlit, alpha-blended white projector. Assign a material
whose albedo map carries the artwork — an *Unlit* material with alpha blending
is recommended so the sticker ignores scene lighting.

Newly created objects are placed at the editor camera's look-at / orbit point,
so a decal lands where you are looking. After focusing an object (press **F**)
the look-at point becomes that object's centre, so the next decal is created on
it.

## Editor visualization

While a decal is selected the editor draws its projection volume as a cyan
wireframe box (near cross-section at the pivot, far cross-section at the
projection distance), so the direction, distance and size are visible even
before the decal intersects any surface.

## Projection properties

| Property           | Description                                                                 |
|--------------------|-----------------------------------------------------------------------------|
| **Project Dir**    | Local-space axis the decal projects along. Default `(0, -1, 0)` — straight down onto the floor. Rotate the object (or change the vector) to project against walls, ceilings, etc. |
| **Project Distance** | How far along the direction the projection volume reaches.                 |
| **Project Size**   | Width (X) and height (Y) of the rectangular cross-section.                  |
| **Project Layers** | Multi-select filter: only meshes on the chosen object layers are projected onto (all layers by default). |
| **Surface Offset** | Pulls the generated fragments back toward the projector so they do not z-fight with the surface underneath. |
| **Shader**         | Built-in material used when no `.mat` is assigned: Flat (unlit), PBR, or Phong. |
| **Material**       | A `.mat` asset whose albedo/alpha map is the decal artwork.                 |
| **Rebuild**        | Forces a re-projection of the geometry.                                     |

The texture is mapped so that `U` runs along the object's local +X and `V` along
the derived cross-section up axis; rotating the decal about its projection axis
rolls the texture.

## Static vs. dynamic decals

* **Dynamic** (default) — the decal follows its own transform *and* the surfaces
  it projects onto. It automatically re-projects when the decal moves or when a
  scene mesh is moved, rotated, or scaled. The check is a cheap hash of every
  mesh's world transform, so it is safe to run every frame.
* **Static** — the decal is baked **once** and then frozen. It stops following
  the scene entirely, so the projected geometry is stable and costs nothing per
  frame. Use this for decals that will never move (worn floor markings, static
  props). Toggle **Static** in the Inspector, or press **Rebuild** to re-bake.

## Implementation notes

* Projected geometry is generated in **world space** and drawn with an identity
  model matrix, so the object's scale cannot distort the surface-offset amount.
* Clipping uses a 6-plane (Sutherland–Hodgman) clip of each scene triangle
  against the projection box; positions and normals are interpolated at the
  crossings.
* Only the decal-specific parameters are serialised (`decal_dir`,
  `decal_distance`, `decal_size`, `decal_offset`, `decal_shader`); the polygon
  mesh is always regenerated on load.
* `kScene::getMeshesRef()` provides a non-copying mesh list for the projection
  rebuild hot path.

## Scripting

`kDecal` exposes the projection parameters through the usual getters/setters
(`getProjectionDirection`/`setProjectionDirection`,
`getProjectionDistance`/`setProjectionDistance`,
`getProjectionSize`/`setProjectionSize`, `getSurfaceOffset`/`setSurfaceOffset`)
and `markGeometryDirty()` to force a re-projection.
