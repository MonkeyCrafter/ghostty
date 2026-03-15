// Ghostty Windows Host Application
//
// This is the Win32 host application that embeds libghostty.dll via its C API.
// It mirrors the role of the macOS Swift/Xcode application: the native GUI
// layer that creates windows, handles input, and drives the terminal rendering.
//
// Architecture:
//   Ghostty.exe (this)  →  libghostty.dll  (Zig core: terminal, renderer, config)
//
// The host app creates an IDWriteFactory for font enumeration, initializes
// libghostty via ghostty_init(), then runs the Win32 message loop.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <combaseapi.h>

#include "ghostty.h"
#include "App.h"

// Forward declaration
static int RunApp(HINSTANCE hInstance, int nCmdShow);

int WINAPI wWinMain(
    _In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR lpCmdLine,
    _In_ int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;

    // Initialize COM for DirectWrite and shell operations.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        MessageBoxW(nullptr,
            L"Failed to initialize COM. Ghostty cannot start.",
            L"Ghostty Error",
            MB_ICONERROR | MB_OK);
        return 1;
    }

    int result = RunApp(hInstance, nCmdShow);

    CoUninitialize();
    return result;
}

static int RunApp(HINSTANCE hInstance, int nCmdShow)
{
    // Initialize libghostty.
    ghostty_error_s init_err;
    if (!ghostty_init(&init_err)) {
        wchar_t msg[512];
        swprintf_s(msg, _countof(msg),
            L"Failed to initialize Ghostty: %S (code %d)",
            init_err.message ? init_err.message : "unknown error",
            init_err.code);
        MessageBoxW(nullptr, msg, L"Ghostty Error", MB_ICONERROR | MB_OK);
        return 1;
    }

    // Create and run the application.
    App app(hInstance, nCmdShow);
    if (!app.Init()) {
        return 1;
    }

    return app.Run();
}
