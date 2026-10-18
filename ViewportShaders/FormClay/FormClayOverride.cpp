#include "FormClayOverride.h"

#include "max.h"
#include "GetCOREInterface.h"
#include "notify.h"
#include "ISceneEventManager.h"
#include "ref.h"
#include "sceneapi.h"

#include <memory>
#include <unordered_map>
#include <utility>

extern void RefreshFormClayOverrideCheckbox();

namespace {

// Holds the material a node had before the override, so a later save still writes that material.
struct StoredOriginal {
    TypedSingleRefMaker<Mtl> ref{ SingleRefMaker::PrePluginUnload };
    bool had = false;
};

bool IsGeometry(INode* node)
{
    if (!node || node->IsRootNode())
        return false;
    Object* object = node->GetObjectRef();
    if (!object)
        return false;
    Object* base = object->FindBaseObject();
    return base && base->SuperClassID() == GEOMOBJECT_CLASS_ID;
}

// One switch for the whole scene. Nodes keep their real materials in mOriginals.
class OverrideState : public INodeEventCallback {
public:
    bool Enabled() const { return mEnabled; }

    void SetEnabled(bool on, Mtl* source)
    {
        if (mShutdown)
            return;
        if (on) {
            if (!source)
                return;
            if (mEnabled && mClay.Get() == source)
                return;
            if (mEnabled)
                TurnOff();
            mClay.Set(source);
            mEnabled = true;
            Register();
            {
                QuietEdit quiet(mApplying);
                ApplyToScene();
            }
            Redraw();
        }
        else {
            TurnOff();
        }
        RefreshFormClayOverrideCheckbox();
    }

    void Shutdown()
    {
        if (mShutdown)
            return;
        mShutdown = true;
        TurnOff();
        Unregister();
    }

    void Added(NodeKeyTab& nodes) override
    {
        if (!mEnabled || mSuspendCount || mApplying)
            return;
        {
            QuietEdit quiet(mApplying);
            for (int i = 0; i < nodes.Count(); ++i) {
                if (INode* node = NodeEventNamespace::GetNodeByKey(nodes[i]))
                    CaptureAndAssign(node);
            }
        }
        Redraw();
    }

    void Deleted(NodeKeyTab& nodes) override
    {
        for (int i = 0; i < nodes.Count(); ++i)
            mOriginals.erase(nodes[i]);
    }

    // A material dropped on an object while the override is on is remembered, then clay is put back.
    void MaterialStructured(NodeKeyTab& nodes) override
    {
        if (!mEnabled || mSuspendCount || mApplying)
            return;
        Mtl* clay = mClay.Get();
        if (!clay) {
            TurnOff();
            RefreshFormClayOverrideCheckbox();
            return;
        }

        bool changed = false;
        {
        QuietEdit quiet(mApplying);
        for (int i = 0; i < nodes.Count(); ++i) {
            INode* node = NodeEventNamespace::GetNodeByKey(nodes[i]);
            if (!IsGeometry(node))
                continue;
            Mtl* now = node->GetMtl();
            if (now == clay)
                continue;
            auto& stored = mOriginals[nodes[i]];
            if (!stored)
                stored = std::make_unique<StoredOriginal>();
            stored->had = now != nullptr;
            stored->ref.Set(now);
            node->SetMtl(clay);
            changed = true;
        }
        }
        if (changed)
            Redraw();
    }

    BOOL VerboseDeleted() override { return TRUE; }

    void OnNotify(NotifyCode code)
    {
        switch (code) {
        case NOTIFY_FILE_PRE_SAVE:
        case NOTIFY_PRE_RENDER:
            ReleaseToScene();
            break;
        case NOTIFY_FILE_POST_SAVE:
        case NOTIFY_POST_RENDER:
            ReclaimFromScene();
            break;
        case NOTIFY_SYSTEM_PRE_NEW:
        case NOTIFY_SYSTEM_PRE_RESET:
        case NOTIFY_FILE_PRE_OPEN:
            TurnOff();
            RefreshFormClayOverrideCheckbox();
            break;
        default:
            break;
        }
    }

    void OnSourceDeleted()
    {
        if (mShutdown || mTurningOff)
            return;
        TurnOff();
        RefreshFormClayOverrideCheckbox();
    }

private:
    class ClayHold : public TypedSingleRefMaker<Mtl> {
    public:
        explicit ClayHold(OverrideState* owner)
            : TypedSingleRefMaker<Mtl>(SingleRefMaker::PrePluginUnload, false, true)
            , mOwner(owner)
        {
        }

        RefResult NotifyRefChanged(const Interval& changeInt, RefTargetHandle hTarget,
            PartID& partID, RefMessage message, BOOL propagate) override
        {
            const bool gone = message == REFMSG_TARGET_DELETED;
            const RefResult result = TypedSingleRefMaker<Mtl>::NotifyRefChanged(
                changeInt, hTarget, partID, message, propagate);
            if (gone && mOwner)
                mOwner->OnSourceDeleted();
            return result;
        }

        void ClearOwner() { mOwner = nullptr; }

    private:
        OverrideState* mOwner;
    };

    // Undo, macro recorder, animation keys, and the scene-dirty flag stay as they were.
    struct QuietEdit {
        SuspendAll suspend;
        int& depth;
        QuietEdit(int& depthIn)
            : suspend(true, true, true, true, false, true, false, true)
            , depth(depthIn)
        {
            ++depth;
        }
        ~QuietEdit() { --depth; }
    };

    void TurnOff()
    {
        if (mTurningOff)
            return;
        mTurningOff = true;
        if (mEnabled || !mOriginals.empty()) {
            QuietEdit quiet(mApplying);
            RestoreOriginals();
            mOriginals.clear();
        }
        mClay.Set(nullptr);
        mEnabled = false;
        mSuspendCount = 0;
        mTurningOff = false;
        if (!mShutdown)
            Redraw();
    }

    void CaptureAndAssign(INode* node)
    {
        if (!IsGeometry(node))
            return;
        Mtl* clay = mClay.Get();
        if (!clay || node->GetMtl() == clay)
            return;

        const AnimHandle key = NodeEventNamespace::GetKeyByNode(node);
        if (key == 0)
            return;
        if (mOriginals.find(key) == mOriginals.end()) {
            auto stored = std::make_unique<StoredOriginal>();
            Mtl* previous = node->GetMtl();
            stored->had = previous != nullptr;
            if (previous)
                stored->ref.Set(previous);
            mOriginals.emplace(key, std::move(stored));
        }
        node->SetMtl(clay);
    }

    void ApplyToScene()
    {
        Interface7* ip = GetCOREInterface7();
        if (!ip || !ip->GetScene())
            return;

        class Proc : public ITreeEnumProc {
        public:
            explicit Proc(OverrideState* owner) : mOwner(owner) {}
            int callback(INode* node) override
            {
                mOwner->CaptureAndAssign(node);
                return TREE_CONTINUE;
            }
        private:
            OverrideState* mOwner;
        } proc(this);

        ip->GetScene()->EnumTree(&proc, true, true);
    }

    void RestoreOriginals()
    {
        for (auto& entry : mOriginals) {
            INode* node = NodeEventNamespace::GetNodeByKey(entry.first);
            if (!node)
                continue;
            StoredOriginal& stored = *entry.second;
            Mtl* previous = stored.had ? stored.ref.Get() : nullptr;
            if (node->GetMtl() != previous)
                node->SetMtl(previous);
        }
    }

    void ReapplyClay()
    {
        Mtl* clay = mClay.Get();
        if (!clay)
            return;
        for (auto& entry : mOriginals) {
            INode* node = NodeEventNamespace::GetNodeByKey(entry.first);
            if (node && node->GetMtl() != clay)
                node->SetMtl(clay);
        }
    }

    // Save and render need the real materials. The override comes back afterwards.
    void ReleaseToScene()
    {
        if (!mEnabled)
            return;
        if (mSuspendCount == 0) {
            QuietEdit quiet(mApplying);
            RestoreOriginals();
        }
        ++mSuspendCount;
    }

    void ReclaimFromScene()
    {
        if (!mEnabled || mSuspendCount == 0)
            return;
        --mSuspendCount;
        if (mSuspendCount > 0)
            return;
        {
            QuietEdit quiet(mApplying);
            ReapplyClay();
        }
        Redraw();
    }

    void Redraw()
    {
        if (mShutdown)
            return;
        if (Interface* ip = GetCOREInterface())
            ip->RedrawViews(ip->GetTime());
    }

    void Register()
    {
        if (mCallbackKey == 0) {
            if (ISceneEventManager* manager = GetISceneEventManager())
                mCallbackKey = manager->RegisterCallback(this, FALSE, 0, FALSE);
        }
        if (!mNotes) {
            RegisterNotification(NotifyThunk, this, NOTIFY_FILE_PRE_SAVE);
            RegisterNotification(NotifyThunk, this, NOTIFY_FILE_POST_SAVE);
            RegisterNotification(NotifyThunk, this, NOTIFY_PRE_RENDER);
            RegisterNotification(NotifyThunk, this, NOTIFY_POST_RENDER);
            RegisterNotification(NotifyThunk, this, NOTIFY_SYSTEM_PRE_NEW);
            RegisterNotification(NotifyThunk, this, NOTIFY_SYSTEM_PRE_RESET);
            RegisterNotification(NotifyThunk, this, NOTIFY_FILE_PRE_OPEN);
            mNotes = true;
        }
    }

    void Unregister()
    {
        if (mCallbackKey != 0) {
            if (ISceneEventManager* manager = GetISceneEventManager())
                manager->UnRegisterCallback(mCallbackKey);
            mCallbackKey = 0;
        }
        if (mNotes) {
            UnRegisterNotification(NotifyThunk, this);
            mNotes = false;
        }
    }

    static void NotifyThunk(void* param, NotifyInfo* info)
    {
        if (param && info)
            static_cast<OverrideState*>(param)->OnNotify(info->intcode);
    }

public:
    OverrideState()
        : mClay(this)
    {
    }

    ~OverrideState()
    {
        mClay.ClearOwner();
    }

private:

    ClayHold mClay;
    std::unordered_map<AnimHandle, std::unique_ptr<StoredOriginal>> mOriginals;
    SceneEventNamespace::CallbackKey mCallbackKey = 0;
    int mApplying = 0;
    int mSuspendCount = 0;
    bool mEnabled = false;
    bool mNotes = false;
    bool mTurningOff = false;
    bool mShutdown = false;
};

OverrideState* gState = nullptr;

OverrideState& State()
{
    if (!gState)
        gState = new OverrideState();
    return *gState;
}

} // namespace

bool FormClayViewportOverrideEnabled()
{
    return gState && gState->Enabled();
}

void FormClaySetViewportOverride(bool on, Mtl* source)
{
    State().SetEnabled(on, source);
}

void FormClayShutdownViewportOverride()
{
    if (!gState)
        return;
    gState->Shutdown();
    delete gState;
    gState = nullptr;
}
