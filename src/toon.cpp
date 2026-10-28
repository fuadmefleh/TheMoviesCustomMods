// Toon Studio spike: the cel-shading pass. Settings: [Toon] in MoviesMod.ini.
//
// Animated films are to be drawn as cartoons wherever they play. This is the look on its own:
// a post-process on the finished frame (inside Present, before the mod's windows), so it works
// without knowing how the game renders a film. The frame is copied to a texture and drawn back
// in two passes: a Kuwahara filter flattens texture detail into paint, then ink is traced on
// the flattened image and its brightness is pulled into soft cel bands. The hand-drawn style
// (before 1995) has bolder ink, paper grain and lines that "boil", redrawn 12 times a second
// like cel animation; the CGI style is softer. Sample spacing grows with the screen height, so
// lines look the same at any resolution.
//
// For now it is a preview: Ctrl+F9 turns it on over anything (and saves a screenshot),
// Ctrl+Shift+F9 switches style. The game's own interface is part of the frame, so it is
// toon-shaded too. To find when a film is playing, changes of the game-mode stack's top
// (0x00F87B04, 0 = the lot) are logged: watch a film with the log open and note its mode.

#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <cstdint>

#include "game.h"
#include "mod.h"
#include "overlay.h"

namespace toon {
namespace {

template <typename T> T Rd(uintptr_t a) { return *reinterpret_cast<const T*>(a); }

constexpr uintptr_t kModes   = 0x00F87B04;   // the game-mode stack
constexpr uintptr_t kModeTop = 0x00423320;   // int __thiscall(modes)

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

void DrawQuad(IDirect3DDevice9* device, IDirect3DSurface9* target, float w, float h)
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
    float spacing = h / 600.0f;   // the look was tuned on 800x600 screenshots
    float drawing = static_cast<float>((GetTickCount() / 83) % 1000);   // 12 drawings a second
    const float constants[12] = {
        1.0f / w, 1.0f / h, w, h,
        g_handDrawn ? 1.0f : 0.0f, drawing, static_cast<float>(g_bands), g_strength,
        spacing < 1 ? 1 : spacing, spacing * 0.75f < 1 ? 1 : spacing * 0.75f, 0, 0,
    };
    if (SUCCEEDED(device->BeginScene())) {
        device->SetPixelShaderConstantF(0, constants, 3);
        device->SetPixelShader(g_flatten);
        device->SetTexture(0, g_copy);
        DrawQuad(device, flat, w, h);
        device->SetPixelShader(g_ink);
        device->SetTexture(0, g_flat);
        device->SetTexture(1, g_copy);
        DrawQuad(device, back, w, h);
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
    if (g_enabled && g_preview && !g_failed)
        Draw(static_cast<IDirect3DDevice9*>(device));
}

bool OurWindowFocused()
{
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

int ModeTop()
{
    __try {
        uintptr_t modes = Rd<uintptr_t>(kModes);
        return modes ? reinterpret_cast<int(__thiscall*)(uintptr_t)>(kModeTop)(modes) : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -2;
    }
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

    static int lastMode = -100;
    int mode = game::Ready() ? ModeTop() : -1;
    if (mode != lastMode) {
        mod::Log("toon: game mode %d -> %d (day %.1f)", lastMode, mode, game::Ready() ? game::DateDays() : 0.0f);
        lastMode = mode;
    }
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
