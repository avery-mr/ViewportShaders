# Form Clay — Product Requirements

Viewport material for 3ds Max 2027. It helps a modeler read form while orbiting a mesh. It is not a production renderer shader.

## Problem

The Nitrous viewport’s realistic materials are built for lookdev. A modeler tumbling a mesh wants the opposite: light that stays put relative to the camera, and creases that show up without a UV unwrap or a baked map.

## User

Someone modeling in 3ds Max 2027 who assigns a material, orbits the mesh, and tweaks a few sliders. They are also learning the C++ SDK, so the plug-in is small enough to read end to end.

## Success

- Form Clay appears in the Material/Map Browser under **Viewport Shaders**.
- Assigned to a mesh with smoothed normals, the viewport shows a warm gray clay.
- Orbiting the view does not swing the key light around the model.
- Raising **Cavity** darkens recesses. Raising **Edge** lightens ridges. Both work with no UV coordinates and no texture.
- A Bitmap in the **Matcap** slot mixes over the clay by **Matcap Strength**. An empty slot leaves the clay unchanged.
- The material is opaque.
- Scanline renders a flat clay diffuse of the same color. Arnold and V-Ray are out of scope and will not match the viewport.
- Quicksilver uses the same HLSL effect as the viewport.

## Look

| Control | Default | What it does |
| --- | --- | --- |
| Clay Color | about `0.76, 0.73, 0.68` | Base color |
| Wrap | `0.4` | How far the key light wraps past the terminator |
| Cavity | `0.6` | Darken concavities |
| Edge | `0.35` | Lighten ridges |
| Specular | low | Broad, weak highlight so the clay stays matte |
| Specular softness | broad | Width of that highlight |
| Matcap Strength | `0` | Mix of the matcap over the clay |
| Matcap | empty Bitmap slot | Sampled by the view-space normal |

Lighting is a studio rig in view space: a warm key, a cooler fill, and a soft rim. Cavity and edge come from screen-space derivatives of the world normal. Coarse meshes read more weakly than dense, smoothed ones. Silhouette pixels must not blow out to white or black.

## Non-goals

- Scene-light binding
- Procedural maps in the Matcap slot (Bitmap only)
- UV-based textures, triplanar color, or normal maps
- Arnold, V-Ray, or any other production-renderer translator
- Transparency
- A Qt UI (the rollout is the classic param-block dialog)

## Technical shape

One native plug-in DLL, `FormClay.dlu` (material plug-ins use the `.dlu` extension), plus `FormClay.fx` copied next to it.

- Class `FormClayMtl` derives from `Mtl` and `MaxSDK::Graphics::IHLSLMaterialTranslator`.
- One `ParamBlockDesc2` and one rollout.
- `GetInterface` hands Max the translator for `IHLSL_MATERIAL_TRANSLATOR_INTERFACE_ID`, and the material defaults to hardware viewport shading.
- The effect is loaded once with `HLSLMaterialHandle::InitializeWithFile`. Technique name is `FormClay`.
- `UpdateHLSLMaterial` pushes the param-block values. Nitrous fills the transform matrices itself when the effect parameters use its semantics (`WORLD`, `WORLDINVERSETRANSPOSE`, `VIEW`, `WORLDVIEWPROJECTION`, `CAMERAPOSITION`).
- The effect asks only for position and normal streams.
- `GetTransparencyHint` returns false.
- `Shade` is a clay-colored diffuse for Scanline.

The effect is HLSL shader model 5.0, one pass, depth write on, backface cull on.

## Build environment

Target is **3ds Max 2027** (`3dsmax.exe` 29.2), which is already installed. The plug-in must be compiled with the toolchain Max itself was built with:

- Visual Studio 2026 is fine as the IDE.
- Platform toolset **v143**, specifically **MSVC v14.38 (17.8)**. The IDE’s default toolset (v14.51 on this machine) is not the one Max 2027 was built with.
- Windows 10 SDK **10.0.19041.0**.
- C++20.
- 64-bit only.

Qt 6.8.3 and .NET 10 are required for Qt or managed plug-ins. This material uses neither.

The 3ds Max 2027 SDK is a separate download from an Autodesk account. It is not part of the Max install. After it is installed, every plug-in in this repo builds inside one Visual Studio solution that imports the SDK’s property sheets. That solution is the master project. Form Clay is the first project added to it.

## How we will build it

The code is written one piece at a time, and each piece is explained before the next one is added:

1. Toolchain and SDK, until a stock SDK sample builds.
2. Master solution.
3. DLL entry and class descriptor (how Max finds the plug-in).
4. The material class with a single color parameter (how the editor talks to the object).
5. The translator and a shader that only shows Clay Color (how the viewport draws it).
6. Studio lighting, then cavity and edge, then the matcap.

A step is done when it builds and loads in Max, not when the file exists on disk.
