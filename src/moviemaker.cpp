// Movie Maker and picture quality. Settings: [MovieMaker] in MoviesMod.ini.
//
//  * Export size. The exporter's width is hard-coded (0x009720B0: mov eax, 768; ret) and the
//    height is width x 9/16, so the best WMV preset is 768x432 and community "resolution
//    changer" profiles only upscale it. We patch the width and write matching loose
//    Data\video\wmvprofile_best(_na).prx files from the game's own profiles
//    (<game>\MoviesMod\video_templates), with the bitrate scaled to the picture size.
//  * Frame rate. The exporter uses 30 fps only for one preset index and 20 for the rest
//    (0x00A2B806: mov [esp+0x850], 20); we make it 30.
//  * Sharper presets. Every preset but Best renders into a 512x512 target before scaling
//    (0x007637B7: jnz past the Best branch); we let them all render at screen size.
//  * People's detail. Every person's costume and face are painted at run time into one
//    texture each, 512x512 at most (0x009D0F30: push 0x200); CharacterTextureSize raises it.
//  * Instant shooting. The sandbox option sbx_shootingoff (index 3, set via 0x004AF4C0)
//    completes every shoot at once; InstantShoot turns it on in any game.
//  Extra title colours come from a loose Data\postproc\titles.ini (content/data/postproc).

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>

#include "mod.h"
#include "overlay.h"

namespace moviemaker {
namespace {

constexpr uintptr_t kExportWidthFn = 0x009720B0;   // B8 00 03 00 00 C3
constexpr uintptr_t kExportFps     = 0x00A2B806;   // C7 84 24 50 08 00 00 14 00 00 00
constexpr uintptr_t kPresetBranch  = 0x007637B7;   // 75 1A
constexpr uintptr_t kCharTexture   = 0x009D0F30;   // 68 00 02 00 00
constexpr uintptr_t kSandboxSet    = 0x004AF4C0;   // void __cdecl(int option, float value)
constexpr uintptr_t kGameLoaded    = 0x0104BCE4;
constexpr int kOptionInstantShoot = 3;

bool  g_instantShoot = false;
void* g_session = nullptr;

bool Patch(uintptr_t at, const void* expect, size_t expectLen, size_t offset, const void* bytes, size_t len,
           const char* what)
{
    if (memcmp(reinterpret_cast<void*>(at), expect, expectLen) != 0) {
        mod::Log("moviemaker: %s - code at %08X is not what we expect, left alone", what, static_cast<unsigned>(at));
        return false;
    }
    DWORD old;
    auto target = reinterpret_cast<void*>(at + offset);
    if (!VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(target, bytes, len);
    VirtualProtect(target, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, len);
    return true;
}

void ReplaceAll(std::wstring& s, const std::wstring& from, const std::wstring& to)
{
    for (size_t at = 0; (at = s.find(from, at)) != std::wstring::npos; at += to.size())
        s.replace(at, from.size(), to);
}

// Writes Data\video\<name> from the game's 768x432 profile with our size and bitrate.
bool WriteProfile(const wchar_t* name, int width, int height)
{
    wchar_t src[MAX_PATH], dst[MAX_PATH], dir[MAX_PATH];
    swprintf_s(src, L"%s\\MoviesMod\\video_templates\\%s", mod::GameDir(), name);
    swprintf_s(dir, L"%s\\Data\\video", mod::GameDir());
    swprintf_s(dst, L"%s\\%s", dir, name);
    FILE* f = _wfopen(src, L"rb");
    if (!f)
        return false;
    std::string raw;
    char buf[4096];
    for (size_t got; (got = fread(buf, 1, sizeof(buf), f)) > 0;)
        raw.append(buf, got);
    fclose(f);
    if (raw.size() < 4)
        return false;
    std::wstring text(reinterpret_cast<const wchar_t*>(raw.data()), raw.size() / 2);

    double scale = static_cast<double>(width) * height / (768.0 * 432.0);
    long long bitrate = static_cast<long long>(8192000 * scale);
    if (bitrate > 24000000)
        bitrate = 24000000;
    std::wstring w = std::to_wstring(width), h = std::to_wstring(height), b = std::to_wstring(bitrate);
    ReplaceAll(text, L"right=\"768\"", L"right=\"" + w + L"\"");
    ReplaceAll(text, L"bottom=\"432\"", L"bottom=\"" + h + L"\"");
    ReplaceAll(text, L"biwidth=\"768\"", L"biwidth=\"" + w + L"\"");
    ReplaceAll(text, L"biheight=\"432\"", L"biheight=\"" + h + L"\"");
    ReplaceAll(text, L"\"8192000\"", L"\"" + b + L"\"");
    ReplaceAll(text, L"(768 x 432)", L"(" + w + L" x " + h + L", MoviesMod)");

    CreateDirectoryW(dir, nullptr);
    f = _wfopen(dst, L"wb");
    if (!f)
        return false;
    fwrite(text.data(), sizeof(wchar_t), text.size(), f);
    fclose(f);
    return true;
}

// Our loose profiles, if the size is back to the game's own.
void RemoveProfiles()
{
    for (const wchar_t* name : { L"wmvprofile_best.prx", L"wmvprofile_best_na.prx" }) {
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\Data\\video\\%s", mod::GameDir(), name);
        FILE* f = _wfopen(path, L"rb");
        if (!f)
            continue;
        wchar_t head[512] = {};
        fread(head, sizeof(wchar_t), 511, f);
        fclose(f);
        if (wcsstr(head, L"MoviesMod"))
            DeleteFileW(path);
    }
}

void SetInstantShoot()
{
    __try {
        reinterpret_cast<void(__cdecl*)(int, float)>(kSandboxSet)(kOptionInstantShoot, 1.0f);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        mod::Log("moviemaker: FAILED to turn on instant shooting");
        g_instantShoot = false;
    }
}

void Frame()
{
    void* session = *reinterpret_cast<void**>(kGameLoaded);
    if (session == g_session)
        return;
    g_session = session;
    if (session && g_instantShoot) {
        SetInstantShoot();
        mod::Log("moviemaker: instant shooting on for this game");
    }
}

} // namespace

void Install()
{
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\MoviesMod.ini", mod::GameDir());
    auto get = [&](const wchar_t* key, int def) { return static_cast<int>(GetPrivateProfileIntW(L"MovieMaker", key, def, ini)); };

    int width = get(L"ExportWidth", 1280);
    width = width < 320 ? 320 : width > 2560 ? 2560 : width & ~15;
    if (width != 768) {
        static const uint8_t expect[] = { 0xB8, 0x00, 0x03, 0x00, 0x00, 0xC3 };
        uint32_t w = static_cast<uint32_t>(width);
        int height = (width * 9 / 16) & ~1;
        bool a = WriteProfile(L"wmvprofile_best.prx", width, height);
        bool b = WriteProfile(L"wmvprofile_best_na.prx", width, height);
        if (a && b && Patch(kExportWidthFn, expect, sizeof(expect), 1, &w, 4, "export width"))
            mod::Log("moviemaker: Best export is %dx%d", width, height);
        else
            mod::Log("moviemaker: export size left at 768x432 (profiles %s)", a && b ? "written" : "MISSING templates");
    } else {
        RemoveProfiles();
    }

    if (get(L"Export30fps", 1)) {
        static const uint8_t expect[] = { 0xC7, 0x84, 0x24, 0x50, 0x08, 0x00, 0x00, 0x14 };
        uint8_t fps = 30;
        if (Patch(kExportFps, expect, sizeof(expect), 7, &fps, 1, "export frame rate"))
            mod::Log("moviemaker: exports at 30 fps");
    }
    if (get(L"SharpPresets", 1)) {
        static const uint8_t expect[] = { 0x75, 0x1A };
        static const uint8_t nops[] = { 0x90, 0x90 };
        if (Patch(kPresetBranch, expect, sizeof(expect), 0, nops, 2, "sharper presets"))
            mod::Log("moviemaker: every export preset renders at screen size");
    }
    int texture = get(L"CharacterTextureSize", 512);
    if (texture == 1024 || texture == 2048) {
        static const uint8_t expect[] = { 0x68, 0x00, 0x02, 0x00, 0x00 };
        uint32_t size = static_cast<uint32_t>(texture);
        if (Patch(kCharTexture, expect, sizeof(expect), 1, &size, 4, "character texture size"))
            mod::Log("moviemaker: people's costume and face textures up to %dx%d (Best graphics)", texture, texture);
    }
    g_instantShoot = get(L"InstantShoot", 0) != 0;
    overlay::AddFrameCallback(&Frame);
}

} // namespace moviemaker
