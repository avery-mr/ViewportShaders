#include "max.h"
#include "iparamm2.h"
#include "stdmat.h"
#include "IMaterialBrowserEntryInfo.h"
#include "IMtlRender_Compatibility.h"
#include <Graphics/IHLSLMaterialTranslator.h>
#include <Graphics/TextureHandleUtility.h>
#include "dbgprint.h"
#include "resource.h"
#include "FormClayOverride.h"

#include <algorithm>
#include <vector>

extern HINSTANCE hInstance;

// Two numbers, unique among every plug-in Max loads.
// Scenes store this pair, so it stays fixed even if the display name changes.
#define FORMCLAY_CLASS_ID Class_ID(0xddd027c5, 0x917bf4df)

ClassDesc2* GetFormClayDesc();

enum { formclay_params };
enum { clay_color, wrap_amount, cavity_amount, edge_amount, matcap_map, matcap_strength };
static const int kRefPblock = 0;

// The effect and the shipped matcaps sit beside the DLL.
static MSTR FormClayPlugInDir()
{
    TCHAR moduleFile[MAX_PATH] = {};
    GetModuleFileName(hInstance, moduleFile, MAX_PATH);
    MSTR path(moduleFile);
    const int slash = path.last(_T('\\'));
    if (slash >= 0)
        path.remove(slash + 1);
    return path;
}

static MSTR FormClayFxPath()
{
    return FormClayPlugInDir() + _T("FormClay.fx");
}

static MSTR MatcapDirectory()
{
    return FormClayPlugInDir() + _T("matcaps\\");
}

class FormClayMtl : public Mtl, public MaxSDK::Graphics::IHLSLMaterialTranslator
{
    IParamBlock2* pblock;
    MaxSDK::Graphics::HLSLMaterialHandle mHLSL;
    MaxSDK::Graphics::TextureHandle mMatcap;
    MaxSDK::Graphics::TextureHandle mNoMatcap;
    Texmap* mMatcapSource;
    MSTR mMatcapFile;
    MSTR mShaderError;
    bool mShaderLoaded;
    bool mMatcapDirty;

    void LoadViewportShader()
    {
        mShaderLoaded = true;
        MaxSDK::Graphics::HLSLMaterialHandle handle;
        const bool compiled = handle.InitializeWithFile(FormClayFxPath());
        mHLSL = handle;
        if (!compiled)
        {
            mShaderError = mHLSL.GetShaderErrorInformation();
            DebugOutputString(mShaderError.data());
            return;
        }
        mHLSL.SetActiveTechniqueName(_M("FormClay"));
        mShaderError = MSTR();
    }

    // One read path for the stored color. Time 0 is the getters, which have no time argument.
    Color ClayColor(TimeValue t) const
    {
        if (pblock)
            return pblock->GetColor(clay_color, t);
        return Color(0.76f, 0.73f, 0.68f);
    }

    float WrapAmount(TimeValue t) const
    {
        if (pblock)
            return pblock->GetFloat(wrap_amount, t);
        return 0.4f;
    }

    float CavityAmount(TimeValue t) const
    {
        if (pblock)
            return pblock->GetFloat(cavity_amount, t);
        return 0.6f;
    }

    float EdgeAmount(TimeValue t) const
    {
        if (pblock)
            return pblock->GetFloat(edge_amount, t);
        return 0.35f;
    }

    float MatcapAmount(TimeValue t) const
    {
        if (pblock)
            return pblock->GetFloat(matcap_strength, t);
        return 0.0f;
    }

    void EnsureNoMatcap()
    {
        if (mNoMatcap.IsValid())
            return;
        if (!mNoMatcap.Initialize(1, 1, MaxSDK::Graphics::TargetFormatA8R8G8B8))
            return;
        MaxSDK::Graphics::LockedRect rect;
        if (mNoMatcap.WriteOnlyLockRectangle(0, rect, nullptr) && rect.pBits)
        {
            *reinterpret_cast<DWORD*>(rect.pBits) = 0xffffffff;
            mNoMatcap.WriteOnlyUnlockRectangle();
        }
    }

    // A Bitmap with a file is the only map the effect samples. Anything else leaves the clay alone.
    bool PrepareMatcap(TimeValue t, MaxSDK::Graphics::HLSLMaterialHandle& handle)
    {
        EnsureNoMatcap();
        Texmap* map = pblock ? pblock->GetTexmap(matcap_map, t) : nullptr;
        BitmapTex* bitmap = map ? GetIBitmapTextInterface(map) : nullptr;
        const MCHAR* file = (bitmap && bitmap->GetMapName()) ? bitmap->GetMapName() : _T("");

        if (!bitmap || file[0] == 0)
        {
            mMatcapSource = nullptr;
            mMatcapFile = MSTR();
            mMatcapDirty = false;
            handle.SetBoolParameter(_M("UseMatcap"), FALSE);
            if (mNoMatcap.IsValid())
                handle.SetTextureParameter(_M("MatcapTex"), mNoMatcap);
            return true;
        }

        if (mMatcapDirty || mMatcapSource != bitmap || mMatcapFile != file)
        {
            mMatcap = MaxSDK::Graphics::TextureHandleUtility::CreateTextureHandle(bitmap, t);
            mMatcapSource = bitmap;
            mMatcapFile = file;
            mMatcapDirty = false;
        }

        const bool ready = mMatcap.IsValid();
        handle.SetBoolParameter(_M("UseMatcap"), ready ? TRUE : FALSE);
        handle.SetTextureParameter(_M("MatcapTex"), ready ? mMatcap : mNoMatcap);
        return true;
    }

public:
    FormClayMtl(BOOL loading)
        : pblock(nullptr)
        , mMatcapSource(nullptr)
        , mShaderLoaded(false)
        , mMatcapDirty(true)
    {
        // Nitrous draws MTL_HW_MAT_ENABLED materials with the HLSL effect.
        SetMtlFlag(MTL_HW_MAT_ENABLED | MTL_HW_TEX_ENABLED);
        // A scene load installs the saved block itself. Building another would leak.
        if (!loading)
            Reset();
    }

    Class_ID ClassID() override { return FORMCLAY_CLASS_ID; }

    // Max calls this instead of delete.
    void DeleteThis() override { delete this; }

    // The material browser drops its sample sphere onto the Slate canvas by cloning it.
    RefTargetHandle Clone(RemapDir& remap) override
    {
        FormClayMtl* copy = new FormClayMtl(FALSE);
        *((MtlBase*)copy) = *((MtlBase*)this);
        copy->ReplaceReference(kRefPblock, remap.CloneRef(pblock));
        BaseClone(this, copy, remap);
        return copy;
    }

    void GetClassName(MSTR& s, bool /*localized*/) const override { s = _M("Form Clay"); }

    RefResult NotifyRefChanged(const Interval& /*changeInt*/, RefTargetHandle hTarget,
        PartID& /*partID*/, RefMessage message, BOOL /*propagate*/) override
    {
        if (message == REFMSG_CHANGE && hTarget == pblock && pblock->LastNotifyParamID() == matcap_map)
            mMatcapDirty = true;
        return REF_SUCCEED;
    }

    void Update(TimeValue /*t*/, Interval& /*valid*/) override {}

    // P_AUTO_CONSTRUCT installs the new block as reference kRefPblock.
    void Reset() override
    {
        GetFormClayDesc()->MakeAutoParamBlocks(this);
    }

    Interval Validity(TimeValue /*t*/) override { return FOREVER; }

    // P_AUTO_UI builds the rollout from the dialog in FormClay.rc.
    ParamDlg* CreateParamDlg(HWND hwMtlEdit, IMtlParams* imp) override
    {
        return GetFormClayDesc()->CreateParamDlgs(hwMtlEdit, imp, this);
    }

    int NumRefs() override { return 1; }
    RefTargetHandle GetReference(int i) override
    {
        return (i == kRefPblock) ? pblock : nullptr;
    }
    void SetReference(int i, RefTargetHandle rtarg) override
    {
        if (i == kRefPblock)
            pblock = static_cast<IParamBlock2*>(rtarg);
    }

    int NumParamBlocks() override { return 1; }
    IParamBlock2* GetParamBlock(int i) override
    {
        return (i == 0) ? pblock : nullptr;
    }
    IParamBlock2* GetParamBlockByID(BlockID id) override
    {
        return (pblock && pblock->ID() == id) ? pblock : nullptr;
    }

    // Opaque. The viewport can skip transparency sorting.
    BOOL GetTransparencyHint(TimeValue /*t*/, Interval& valid) override
    {
        valid = FOREVER;
        return FALSE;
    }

    // Max asks the material for IHLSL_MATERIAL_TRANSLATOR_INTERFACE_ID, then calls these two.
    BaseInterface* GetInterface(Interface_ID id) override
    {
        if (id == IHLSL_MATERIAL_TRANSLATOR_INTERFACE_ID)
            return static_cast<MaxSDK::Graphics::IHLSLMaterialTranslator*>(this);
        return Mtl::GetInterface(id);
    }

    const MaxSDK::Graphics::HLSLMaterialHandle& GetHLSLMaterialHandle(
        MaxSDK::Graphics::GraphicFeatureLevel /*featureLevel*/) override
    {
        if (!mShaderLoaded)
            LoadViewportShader();
        return mHLSL;
    }

    bool UpdateHLSLMaterial(const TimeValue t,
        MaxSDK::Graphics::GraphicFeatureLevel featureLevel) override
    {
        MaxSDK::Graphics::HLSLMaterialHandle handle = GetHLSLMaterialHandle(featureLevel);
        if (mShaderError.Length() > 0)
            return false;
        const Color c = ClayColor(t);
        handle.SetFloat4Parameter(_M("ClayColor"), Point4(c.r, c.g, c.b, 1.0f));
        handle.SetFloatParameter(_M("Wrap"), WrapAmount(t));
        handle.SetFloatParameter(_M("Cavity"), CavityAmount(t));
        handle.SetFloatParameter(_M("Edge"), EdgeAmount(t));
        handle.SetFloatParameter(_M("MatcapStrength"), MatcapAmount(t));
        PrepareMatcap(t, handle);
        return true;
    }

    // Class Mtl requires these. Shade is the Scanline path. The viewport uses the effect.
    Color GetAmbient(int /*mtlNum*/, BOOL /*backFace*/) override { return ClayColor(0); }
    Color GetDiffuse(int /*mtlNum*/, BOOL /*backFace*/) override { return ClayColor(0); }
    Color GetSpecular(int /*mtlNum*/, BOOL /*backFace*/) override { return Color(0.2f, 0.2f, 0.2f); }
    float GetShininess(int /*mtlNum*/, BOOL /*backFace*/) override { return 0.0f; }
    float GetShinStr(int /*mtlNum*/, BOOL /*backFace*/) override { return 0.0f; }
    float GetXParency(int /*mtlNum*/, BOOL /*backFace*/) override { return 0.0f; }

    void SetAmbient(Color /*c*/, TimeValue /*t*/) override {}
    void SetDiffuse(Color /*c*/, TimeValue /*t*/) override {}
    void SetSpecular(Color /*c*/, TimeValue /*t*/) override {}
    void SetShininess(float /*v*/, TimeValue /*t*/) override {}

    int NumSubTexmaps() override { return 1; }
    Texmap* GetSubTexmap(int i) override
    {
        if (i == 0 && pblock)
            return pblock->GetTexmap(matcap_map);
        return nullptr;
    }
    void SetSubTexmap(int i, Texmap* m) override
    {
        if (i == 0 && pblock)
        {
            pblock->SetValue(matcap_map, 0, m);
            mMatcapDirty = true;
        }
    }
    MSTR GetSubTexmapSlotName(int i, bool /*localized*/) override
    {
        return (i == 0) ? MSTR(_M("Matcap")) : MSTR();
    }

    void Shade(ShadeContext& sc) override
    {
        sc.out.c = ClayColor(sc.CurTime());
        sc.out.t = Color(0.0f, 0.0f, 0.0f);
    }
};

class FormClayClassDesc
    : public ClassDesc2
    , public IMaterialBrowserEntryInfo
    , public IMtlRender_Compatibility_MtlBase
{
public:
    FormClayClassDesc()
    {
        IMtlRender_Compatibility_MtlBase::Init(*this);
    }

    // The Slate canvas refuses a drop the active renderer has not accepted.
    bool IsCompatibleWithRenderer(ClassDesc& /*rendererClassDesc*/) override { return true; }

    int IsPublic() override { return TRUE; }
    void* Create(BOOL loading) override { return new FormClayMtl(loading); }
    const TCHAR* ClassName() override { return _T("Form Clay"); }
    const TCHAR* NonLocalizedClassName() override { return _T("Form Clay"); }
    SClass_ID SuperClassID() override { return MATERIAL_CLASS_ID; }
    Class_ID ClassID() override { return FORMCLAY_CLASS_ID; }
    const TCHAR* Category() override { return _T("Viewport Shaders"); }
    const TCHAR* InternalName() override { return _T("FormClay"); }
    HINSTANCE HInstance() override { return hInstance; }

    FPInterface* GetInterface(Interface_ID id) override
    {
        if (id == IMATERIAL_BROWSER_ENTRY_INFO_INTERFACE)
            return static_cast<IMaterialBrowserEntryInfo*>(this);
        return ClassDesc2::GetInterface(id);
    }

    // The material browser reads these. Category() does not place the entry.
    const MCHAR* GetEntryName() const override { return _T("Form Clay"); }
    const MCHAR* GetEntryCategory() const override { return _T("Materials\\Viewport Shaders"); }
    Bitmap* GetEntryThumbnail() const override { return nullptr; }
};

static FormClayClassDesc gFormClayDesc;

ClassDesc2* GetFormClayDesc()
{
    return &gFormClayDesc;
}

// The preset list is not a saved parameter. It writes the Matcap Bitmap slot.
struct MatcapPreset
{
    bool custom = false;
    MSTR path;
};

static bool IsMatcapFile(const MCHAR* name)
{
    const MCHAR* dot = _tcsrchr(name, _T('.'));
    if (!dot)
        return false;
    static const MCHAR* kExt[] = {
        _T(".png"), _T(".jpg"), _T(".jpeg"), _T(".bmp"),
        _T(".tga"), _T(".tif"), _T(".tiff"), _T(".exr"),
        _T(".hdr"), _T(".dds")
    };
    for (const MCHAR* ext : kExt)
    {
        if (_tcsicmp(dot, ext) == 0)
            return true;
    }
    return false;
}

static bool SameMatcapFile(const MCHAR* assigned, const MSTR& preset)
{
    if (_tcsicmp(assigned, preset) == 0)
        return true;
    const MCHAR* assignedName = _tcsrchr(assigned, _T('\\'));
    const MCHAR* presetName = _tcsrchr(preset.data(), _T('\\'));
    assignedName = assignedName ? assignedName + 1 : assigned;
    presetName = presetName ? presetName + 1 : preset.data();
    return _tcsicmp(assignedName, presetName) == 0;
}

class FormClayDlgProc : public ParamMap2UserDlgProc
{
    HWND mHwnd = nullptr;
    IParamMap2* mMap = nullptr;
    bool mFilling = false;
    std::vector<HWND> mDialogs;
    std::vector<MatcapPreset> mPresets;

    void Fill(IParamBlock2* pb, TimeValue t)
    {
        if (!mHwnd)
            return;
        mFilling = true;

        mPresets.clear();
        std::vector<MSTR> files;
        const MSTR folder = MatcapDirectory();
        const MSTR pattern = folder + _T("*.*");
        WIN32_FIND_DATA data{};
        HANDLE find = FindFirstFile(pattern, &data);
        if (find != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    continue;
                if (!IsMatcapFile(data.cFileName))
                    continue;
                files.push_back(folder + data.cFileName);
            } while (FindNextFile(find, &data));
            FindClose(find);
        }
        std::sort(files.begin(), files.end(), [](const MSTR& a, const MSTR& b) {
            return _tcsicmp(a, b) < 0;
        });

        const MCHAR* assigned = _T("");
        Texmap* map = pb ? pb->GetTexmap(matcap_map, t) : nullptr;
        BitmapTex* bitmap = map ? GetIBitmapTextInterface(map) : nullptr;
        if (bitmap && bitmap->GetMapName())
            assigned = bitmap->GetMapName();

        int matched = -1;
        for (int i = 0; i < static_cast<int>(files.size()); ++i)
        {
            if (assigned[0] && SameMatcapFile(assigned, files[i]))
                matched = i;
        }

        HWND combo = GetDlgItem(mHwnd, IDC_MATCAP_PRESET);
        SendMessage(combo, CB_RESETCONTENT, 0, 0);

        int selection = 0;
        if (assigned[0] && matched < 0)
        {
            SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)_T("Custom"));
            mPresets.push_back({ true, MSTR() });
            selection = 0;
        }
        SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)_T("None"));
        mPresets.push_back({ false, MSTR() });
        if (!(assigned[0] && matched < 0))
            selection = static_cast<int>(mPresets.size()) - 1;

        for (int i = 0; i < static_cast<int>(files.size()); ++i)
        {
            const MCHAR* slash = _tcsrchr(files[i].data(), _T('\\'));
            const MCHAR* label = slash ? slash + 1 : files[i].data();
            SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)label);
            mPresets.push_back({ false, files[i] });
            if (i == matched)
                selection = static_cast<int>(mPresets.size()) - 1;
        }

        SendMessage(combo, CB_SETCURSEL, selection, 0);
        mFilling = false;
    }

    void Apply(IParamMap2* map, TimeValue t)
    {
        HWND combo = GetDlgItem(mHwnd, IDC_MATCAP_PRESET);
        const int sel = static_cast<int>(SendMessage(combo, CB_GETCURSEL, 0, 0));
        if (sel < 0 || sel >= static_cast<int>(mPresets.size()))
            return;
        const MatcapPreset& preset = mPresets[sel];
        if (preset.custom)
            return;

        IParamBlock2* pb = map ? map->GetParamBlock() : nullptr;
        if (!pb)
            return;
        if (preset.path.Length() == 0)
        {
            pb->SetValue(matcap_map, t, static_cast<Texmap*>(nullptr));
            return;
        }

        BitmapTex* bitmap = NewDefaultBitmapTex();
        bitmap->SetMapName(preset.path);
        pb->SetValue(matcap_map, t, static_cast<Texmap*>(bitmap));
    }

public:
    INT_PTR DlgProc(TimeValue t, IParamMap2* map, HWND hWnd, UINT msg, WPARAM wParam, LPARAM /*lParam*/) override
    {
        switch (msg)
        {
        case WM_INITDIALOG:
            mHwnd = hWnd;
            mMap = map;
            if (std::find(mDialogs.begin(), mDialogs.end(), hWnd) == mDialogs.end())
                mDialogs.push_back(hWnd);
            Fill(map ? map->GetParamBlock() : nullptr, t);
            RefreshOverride();
            return TRUE;
        case WM_DESTROY:
            mDialogs.erase(std::remove(mDialogs.begin(), mDialogs.end(), hWnd), mDialogs.end());
            if (mHwnd == hWnd)
                mHwnd = mDialogs.empty() ? nullptr : mDialogs.back();
            return FALSE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_VIEWPORT_OVERRIDE && HIWORD(wParam) == BN_CLICKED) {
                const bool on = IsDlgButtonChecked(hWnd, IDC_VIEWPORT_OVERRIDE) == BST_CHECKED;
                IParamBlock2* pb = map ? map->GetParamBlock() : nullptr;
                Mtl* source = pb ? dynamic_cast<Mtl*>(pb->GetOwner()) : nullptr;
                FormClaySetViewportOverride(on, source);
                RefreshOverride();
            }
            if (!mFilling && LOWORD(wParam) == IDC_MATCAP_PRESET && HIWORD(wParam) == CBN_SELCHANGE)
                Apply(map, t);
            return FALSE;
        }
        return FALSE;
    }

    void Update(TimeValue t) override
    {
        if (!mFilling && mMap)
            Fill(mMap->GetParamBlock(), t);
    }

    void DeleteThis() override {}

    void RefreshOverride()
    {
        const UINT state = FormClayViewportOverrideEnabled() ? BST_CHECKED : BST_UNCHECKED;
        for (auto it = mDialogs.begin(); it != mDialogs.end(); ) {
            if (!IsWindow(*it)) {
                it = mDialogs.erase(it);
                continue;
            }
            CheckDlgButton(*it, IDC_VIEWPORT_OVERRIDE, state);
            ++it;
        }
    }
};

static FormClayDlgProc gFormClayDlgProc;

void RefreshFormClayOverrideCheckbox()
{
    gFormClayDlgProc.RefreshOverride();
}

// Registered with the class descriptor at startup, so this stays below GetFormClayDesc.
// clay_color stays 0: scenes store the parameter id.
static ParamBlockDesc2 formclay_param_blk(
    formclay_params, _T("params"), 0, GetFormClayDesc(),
    P_AUTO_CONSTRUCT + P_AUTO_UI, kRefPblock,
    IDD_FORMCLAY, IDS_FORMCLAY_PARAMS, 0, 0, &gFormClayDlgProc,
    clay_color, _T("clayColor"), TYPE_RGBA, P_ANIMATABLE, IDS_CLAY_COLOR,
        p_default, Color(0.76f, 0.73f, 0.68f),
        p_ui, TYPE_COLORSWATCH, IDC_CLAY_COLOR,
        p_nonLocalizedName, _T("Clay Color"),
        p_end,
    wrap_amount, _T("wrap"), TYPE_FLOAT, P_ANIMATABLE, IDS_WRAP,
        p_default, 0.4f,
        p_range, 0.0f, 1.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_WRAP_EDIT, IDC_WRAP_SPIN, 0.01f,
        p_nonLocalizedName, _T("Wrap"),
        p_end,
    cavity_amount, _T("cavity"), TYPE_FLOAT, P_ANIMATABLE, IDS_CAVITY,
        p_default, 0.6f,
        p_range, 0.0f, 1.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_CAVITY_EDIT, IDC_CAVITY_SPIN, 0.01f,
        p_nonLocalizedName, _T("Cavity"),
        p_end,
    edge_amount, _T("edge"), TYPE_FLOAT, P_ANIMATABLE, IDS_EDGE,
        p_default, 0.35f,
        p_range, 0.0f, 1.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_EDGE_EDIT, IDC_EDGE_SPIN, 0.01f,
        p_nonLocalizedName, _T("Edge"),
        p_end,
    matcap_map, _T("matcap"), TYPE_TEXMAP, 0, IDS_MATCAP,
        p_subtexno, 0,
        p_ui, TYPE_TEXMAPBUTTON, IDC_MATCAP,
        p_nonLocalizedName, _T("Matcap"),
        p_end,
    matcap_strength, _T("matcapStrength"), TYPE_FLOAT, P_ANIMATABLE, IDS_MATCAP_STRENGTH,
        p_default, 0.0f,
        p_range, 0.0f, 1.0f,
        p_ui, TYPE_SPINNER, EDITTYPE_FLOAT, IDC_MATCAP_STR_EDIT, IDC_MATCAP_STR_SPIN, 0.01f,
        p_nonLocalizedName, _T("Matcap Strength"),
        p_end,
    p_end);