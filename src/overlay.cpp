// In-game overlay: Dear ImGui drawn on the game's Direct3D 9 device.
//
// We do not know whether the game creates its device before or after our DLL is
// loaded, so instead of hooking device creation we create a throwaway device,
// read IDirect3DDevice9's vtable (shared by every device from d3d9.dll), and hook
// Present/Reset there. The first Present call gives us the game's device and window.

#define DIRECTINPUT_VERSION 0x0800
#include "overlay.h"

#include <d3d9.h>
#include <dinput.h>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "MinHook.h"
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "mod.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace overlay {
namespace {

constexpr int kSlotReset   = 16;
constexpr int kSlotPresent = 17;
constexpr int kSlotDICreateDevice    = 3;   // IDirectInput8A
constexpr int kSlotDIGetDeviceState  = 9;   // IDirectInputDevice8A
constexpr int kSlotDIGetDeviceData   = 10;

using Present_t = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using Reset_t   = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using DICreateDevice_t = HRESULT(WINAPI*)(IDirectInput8A*, REFGUID, LPDIRECTINPUTDEVICE8A*, LPUNKNOWN);
using DIGetState_t     = HRESULT(WINAPI*)(IDirectInputDevice8A*, DWORD, LPVOID);
using DIGetData_t      = HRESULT(WINAPI*)(IDirectInputDevice8A*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);

Present_t        g_origPresent = nullptr;
Reset_t          g_origReset = nullptr;
DICreateDevice_t g_origDICreateDevice = nullptr;
DIGetState_t     g_origDIGetState = nullptr;
DIGetData_t      g_origDIGetData = nullptr;

bool    g_imguiReady = false;
bool    g_open = false;
ImFont* g_titleFont = nullptr;
IDirect3DDevice9* g_device = nullptr;
struct LoadedTexture {
    IDirect3DTexture9* texture = nullptr;
    int width = 0, height = 0;
};
std::map<std::string, LoadedTexture> g_textures;
std::string g_shotName;   // a screenshot to take after this frame
ImFont* g_headingFont = nullptr;
HWND    g_hwnd = nullptr;
WNDPROC g_origWndProc = nullptr;
UINT    g_toggleKey = VK_F8;

std::set<IDirectInputDevice8A*> g_mice;
std::set<IDirectInputDevice8A*> g_keyboards;

// The game reads the mouse through DirectInput in exclusive mode, so Windows sends no
// mouse messages and the OS cursor is frozen. While the overlay is open we take the raw
// DirectInput movement for our own cursor and give the game none.
CRITICAL_SECTION g_mouseLock;
long  g_mouseDX = 0, g_mouseDY = 0, g_mouseWheel = 0;
BYTE  g_mouseButtons[3] = {};
float g_cursorX = -1, g_cursorY = -1;
bool  g_mouseButtonsSent[3] = {};
float g_mouseSpeed = 1.0f;

bool UseDirectInputMouse() { return !g_mice.empty(); }

void AccumulateMouse(long dx, long dy, long wheel, const BYTE* buttons, int buttonCount)
{
    EnterCriticalSection(&g_mouseLock);
    g_mouseDX += dx;
    g_mouseDY += dy;
    g_mouseWheel += wheel;
    for (int i = 0; i < buttonCount && i < 3; i++)
        g_mouseButtons[i] = buttons[i];
    LeaveCriticalSection(&g_mouseLock);
}

// Applies DirectInput movement as ImGui mouse events. Runs after the Win32 backend's
// NewFrame, so our position wins over the (frozen) OS cursor it reports.
void ApplyDirectInputMouse()
{
    ImGuiIO& io = ImGui::GetIO();
    EnterCriticalSection(&g_mouseLock);
    long dx = g_mouseDX, dy = g_mouseDY, wheel = g_mouseWheel;
    BYTE buttons[3] = { g_mouseButtons[0], g_mouseButtons[1], g_mouseButtons[2] };
    g_mouseDX = g_mouseDY = g_mouseWheel = 0;
    LeaveCriticalSection(&g_mouseLock);

    if (g_cursorX < 0) {
        g_cursorX = io.DisplaySize.x * 0.5f;
        g_cursorY = io.DisplaySize.y * 0.5f;
    }
    g_cursorX = min(max(g_cursorX + dx * g_mouseSpeed, 0.0f), io.DisplaySize.x - 1);
    g_cursorY = min(max(g_cursorY + dy * g_mouseSpeed, 0.0f), io.DisplaySize.y - 1);
    io.AddMousePosEvent(g_cursorX, g_cursorY);
    for (int i = 0; i < 3; i++) {
        bool down = (buttons[i] & 0x80) != 0;
        if (down != g_mouseButtonsSent[i]) {
            io.AddMouseButtonEvent(i, down);
            g_mouseButtonsSent[i] = down;
        }
    }
    if (wheel)
        io.AddMouseWheelEvent(0, wheel / 120.0f);
}

struct Panel {
    std::string name;
    DrawFn      draw;
};
std::vector<Panel> g_panels;
std::vector<DrawFn> g_huds;
std::vector<void (*)()> g_frameCallbacks;

ImVec4 Hex(unsigned rgb, float alpha = 1.0f)
{
    return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha);
}

// Styled after the game's own interface: Arial Rounded MT Bold (the game's UI font, see
// Data\fonts\*.fnt) and colours taken from the shapes in its Flash UI files
// (re\ui_palette.json): dark slate panels, teal outlines, slate-blue headers, white text
// and gold highlights.
void ApplyMoviesTheme()
{
    ImGuiIO& io = ImGui::GetIO();
    wchar_t winDir[MAX_PATH];
    GetWindowsDirectoryW(winDir, MAX_PATH);
    char fontPath[MAX_PATH];
    snprintf(fontPath, sizeof(fontPath), "%ls\\Fonts\\ARLRDBD.TTF", winDir);
    if (GetFileAttributesA(fontPath) != INVALID_FILE_ATTRIBUTES) {
        io.Fonts->AddFontFromFileTTF(fontPath, 17.0f);
        g_titleFont = io.Fonts->AddFontFromFileTTF(fontPath, 38.0f);
        g_headingFont = io.Fonts->AddFontFromFileTTF(fontPath, 22.0f);
        mod::Log("overlay font: Arial Rounded MT Bold");
    } else {
        io.Fonts->AddFontDefault();
        mod::Log("overlay font: ARLRDBD.TTF not found, using default");
    }

    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 14.0f;
    s.ChildRounding     = 10.0f;
    s.FrameRounding     = 9.0f;
    s.PopupRounding     = 10.0f;
    s.GrabRounding      = 9.0f;
    s.TabRounding       = 9.0f;
    s.ScrollbarRounding = 9.0f;
    s.WindowBorderSize  = 2.0f;
    s.FrameBorderSize   = 1.0f;
    s.WindowPadding     = ImVec2(16, 12);
    s.FramePadding      = ImVec2(12, 5);
    s.ItemSpacing       = ImVec2(9, 7);
    s.CellPadding       = ImVec2(8, 4);
    s.WindowTitleAlign  = ImVec2(0.5f, 0.5f);
    s.SeparatorTextBorderSize = 2.0f;

    // The look of the game's Studio Ranking screen: periwinkle tables with slate edges, dark
    // slate text, light blue rows, yellow for what's selected.
    const ImVec4 ink        = Hex(0x303F3F);
    const ImVec4 slate      = Hex(0x77909F);
    const ImVec4 periwinkle = Hex(0xA7BBD7);
    const ImVec4 rowBlue    = Hex(0xD7E3F9);
    const ImVec4 paleBlue   = Hex(0xEDF7F6);
    const ImVec4 yellow     = Hex(0xF9E81E);
    const ImVec4 gold       = Hex(0xCC9933);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                 = ink;
    c[ImGuiCol_TextDisabled]         = Hex(0x56697A);
    c[ImGuiCol_WindowBg]             = Hex(0xA7BBD7, 0.98f);
    c[ImGuiCol_ChildBg]              = Hex(0xB9CAE2, 0.45f);
    c[ImGuiCol_PopupBg]              = Hex(0xC7D5EA, 0.99f);
    c[ImGuiCol_Border]               = slate;
    c[ImGuiCol_BorderShadow]         = Hex(0x000000, 0.0f);
    c[ImGuiCol_FrameBg]              = Hex(0xEDF3FB);
    c[ImGuiCol_FrameBgHovered]       = Hex(0xFFFFFF);
    c[ImGuiCol_FrameBgActive]        = Hex(0xFBF3A0);
    c[ImGuiCol_TitleBg]              = slate;
    c[ImGuiCol_TitleBgActive]        = Hex(0x637C8B);
    c[ImGuiCol_TitleBgCollapsed]     = Hex(0x77909F, 0.7f);
    c[ImGuiCol_MenuBarBg]            = periwinkle;
    c[ImGuiCol_ScrollbarBg]          = Hex(0x8FA5BD, 0.45f);
    c[ImGuiCol_ScrollbarGrab]        = slate;
    c[ImGuiCol_ScrollbarGrabHovered] = Hex(0x637C8B);
    c[ImGuiCol_ScrollbarGrabActive]  = gold;
    c[ImGuiCol_CheckMark]            = ink;
    c[ImGuiCol_SliderGrab]           = gold;
    c[ImGuiCol_SliderGrabActive]     = Hex(0xE1AF5E);
    c[ImGuiCol_Button]               = rowBlue;
    c[ImGuiCol_ButtonHovered]        = paleBlue;
    c[ImGuiCol_ButtonActive]         = yellow;
    c[ImGuiCol_Header]               = Hex(0xC2D2EA);
    c[ImGuiCol_HeaderHovered]        = Hex(0xDCE7F7);
    c[ImGuiCol_HeaderActive]         = yellow;
    c[ImGuiCol_Separator]            = slate;
    c[ImGuiCol_SeparatorHovered]     = Hex(0x637C8B);
    c[ImGuiCol_SeparatorActive]      = gold;
    c[ImGuiCol_ResizeGrip]           = Hex(0x77909F, 0.5f);
    c[ImGuiCol_ResizeGripHovered]    = Hex(0x637C8B);
    c[ImGuiCol_ResizeGripActive]     = gold;
    c[ImGuiCol_Tab]                  = Hex(0xC2D2EA);
    c[ImGuiCol_TabHovered]           = paleBlue;
    c[ImGuiCol_TabSelected]          = yellow;
    c[ImGuiCol_PlotLines]            = Hex(0x990000);
    c[ImGuiCol_PlotHistogram]        = gold;
    c[ImGuiCol_TableHeaderBg]        = Hex(0x8EA6B8);
    c[ImGuiCol_TableBorderStrong]    = slate;
    c[ImGuiCol_TableBorderLight]     = Hex(0x8EA6B8, 0.6f);
    c[ImGuiCol_TableRowBg]           = Hex(0xD7E3F9, 0.35f);
    c[ImGuiCol_TableRowBgAlt]        = Hex(0xFFFFFF, 0.25f);
    c[ImGuiCol_TextSelectedBg]       = Hex(0xF9E81E, 0.5f);
    c[ImGuiCol_NavHighlight]         = yellow;
    c[ImGuiCol_PlotLinesHovered]     = gold;
}

bool CaptureMouse()    { return g_imguiReady && g_open && ImGui::GetIO().WantCaptureMouse; }
bool CaptureKeyboard() { return g_imguiReady && g_open && ImGui::GetIO().WantCaptureKeyboard; }

LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && wp == g_toggleKey && !(lp & (1 << 30))) {
        g_open = !g_open;
        if (g_open) {
            g_cursorX = g_cursorY = -1;   // recentre our cursor each time the overlay opens
            for (bool& b : g_mouseButtonsSent)
                b = false;
        }
        mod::Log("overlay %s (%s mouse)", g_open ? "opened" : "closed", UseDirectInputMouse() ? "DirectInput" : "Windows");
        return 0;
    }
    if (g_imguiReady && g_open) {
        bool mouseMsg = msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
        bool keyMsg = (msg >= WM_KEYFIRST && msg <= WM_KEYLAST);
        if (!(mouseMsg && UseDirectInputMouse()))
            ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
        if ((mouseMsg && CaptureMouse()) || (keyMsg && CaptureKeyboard()))
            return 0;
    }
    return CallWindowProcW(g_origWndProc, hwnd, msg, wp, lp);
}

void InitImGui(IDirect3DDevice9* device)
{
    D3DDEVICE_CREATION_PARAMETERS cp{};
    device->GetCreationParameters(&cp);
    g_hwnd = cp.hFocusWindow;
    if (!g_hwnd) {
        D3DPRESENT_PARAMETERS pp{};
        IDirect3DSwapChain9* sc = nullptr;
        if (SUCCEEDED(device->GetSwapChain(0, &sc))) {
            sc->GetPresentParameters(&pp);
            sc->Release();
        }
        g_hwnd = pp.hDeviceWindow;
    }

    g_device = device;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;          // window layout is not persisted
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigInputTrickleEventQueue = false;   // our DirectInput mouse events replace the OS cursor's
    ApplyMoviesTheme();

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX9_Init(device);
    g_origWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookWndProc)));
    g_imguiReady = true;
    mod::Log("overlay ready (window 0x%p) - press F8 in game", g_hwnd);
}

// The back buffer, halved, as a 24-bit BMP.
void SaveScreenshot(IDirect3DDevice9* device, const std::string& name)
{
    IDirect3DSurface9* back = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)))
        return;
    D3DSURFACE_DESC desc;
    back->GetDesc(&desc);
    IDirect3DSurface9* plain = nullptr;
    IDirect3DSurface9* sys = nullptr;
    bool ok = false;
    if (desc.Format == D3DFMT_X8R8G8B8 || desc.Format == D3DFMT_A8R8G8B8) {
        IDirect3DSurface9* source = back;
        if (desc.MultiSampleType != D3DMULTISAMPLE_NONE &&
            SUCCEEDED(device->CreateRenderTarget(desc.Width, desc.Height, desc.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &plain, nullptr)) &&
            SUCCEEDED(device->StretchRect(back, nullptr, plain, nullptr, D3DTEXF_NONE)))
            source = plain;
        if (SUCCEEDED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr)) &&
            SUCCEEDED(device->GetRenderTargetData(source, sys))) {
            D3DLOCKED_RECT lr;
            if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
                int w = desc.Width / 2, h = desc.Height / 2;
                int row = (w * 3 + 3) & ~3;
                std::vector<uint8_t> bmp(54 + static_cast<size_t>(row) * h);
                uint8_t* hd = bmp.data();
                hd[0] = 'B';
                hd[1] = 'M';
                *reinterpret_cast<uint32_t*>(hd + 2) = static_cast<uint32_t>(bmp.size());
                *reinterpret_cast<uint32_t*>(hd + 10) = 54;
                *reinterpret_cast<uint32_t*>(hd + 14) = 40;
                *reinterpret_cast<int32_t*>(hd + 18) = w;
                *reinterpret_cast<int32_t*>(hd + 22) = h;
                *reinterpret_cast<uint16_t*>(hd + 26) = 1;
                *reinterpret_cast<uint16_t*>(hd + 28) = 24;
                for (int y = 0; y < h; y++) {
                    const uint8_t* src = static_cast<const uint8_t*>(lr.pBits) + static_cast<size_t>(y * 2) * lr.Pitch;
                    uint8_t* dst = bmp.data() + 54 + static_cast<size_t>(h - 1 - y) * row;
                    for (int x = 0; x < w; x++) {
                        dst[x * 3 + 0] = src[x * 8 + 0];
                        dst[x * 3 + 1] = src[x * 8 + 1];
                        dst[x * 3 + 2] = src[x * 8 + 2];
                    }
                }
                sys->UnlockRect();
                wchar_t dir[MAX_PATH], path[MAX_PATH];
                swprintf_s(dir, L"%s\\MoviesMod\\shots", mod::GameDir());
                CreateDirectoryW(dir, nullptr);
                swprintf_s(path, L"%s\\%S.bmp", dir, name.c_str());
                if (FILE* f = _wfopen(path, L"wb")) {
                    fwrite(bmp.data(), 1, bmp.size(), f);
                    fclose(f);
                    ok = true;
                }
            }
        }
    }
    if (sys)
        sys->Release();
    if (plain)
        plain->Release();
    back->Release();
    mod::Log("overlay: screenshot '%s' %s (%ux%u format %d)", name.c_str(), ok ? "saved" : "failed", desc.Width, desc.Height,
             static_cast<int>(desc.Format));
}

void Render(IDirect3DDevice9* device)
{
    for (auto fn : g_frameCallbacks)
        fn();
    if (!g_imguiReady)
        InitImGui(device);
    if (!g_open && g_huds.empty())
        return;

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    if (g_open && UseDirectInputMouse())
        ApplyDirectInputMouse();
    ImGui::NewFrame();
    // With DirectInput the game's own cursor is frozen while we are open, so always draw ours.
    ImGui::GetIO().MouseDrawCursor = g_open && (UseDirectInputMouse() || ImGui::GetIO().WantCaptureMouse);
    for (DrawFn hud : g_huds)
        hud();
    if (g_open)
        for (const Panel& p : g_panels)
            p.draw();
    ImGui::EndFrame();

    if (SUCCEEDED(device->BeginScene())) {
        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        device->EndScene();
        if (!g_shotName.empty()) {
            SaveScreenshot(device, g_shotName);
            g_shotName.clear();
        }
    }
}

HRESULT WINAPI HookPresent(IDirect3DDevice9* device, const RECT* src, const RECT* dst, HWND wnd, const RGNDATA* dirty)
{
    Render(device);
    return g_origPresent(device, src, dst, wnd, dirty);
}

HRESULT WINAPI HookReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* pp)
{
    if (g_imguiReady)
        ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = g_origReset(device, pp);
    if (g_imguiReady && SUCCEEDED(hr))
        ImGui_ImplDX9_CreateDeviceObjects();
    return hr;
}

HRESULT WINAPI HookDIGetState(IDirectInputDevice8A* dev, DWORD size, LPVOID data)
{
    HRESULT hr = g_origDIGetState(dev, size, data);
    if (FAILED(hr) || !data)
        return hr;
    if (g_mice.count(dev) && g_open && g_imguiReady) {
        if (size >= sizeof(DIMOUSESTATE)) {
            auto* ms = static_cast<DIMOUSESTATE*>(data);
            AccumulateMouse(ms->lX, ms->lY, ms->lZ, ms->rgbButtons, 3);
        }
        memset(data, 0, size);   // the game sees a still mouse while the overlay is open
    } else if (g_keyboards.count(dev) && CaptureKeyboard()) {
        memset(data, 0, size);
    }
    return hr;
}

HRESULT WINAPI HookDIGetData(IDirectInputDevice8A* dev, DWORD size, LPDIDEVICEOBJECTDATA data, LPDWORD count, DWORD flags)
{
    HRESULT hr = g_origDIGetData(dev, size, data, count, flags);
    if (FAILED(hr) || !count)
        return hr;
    if (g_mice.count(dev) && g_open && g_imguiReady) {
        if (data && !(flags & DIGDD_PEEK)) {
            for (DWORD i = 0; i < *count; i++) {
                auto* rec = reinterpret_cast<DIDEVICEOBJECTDATA*>(reinterpret_cast<BYTE*>(data) + i * size);
                long v = static_cast<long>(rec->dwData);
                switch (rec->dwOfs) {
                case DIMOFS_X: AccumulateMouse(v, 0, 0, nullptr, 0); break;
                case DIMOFS_Y: AccumulateMouse(0, v, 0, nullptr, 0); break;
                case DIMOFS_Z: AccumulateMouse(0, 0, v, nullptr, 0); break;
                case DIMOFS_BUTTON0:
                case DIMOFS_BUTTON1:
                case DIMOFS_BUTTON2: {
                    EnterCriticalSection(&g_mouseLock);
                    g_mouseButtons[rec->dwOfs - DIMOFS_BUTTON0] = static_cast<BYTE>(rec->dwData);
                    LeaveCriticalSection(&g_mouseLock);
                    break;
                }
                }
            }
        }
        *count = 0;
    } else if (g_keyboards.count(dev) && CaptureKeyboard()) {
        *count = 0;
    }
    return hr;
}

void HookVtableSlot(void* object, int slot, void* detour, void** original, const char* what)
{
    void* target = (*reinterpret_cast<void***>(object))[slot];
    if (MH_CreateHook(target, detour, original) == MH_OK && MH_EnableHook(target) == MH_OK)
        mod::Log("hooked %s", what);
    else
        mod::Log("FAILED to hook %s", what);
}

HRESULT WINAPI HookDICreateDevice(IDirectInput8A* di, REFGUID guid, LPDIRECTINPUTDEVICE8A* out, LPUNKNOWN outer)
{
    HRESULT hr = g_origDICreateDevice(di, guid, out, outer);
    if (SUCCEEDED(hr) && out && *out) {
        if (IsEqualGUID(guid, GUID_SysMouse))
            g_mice.insert(*out);
        else if (IsEqualGUID(guid, GUID_SysKeyboard))
            g_keyboards.insert(*out);
        if (!g_origDIGetState) {
            HookVtableSlot(*out, kSlotDIGetDeviceState, &HookDIGetState, reinterpret_cast<void**>(&g_origDIGetState), "DirectInput GetDeviceState");
            HookVtableSlot(*out, kSlotDIGetDeviceData, &HookDIGetData, reinterpret_cast<void**>(&g_origDIGetData), "DirectInput GetDeviceData");
        }
    }
    return hr;
}

bool HookD3D9()
{
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) {
        mod::Log("Direct3DCreate9 failed - overlay disabled");
        return false;
    }
    WNDCLASSEXW wc{ sizeof(wc), CS_CLASSDC, DefWindowProcW, 0, 0, GetModuleHandleW(nullptr), nullptr, nullptr, nullptr, nullptr, L"MoviesModDummy", nullptr };
    RegisterClassExW(&wc);
    HWND wnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 16, 16, nullptr, nullptr, wc.hInstance, nullptr);

    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow = wnd;

    IDirect3DDevice9* device = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, wnd,
                                   D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
    if (FAILED(hr))
        hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                               D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT, &pp, &device);
    bool ok = SUCCEEDED(hr) && device;
    if (ok) {
        HookVtableSlot(device, kSlotPresent, &HookPresent, reinterpret_cast<void**>(&g_origPresent), "IDirect3DDevice9::Present");
        HookVtableSlot(device, kSlotReset, &HookReset, reinterpret_cast<void**>(&g_origReset), "IDirect3DDevice9::Reset");
        device->Release();
    } else {
        mod::Log("dummy Direct3D device creation failed (0x%08X) - overlay disabled", hr);
    }
    d3d->Release();
    DestroyWindow(wnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

} // namespace

void Install()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    InitializeCriticalSection(&g_mouseLock);
    HookD3D9();
}

void OnDirectInputCreated(void* directInput8)
{
    if (g_origDICreateDevice || !directInput8)
        return;
    HookVtableSlot(directInput8, kSlotDICreateDevice, &HookDICreateDevice,
                   reinterpret_cast<void**>(&g_origDICreateDevice), "IDirectInput8::CreateDevice");
}

void AddPanel(const char* name, DrawFn draw) { g_panels.push_back({ name, draw }); }

void AddHud(DrawFn draw) { g_huds.push_back(draw); }

void AddFrameCallback(void (*fn)()) { g_frameCallbacks.push_back(fn); }

bool IsOpen() { return g_open; }

void Open()
{
    if (g_open)
        return;
    g_open = true;
    g_cursorX = g_cursorY = -1;
    for (bool& b : g_mouseButtonsSent)
        b = false;
    mod::Log("overlay opened by the mod");
}

void Close()
{
    if (!g_open)
        return;
    g_open = false;
    mod::Log("overlay closed by the mod");
}

ImFont* TitleFont() { return g_titleFont; }

void RequestScreenshot(const char* name) { g_shotName = name; }

void* Texture(const char* name, int* width, int* height)
{
    auto it = g_textures.find(name);
    if (it == g_textures.end()) {
        LoadedTexture t;
        wchar_t path[MAX_PATH];
        swprintf_s(path, L"%s\\MoviesMod\\ui\\%S.tex", mod::GameDir(), name);
        FILE* f = _wfopen(path, L"rb");
        if (f) {
            char magic[4];
            uint32_t w = 0, h = 0;
            if (fread(magic, 1, 4, f) == 4 && memcmp(magic, "MMTX", 4) == 0 && fread(&w, 4, 1, f) == 1 &&
                fread(&h, 4, 1, f) == 1 && w && h && w <= 4096 && h <= 4096 && g_device) {
                std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
                if (fread(pixels.data(), 1, pixels.size(), f) == pixels.size() &&
                    SUCCEEDED(g_device->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t.texture, nullptr))) {
                    D3DLOCKED_RECT lr;
                    if (SUCCEEDED(t.texture->LockRect(0, &lr, nullptr, 0))) {
                        for (uint32_t y = 0; y < h; y++)
                            memcpy(static_cast<uint8_t*>(lr.pBits) + y * lr.Pitch, pixels.data() + y * w * 4, w * 4);
                        t.texture->UnlockRect(0);
                        t.width = static_cast<int>(w);
                        t.height = static_cast<int>(h);
                    } else {
                        t.texture->Release();
                        t.texture = nullptr;
                    }
                }
            }
            fclose(f);
        }
        mod::Log("overlay: texture '%s' %s", name, t.texture ? "loaded" : "not available");
        it = g_textures.emplace(name, t).first;
    }
    if (width)
        *width = it->second.width;
    if (height)
        *height = it->second.height;
    return it->second.texture;
}
ImFont* HeadingFont() { return g_headingFont; }

} // namespace overlay
