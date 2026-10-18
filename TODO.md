# Form Clay — Todo

Work top to bottom. Do not start a later step until the current one loads in Max or, for the setup steps, until the check at the end of the step passes.

## 1. Toolchain

This machine has Visual Studio Community 2026 and MSVC 14.51. Max 2027 plug-ins need an older compiler kept side by side.

- [x] In the Visual Studio Installer, Individual components, install **MSVC v143 – VS 2022 C++ x64/x86 build tools (v14.38-17.8)**. It is listed under Out of Support. Installed as `14.38.33130`.
- [x] Install the **Windows 10 SDK, version 10.0.19041.0** from the Windows SDK archive. The VS 2026 installer does not offer this version.
- [x] Confirm both are on disk: a `14.38*` folder under the MSVC tools directory, and `C:\Program Files (x86)\Windows Kits\10\Include\10.0.19041.0`.

## 2. 3ds Max 2027 SDK

- [x] Download the **3ds Max 2027 SDK** from the [developer help center](https://help.autodesk.com/view/MAXDEV/2027/ENU/) (SDK Download). It requires an Autodesk account.
- [x] Installed at `C:\Program Files\Autodesk\3ds Max 2027 SDK`. The path has spaces. Projects will point at `maxsdk` with a full path, so that is fine.
- [x] Confirm `maxsdk\include\max.h` and `maxsdk\ProjectSettings\propertySheets\3dsmax.general.project.settings.props` exist.
- [x] Toolchain check: the SDK sample `wooddent` compiled Hybrid | x64 against this SDK. Linking into `maxsdk\pdb` failed because that folder is under Program Files and is not writable. Our solution will send output into this repo instead.

## 3. Master solution

- [ ] Create a Visual Studio solution that will hold every viewport-shader plug-in. The SDK property sheets are the shared build settings; the solution is just where the projects live.
- [ ] Point the debugger at `C:\Program Files\Autodesk\3ds Max 2027\3dsmax.exe`.
- [ ] Add a post-build habit: copy the DLL and the `.fx` into a plug-in folder Max actually scans (the Max `plugins` directory, or an extra path in `plugin.ini`).

## 4. Plug-in skeleton — walk through together

- [ ] DLL exports (`LibDescription`, `LibNumberClasses`, `LibClassDesc`, `LibVersion`) and the `.def` file. Learn how Max discovers the DLL.
- [ ] `ClassDesc2`: class id, `MATERIAL_CLASS_ID`, category **Viewport Shaders**, `Create()`.
- [ ] Empty `FormClayMtl : public Mtl` that loads in the browser and assigns without drawing anything custom.
- [ ] Load Max, find Form Clay in the browser, assign it. Stop here until that works.

## 5. One parameter

- [ ] A param block with **Clay Color** only, and a rollout that shows it.
- [ ] `Shade()` returns that color as a diffuse, so Scanline is not black.
- [ ] Change the color in the editor and confirm the swatch tracks it.

## 6. Viewport path, flat color

- [ ] `IHLSLMaterialTranslator`: `GetInterface`, `GetHLSLMaterialHandle`, `UpdateHLSLMaterial`.
- [ ] `FormClay.fx` that transforms position and outputs Clay Color. No lighting yet.
- [ ] Default the material to hardware viewport shading.
- [ ] Confirm the mesh in the viewport is the clay color, and that a bad `.fx` reports `GetShaderErrorInformation()`.

## 7. The clay look

- [ ] View-space studio rig: wrap key, cool fill, soft rim.
- [ ] Cavity and Edge from `ddx` / `ddy`, with a slider each.
- [ ] Matcap Bitmap slot and Matcap Strength. Ignore non-Bitmap maps.
- [ ] `GetTransparencyHint` returns false. Required streams are position and normal only.

## 8. Done when

- [ ] Orbiting does not swing the key light.
- [ ] Cavity and Edge change the read on a creased mesh with no UVs.
- [ ] A matcap bitmap mixes in, and clearing the slot returns to clay.
- [ ] Rebuild after a shader edit, restart Max, and the new effect loads.
