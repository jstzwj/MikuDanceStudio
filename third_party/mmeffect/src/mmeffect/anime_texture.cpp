// anime_texture.cpp - see anime_texture.h
//
// Port shape (FUN_180001320 tick + CAnimeGIF/CAnimeTex decode, evidence in
// anime_texture.h): the original keeps keyed instance containers under the
// ctx+0x48 sub-object and prunes them in edit mode; the runtime-visible
// behavior this port reproduces is the per-frame register/advance/decode/
// SetTexture cycle with the FUN_180005360 direct-into-locked-texture GDI+
// draw and the FUN_180005500 D3DXCreateTexture(DYNAMIC, A8R8G8B8, DEFAULT).
#include "anime_texture.h"

#include <cstring>
#include <string>
#include <vector>

#include "effect_engine.h"
#include "mme_globals.h"
#include "mme_log.h"
#include "mme_ui.h"
#include "mme_util.h"
#include "sas_exec.h"       // SasEffect / SasResource definitions
#include "shlwapi.h"

// GDI+ (the flat API). windows.h is already included through mme_globals.h,
// which defines the min/max macros the GDI+ headers do not expect.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <gdiplus.h>

// [toolchain divergence] the June 2010 SDK declared the GDI+ flat API (GpImage,
// GdipLoadImageFromFile, ...) at global scope; the modern Windows SDK wraps it
// in namespace Gdiplus (+ the flat functions in Gdiplus::DllExports). Pull them
// back in for the flat-API style of the original.
using namespace Gdiplus;
using namespace Gdiplus::DllExports;

#include "mme_context.h"    // MmeContext (ctx->device / animatedTextures)
#include "mmhack_api.h"     // IsEditMode

namespace mme {

namespace {

// FrameDimensionTime ({6aedbd6d-3fb5-418a-83a6-7f45229dc872}) - the dimension
// GUID FUN_180004810 passes to GdipImageGetFrameDimensionsList/FrameCount and
// FUN_180005360 passes to GdipImageSelectActiveFrame (DAT_1800aa820).
const GUID kFrameDimensionTime =
    { 0x6aedbd6d, 0x3fb5, 0x418a, { 0x83, 0xa6, 0x7f, 0x45, 0x22, 0x9d, 0xc8, 0x72 } };

// PropertyTagFrameDelay [big-C 5533]: the per-frame delay array in
// centiseconds; the original stores value * 10 (milliseconds).
const PROPID kPropertyTagFrameDelay = 0x5100;

// One animated texture bound to an effect parameter (the CAnimeTex instance).
struct AnimeEntry {
    ID3DXEffect* effect = nullptr;      // owning effect (borrowed)
    D3DXHANDLE   param = nullptr;       // the texture parameter
    std::string  effectPath;            // owning effect file (identity key)
    std::string  resourcePath;          // resolved absolute image path
    float        offset = 0.0f;         // "Offset" (seconds)
    float        speed = 1.0f;          // "Speed" (multiplier)
    std::string  seekVariable;          // "SeekVariable" ("" = frame time)
    bool         syncInEditMode = false;// "SyncInEditMode"
    // GDI+ decode state (the CAnimeGIF/CAnimePNG object).
    GpImage* image = nullptr;
    unsigned int frameCount = 0;
    std::vector<unsigned int> delays;   // ms per frame (PropertyTagFrameDelay*10)
    // Runtime state.
    IDirect3DTexture9* texture = nullptr;   // the DYNAMIC A8R8G8B8 upload target
    int    lastFrame = -1;
    double animTime = 0.0;              // accumulated playback seconds
    double lastSeek = 0.0;              // last SeekVariable value
};

// The ctx+0x48 collection impl (FUN_180001320's containers).
struct AnimeSetImpl {
    std::vector<AnimeEntry*> entries;
    std::vector<std::string> scannedEffects;   // effect paths already scanned
};

// [FUN_1800675e0 / big-C 81473] exe-relative path normalization through
// PathRelativePathToA. Returns the path relative to `fromDir` when the path
// lives inside it (leading ".\" stripped), otherwise the input unchanged.
std::string MakeExeRelativePath(const std::string& fromDir, const std::string& path)
{
    if (fromDir.empty() || path.empty() || path.size() >= MAX_PATH) {
        return path;
    }
    char rel[MAX_PATH];
    memset(rel, 0, sizeof(rel));
    // 0x10 = FILE_ATTRIBUTE_DIRECTORY for the base, 0x80 = FILE_ATTRIBUTE_NORMAL
    // for the target - the exact attribute pair of the original call.
    if (!PathRelativePathToA(rel, fromDir.c_str(), 0x10, path.c_str(), 0x80)) {
        return path;
    }
    std::string result(rel);
    // [big-C 81682-81700] strip the ".\" / ".\" leading segment.
    if (result.size() >= 2 && result[0] == '.' &&
        (result[1] == '\\' || result[1] == '/')) {
        result = result.substr(2);
    } else if (!result.empty() && (result[0] == '\\' || result[0] == '/')) {
        result = result.substr(1);
    }
    return result;
}

// Resolve the ResourceName: (1) beside the effect file (REFERENCE.txt: the
// effect file's folder is the base for relative paths), (2) as-is (absolute
// or the process CWD), (3) the exe directory. The absolute result is
// normalized exe-relative like the original stores it (FUN_1800675e0), then
// resolved back against the exe dir.
std::string ResolveResourcePath(const std::string& effectPath,
                                const std::string& resourceName)
{
    if (resourceName.empty()) {
        return std::string();
    }
    // Candidate 1: <effect dir>\<resource>.
    std::string effectDir;
    {
        char drive[3] = { 0 };
        char dir[0x100] = { 0 };
        if (_splitpath_s(effectPath.c_str(), drive, sizeof(drive), dir, sizeof(dir),
                         nullptr, 0, nullptr, 0) == 0) {
            char full[0x104];
            if (_makepath_s(full, sizeof(full), drive, dir, nullptr, nullptr) == 0) {
                effectDir = full;
            }
        }
    }
    if (!effectDir.empty()) {
        std::string candidate = effectDir;
        if (!candidate.empty() && candidate[candidate.size() - 1] != '\\') {
            candidate += "\\";
        }
        candidate += resourceName;
        if (MmeEngineQueryFileStamp(candidate) != 0) {
            return MakeExeRelativePath(g_exeDir, candidate);
        }
    }
    // Candidate 2: as-is.
    if (MmeEngineQueryFileStamp(resourceName) != 0) {
        return MakeExeRelativePath(g_exeDir, resourceName);
    }
    // Candidate 3: exe dir.
    if (!g_exeDir.empty()) {
        std::string candidate = g_exeDir;
        if (!candidate.empty() && candidate[candidate.size() - 1] != '\\') {
            candidate += "\\";
        }
        candidate += resourceName;
        if (MmeEngineQueryFileStamp(candidate) != 0) {
            return MakeExeRelativePath(g_exeDir, candidate);
        }
    }
    return std::string();
}

// Load the image + frame table (the CAnimeGIF ctor FUN_180004810 decode /
// the CAnimePNG stream loader approximation - see PHASE4 notes).
void LoadImageForEntry(AnimeEntry* entry)
{
    entry->image = nullptr;
    entry->frameCount = 0;
    entry->delays.clear();
    if (entry->resourcePath.empty()) {
        return;
    }
    std::wstring wide = MmeAnsiToWide(entry->resourcePath.c_str());
    GpImage* image = nullptr;
    // [FUN_180004810 / CAnimePNG] the original loads the image through an
    // OLE memory stream (ole32!CreateStreamOnHGlobal is imported for this):
    // read the file into HGLOBAL -> IStream -> GdipLoadImageFromStream.
    {
        void* fileHandle = CreateFileA(entry->resourcePath.c_str(), GENERIC_READ,
                                       FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                       FILE_ATTRIBUTE_NORMAL, nullptr);
        if (fileHandle != INVALID_HANDLE_VALUE) {
            DWORD high = 0;
            DWORD size = GetFileSize(fileHandle, &high);
            if (size != INVALID_FILE_SIZE && size > 0) {
                HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
                if (memory != nullptr) {
                    void* bytes = GlobalLock(memory);
                    DWORD read = 0;
                    if (bytes != nullptr && ReadFile(fileHandle, bytes, size, &read, nullptr) &&
                        read == size) {
                        GlobalUnlock(memory);
                        IStream* stream = nullptr;
                        if (SUCCEEDED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
                            memory = nullptr;   // the stream owns it now
                            GdipLoadImageFromStream(stream, &image);
                            stream->Release();
                        }
                    } else {
                        GlobalUnlock(memory);
                    }
                    if (memory != nullptr) {
                        GlobalFree(memory);
                    }
                }
            }
            CloseHandle(fileHandle);
        }
    }
    GpStatus status = GdipLoadImageFromFile(wide.c_str(), &image);
    if (status != Ok || image == nullptr) {
        // [0x1800b25e8] "failed to load '" - the original reports through the
        // CAnime ctor status; the tick simply keeps the entry unloaded.
        return;
    }
    entry->image = image;

    GUID dimensionId = kFrameDimensionTime;
    unsigned int count = 0;
    if (GdipImageGetFrameDimensionsList(image, &dimensionId, 1) == Ok &&
        GdipImageGetFrameCount(image, &dimensionId,
                               reinterpret_cast<UINT*>(&count)) == Ok) {
        entry->frameCount = count;
    }
    if (entry->frameCount < 1) {
        entry->frameCount = 1;
    }
    // PropertyTagFrameDelay -> ms per frame (value * 10) [big-C 5552].
    if (entry->frameCount > 1) {
        UINT size = 0;
        if (GdipGetPropertyItemSize(image, kPropertyTagFrameDelay, &size) == Ok &&
            size >= sizeof(PropertyItem)) {
            PropertyItem* item = static_cast<PropertyItem*>(GdipAlloc(size));
            if (item != nullptr &&
                GdipGetPropertyItem(image, kPropertyTagFrameDelay, size, item) == Ok &&
                item->value != nullptr) {
                unsigned int available = static_cast<unsigned int>(item->length) / sizeof(unsigned int);
                for (unsigned int i = 0; i < entry->frameCount; ++i) {
                    unsigned int delayMs = 100;   // default when the table is short
                    if (i < available) {
                        unsigned int centi =
                            reinterpret_cast<const unsigned int*>(item->value)[i];
                        delayMs = centi * 10;     // centiseconds -> milliseconds
                    }
                    if (delayMs == 0) {
                        delayMs = 100;
                    }
                    entry->delays.push_back(delayMs);
                }
            }
            if (item != nullptr) {
                GdipFree(item);
            }
        }
    }
}

// [FUN_180005500] create the upload target: D3DXCreateTexture(device, w, h,
// 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT).
void EnsureTexture(AnimeEntry* entry, IDirect3DDevice9* device)
{
    if (entry->texture != nullptr || device == nullptr || entry->image == nullptr) {
        return;
    }
    unsigned int width = 0;
    unsigned int height = 0;
    if (GdipGetImageWidth(entry->image, reinterpret_cast<UINT*>(&width)) != Ok ||
        GdipGetImageHeight(entry->image, reinterpret_cast<UINT*>(&height)) != Ok) {
        return;
    }
    if (width == 0 || height == 0) {
        return;
    }
    if (FAILED(D3DXCreateTexture(device, width, height, 1, D3DUSAGE_DYNAMIC,
                                 D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                 &entry->texture))) {
        entry->texture = nullptr;
    }
}

// [FUN_180005360] select the frame and blit it into the locked texture.
void DrawFrame(AnimeEntry* entry, unsigned int frameIndex)
{
    if (entry->image == nullptr || entry->texture == nullptr) {
        return;
    }
    GpStatus status = GdipImageSelectActiveFrame(entry->image, &kFrameDimensionTime,
                                                 frameIndex);
    if (status != Ok) {
        return;
    }
    D3DLOCKED_RECT locked;
    memset(&locked, 0, sizeof(locked));
    if (FAILED(entry->texture->LockRect(0, &locked, nullptr, 0))) {
        return;
    }
    unsigned int width = 0;
    unsigned int height = 0;
    GdipGetImageWidth(entry->image, reinterpret_cast<UINT*>(&width));
    GdipGetImageHeight(entry->image, reinterpret_cast<UINT*>(&height));
    // GDI+ draws straight into the locked texture memory (SourceCopy).
    GpBitmap* bitmap = nullptr;
    if (GdipCreateBitmapFromScan0(static_cast<int>(width), static_cast<int>(height),
                                  locked.Pitch, PixelFormat32bppARGB,
                                  static_cast<BYTE*>(locked.pBits), &bitmap) == Ok &&
        bitmap != nullptr) {
        GpGraphics* graphics = nullptr;
        if (GdipGetImageGraphicsContext(bitmap, &graphics) == Ok && graphics != nullptr) {
            GdipSetCompositingMode(graphics, CompositingModeSourceCopy);
            GdipDrawImageI(graphics, entry->image, 0, 0);
            GdipFlush(graphics, FlushIntentionFlush);   // [FUN_180005360] 0 = FlushIntentionFlush
            GdipDeleteGraphics(graphics);
        }
        GdipDisposeImage(bitmap);
    }
    entry->texture->UnlockRect(0);
}

// Advance the playback time and compute the current frame index
// (the FUN_1800050c0 frame walk over the per-frame delay table).
unsigned int ComputeFrame(AnimeEntry* entry)
{
    if (entry->frameCount <= 1) {
        return 0;
    }
    double total = 0.0;
    for (size_t i = 0; i < entry->delays.size(); ++i) {
        total += static_cast<double>(entry->delays[i]);
    }
    if (total <= 0.0) {
        total = 100.0 * static_cast<double>(entry->frameCount);
    }
    double timeMs = (entry->animTime - static_cast<double>(entry->offset)) * 1000.0;
    if (timeMs < 0.0) {
        timeMs = 0.0;
    }
    double wrapped = timeMs - (static_cast<double>(static_cast<unsigned long long>(
        timeMs / total)) * total);
    double acc = 0.0;
    for (unsigned int i = 0; i < entry->frameCount; ++i) {
        double delay = 100.0;
        if (i < entry->delays.size()) {
            delay = static_cast<double>(entry->delays[i]);
        }
        acc += delay;
        if (wrapped < acc) {
            return i;
        }
    }
    return entry->frameCount - 1;
}

// Register one ANIMATEDTEXTURE resource (SyncInEditMode is read here because
// the SAS model does not carry it; 0x1800b4108).
void RegisterResource(AnimeSetImpl* impl, SasEffect* sas, const SasResource& res)
{
    AnimeEntry* entry = new AnimeEntry();
    entry->effect = sas->effect;
    entry->param = res.param;
    entry->effectPath = sas->path;
    entry->offset = res.offset;
    entry->speed = res.speed != 0.0f ? res.speed : 1.0f;
    entry->seekVariable = res.seekVariable;
    if (sas->effect != nullptr) {
        D3DXHANDLE sync = sas->effect->GetAnnotationByName(res.param, "SyncInEditMode");
        if (sync != nullptr) {
            BOOL value = FALSE;
            if (sas->effect->GetBool(sync, &value) == S_OK) {
                entry->syncInEditMode = value != FALSE;
            }
        }
    }
    entry->resourcePath = ResolveResourcePath(sas->path, res.resourceName);
    LoadImageForEntry(entry);
    if (entry->image == nullptr) {
        // [0x1800b25a8/0x1800b25c0] "failed to open '...' ': source image must
        // be animated" family - the original surfaces the CAnime status
        // through the effect error path; keep the entry in a failed state and
        // do not spam the log every frame.
        delete entry;
        return;
    }
    impl->entries.push_back(entry);
}

void ReleaseEntryTexture(AnimeEntry* entry)
{
    if (entry->texture != nullptr) {
        entry->texture->Release();
        entry->texture = nullptr;
    }
    entry->lastFrame = -1;
}

void DestroyEntry(AnimeEntry* entry)
{
    ReleaseEntryTexture(entry);
    if (entry->image != nullptr) {
        GdipDisposeImage(entry->image);
        entry->image = nullptr;
    }
    delete entry;
}

struct ScanContext {
    AnimeSetImpl* impl;
    MmeContext* ctx;
};

// The FUN_180001320 registration half: visit every loaded effect's SAS model
// and register unseen ANIMATEDTEXTURE resources (semanticId 0x2D).
bool ScanVisitor(void* user, SasEffect* sas)
{
    ScanContext* scan = static_cast<ScanContext*>(user);
    if (sas == nullptr || sas->effect == nullptr || sas->path.empty()) {
        return true;
    }
    for (size_t i = 0; i < scan->impl->scannedEffects.size(); ++i) {
        if (scan->impl->scannedEffects[i] == sas->path) {
            return true;   // already registered
        }
    }
    for (size_t r = 0; r < sas->resources.size(); ++r) {
        if (sas->resources[r].semanticId == 0x2D &&
            !sas->resources[r].resourceName.empty()) {
            RegisterResource(scan->impl, sas, sas->resources[r]);
        }
    }
    scan->impl->scannedEffects.push_back(sas->path);
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// MmeAnimatedTextureSet
// ---------------------------------------------------------------------------

MmeAnimatedTextureSet::MmeAnimatedTextureSet() : impl(nullptr) {}

MmeAnimatedTextureSet::~MmeAnimatedTextureSet()
{
    MmeAnimeDestroy(this);
}

MmeAnimatedTextureSet* MmeAnimeCreate()
{
    MmeAnimatedTextureSet* set = new MmeAnimatedTextureSet();
    set->impl = new AnimeSetImpl();
    return set;
}

void MmeAnimeDestroy(MmeAnimatedTextureSet* set)
{
    if (set == nullptr || set->impl == nullptr) {
        return;
    }
    AnimeSetImpl* impl = static_cast<AnimeSetImpl*>(set->impl);
    for (size_t i = 0; i < impl->entries.size(); ++i) {
        DestroyEntry(impl->entries[i]);
    }
    impl->entries.clear();
    impl->scannedEffects.clear();
    delete impl;
    set->impl = nullptr;
}

void MmeAnimeOnDeviceLost(MmeAnimatedTextureSet* set)
{
    if (set == nullptr || set->impl == nullptr) {
        return;
    }
    AnimeSetImpl* impl = static_cast<AnimeSetImpl*>(set->impl);
    for (size_t i = 0; i < impl->entries.size(); ++i) {
        ReleaseEntryTexture(impl->entries[i]);
    }
}

void MmeAnimeTick(MmeContext* ctx)
{
    if (ctx == nullptr || ctx->animatedTextures == nullptr) {
        return;
    }
    MmeAnimatedTextureSet* set = static_cast<MmeAnimatedTextureSet*>(ctx->animatedTextures);
    AnimeSetImpl* impl = static_cast<AnimeSetImpl*>(set->impl);

    // [FUN_180001320] registration/prune half. The original prunes stale
    // instances in edit mode; this port registers unseen effect resources and
    // drops entries whose owning effect file left the engine cache.
    ScanContext scan;
    scan.impl = impl;
    scan.ctx = ctx;
    MmeEngineForEachSas(ScanVisitor, &scan);
    for (size_t i = impl->entries.size(); i > 0; --i) {
        AnimeEntry* entry = impl->entries[i - 1];
        bool alive = false;
        for (size_t k = 0; k < impl->scannedEffects.size(); ++k) {
            if (impl->scannedEffects[k] == entry->effectPath) {
                alive = true;
                break;
            }
        }
        if (!alive) {
            DestroyEntry(entry);
            impl->entries.erase(impl->entries.begin() + (i - 1));
        }
    }
    // Drop the scan bookkeeping of disappeared effect files so a re-load
    // re-registers their resources.
    for (size_t i = impl->scannedEffects.size(); i > 0; --i) {
        const std::string& path = impl->scannedEffects[i - 1];
        if (MmeEngineQueryFileStamp(path) == 0) {
            impl->scannedEffects.erase(impl->scannedEffects.begin() + (i - 1));
        }
    }

    // [FUN_180005360] advance + decode + SetTexture.
    for (size_t i = 0; i < impl->entries.size(); ++i) {
        AnimeEntry* entry = impl->entries[i];
        if (entry->effect == nullptr || entry->param == nullptr) {
            continue;
        }
        bool editMode = IsEditMode() != 0;
        if (entry->syncInEditMode && !editMode) {
            continue;   // edit-time-synced textures hold still outside edit mode
        }

        if (!entry->seekVariable.empty()) {
            // SeekVariable: the parameter value drives the animation position
            // (REFERENCE.txt: the animation follows the value's change).
            D3DXHANDLE seek = entry->effect->GetParameterByName(nullptr,
                                                                entry->seekVariable.c_str());
            if (seek != nullptr) {
                float value = 0.0f;
                if (entry->effect->GetFloat(seek, &value) == S_OK) {
                    if (value != static_cast<float>(entry->lastSeek)) {
                        entry->lastSeek = value;
                        entry->animTime = static_cast<double>(value) *
                                          static_cast<double>(entry->speed);
                    }
                }
            }
        } else {
            // Frame-time drive: advance by the delta seconds (the edit-mode
            // clock when SyncInEditMode, otherwise the playback clock).
            double delta = g_deltaSeconds;
            entry->animTime += delta * static_cast<double>(entry->speed);
        }

        unsigned int frame = ComputeFrame(entry);
        if (static_cast<int>(frame) == entry->lastFrame && entry->texture != nullptr) {
            continue;   // frame unchanged - nothing to upload
        }
        EnsureTexture(entry, ctx->device);
        if (entry->texture == nullptr) {
            continue;
        }
        DrawFrame(entry, frame);
        entry->lastFrame = static_cast<int>(frame);
        entry->effect->SetTexture(entry->param, entry->texture);
    }

    // [40001 auto-update watch] the original polls the file stamps from the
    // apply path (FUN_18000b880's 100 ms GetTickCount gate); the port polls
    // from this per-frame tick (same observable reload-between-frames).
    MmeUiTickAutoReload();
}

} // namespace mme
