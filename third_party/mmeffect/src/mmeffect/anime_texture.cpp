// anime_texture.cpp - see anime_texture.h for the binary evidence.
//
// Port shape: the CAnimeGIF/CAnimePNG objects are constructed AT PARSE TIME
// (sub_18000F3A0 case 45) so the ctor's error strings can fail the effect
// load exactly like the original; the ctx+0x48 set adopts them immediately
// (the original registers into the sas runtime record vector through
// sub_18001F090 at the same point). The per-frame tick drives the absolute-
// time frame walk (sub_1800050C0), the GIF GDI+ decode (sub_180005360) / the
// APNG IStream re-decode (sub_180007A50) and re-asserts the texture binding
// (sub_18001B5B0 case 45), then consumes the 0x33 TEXTUREVALUE records
// (sub_18001B0A0 case 51 -> sub_180063AA0).
#include "anime_texture.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

#include "effect_engine.h"
#include "mme_globals.h"
#include "mme_util.h"
#include "sas_exec.h"       // SasEffect / SasResource definitions

// GDI+ (the flat API). windows.h is already included through d3d9.h /
// mme_globals.h, which define the min/max macros the GDI+ headers do not
// expect.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#include <gdiplus.h>

// [toolchain divergence] the June 2010 SDK declared the GDI+ flat API at
// global scope; the modern Windows SDK wraps it in namespace Gdiplus (flat
// functions in Gdiplus::DllExports). Pull them back in for the flat-API
// style of the original.
using namespace Gdiplus;
using namespace Gdiplus::DllExports;

#include "mme_context.h"    // MmeContext (ctx->device / animatedTextures)

namespace mme {

namespace {

// FrameDimensionTime ({6aedbd6d-3fb5-418a-83a6-7f45229dc872}) - the dimension
// GUID the original passes to GdipImageSelectActiveFrame (DAT_1800aa820).
const GUID kFrameDimensionTime =
    { 0x6aedbd6d, 0x3fb5, 0x418a, { 0x83, 0xa6, 0x7f, 0x45, 0x22, 0x9d, 0xc8, 0x72 } };

// PropertyTagFrameDelay [0x5100 / 20736]: the per-frame delay array in
// centiseconds; the original stores value * 10 (milliseconds).
const PROPID kPropertyTagFrameDelay = 0x5100;
// PropertyTagLoopCount [0x5101 / 20737]: 0xFFFF (infinite) normalizes to 0.
const PROPID kPropertyTagLoopCount = 0x5101;

const unsigned int kArgbFormat = 2498570;   // PixelFormat32bppARGB

std::wstring AnsiToWideFile(const char* path)
{
    return MmeAnsiToWide(path);
}

// ---------------------------------------------------------------------------
// The anime object interface (the CAnimeGIF / CAnimePNG vtable):
//   +0x08 GetTexture, +0x10 SetFrame(double), +0x18 the device-lost release
//   (ReleaseDynamicTexture), +0x20 RecreateTexture
// plus the shared record head: speed(+8, double), offset(+16, double),
// error string(+24), device(+64), loop count(+100), total duration(+104),
// last query time(+112) and the cumulative-start frame timeline.
// ---------------------------------------------------------------------------

struct AnimeTimelineEntry {
    double       start;    // cumulative start time (seconds)
    unsigned int frame;   // frame index
};

class AnimeObject {
public:
    AnimeObject(IDirect3DDevice9* dev, const char* path);
    virtual ~AnimeObject();

    virtual void SetFrame(double time) = 0;                     // vtable+0x10
    virtual IDirect3DBaseTexture9* GetTexture() = 0;            // vtable+0x08
    // [sub_180005500 / sub_1800082C0] returns the D3DXCreateTexture result so
    // the post-Reset walk (sub_180016660 case 45 @0x1800166d8) can feed
    // OnResetDevice's failure chain.
    virtual HRESULT RecreateTexture() = 0;                      // vtable+0x20
    // Device-loss half [vtbl+0x18; sub_1800054D0 / sub_180008290]: drop the
    // DYNAMIC/DEFAULT texture without creating a new one while the device is
    // lost (the re-create belongs to the post-Reset walk, sub_180016660).
    virtual void ReleaseDynamicTexture() = 0;

    double       speed = 1.0;         // +8  ("Speed"; ctor default 1.0)
    double       offset = 0.0;        // +16 ("Offset")
    std::string  error;               // +24 (non-empty = ctor failure)
    IDirect3DDevice9* device = nullptr;  // +64 (AddRef'd like the original)
    unsigned int loopCount = 0;       // +100 (0 = loop forever)
    double       totalDuration = 0.0; // +104
    std::vector<AnimeTimelineEntry> timeline;  // the map<double, frame>
    unsigned int frameCount = 0;

    // [sub_1800050C0] the absolute-time frame walk. Returns the frame index
    // (the original returns the map node; the index carries the same info).
    int FindFrame(double time);
    int CurrentFrame() const { return currentFrame; }

protected:
    // +112/+152 caches: same time -> same node without a search.
    double lastQueryTime = 0.0;
    bool   haveLastQuery = false;
    int    lastResult = -1;
    int    currentFrame = -1;   // +152 current node (frame index)
};

AnimeObject::AnimeObject(IDirect3DDevice9* dev, const char* path)
    : device(dev)
{
    (void)path;
    if (device != nullptr) {
        device->AddRef();
    }
}

AnimeObject::~AnimeObject()
{
    if (device != nullptr) {
        device->Release();
    }
}

int AnimeObject::FindFrame(double time)
{
    // [sub_1800050C0 head] frameCount == 0 -> the begin node (frame 0).
    if (frameCount == 0 || timeline.empty()) {
        return 0;
    }
    // Same-time cache: `*(this+112) == time && last node valid -> return it`.
    if (haveLastQuery && lastQueryTime == time && lastResult >= 0) {
        return lastResult;
    }
    haveLastQuery = true;
    lastQueryTime = time;
    // pos = (time - offset) * speed; negative -> the first frame.
    double pos = (time - offset) * speed;
    if (pos < 0.0) {
        lastResult = static_cast<int>(timeline.front().frame);
        return lastResult;
    }
    double total = totalDuration > 0.0 ? totalDuration : 1.0;
    int loops = static_cast<int>(pos / total);
    // loopCount != 0 && loops >= loopCount -> hold the LAST frame.
    if (loopCount != 0 && loops >= static_cast<int>(loopCount)) {
        lastResult = static_cast<int>(timeline.back().frame);
        return lastResult;
    }
    double frac = pos - static_cast<double>(loops) * total;
    // lower_bound over the cumulative-start table (the original fast-forwards
    // from the cached node first - same result).
    unsigned int lo = 0, hi = static_cast<unsigned int>(timeline.size());
    while (lo < hi) {
        unsigned int mid = (lo + hi) / 2;
        if (timeline[mid].start <= frac) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    // lo = number of entries with start <= frac; the active frame is the
    // last of them (the table always covers the full loop).
    if (lo == 0) {
        lo = 1;
    }
    lastResult = static_cast<int>(timeline[lo - 1].frame);
    return lastResult;
}

// ---------------------------------------------------------------------------
// CAnimeGIF (ctor sub_180004810 / SetFrame sub_180005360 / texture
// recreation sub_180005500).
// ---------------------------------------------------------------------------

class AnimeGif : public AnimeObject {
public:
    AnimeGif(IDirect3DDevice9* device, const char* path);
    ~AnimeGif() override;

    void SetFrame(double time) override;
    IDirect3DBaseTexture9* GetTexture() override { return texture; }
    HRESULT RecreateTexture() override;
    void ReleaseDynamicTexture() override;

private:
    void DrawFrame(unsigned int frame);

    GpImage* image = nullptr;          // +80 (GdipLoadImageFromFile result)
    IDirect3DTexture9* texture = nullptr;  // +160 (DYNAMIC A8R8G8B8 DEFAULT)
};

AnimeGif::AnimeGif(IDirect3DDevice9* dev, const char* path)
    : AnimeObject(dev, path)
{
    // [sub_180004810] GdipLoadImageFromFile (the original converts the ANSI
    // path through sub_180006610; the GDI+ load uses the WIDE path).
    std::wstring wide = AnsiToWideFile(path);
    GpStatus status = GdipLoadImageFromFile(wide.c_str(), &image);
    if (status != Ok) {
        image = nullptr;
    }
    GUID dimension = kFrameDimensionTime;
    if (GdipImageGetFrameDimensionsList(image, &dimension, 1) != Ok) {
        // [0x1800b25a8/0x1800b25a4] "failed to open '<path>'" + "'\n".
        error = std::string("failed to open '") + path + "'\n";
        return;
    }
    unsigned int count = 0;
    if (GdipImageGetFrameCount(image, &dimension,
                               reinterpret_cast<UINT*>(&count)) == Ok) {
        frameCount = count;
    }
    if (frameCount <= 1) {
        // [0x1800b25a8 + 0x1800b25c0] "failed to open '<path>'"
        // + "': source image must be animated\n".
        error = std::string("failed to open '") + path +
                "': source image must be animated\n";
        return;
    }
    // PropertyTagLoopCount (0x5101): first u16, 0xFFFF -> 0 (loop forever).
    {
        UINT size = 0;
        if (GdipGetPropertyItemSize(image, kPropertyTagLoopCount, &size) == Ok &&
            size >= sizeof(PropertyItem)) {
            PropertyItem* item = static_cast<PropertyItem*>(GdipAlloc(size));
            if (item != nullptr &&
                GdipGetPropertyItem(image, kPropertyTagLoopCount, size,
                                    item) == Ok &&
                item->value != nullptr) {
                unsigned int loop =
                    *static_cast<const unsigned short*>(item->value);
                loopCount = (loop == 0xFFFF) ? 0u : loop;
            }
            if (item != nullptr) {
                GdipFree(item);
            }
        }
    }
    // PropertyTagFrameDelay (0x5100) in centiseconds -> milliseconds (*10).
    // The table only feeds frames while 4*(i+1) <= the property byte length;
    // frames past the table (or with a zero delay) get 1/30 s each.
    std::vector<unsigned int> delayMs;
    {
        UINT size = 0;
        if (GdipGetPropertyItemSize(image, kPropertyTagFrameDelay, &size) == Ok &&
            size >= sizeof(PropertyItem)) {
            PropertyItem* item = static_cast<PropertyItem*>(GdipAlloc(size));
            if (item != nullptr &&
                GdipGetPropertyItem(image, kPropertyTagFrameDelay, size,
                                    item) == Ok &&
                item->value != nullptr) {
                unsigned int available =
                    static_cast<unsigned int>(item->length) / sizeof(UINT);
                delayMs.reserve(available);
                for (unsigned int i = 0; i < available; ++i) {
                    delayMs.push_back(
                        10u * static_cast<const unsigned int*>(item->value)[i]);
                }
            }
            if (item != nullptr) {
                GdipFree(item);
            }
        }
    }
    // The cumulative-start timeline: key = zeroDelayFrames/30 + accMs/1000.
    {
        unsigned long long accMs = 0;
        unsigned long long zeroDelay = 0;
        for (unsigned int i = 0; i < frameCount; ++i) {
            AnimeTimelineEntry entry;
            entry.start = static_cast<double>(zeroDelay) / 30.0 +
                          static_cast<double>(accMs) / 1000.0;
            entry.frame = i;
            timeline.push_back(entry);
            if (i < delayMs.size() && delayMs[i] != 0) {
                accMs += delayMs[i];
            } else {
                ++zeroDelay;
            }
        }
        totalDuration = static_cast<double>(zeroDelay) / 30.0 +
                        static_cast<double>(accMs) / 1000.0;
    }
    // The D3D upload target, created IN the ctor: D3DXCreateTexture(dev, w,
    // h, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT).
    unsigned int width = 0;
    unsigned int height = 0;
    GdipGetImageWidth(image, reinterpret_cast<UINT*>(&width));
    GdipGetImageHeight(image, reinterpret_cast<UINT*>(&height));
    if (D3DXCreateTexture(device, width, height, 1, D3DUSAGE_DYNAMIC,
                          D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                          &texture) != S_OK ||
        texture == nullptr) {
        // [0x1800b25e8] "failed to load '<path>'\n".
        error = std::string("failed to load '") + path + "'\n";
        return;
    }
}

AnimeGif::~AnimeGif()
{
    if (texture != nullptr) {
        texture->Release();
        texture = nullptr;
    }
    if (image != nullptr) {
        GdipDisposeImage(image);
        image = nullptr;
    }
}

HRESULT AnimeGif::RecreateTexture()
{
    // [sub_180005500] release + recreate with the image's own size; the
    // original returns the D3DXCreateTexture result straight to the
    // post-Reset walk (sub_180016660 case 45).
    if (texture != nullptr) {
        texture->Release();
        texture = nullptr;
    }
    unsigned int width = 0;
    unsigned int height = 0;
    GdipGetImageWidth(image, reinterpret_cast<UINT*>(&width));
    GdipGetImageHeight(image, reinterpret_cast<UINT*>(&height));
    HRESULT hr = D3DXCreateTexture(device, width, height, 1, D3DUSAGE_DYNAMIC,
                                   D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                   &texture);
    if (FAILED(hr)) {
        texture = nullptr;
    }
    currentFrame = -1;
    return hr;
}

void AnimeGif::ReleaseDynamicTexture()
{
    if (texture != nullptr) {
        texture->Release();
        texture = nullptr;
    }
    currentFrame = -1;
}

// [sub_180005360] select the frame and blit it into the locked texture.
void AnimeGif::DrawFrame(unsigned int frame)
{
    if (image == nullptr || texture == nullptr) {
        return;
    }
    if (GdipImageSelectActiveFrame(image, &kFrameDimensionTime,
                                   frame) != Ok) {
        return;
    }
    D3DLOCKED_RECT locked;
    memset(&locked, 0, sizeof(locked));
    if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) {
        return;
    }
    unsigned int width = 0;
    unsigned int height = 0;
    GdipGetImageWidth(image, reinterpret_cast<UINT*>(&width));
    GdipGetImageHeight(image, reinterpret_cast<UINT*>(&height));
    // GDI+ draws straight into the locked texture memory (SourceCopy).
    GpBitmap* bitmap = nullptr;
    if (GdipCreateBitmapFromScan0(static_cast<int>(width),
                                  static_cast<int>(height), locked.Pitch,
                                  kArgbFormat,
                                  static_cast<BYTE*>(locked.pBits),
                                  &bitmap) == Ok &&
        bitmap != nullptr) {
        GpGraphics* graphics = nullptr;
        if (GdipGetImageGraphicsContext(bitmap, &graphics) == Ok &&
            graphics != nullptr) {
            GdipSetCompositingMode(graphics, CompositingModeSourceCopy);
            GdipDrawImageI(graphics, image, 0, 0);
            GdipFlush(graphics, FlushIntentionFlush);
            GdipDeleteGraphics(graphics);
        }
        GdipDisposeImage(bitmap);
    }
    texture->UnlockRect(0);
}

void AnimeGif::SetFrame(double time)
{
    int frame = FindFrame(time);
    if (frame == currentFrame) {
        return;   // same node - no reselect / no reupload
    }
    currentFrame = frame;
    if (texture == nullptr) {
        // PORT ADDITION (defensive): the original's SetFrame (sub_180005360)
        // never creates a texture - the post-Reset walk (sub_180016660 case
        // 45) re-creates it. Kept as a belt-and-braces rebuild for a texture
        // lost through any path the walk missed.
        RecreateTexture();
        if (texture == nullptr) {
            return;
        }
    }
    DrawFrame(static_cast<unsigned int>(frame));
}

// ---------------------------------------------------------------------------
// CAnimePNG (ctor sub_1800068A0 - a complete APNG chunk parser; per-frame
// decode sub_180007A50 - the PNG-stream reconstruction through an IStream).
// ---------------------------------------------------------------------------

struct PngChunk {
    unsigned int type = 0;      // big-endian chunk type as u32
    std::vector<unsigned char> blob;  // data + CRC (the original stores
                                      // Size+4 bytes as one blob)
};

struct PngFrame {
    long long filePos = 0;      // stream position right after the fcTL
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int x = 0;
    unsigned int y = 0;
    unsigned short delayNum = 0;
    unsigned short delayDen = 0;
    unsigned char disposeOp = 0;
    unsigned char blendOp = 0;
};

unsigned int ReadBe32(const unsigned char* p)
{
    return (static_cast<unsigned int>(p[0]) << 24) |
           (static_cast<unsigned int>(p[1]) << 16) |
           (static_cast<unsigned int>(p[2]) << 8) |
           static_cast<unsigned int>(p[3]);
}

unsigned short ReadBe16(const unsigned char* p)
{
    return static_cast<unsigned short>(
        (static_cast<unsigned short>(p[0]) << 8) | p[1]);
}

// [sub_1800067C0] Euclid's gcd, the exact-rational timeline accumulator's
// normalizer (iterative form of the original's recursion).
unsigned long long MmeGcdUnsigned(unsigned long long a, unsigned long long b)
{
    while (b != 0) {
        unsigned long long t = a % b;
        a = b;
        b = t;
    }
    return a;
}

// Chunk type constants as big-endian byte quadruples read as u32
// (the original compares the raw HIDWORD of the 8-byte chunk header).
const unsigned int kPngIHDR = 0x49484452u;
const unsigned int kPngPLTE = 0x504C5445u;
const unsigned int kPngIDAT = 0x49444154u;
const unsigned int kPngIEND = 0x494E4544u;
const unsigned int kPngAcTL = 0x6163544Cu;
const unsigned int kPngFcTL = 0x6663544Cu;
const unsigned int kPngFdAT = 0x66644154u;

class AnimePng : public AnimeObject {
public:
    AnimePng(IDirect3DDevice9* device, const char* path);
    ~AnimePng() override;

    void SetFrame(double time) override;
    IDirect3DBaseTexture9* GetTexture() override;
    HRESULT RecreateTexture() override;
    void ReleaseDynamicTexture() override;

private:
    bool DecodeFrame(unsigned int frameIndex, IDirect3DTexture9* target);

    FILE* file = nullptr;                    // +136 (kept open)
    GpBitmap* baseBitmap = nullptr;          // +152 (persistent compose target)
    std::vector<PngChunk> chunks;            // +160 (pre-frame chunks)
    std::vector<PngFrame> frames;            // +224 (fcTL records)
    std::vector<IDirect3DTexture9*> preloaded;  // +192 (preload mode)
    IDirect3DTexture9* dynamicTexture = nullptr;  // +256 (on-demand mode)
    bool preloadAll = false;                 // +264
};

AnimePng::AnimePng(IDirect3DDevice9* dev, const char* path)
    : AnimeObject(dev, path)
{
    // [sub_1800068A0] fopen_s "rb" + PNG signature check.
    if (fopen_s(&file, path, "rb") != 0 || file == nullptr) {
        error = std::string("failed to open '") + path + "'\n";
        return;
    }
    unsigned long long signature = 0;
    if (fread(&signature, 8, 1, file) != 1 ||
        signature != 0xA1A0A0D474E5089ull) {
        error = std::string("failed to open '") + path + "'\n";
        return;
    }
    unsigned int imageWidth = 0;
    unsigned int imageHeight = 0;
    unsigned int numFrames = 0;     // acTL (+72)
    unsigned int numPlays = 0;      // acTL (+76)
    bool finalized = false;
    while (!finalized) {
        unsigned char header[8];
        if (fread(header, 8, 1, file) != 1) {
            break;   // EOF: the generic failure below
        }
        unsigned int length = ReadBe32(header);
        unsigned int type =
            (static_cast<unsigned int>(header[4]) << 24) |
            (static_cast<unsigned int>(header[5]) << 16) |
            (static_cast<unsigned int>(header[6]) << 8) |
            static_cast<unsigned int>(header[7]);
        if (type == kPngIDAT || type == kPngFdAT) {
            // IDAT / fdAT bodies are NOT cached; they are re-read per frame
            // from the recorded file positions (sub_180007A50).
            _fseeki64(file, static_cast<long long>(length) + 4, SEEK_CUR);
            continue;
        }
        if (type == kPngIEND) {
            finalized = true;
            continue;
        }
        if (type == kPngAcTL) {
            // acTL itself never enters a reconstructed frame stream.
            std::vector<unsigned char> body(static_cast<size_t>(length) + 4);
            if (!body.empty() &&
                fread(body.data(), body.size(), 1, file) != 1) {
                break;
            }
            if (length >= 8) {
                numFrames = ReadBe32(body.data());
                numPlays = ReadBe32(body.data() + 4);
            }
            continue;
        }
        if (type == kPngFcTL) {
            std::vector<unsigned char> body(static_cast<size_t>(length) + 4);
            if (!body.empty() &&
                fread(body.data(), body.size(), 1, file) != 1) {
                break;
            }
            if (length >= 26) {
                PngFrame frame;
                const unsigned char* d = body.data();
                frame.width = ReadBe32(d + 4);
                frame.height = ReadBe32(d + 8);
                frame.x = ReadBe32(d + 12);
                frame.y = ReadBe32(d + 16);
                frame.delayNum = ReadBe16(d + 20);
                frame.delayDen = ReadBe16(d + 22);
                frame.disposeOp = d[24];
                frame.blendOp = d[25];
                frame.filePos = _ftelli64(file);
                frames.push_back(frame);
            }
            continue;
        }
        // IHDR / PLTE / tRNS / gAMA / ... : cached verbatim (data + CRC as
        // one blob); chunk 0 (IHDR) gets its width/height patched per frame.
        PngChunk chunk;
        chunk.type = type;
        chunk.blob.resize(length + 4);
        if (!chunk.blob.empty() &&
            fread(chunk.blob.data(), chunk.blob.size(), 1, file) != 1) {
            break;
        }
        if (type == kPngIHDR && length >= 8) {
            imageWidth = ReadBe32(chunk.blob.data());
            imageHeight = ReadBe32(chunk.blob.data() + 4);
        }
        chunks.push_back(std::move(chunk));
    }
    // [LABEL_139, 0x180007671] BOTH the truncated file (EOF before IEND) and
    // the IEND gate failure land in the SAME generic-error block: the gate
    // checks acTL's numFrames == the fcTL counter == the record count and a
    // non-zero IHDR size; any mismatch reports "failed to open '<path>'\n"
    // and ZEROES numFrames (+72) - a half-built object never advances. The
    // mismatch error PRECEDES the numFrames == 0 "must be animated" branch
    // (which only runs once the gate has passed: a file with fcTLs but no
    // acTL is the generic open failure, not the animated message).
    if (!finalized || numFrames != frames.size() || imageWidth == 0 ||
        imageHeight == 0) {
        error = std::string("failed to open '") + path + "'\n";
        return;
    }
    frameCount = 0;
    timeline.clear();
    if (numFrames == 0) {
        // [0x1800b25c0] "failed to open '<path>': source image must be
        // animated\n" (a plain PNG has no acTL at all).
        error = std::string("failed to open '") + path +
                "': source image must be animated\n";
        return;
    }
    frameCount = numFrames;
    // The persistent base bitmap, cleared to transparent black.
    {
        GpBitmap* created = nullptr;
        if (GdipCreateBitmapFromScan0(static_cast<int>(imageWidth),
                                      static_cast<int>(imageHeight), 0,
                                      kArgbFormat, nullptr,
                                      &created) == Ok && created != nullptr) {
            baseBitmap = created;
            GpGraphics* graphics = nullptr;
            if (GdipGetImageGraphicsContext(baseBitmap, &graphics) == Ok &&
                graphics != nullptr) {
                GdipGraphicsClear(graphics, 0);
                GdipDeleteGraphics(graphics);
            }
        }
    }
    if (baseBitmap == nullptr) {
        return;
    }
    // [0x180007034-0x180007227] the cumulative-start timeline with the fcTL
    // delay defaults (den == 0 -> 100 first [0x1800070d8], then num == 0 ->
    // the dword store 0x001E0001 = {num 1, den 30} [0x1800070ec]). The
    // cumulative key/total are maintained as an EXACT rational (num/den
    // summed through the LCM, gcd-normalized), latching to the plain double
    // accumulation once the numerator or denominator reaches 2^48; the map
    // keys and totalDuration take the exact rational while it fits.
    {
        unsigned long long ratNum = 0;
        unsigned long long ratDen = 1;
        double doubleAcc = 0.0;
        bool overflowed = false;
        for (unsigned int i = 0; i < frames.size(); ++i) {
            PngFrame& frame = frames[i];
            if (frame.delayDen == 0) {
                frame.delayDen = 100;
            }
            if (frame.delayNum == 0) {
                frame.delayNum = 1;
                frame.delayDen = 30;
            }
            double key = overflowed
                             ? doubleAcc
                             : static_cast<double>(ratNum) /
                                   static_cast<double>(ratDen);
            AnimeTimelineEntry entry;
            entry.start = key;
            entry.frame = i;
            timeline.push_back(entry);
            const unsigned long long num = frame.delayNum;
            const unsigned long long den = frame.delayDen;
            unsigned long long g;
            if (ratDen % den != 0) {
                const unsigned long long r = ratDen % den;
                g = MmeGcdUnsigned(r, den % r);   // == gcd(ratDen, den)
            } else {
                g = den;
            }
            const unsigned long long newNum =
                ratNum * (den / g) + num * (ratDen / g);
            const unsigned long long newDen = (den / g) * ratDen;
            ratNum = newNum;
            ratDen = newDen;
            if ((newNum & 0xFFFF000000000000ull) != 0 ||
                (newDen & 0xFFFF000000000000ull) != 0) {
                overflowed = true;                // [0x18000717f] the latch
            }
            doubleAcc += static_cast<double>(num) / static_cast<double>(den);
        }
        totalDuration = overflowed
                            ? doubleAcc
                            : static_cast<double>(ratNum) /
                                  static_cast<double>(ratDen);
    }
    loopCount = numPlays;
    // [ctor +264] preload decision: 4 * numFrames * w * h <= 0xA00000 ->
    // one MANAGED texture per frame, decoded immediately.
    preloadAll = 4ull * numFrames * imageWidth * imageHeight <= 0xA00000ull;
    if (preloadAll) {
        for (unsigned int i = 0; i < frames.size(); ++i) {
            IDirect3DTexture9* tex = nullptr;
            if (D3DXCreateTexture(device, imageWidth, imageHeight, 1, 0,
                                  D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                  &tex) != S_OK ||
                tex == nullptr || !DecodeFrame(i, tex)) {
                if (tex != nullptr) {
                    tex->Release();
                }
                for (size_t k = 0; k < preloaded.size(); ++k) {
                    preloaded[k]->Release();
                }
                preloaded.clear();
                frameCount = 0;
                error = std::string("failed to open '") + path + "'\n";
                return;
            }
            preloaded.push_back(tex);
        }
    } else {
        if (D3DXCreateTexture(device, imageWidth, imageHeight, 1,
                              D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
                              D3DPOOL_DEFAULT, &dynamicTexture) != S_OK ||
            dynamicTexture == nullptr) {
            error = std::string("failed to load '") + path + "'\n";
            return;
        }
    }
}

AnimePng::~AnimePng()
{
    for (size_t i = 0; i < preloaded.size(); ++i) {
        preloaded[i]->Release();
    }
    preloaded.clear();
    if (dynamicTexture != nullptr) {
        dynamicTexture->Release();
        dynamicTexture = nullptr;
    }
    if (baseBitmap != nullptr) {
        GdipDisposeImage(baseBitmap);
        baseBitmap = nullptr;
    }
    if (file != nullptr) {
        fclose(file);
        file = nullptr;
    }
}

void AnimePng::ReleaseDynamicTexture()
{
    // The MANAGED preloaded set survives the reset; only the DYNAMIC upload
    // target dies with the device.
    if (dynamicTexture != nullptr) {
        dynamicTexture->Release();
        dynamicTexture = nullptr;
    }
    currentFrame = -1;
}

IDirect3DBaseTexture9* AnimePng::GetTexture()
{
    if (preloadAll) {
        if (currentFrame >= 0 &&
            currentFrame < static_cast<int>(preloaded.size())) {
            return preloaded[currentFrame];
        }
        return preloaded.empty() ? nullptr : preloaded[0];
    }
    return dynamicTexture;
}

HRESULT AnimePng::RecreateTexture()
{
    // [sub_1800082C0] the DYNAMIC texture dies with the device (the MANAGED
    // preloaded set survives); rebuild it at the image size. The original
    // re-creates unconditionally and returns the D3DXCreateTexture result to
    // the post-Reset walk (sub_180016660 case 45).
    if (dynamicTexture != nullptr) {
        dynamicTexture->Release();
        dynamicTexture = nullptr;
    }
    unsigned int width = 0;
    unsigned int height = 0;
    GdipGetImageWidth(baseBitmap, reinterpret_cast<UINT*>(&width));
    GdipGetImageHeight(baseBitmap, reinterpret_cast<UINT*>(&height));
    HRESULT hr = D3DXCreateTexture(device, width, height, 1, D3DUSAGE_DYNAMIC,
                                   D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                   &dynamicTexture);
    if (FAILED(hr)) {
        dynamicTexture = nullptr;
    }
    currentFrame = -1;
    return hr;
}

void AnimePng::SetFrame(double time)
{
    // [0x180008236] `if (!this+72) return` - a zero frame count (a failed
    // ctor) advances nothing at all.
    if (frameCount == 0) {
        return;
    }
    int frame = FindFrame(time);
    if (frame == currentFrame) {
        return;
    }
    currentFrame = frame;
    if (preloadAll) {
        return;   // preloaded frames were decoded in the ctor
    }
    if (dynamicTexture == nullptr) {
        // PORT ADDITION (defensive; see AnimeGif::SetFrame): the original's
        // SetFrame (sub_180008230) never creates a texture - the post-Reset
        // walk (sub_180016660 case 45) re-creates it.
        RecreateTexture();
        if (dynamicTexture == nullptr) {
            return;
        }
    }
    DecodeFrame(static_cast<unsigned int>(frame), dynamicTexture);
}

// [sub_180007A50] reconstruct one frame as a standalone PNG stream, decode
// it through GDI+ and compose it onto the base bitmap / the locked texture.
bool AnimePng::DecodeFrame(unsigned int frameIndex, IDirect3DTexture9* target)
{
    if (frameIndex >= frames.size() || file == nullptr ||
        baseBitmap == nullptr) {
        return false;
    }
    const PngFrame& frame = frames[frameIndex];
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) ||
        stream == nullptr) {
        return false;
    }
    bool ok = false;
    // The PNG signature.
    const unsigned char kSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A,
                                          0x1A, 0x0A };
    bool streamOk = true;
    auto writeAll = [&](const void* data, ULONG size) -> bool {
        ULONG done = 0;
        return stream->Write(data, size, &done) == S_OK && done == size;
    };
    auto writeBe32 = [&](unsigned int value) -> bool {
        unsigned char b[4] = { static_cast<unsigned char>(value >> 24),
                               static_cast<unsigned char>(value >> 16),
                               static_cast<unsigned char>(value >> 8),
                               static_cast<unsigned char>(value) };
        return writeAll(b, 4);
    };
    do {
        if (!writeAll(kSignature, sizeof(kSignature))) {
            streamOk = false;
            break;
        }
        // The cached pre-frame chunks; chunk 0 (IHDR) is patched to the
        // FRAME's width/height so the reconstructed PNG is the tile itself.
        for (size_t i = 0; i < chunks.size() && streamOk; ++i) {
            const unsigned char* blob = chunks[i].blob.data();
            unsigned int length = chunks[i].blob.empty()
                                      ? 0u
                                      : static_cast<unsigned int>(
                                            chunks[i].blob.size() - 4);
            if (i == 0 && length >= 8) {
                unsigned char patched[8] = {
                    static_cast<unsigned char>(frame.width >> 24),
                    static_cast<unsigned char>(frame.width >> 16),
                    static_cast<unsigned char>(frame.width >> 8),
                    static_cast<unsigned char>(frame.width),
                    static_cast<unsigned char>(frame.height >> 24),
                    static_cast<unsigned char>(frame.height >> 16),
                    static_cast<unsigned char>(frame.height >> 8),
                    static_cast<unsigned char>(frame.height)
                };
                streamOk = writeBe32(length) && writeBe32(chunks[i].type) &&
                           writeAll(patched, sizeof(patched)) &&
                           writeAll(blob + 8, chunks[i].blob.size() - 8);
                continue;
            }
            streamOk = writeBe32(length) && writeBe32(chunks[i].type) &&
                       writeAll(chunks[i].blob.data(),
                                static_cast<ULONG>(chunks[i].blob.size()));
        }
        if (!streamOk) {
            break;
        }
        // The frame's own data run: IDAT passes through, fdAT is re-tagged
        // IDAT minus its 4-byte sequence number; the run ends at the next
        // fcTL or IEND, which is emitted as a plain IEND.
        if (_fseeki64(file, frame.filePos, SEEK_SET) != 0) {
            streamOk = false;
            break;
        }
        bool dataDone = false;
        while (!dataDone && streamOk) {
            unsigned char header[8];
            if (fread(header, 8, 1, file) != 1) {
                streamOk = false;
                break;
            }
            unsigned int length = ReadBe32(header);
            unsigned int type =
                (static_cast<unsigned int>(header[4]) << 24) |
                (static_cast<unsigned int>(header[5]) << 16) |
                (static_cast<unsigned int>(header[6]) << 8) |
                static_cast<unsigned int>(header[7]);
            if (type == kPngFcTL || type == kPngIEND) {
                streamOk = writeBe32(0) && writeBe32(kPngIEND);
                dataDone = true;
                break;
            }
            std::vector<unsigned char> body(static_cast<size_t>(length) + 4);
            if (!body.empty() &&
                fread(body.data(), body.size(), 1, file) != 1) {
                streamOk = false;
                break;
            }
            if (type == kPngIDAT) {
                streamOk = writeBe32(length) && writeBe32(kPngIDAT) &&
                           writeAll(body.data(),
                                    static_cast<ULONG>(body.size()));
            } else if (type == kPngFdAT && length >= 4) {
                streamOk = writeBe32(length - 4) && writeBe32(kPngIDAT) &&
                           writeAll(body.data() + 4,
                                    static_cast<ULONG>(body.size() - 4));
            }
        }
        if (!streamOk) {
            break;
        }
        // Decode the reconstructed stream.
        LARGE_INTEGER zero = {};
        stream->Seek(zero, STREAM_SEEK_SET, nullptr);
        GpImage* tile = nullptr;
        if (GdipLoadImageFromStreamICM(stream, &tile) != Ok || tile == nullptr) {
            break;
        }
        unsigned int tileWidth = 0;
        GdipGetImageWidth(tile, reinterpret_cast<UINT*>(&tileWidth));
        if (tileWidth == 0) {
            GdipDisposeImage(tile);
            break;
        }
        // Compose onto the persistent base bitmap.
        GpGraphics* base = nullptr;
        GdipGetImageGraphicsContext(baseBitmap, &base);
        if (base != nullptr) {
            if (frameIndex == 0) {
                GdipGraphicsClear(base, 0);
            }
            if (frame.disposeOp <= 1) {
                GdipSetCompositingMode(
                    base, frame.blendOp == 1 ? CompositingModeSourceOver
                                             : CompositingModeSourceCopy);
                GdipDrawImageI(base, tile, static_cast<int>(frame.x),
                               static_cast<int>(frame.y));
            }
        }
        // Blit the base into the locked texture.
        if (target != nullptr) {
            D3DLOCKED_RECT locked;
            memset(&locked, 0, sizeof(locked));
            if (target->LockRect(0, &locked, nullptr, 0) == S_OK) {
                unsigned int width = 0;
                unsigned int height = 0;
                GdipGetImageWidth(baseBitmap,
                                  reinterpret_cast<UINT*>(&width));
                GdipGetImageHeight(baseBitmap,
                                   reinterpret_cast<UINT*>(&height));
                GpBitmap* surface = nullptr;
                if (GdipCreateBitmapFromScan0(
                        static_cast<int>(width), static_cast<int>(height),
                        locked.Pitch, kArgbFormat,
                        static_cast<BYTE*>(locked.pBits),
                        &surface) == Ok &&
                    surface != nullptr) {
                    GpGraphics* g = nullptr;
                    if (GdipGetImageGraphicsContext(surface, &g) == Ok &&
                        g != nullptr) {
                        GdipSetCompositingMode(g, CompositingModeSourceCopy);
                        GdipDrawImageI(g, baseBitmap, 0, 0);
                        if (frame.disposeOp == 2) {
                            // PREVIOUS: the tile is additionally drawn into
                            // the uploaded copy (original behavior).
                            GdipSetCompositingMode(
                                g, frame.blendOp != 1
                                       ? CompositingModeSourceCopy
                                       : CompositingModeSourceOver);
                            GdipDrawImageI(g, tile, static_cast<int>(frame.x),
                                           static_cast<int>(frame.y));
                        }
                        GdipFlush(g, FlushIntentionFlush);
                        GdipDeleteGraphics(g);
                    }
                    GdipDisposeImage(surface);
                }
                target->UnlockRect(0);
            }
        }
        if (base != nullptr) {
            if (frame.disposeOp == 1) {
                // BACKGROUND: clear the frame region on the base AFTER the
                // upload (transparent black through a SourceCopy fill).
                GdipSetCompositingMode(base, CompositingModeSourceCopy);
                GpBrush* brush = nullptr;
                if (GdipCreateSolidFill(0, reinterpret_cast<GpSolidFill**>(
                                                &brush)) == Ok &&
                    brush != nullptr) {
                    GdipFillRectangleI(base, brush,
                                       static_cast<int>(frame.x),
                                       static_cast<int>(frame.y),
                                       static_cast<int>(frame.width),
                                       static_cast<int>(frame.height));
                    GdipDeleteBrush(brush);
                }
            }
            GdipDeleteGraphics(base);
        }
        GdipDisposeImage(tile);
        ok = true;
    } while (false);
    stream->Release();
    return ok;
}

// ---------------------------------------------------------------------------
// The ctx+0x48 set entries.
// ---------------------------------------------------------------------------

struct AnimeEntry {
    ID3DXEffect* effect = nullptr;      // owning effect (borrowed)
    D3DXHANDLE   param = nullptr;       // the texture parameter
    std::string  effectPath;            // owning effect file (identity key)
    D3DXHANDLE   seekParam = nullptr;   // SeekVariable handle (0 = clock)
    AnimeObject* object = nullptr;      // owned (CAnimeGIF / CAnimePNG)
};

struct AnimeSetImpl {
    std::vector<AnimeEntry*> entries;
};

} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction / registration API
// ---------------------------------------------------------------------------

void* MmeAnimeConstructGif(IDirect3DDevice9* device, const char* path,
                           std::string* error)
{
    AnimeGif* object = new AnimeGif(device, path);
    if (error != nullptr) {
        *error = object->error;
    }
    return object;
}

void* MmeAnimeConstructPng(IDirect3DDevice9* device, const char* path,
                           std::string* error)
{
    AnimePng* object = new AnimePng(device, path);
    if (error != nullptr) {
        *error = object->error;
    }
    return object;
}

void MmeAnimeDestroyObject(void* object)
{
    delete static_cast<AnimeObject*>(object);
}

void MmeAnimeRegisterParsed(MmeContext* ctx, void* object,
                            ID3DXEffect* effect, D3DXHANDLE param,
                            const char* effectPath, double offset,
                            double speed, D3DXHANDLE seekParam)
{
    AnimeObject* anime = static_cast<AnimeObject*>(object);
    if (ctx == nullptr || ctx->animatedTextures == nullptr) {
        // Defensive: without a set the object cannot be owned anywhere.
        delete anime;
        return;
    }
    MmeAnimatedTextureSet* set =
        static_cast<MmeAnimatedTextureSet*>(ctx->animatedTextures);
    AnimeSetImpl* impl = static_cast<AnimeSetImpl*>(set->impl);
    // Deduplicate by (effect path, parameter): a re-parse of the same effect
    // produces a fresh SasEffect while the old entries are pruned by the
    // tick through the same path key.
    for (size_t i = 0; i < impl->entries.size(); ++i) {
        if (impl->entries[i]->effectPath == effectPath &&
            impl->entries[i]->param == param) {
            delete anime;
            return;
        }
    }
    AnimeEntry* entry = new AnimeEntry();
    entry->effect = effect;
    entry->param = param;
    entry->effectPath = effectPath;
    entry->seekParam = seekParam;
    entry->object = anime;
    // Offset/Speed live on the object (the original writes obj+16 / obj+8).
    anime->offset = offset;
    anime->speed = speed;
    impl->entries.push_back(entry);
}

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
        delete impl->entries[i];
    }
    impl->entries.clear();
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
        AnimeEntry* entry = impl->entries[i];
        if (entry->object == nullptr) {
            continue;
        }
        // [sub_1800164C0 case 45 @0x18001654d-0x18001656a] the device-lost
        // record walk: FIRST clear the texture parameter
        // (effect->SetTexture(param, NULL) @0x18001655d) so the effect no
        // longer references the texture that is about to die, THEN call the
        // anime object's vtbl+0x18 slot (@0x18001656a; CAnimeGIF
        // sub_1800054D0 / CAnimePNG sub_180008290): release the DYNAMIC /
        // D3DPOOL_DEFAULT upload target and create nothing while the device
        // is lost.
        if (entry->effect != nullptr && entry->param != nullptr) {
            entry->effect->SetTexture(entry->param, nullptr);
        }
        entry->object->ReleaseDynamicTexture();
    }
}

HRESULT MmeAnimeOnDeviceReset(MmeAnimatedTextureSet* set)
{
    if (set == nullptr || set->impl == nullptr) {
        return S_OK;
    }
    AnimeSetImpl* impl = static_cast<AnimeSetImpl*>(set->impl);
    HRESULT hr = S_OK;
    for (size_t i = 0; i < impl->entries.size(); ++i) {
        AnimeEntry* entry = impl->entries[i];
        if (entry->object == nullptr) {
            continue;
        }
        // [sub_180016660 case 45 @0x1800166cb-0x1800166d8] the post-Reset
        // record walk: each entry's vtbl+0x20 slot re-creates the upload
        // texture IMMEDIATELY (CAnimeGIF sub_180005500 / CAnimePNG
        // sub_1800082C0). A same-time frame must find a live texture (the
        // original's SetFrame never rebuilds), so a lazy rebuild would leave
        // a permanently NULL binding for a paused animation. A failed
        // D3DXCreateTexture feeds the walk's last-non-zero-wins failure chain
        // (sub_180016660 `if (v8) v3 = v8` / sub_18002DE40 `if (v5) v1 = v5`)
        // -> OnResetDevice's "Failed to reset MikuMikuEffect". No SetTexture
        // here: the per-draw walk (sub_18001B5B0 case 45) re-asserts the
        // binding at the next Begin.
        HRESULT step = entry->object->RecreateTexture();
        if (step != S_OK) {
            hr = step;
        }
    }
    return hr;
}

// ---------------------------------------------------------------------------
// 0x33 TEXTUREVALUE consumption (sub_18001B0A0 case 51 -> sub_180063AA0 ->
// sub_180063B30 -> sub_180064020)
// ---------------------------------------------------------------------------

namespace {

// IEEE half -> float (the A16B16G16R16F readback; D3DXFloat16To32Array is
// not forwarded by the port's d3dx9 shim, and the bit twiddle is exact).
float HalfToFloat(unsigned short half)
{
    unsigned int sign = (half >> 15) & 1u;
    unsigned int exponent = (half >> 10) & 0x1Fu;
    unsigned int mantissa = half & 0x3FFu;
    unsigned int bits = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign << 31;
        } else {
            // Subnormal half -> normalized float.
            exponent = 127 - 15 + 1;
            while ((mantissa & 0x400u) == 0) {
                mantissa <<= 1;
                --exponent;
            }
            mantissa &= 0x3FFu;
            bits = (sign << 31) | (exponent << 23) | (mantissa << 13);
        }
    } else if (exponent == 0x1F) {
        bits = (sign << 31) | 0x7F800000u | (mantissa << 13);
    } else {
        bits = (sign << 31) | ((exponent - 15 + 127) << 23) | (mantissa << 13);
    }
    float out;
    memcpy(&out, &bits, sizeof(out));
    return out;
}

// [sub_180064020] LockRect the surface and convert the first
// min(count, w*h) texels to float4s (row-major through the pitch).
bool ReadSurfaceFloat4s(IDirect3DSurface9* surface, float* out,
                        unsigned int count)
{
    if (surface == nullptr) {
        return false;
    }
    if (count == 0) {
        return true;
    }
    D3DSURFACE_DESC desc;
    memset(&desc, 0, sizeof(desc));
    if (FAILED(surface->GetDesc(&desc))) {
        return false;
    }
    unsigned long long available =
        static_cast<unsigned long long>(desc.Width) * desc.Height;
    if (count > available) {
        count = static_cast<unsigned int>(available);
    }
    D3DLOCKED_RECT locked;
    memset(&locked, 0, sizeof(locked));
    if (FAILED(surface->LockRect(&locked, nullptr, 0))) {
        return false;
    }
    const unsigned char* base = static_cast<const unsigned char*>(locked.pBits);
    const unsigned int width = desc.Width;
    const float kInv255 = 1.0f / 255.0f;
    bool supported = true;
    for (unsigned int i = 0; i < count; ++i) {
        unsigned int x = i % width;
        unsigned int y = i / width;
        const unsigned char* p = base + static_cast<size_t>(y) * locked.Pitch;
        float* dst = out + static_cast<size_t>(i) * 4;
        dst[0] = dst[1] = dst[2] = 0.0f;
        dst[3] = 0.0f;
        switch (desc.Format) {
            case D3DFMT_R8G8B8: {
                const unsigned char* t = p + static_cast<size_t>(x) * 3;
                dst[0] = t[2] * kInv255;
                dst[1] = t[1] * kInv255;
                dst[2] = t[0] * kInv255;
                dst[3] = 0.0f;
                break;
            }
            case D3DFMT_A8R8G8B8:
            case D3DFMT_X8R8G8B8: {
                unsigned int pixel;
                memcpy(&pixel, p + static_cast<size_t>(x) * 4, 4);
                dst[0] = ((pixel >> 16) & 0xFF) * kInv255;
                dst[1] = ((pixel >> 8) & 0xFF) * kInv255;
                dst[2] = (pixel & 0xFF) * kInv255;
                dst[3] = ((pixel >> 24) & 0xFF) * kInv255;
                break;
            }
            case D3DFMT_A8B8G8R8: {
                const unsigned char* t = p + static_cast<size_t>(x) * 4;
                dst[0] = t[0] * kInv255;
                dst[1] = t[1] * kInv255;
                dst[2] = t[2] * kInv255;
                dst[3] = t[3] * kInv255;
                break;
            }
            case D3DFMT_R5G6B5: {
                unsigned short pixel;
                memcpy(&pixel, p + static_cast<size_t>(x) * 2, 2);
                dst[0] = ((pixel >> 11) & 0x1F) / 31.0f;
                dst[1] = ((pixel >> 5) & 0x3F) / 63.0f;
                dst[2] = (pixel & 0x1F) / 31.0f;
                break;
            }
            case D3DFMT_A1R5G5B5: {
                unsigned short pixel;
                memcpy(&pixel, p + static_cast<size_t>(x) * 2, 2);
                dst[0] = ((pixel >> 10) & 0x1F) / 31.0f;
                dst[1] = ((pixel >> 5) & 0x1F) / 31.0f;
                dst[2] = (pixel & 0x1F) / 31.0f;
                dst[3] = (pixel >> 15) & 1u ? 1.0f : 0.0f;
                break;
            }
            case D3DFMT_A4R4G4B4: {
                unsigned short pixel;
                memcpy(&pixel, p + static_cast<size_t>(x) * 2, 2);
                dst[0] = ((pixel >> 8) & 0xF) / 15.0f;
                dst[1] = ((pixel >> 4) & 0xF) / 15.0f;
                dst[2] = (pixel & 0xF) / 15.0f;
                dst[3] = ((pixel >> 12) & 0xF) / 15.0f;
                break;
            }
            case D3DFMT_A8: {
                dst[3] = p[x] * kInv255;
                break;
            }
            case D3DFMT_L8: {
                float l = p[x] * kInv255;
                dst[0] = dst[1] = dst[2] = l;
                dst[3] = 1.0f;
                break;
            }
            case D3DFMT_A8L8: {
                unsigned short pixel;
                memcpy(&pixel, p + static_cast<size_t>(x) * 2, 2);
                float l = (pixel & 0xFF) * kInv255;
                dst[0] = dst[1] = dst[2] = l;
                dst[3] = ((pixel >> 8) & 0xFF) * kInv255;
                break;
            }
            case D3DFMT_A16B16G16R16F: {
                unsigned short halves[4];
                memcpy(halves, p + static_cast<size_t>(x) * 8, 8);
                dst[0] = HalfToFloat(halves[0]);
                dst[1] = HalfToFloat(halves[1]);
                dst[2] = HalfToFloat(halves[2]);
                dst[3] = HalfToFloat(halves[3]);
                break;
            }
            case D3DFMT_A32B32G32R32F: {
                memcpy(dst, p + static_cast<size_t>(x) * 16, 16);
                break;
            }
            default:
                // The original's 97-case switch covers every D3DFMT; the
                // port converts the formats a bound texture can realistically
                // carry and reports failure (-> zeroed values) for the rest.
                supported = false;
                break;
        }
        if (!supported) {
            break;
        }
    }
    surface->UnlockRect();
    return supported;
}

// [sub_180063B30] read the current texture of `texParam` back into `out`.
// Returns false when the caller must zero the array (the original returns
// nonzero and sub_180063AA0 memsets + still calls SetVectorArray).
bool ReadTextureValueFloat4s(IDirect3DDevice9* device, ID3DXEffect* effect,
                             D3DXHANDLE texParam, float* out,
                             unsigned int elements)
{
    IDirect3DBaseTexture9* base = nullptr;
    if (effect->GetTexture(texParam, &base) != S_OK) {
        return false;
    }
    if (base == nullptr) {
        return false;   // NULL texture -> zeroed values
    }
    if (base->GetType() != D3DRTYPE_TEXTURE) {
        base->Release();
        return false;   // cube/volume: not read back
    }
    IDirect3DTexture9* texture = static_cast<IDirect3DTexture9*>(base);
    IDirect3DSurface9* surface = nullptr;
    if (texture->GetSurfaceLevel(0, &surface) != S_OK || surface == nullptr) {
        base->Release();
        return false;
    }
    D3DSURFACE_DESC desc;
    memset(&desc, 0, sizeof(desc));
    surface->GetDesc(&desc);
    IDirect3DSurface9* readSurface = surface;
    IDirect3DSurface9* staging = nullptr;
    if ((desc.Usage & D3DUSAGE_RENDERTARGET) != 0 &&
        desc.Pool == D3DPOOL_DEFAULT) {
        // A DEFAULT-pool render target cannot be locked: stage through
        // GetRenderTargetData into a SYSTEMMEM copy. (The original caches
        // the staging surface on the record; the port recreates it per call
        // - same bytes, no lifecycle coupling.)
        if (device == nullptr ||
            device->CreateOffscreenPlainSurface(
                desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM,
                &staging, nullptr) != S_OK ||
            staging == nullptr ||
            device->GetRenderTargetData(surface, staging) != S_OK) {
            if (staging != nullptr) {
                staging->Release();
            }
            surface->Release();
            base->Release();
            return false;
        }
        readSurface = staging;
    }
    bool ok = ReadSurfaceFloat4s(readSurface, out, elements);
    if (staging != nullptr) {
        staging->Release();
    }
    surface->Release();
    base->Release();
    return ok;
}

// [sub_18001B0A0 case 51 -> sub_180063AA0] one record's per-frame update.
void ConsumeTextureValue(IDirect3DDevice9* device, ID3DXEffect* effect,
                         const SasResource& res)
{
    int elements = res.textureValueElements;
    if (elements <= 0 || res.textureValueParam == nullptr) {
        return;   // SetVectorArray with count 0 is a no-op
    }
    std::vector<float> values(static_cast<size_t>(elements) * 4, 0.0f);
    ReadTextureValueFloat4s(device, effect, res.textureValueParam,
                            values.data(), static_cast<unsigned int>(elements));
    // Failures leave the array zeroed; SetVectorArray runs either way.
    effect->SetVectorArray(
        res.param,
        reinterpret_cast<const D3DXVECTOR4*>(values.data()),
        static_cast<unsigned int>(elements));
}

struct TextureValueScan {
    IDirect3DDevice9* device;
};

bool TextureValueVisitor(void* user, SasEffect* sas)
{
    TextureValueScan* scan = static_cast<TextureValueScan*>(user);
    if (sas == nullptr || sas->effect == nullptr) {
        return true;
    }
    for (size_t i = 0; i < sas->resources.size(); ++i) {
        const SasResource& res = sas->resources[i];
        if (res.semanticId == 0x33 && res.textureValueParam != nullptr) {
            ConsumeTextureValue(scan->device, sas->effect, res);
        }
    }
    return true;
}

// The live-effect collector for the prune below: the original's 0x2D records
// live INSIDE the sas object and die with it; the port's ctx+0x48 set must
// detect the same teardown by checking which effects the engine cache still
// enumerates (covers both file removal and cache eviction / re-parse).
struct LiveEffectScan {
    std::vector<ID3DXEffect*> effects;
};

bool LiveEffectVisitor(void* user, SasEffect* sas)
{
    LiveEffectScan* scan = static_cast<LiveEffectScan*>(user);
    if (sas != nullptr && sas->effect != nullptr) {
        scan->effects.push_back(sas->effect);
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// The tick
// ---------------------------------------------------------------------------

void MmeAnimeTick(MmeContext* ctx)
{
    if (ctx == nullptr || ctx->animatedTextures == nullptr) {
        return;
    }
    MmeAnimatedTextureSet* set =
        static_cast<MmeAnimatedTextureSet*>(ctx->animatedTextures);
    AnimeSetImpl* impl = static_cast<AnimeSetImpl*>(set->impl);
    if (impl == nullptr) {
        return;
    }

    // [sub_18001B0A0 case 51] the 0x33 TEXTUREVALUE consumption runs with
    // the pre-draw registry walk (before the techniques of this frame).
    if (ctx->device != nullptr) {
        TextureValueScan scan;
        scan.device = ctx->device;
        MmeEngineForEachSas(TextureValueVisitor, &scan);
    }

    // [FUN_180001320 / sub_18001B5B0 case 45] prune entries whose owning
    // effect left the engine cache (the original's records die WITH their
    // sas object; the port detects the same teardown by matching the live
    // effect pointers - covers file removal, cache eviction and re-parse),
    // then advance every entry.
    LiveEffectScan live;
    MmeEngineForEachSas(LiveEffectVisitor, &live);
    for (size_t i = impl->entries.size(); i > 0; --i) {
        AnimeEntry* entry = impl->entries[i - 1];
        bool alive = false;
        for (size_t k = 0; k < live.effects.size(); ++k) {
            if (live.effects[k] == entry->effect) {
                alive = true;
                break;
            }
        }
        if (!alive) {
            delete entry;
            impl->entries.erase(impl->entries.begin() + (i - 1));
        }
    }
    for (size_t i = 0; i < impl->entries.size(); ++i) {
        AnimeEntry* entry = impl->entries[i];
        if (entry->effect == nullptr || entry->param == nullptr ||
            entry->object == nullptr) {
            continue;
        }
        // [sub_18001B5B0 case 45] the animation time: the SeekVariable's
        // live value (GetFloatArray(handle, &v, 1)) or the host clock.
        double time = 0.0;
        if (entry->seekParam != nullptr) {
            float value = 0.0f;
            if (entry->effect->GetFloatArray(entry->seekParam, &value, 1) ==
                S_OK) {
                time = value;
            }
        } else {
            // The host clock (the walk object's vtbl+0x70/112 float getter).
            // Verified (x64): the walk object is the ModelData instance the
            // Draw methods pass as this-8 (sub_18005A740 @ 0x18005A840 /
            // sub_18005A9C0 @ 0x18005ABC3); its primary vftable slot 14
            // (0x1800B5A28 -> sub_1800583F0 @ 0x1800583F8) returns
            // DAT_1800d98fc - the RAW frame clock (written unconditionally
            // to GetCurrentFrameTime() at 0x1800570A0), NOT the "Time"
            // semantic clock DAT_1800d9904 (play = frame time, edit = wall
            // clock since the mode switch). x86 cross-check: slot 14 via
            // vtbl+0x38 (0x100AB3A4 -> sub_10050E30 @ 0x10050E36) returns
            // flt_100BB798 (= GetCurrentFrameTime() at 0x1004FD2E).
            time = g_lastFrameTime;
        }
        entry->object->SetFrame(time);
        entry->effect->SetTexture(entry->param, entry->object->GetTexture());
    }
}

} // namespace mme
