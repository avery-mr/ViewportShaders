# Form Clay

A 3ds Max 2027 viewport material for reading form while modeling. The light stays fixed to the camera, and creases show up from the mesh normals, with no UVs and no baked map.

It is a modeler's viewport material. Scanline renders a flat clay color. Arnold and V-Ray do not reproduce the viewport.

## Controls

| Control | Default | What it does |
| --- | --- | --- |
| Clay Color | 0.76, 0.73, 0.68 | Base color |
| Wrap | 0.4 | How far the key light wraps past the terminator |
| Cavity | 0.6 | Darken recesses |
| Edge | 0.35 | Lighten ridges |
| Matcap | empty Bitmap | A picture of a lit sphere, sampled by the view-space normal |
| Matcap Strength | 0 | Mix of that picture over the clay. 0 leaves the clay unchanged |

## Build

- 3ds Max 2027, and the matching SDK
- Platform toolset **v143**, MSVC **14.38 (17.8)**
- Windows 10 SDK **10.0.19041.0**
- **Hybrid | x64**

Open `ViewportShaders/ViewportShaders.slnx` and build the FormClay project. The project imports the Max SDK property sheets. `MaxSDK` in `FormClay.vcxproj` points at `C:\Program Files\Autodesk\3ds Max 2027 SDK\maxsdk`. Change that path if the SDK lives somewhere else.

The build writes `FormClay.dlu` and copies `FormClay.fx` beside it, into `ViewportShaders/plugins/`. That folder is build output and is not committed. Max only loads the effect if the `.fx` file is in the same folder as the `.dlu`.

## Load in Max

Add `ViewportShaders/plugins` under **Customize → Configure System Paths → 3rd Party Plugins**, one path per line in `Plugin.UserSettings.ini`. Restart Max.

Form Clay is under **Materials → Viewport Shaders** in the Material/Map Browser.
