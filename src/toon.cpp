// Toon Studio: the cel-shading pass. Settings: [Toon] in MoviesMod.ini.
//
// Cartoons (tv_animation.cpp) are drawn as cartoons when they play. The look is a
// post-process on the finished frame (inside Present, before the mod's windows), so it works
// without knowing how the game renders a film. The frame is copied to a texture and drawn back
// in two passes: a Kuwahara filter flattens texture detail into paint, then ink is traced on
// the flattened image and its brightness is pulled into soft cel bands. The hand-drawn style
// (before 1995) has bolder ink, paper grain and lines that "boil", redrawn 12 times a second
// like cel animation; the CGI style is softer. Sample spacing grows with the screen height, so
// lines look the same at any resolution.
//
// A film plays in the film viewer (WTrailerEditorLite, vtable 0x00D4F0AC), opened by
// 0x007843B0(CProject*, ...) from the films list, the premiere and the cinema rooms: the open
// viewer is [0x0104E478] and its film [0x0104E490]. While a cartoon is playing there, the pass
// runs in its style (hand-drawn, or CGI for a cartoon from the Render Farm), only inside the
// viewer's picture: the largest WViewport* among the viewer's windows (children are a list
// from +0x120, linked at the child's +0x150; a child's parent is at +0x118). The rest of the
// viewer (its buttons, the timeline) stays as it is. Without a picture found, the whole screen.
//
// Ctrl+F9 still previews the look over anything (and saves a screenshot), Ctrl+Shift+F9
// switches the preview's style.

#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <cstdint>
#include <string>

#include "game.h"
#include "mod.h"
#include "overlay.h"

namespace toon {
namespace {

template <typename T> T Rd(uintptr_t a) { return *reinterpret_cast<const T*>(a); }

constexpr uintptr_t kViewer = 0x0104E478;          // the open film viewer, or 0
constexpr uintptr_t kViewerFilm = 0x0104E490;      // its CProject
constexpr uintptr_t kViewerVtable = 0x00D4F0AC;    // WTrailerEditorLite
constexpr uintptr_t kScreenWindow = 0x0071B2A0;    // the root window: cdecl() -> WWindow*
constexpr int kChildren = 0x120, kChildLink = 0x150, kParent = 0x118;
constexpr int kLeft = 0xC0, kTop = 0x9C, kRight = 0x108, kBottom = 0xE4;

// The cartoon playing now (none if film is 0), and where its picture is on the screen.
struct Playing {
    uintptr_t viewer = 0, film = 0;
    int       style = 0;          // 1 hand-drawn, 2 CGI
    bool      hasPicture = false;
    float     picture[4] = {};    // left, top, right, bottom, in interface units
    float     screen[2] = {};     // the interface's width and height
    DWORD     looked = 0;         // when the picture was last looked for
};
Playing g_playing;

bool  g_enabled = true;
bool  g_preview = false;
bool  g_handDrawn = true;
float g_strength = 1.0f;   // 0 = untouched frame, 1 = full cartoon
int   g_bands = 5;         // colour bands in the hand-drawn style

IDirect3DTexture9*     g_copy = nullptr;   // the game's frame
IDirect3DTexture9*     g_flat = nullptr;   // after pass 1
IDirect3DPixelShader9* g_flatten = nullptr;
IDirect3DPixelShader9* g_ink = nullptr;
UINT g_width = 0, g_height = 0;
D3DFORMAT g_format = D3DFMT_UNKNOWN;
bool g_failed = false;   // shader compile or resource creation failed: stay off

// Pass 1: Kuwahara filter. Of the four quadrants around a pixel, take the mean colour of the one
// with the least variation: texture detail flattens into paint while shape edges stay sharp.
const char kFlattenShader[] = R"(
sampler2D scene : register(s0);
float4 texel : register(c0);   // 1/w, 1/h, w, h
float4 style : register(c1);   // x: hand-drawn 1 / CGI 0, y: drawing number, z: bands, w: strength
float4 scale : register(c2);   // x: sample spacing in pixels

float Lum(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 t = texel.xy * scale.x;
    float3 best = 0;
    float bestVar = 1e9;
    const float2 dirs[4] = { float2(-1, -1), float2(1, -1), float2(-1, 1), float2(1, 1) };
    [unroll] for (int q = 0; q < 4; q++) {
        float2 dir = dirs[q];
        float3 sum = 0;
        float s = 0, s2 = 0;
        [unroll] for (int i = 0; i < 3; i++)
            [unroll] for (int j = 0; j < 3; j++) {
                float3 c = tex2D(scene, uv + dir * t * float2(i, j)).rgb;
                float l = Lum(c);
                sum += c;
                s += l;
                s2 += l * l;
            }
        float var = s2 / 9 - (s / 9) * (s / 9);
        if (var < bestVar) {
            bestVar = var;
            best = sum / 9;
        }
    }
    return float4(best, 1);
}
)";

// Pass 2: ink traced on the flattened image (strongest edge of the three colour channels), soft
// cel bands that keep the hue, stronger colour and, for hand-drawn films, paper and boiling lines.
const char kInkShader[] = R"(
sampler2D flat : register(s0);
sampler2D scene : register(s1);
float4 texel : register(c0);
float4 style : register(c1);
float4 scale : register(c2);   // y: ink sample spacing in pixels

float Lum(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }
float Hash(float2 p) { return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453); }

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 t = texel.xy * scale.y;
    // Hand-drawn lines wobble a little, redrawn 12 times a second.
    float2 cell = floor(uv * texel.zw / 40) + style.y;
    float2 e = uv + style.x * (float2(Hash(cell), Hash(cell + 7.13)) - 0.5) * t * 1.2;
    float3 tl = tex2D(flat, e + t * float2(-1, -1)).rgb, tm = tex2D(flat, e + t * float2(0, -1)).rgb;
    float3 tr = tex2D(flat, e + t * float2(1, -1)).rgb, ml = tex2D(flat, e + t * float2(-1, 0)).rgb;
    float3 mr = tex2D(flat, e + t * float2(1, 0)).rgb, bl = tex2D(flat, e + t * float2(-1, 1)).rgb;
    float3 bm = tex2D(flat, e + t * float2(0, 1)).rgb, br = tex2D(flat, e + t * float2(1, 1)).rgb;
    float3 gx = (tr + 2 * mr + br) - (tl + 2 * ml + bl);
    float3 gy = (bl + 2 * bm + br) - (tl + 2 * tm + tr);
    float3 g = sqrt(gx * gx + gy * gy);
    float edge = max(g.r, max(g.g, g.b));
    float ink = smoothstep(lerp(0.55, 0.35, style.x), lerp(0.95, 0.70, style.x), edge);

    float3 k = tex2D(flat, uv).rgb;
    float l = Lum(k);
    float bands = lerp(8, style.z, style.x);
    float x = l * bands;
    float stepped = (floor(x) + smoothstep(0.35, 0.65, frac(x))) / bands;
    float q = lerp(l, stepped, lerp(0.5, 0.85, style.x));
    float3 paint = k * (q / max(l, 0.03));
    paint = lerp(Lum(paint).xxx, paint, lerp(1.12, 1.25, style.x));
    float grain = (Hash(uv * texel.zw + style.y) - 0.5) * 0.035 * style.x;
    paint = paint * lerp(float3(1, 1, 1), float3(1.0, 0.975, 0.93), 0.5 * style.x) + grain;

    float3 o = lerp(saturate(paint), float3(0.10, 0.07, 0.06), ink * lerp(0.55, 0.9, style.x));
    return float4(lerp(tex2D(scene, uv).rgb, o, style.w), 1);
}
)";

using D3DCompile_t = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR,
                                      UINT, UINT, ID3DBlob**, ID3DBlob**);

IDirect3DPixelShader9* CompileShader(IDirect3DDevice9* device, D3DCompile_t compile, const char* source, size_t size,
                                     const char* name)
{
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    HRESULT hr = compile(source, size, name, nullptr, nullptr, "main", "ps_3_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                         &code, &errors);
    if (FAILED(hr)) {
        mod::Log("toon: %s shader compile failed: %s", name,
                 errors ? static_cast<const char*>(errors->GetBufferPointer()) : "?");
        if (errors)
            errors->Release();
        return nullptr;
    }
    if (errors)
        errors->Release();
    IDirect3DPixelShader9* shader = nullptr;
    hr = device->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()), &shader);
    code->Release();
    if (FAILED(hr))
        mod::Log("toon: CreatePixelShader %s failed (0x%08lX)", name, hr);
    return SUCCEEDED(hr) ? shader : nullptr;
}

bool CompileShaders(IDirect3DDevice9* device)
{
    HMODULE dll = LoadLibraryW(L"d3dcompiler_47.dll");
    auto compile = dll ? reinterpret_cast<D3DCompile_t>(GetProcAddress(dll, "D3DCompile")) : nullptr;
    if (!compile) {
        mod::Log("toon: d3dcompiler_47.dll not found - cel shading off");
        return false;
    }
    g_flatten = CompileShader(device, compile, kFlattenShader, sizeof(kFlattenShader) - 1, "flatten");
    g_ink = CompileShader(device, compile, kInkShader, sizeof(kInkShader) - 1, "ink");
    return g_flatten && g_ink;
}

void Release()
{
    for (IDirect3DTexture9** t : { &g_copy, &g_flat }) {
        if (*t)
            (*t)->Release();
        *t = nullptr;
    }
    g_width = g_height = 0;
}

bool EnsureTextures(IDirect3DDevice9* device, const D3DSURFACE_DESC& desc)
{
    if (g_copy && desc.Width == g_width && desc.Height == g_height && desc.Format == g_format)
        return true;
    Release();
    HRESULT hr = device->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT,
                                       &g_copy, nullptr);
    if (SUCCEEDED(hr))
        hr = device->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT,
                                   &g_flat, nullptr);
    if (FAILED(hr)) {
        mod::Log("toon: frame textures %ux%u format %d failed (0x%08lX)", desc.Width, desc.Height,
                 static_cast<int>(desc.Format), hr);
        Release();
        return false;
    }
    g_width = desc.Width;
    g_height = desc.Height;
    g_format = desc.Format;
    return true;
}

void SetCommonState(IDirect3DDevice9* device)
{
    device->SetVertexShader(nullptr);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    device->SetRenderState(D3DRS_ZENABLE, FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    for (DWORD s = 0; s < 2; s++) {
        device->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        device->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        device->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        device->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        device->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
    }
}

// The picture's rectangle on the back buffer, if a cartoon is playing in a known picture.
bool PictureRect(float w, float h, RECT* r)
{
    if (!g_playing.film || !g_playing.hasPicture || g_playing.screen[0] <= 0 || g_playing.screen[1] <= 0)
        return false;
    float sx = w / g_playing.screen[0], sy = h / g_playing.screen[1];
    r->left = static_cast<LONG>(g_playing.picture[0] * sx);
    r->top = static_cast<LONG>(g_playing.picture[1] * sy);
    r->right = static_cast<LONG>(g_playing.picture[2] * sx + 0.5f);
    r->bottom = static_cast<LONG>(g_playing.picture[3] * sy + 0.5f);
    return r->right > r->left && r->bottom > r->top;
}

// Draws the full-screen quad into target; with clip, only inside it (SetRenderTarget resets the
// scissor rectangle, so it is set after).
void DrawQuad(IDirect3DDevice9* device, IDirect3DSurface9* target, float w, float h, const RECT* clip)
{
    struct Vertex {
        float x, y, z, rhw, u, v;
    };
    const Vertex quad[4] = {
        { -0.5f, -0.5f, 0, 1, 0, 0 }, { w - 0.5f, -0.5f, 0, 1, 1, 0 },
        { -0.5f, h - 0.5f, 0, 1, 0, 1 }, { w - 0.5f, h - 0.5f, 0, 1, 1, 1 },
    };
    device->SetRenderTarget(0, target);
    D3DVIEWPORT9 vp = { 0, 0, static_cast<DWORD>(w), static_cast<DWORD>(h), 0.0f, 1.0f };
    device->SetViewport(&vp);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE, clip ? TRUE : FALSE);
    if (clip)
        device->SetScissorRect(clip);
    device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
}

void Draw(IDirect3DDevice9* device)
{
    if (!g_flatten && !CompileShaders(device)) {
        g_failed = true;
        return;
    }
    IDirect3DSurface9* back = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
        return;
    D3DSURFACE_DESC desc{};
    back->GetDesc(&desc);
    IDirect3DSurface9* copy = nullptr;
    IDirect3DSurface9* flat = nullptr;
    if (!EnsureTextures(device, desc) || FAILED(g_copy->GetSurfaceLevel(0, &copy)) ||
        FAILED(g_flat->GetSurfaceLevel(0, &flat))) {
        if (copy)
            copy->Release();
        back->Release();
        g_failed = true;
        return;
    }
    IDirect3DStateBlock9* saved = nullptr;
    if (FAILED(device->StretchRect(back, nullptr, copy, nullptr, D3DTEXF_NONE)) ||
        FAILED(device->CreateStateBlock(D3DSBT_ALL, &saved))) {
        copy->Release();
        flat->Release();
        back->Release();
        return;
    }
    IDirect3DSurface9* oldTarget = nullptr;
    device->GetRenderTarget(0, &oldTarget);
    SetCommonState(device);

    float w = static_cast<float>(desc.Width), h = static_cast<float>(desc.Height);
    // A playing cartoon: only its picture, in its style. The preview: everything, as chosen.
    RECT picture;
    bool clip = !g_preview && PictureRect(w, h, &picture);
    bool handDrawn = g_preview ? g_handDrawn : g_playing.style != 2;
    float spacing = h / 600.0f;   // the look was tuned on 800x600 screenshots
    float drawing = static_cast<float>((GetTickCount() / 83) % 1000);   // 12 drawings a second
    const float constants[12] = {
        1.0f / w, 1.0f / h, w, h,
        handDrawn ? 1.0f : 0.0f, drawing, static_cast<float>(g_bands), g_strength,
        spacing < 1 ? 1 : spacing, spacing * 0.75f < 1 ? 1 : spacing * 0.75f, 0, 0,
    };
    if (SUCCEEDED(device->BeginScene())) {
        device->SetPixelShaderConstantF(0, constants, 3);
        device->SetPixelShader(g_flatten);
        device->SetTexture(0, g_copy);
        DrawQuad(device, flat, w, h, clip ? &picture : nullptr);
        device->SetPixelShader(g_ink);
        device->SetTexture(0, g_flat);
        device->SetTexture(1, g_copy);
        DrawQuad(device, back, w, h, clip ? &picture : nullptr);
        device->EndScene();
    }

    device->SetTexture(0, nullptr);
    device->SetTexture(1, nullptr);
    if (oldTarget) {
        device->SetRenderTarget(0, oldTarget);
        oldTarget->Release();
    }
    saved->Apply();
    saved->Release();
    copy->Release();
    flat->Release();
    back->Release();
}

void PostProcess(void* device)
{
    if (g_enabled && !g_failed && (g_preview || g_playing.film))
        Draw(static_cast<IDirect3DDevice9*>(device));
}

bool OurWindowFocused()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

bool ReadPtr(uintptr_t at, uintptr_t* out)
{
    __try {
        *out = Rd<uintptr_t>(at);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadRect(uintptr_t window, float r[4])
{
    __try {
        r[0] = Rd<float>(window + kLeft);
        r[1] = Rd<float>(window + kTop);
        r[2] = Rd<float>(window + kRight);
        r[3] = Rd<float>(window + kBottom);
        return r[2] > r[0] && r[3] > r[1];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uintptr_t ScreenWindow()
{
    __try {
        return reinterpret_cast<uintptr_t(__cdecl*)()>(kScreenWindow)();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// The film viewer if one is open and showing on the screen, and its film.
uintptr_t OpenViewer(uintptr_t* film)
{
    uintptr_t viewer = 0, vtable = 0, parent = 0;
    if (!ReadPtr(kViewer, &viewer) || !viewer || !ReadPtr(viewer, &vtable) || vtable != kViewerVtable ||
        !ReadPtr(viewer + kParent, &parent) || !parent || !ReadPtr(kViewerFilm, film) || !*film)
        return 0;
    return game::ClassName(*film) == "CProject" ? viewer : 0;
}

// The largest WViewport* under a window (the film's picture), searched a few levels deep.
void FindPicture(uintptr_t window, int depth, uintptr_t* best, float* bestArea, std::string* seen)
{
    uintptr_t head = window + kChildren, node = 0;
    if (!ReadPtr(head, &node))
        return;
    for (int n = 0; node && node != head && n < 200; n++) {
        uintptr_t child = node - kChildLink;
        std::string name = game::ClassName(child);
        if (depth == 0 && seen->size() < 600)
            *seen += (seen->empty() ? "" : ", ") + name;
        float r[4];
        if (name.rfind("WViewport", 0) == 0 && ReadRect(child, r) && (r[2] - r[0]) * (r[3] - r[1]) > *bestArea) {
            *bestArea = (r[2] - r[0]) * (r[3] - r[1]);
            *best = child;
        }
        if (depth < 5)
            FindPicture(child, depth + 1, best, bestArea, seen);
        if (!ReadPtr(node, &node))
            break;
    }
}

void LookForPicture()
{
    uintptr_t picture = 0;
    float area = 0;
    std::string seen;
    FindPicture(g_playing.viewer, 0, &picture, &area, &seen);
    float screen[4];
    uintptr_t root = ScreenWindow();
    bool had = g_playing.hasPicture;
    g_playing.hasPicture = picture && ReadRect(picture, g_playing.picture) && root && ReadRect(root, screen);
    if (g_playing.hasPicture) {
        g_playing.screen[0] = screen[2] - screen[0];
        g_playing.screen[1] = screen[3] - screen[1];
    }
    if (g_playing.hasPicture != had || !g_playing.looked)
        mod::Log(g_playing.hasPicture ? "toon: the picture is %s at %.0f,%.0f - %.0f,%.0f of %.0fx%.0f"
                                      : "toon: no picture in the viewer yet (%s) - the whole screen",
                 g_playing.hasPicture ? game::ClassName(picture).c_str() : seen.c_str(), g_playing.picture[0],
                 g_playing.picture[1], g_playing.picture[2], g_playing.picture[3], g_playing.screen[0],
                 g_playing.screen[1]);
    g_playing.looked = GetTickCount();
}

// Is a cartoon playing? Checked every frame; the picture looked for twice a second.
void WatchViewer()
{
    uintptr_t film = 0;
    uintptr_t viewer = game::Ready() ? OpenViewer(&film) : 0;
    if (viewer != g_playing.viewer || film != g_playing.film) {
        int style = viewer ? tv::CartoonStyle(film) : 0;
        if (viewer)
            mod::Log("toon: the film viewer shows '%ls' - %s", game::ReadProjectAt(film).title.c_str(),
                     style == 2 ? "a CGI cartoon" : style == 1 ? "a hand-drawn cartoon" : "not a cartoon");
        else if (g_playing.film)
            mod::Log("toon: the film viewer closed");
        g_playing = Playing{};
        g_playing.viewer = viewer;
        if (style) {
            g_playing.film = film;
            g_playing.style = style;
        }
    }
    if (g_playing.film && GetTickCount() - g_playing.looked > 500)
        LookForPicture();
}

void Frame()
{
    // Ctrl+F9: preview on/off; Ctrl+Shift+F9: hand-drawn or CGI.
    static bool keyDown = false;
    bool down = (GetAsyncKeyState(VK_F9) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000);
    if (down && !keyDown && OurWindowFocused()) {
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
            g_handDrawn = !g_handDrawn;
        else
            g_preview = !g_preview;
        mod::Log("toon: preview %s, %s style", g_preview ? "on" : "off", g_handDrawn ? "hand-drawn" : "CGI");
        if (g_preview)
            overlay::RequestScreenshot(g_handDrawn ? "toon_handdrawn" : "toon_cgi");
    }
    keyDown = down;
    WatchViewer();
}

} // namespace

void Install()
{
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\MoviesMod.ini", mod::GameDir());
    g_enabled = GetPrivateProfileIntW(L"Toon", L"CelShading", 1, ini) != 0;
    g_bands = static_cast<int>(GetPrivateProfileIntW(L"Toon", L"Bands", 5, ini));
    if (g_bands < 2)
        g_bands = 2;
    wchar_t buf[32];
    GetPrivateProfileStringW(L"Toon", L"Strength", L"", buf, 32, ini);
    if (buf[0])
        g_strength = static_cast<float>(_wtof(buf));
    if (!g_enabled) {
        mod::Log("toon: cel shading off");
        return;
    }
    overlay::AddPostProcess(&PostProcess, &Release);
    overlay::AddFrameCallback(&Frame);
    mod::Log("toon: cel shading ready - Ctrl+F9 previews it, Ctrl+Shift+F9 switches hand-drawn/CGI (%d bands, strength %.2f)",
             g_bands, g_strength);
}

} // namespace toon
