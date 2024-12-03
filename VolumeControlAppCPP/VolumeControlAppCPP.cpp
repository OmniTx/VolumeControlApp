#include <windows.h>
#include <shellapi.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <windowsx.h>
#include <iostream>
#include <chrono>
#include <string>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_APP_ICON 1001
#define ID_TRAY_STARTUP 2003
#define ID_TRAY_ABOUT 1003
#define ID_TRAY_EXIT 1004
#define IDI_ICON1 134

HHOOK hMouseHook;
std::chrono::steady_clock::time_point lastMuteToggle = std::chrono::steady_clock::now(); // Timestamp for mute/unmute

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam);
void AddTrayIcon(HWND hwnd);
void ShowAboutDialog(HWND hwnd);
void AdjustVolume(float volumeIncrement, bool toggleMute);
HMENU CreateContextMenu();
void SetupMouseHook();
void RemoveMouseHook();
bool IsCursorOverSoundIconArea();
void SetStartup(bool enable);
bool IsStartupEnabled();

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR pCmdLine, int nCmdShow)
{
    // const wchar_t CLASS_NAME[] = L"VolumeControlApp";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"VolumeControlApp";

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, L"VolumeControlApp", L"Volume Control App", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        NULL, NULL, hInstance, NULL);

    if (hwnd == NULL)
    {
        return 0;
    }

    AddTrayIcon(hwnd);
    SetupMouseHook();

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    RemoveMouseHook();
    return 0;
}

void AddTrayIcon(HWND hwnd)
{
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_APP_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1)); // Load the new icon
    wcscpy_s(nid.szTip, L"Volume Control App");

    Shell_NotifyIcon(NIM_ADD, &nid);
}

HMENU CreateContextMenu()
{
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, ID_TRAY_STARTUP, L"Start with Windows");
    AppendMenu(hMenu, MF_STRING, ID_TRAY_ABOUT, L"About");
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    // Check the current startup status and add a check mark if enabled
    if (IsStartupEnabled())
    {
        CheckMenuItem(hMenu, ID_TRAY_STARTUP, MF_CHECKED);
    }
    else
    {
        CheckMenuItem(hMenu, ID_TRAY_STARTUP, MF_UNCHECKED);
    }

    return hMenu;
}

void ShowAboutDialog(HWND hwnd)
{
    std::wstring aboutText =
        L"Volume Control App\n\n"
        L"Devloper: Imran Ahmed\n"
        L"Email: itsimran.official001@gmail.com\n"
        L"GitHub: github.com/omnitx";

    MessageBox(hwnd, aboutText.c_str(), L"About Volume Control App", MB_ICONINFORMATION | MB_OK);
}

void SetStartup(bool enable)
{
    HKEY hKey;
    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey);
    if (result == ERROR_SUCCESS)
    {
        if (enable)
        {
            wchar_t path[MAX_PATH];
            GetModuleFileName(NULL, path, MAX_PATH);
            RegSetValueEx(hKey, L"VolumeControlApp", 0, REG_SZ, (BYTE*)path, (wcslen(path) + 1) * sizeof(wchar_t));
        }
        else
        {
            RegDeleteValue(hKey, L"VolumeControlApp");
        }
        RegCloseKey(hKey);
    }
}

bool IsStartupEnabled()
{
    HKEY hKey;
    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ, &hKey);
    if (result == ERROR_SUCCESS)
    {
        if (RegQueryValueEx(hKey, L"VolumeControlApp", NULL, NULL, NULL, NULL) == ERROR_SUCCESS)
        {
            RegCloseKey(hKey);
            return true;
        }
        RegCloseKey(hKey);
    }
    return false;
}

void AdjustVolume(float volumeIncrement, bool toggleMute)
{
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr))
    {
        return;
    }

    IMMDeviceEnumerator* deviceEnumerator = NULL;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (LPVOID*)&deviceEnumerator);
    if (FAILED(hr))
    {
        CoUninitialize();
        return;
    }

    IMMDevice* defaultDevice = NULL;
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
    if (FAILED(hr))
    {
        deviceEnumerator->Release();
        CoUninitialize();
        return;
    }

    IAudioEndpointVolume* endpointVolume = NULL;
    hr = defaultDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (LPVOID*)&endpointVolume);
    if (FAILED(hr))
    {
        defaultDevice->Release();
        deviceEnumerator->Release();
        CoUninitialize();
        return;
    }

    if (toggleMute)
    {
        BOOL mute = FALSE;
        hr = endpointVolume->GetMute(&mute);
        if (SUCCEEDED(hr))
        {
            hr = endpointVolume->SetMute(!mute, NULL);
        }
    }
    else
    {
        float currentVolume = 0;
        hr = endpointVolume->GetMasterVolumeLevelScalar(&currentVolume);
        if (SUCCEEDED(hr))
        {
            currentVolume += volumeIncrement;
            if (currentVolume > 1.0f) currentVolume = 1.0f;
            if (currentVolume < 0.0f) currentVolume = 0.0f;

            hr = endpointVolume->SetMasterVolumeLevelScalar(currentVolume, NULL);
        }
    }

    endpointVolume->Release();
    defaultDevice->Release();
    deviceEnumerator->Release();
    CoUninitialize();
}

bool IsCursorOverSoundIconArea()
{
    POINT pt;
    GetCursorPos(&pt);

    RECT soundIconArea;
    soundIconArea.left = GetSystemMetrics(SM_CXSCREEN) - 160;  // Adjust the area as needed
    soundIconArea.top = GetSystemMetrics(SM_CYSCREEN) - 40;   // Adjust the area as needed
    soundIconArea.right = GetSystemMetrics(SM_CXSCREEN) - 100;
    soundIconArea.bottom = GetSystemMetrics(SM_CYSCREEN);    // Adjust the area as needed

    return PtInRect(&soundIconArea, pt);
}

LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0 && wParam == WM_MOUSEWHEEL)
    {
        MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;
        if (pMouseStruct != NULL && IsCursorOverSoundIconArea())
        {
            int delta = GET_WHEEL_DELTA_WPARAM(pMouseStruct->mouseData);

            if (GetKeyState(VK_SHIFT) & 0x8000)
            {
                AdjustVolume(delta > 0 ? 0.05f : -0.05f, false); // Larger increment for Shift+Scroll
            }
            else if (GetKeyState(VK_CONTROL) & 0x8000)
            {
                auto now = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMuteToggle);
                if (duration.count() > 1000) // 1-second delay to avoid rapid toggling
                {
                    AdjustVolume(0, true); // Mute/Unmute for Control+Scroll
                    lastMuteToggle = now;
                }
            }
            else
            {
                AdjustVolume(delta > 0 ? 0.01f : -0.01f, false);
            }
        }
    }
    return CallNextHookEx(hMouseHook, nCode, wParam, lParam);
}

void SetupMouseHook()
{
    hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, NULL, 0);
    if (hMouseHook == NULL) {
        // Debug statement removed
    }
    else {
        // Debug statement removed
    }
}

void RemoveMouseHook()
{
    UnhookWindowsHookEx(hMouseHook);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_CREATE:
        SetupMouseHook();
        break;
    case WM_TRAYICON:
        if (LOWORD(lParam) == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);

            HMENU hMenu = CreateContextMenu();
            int cmd = TrackPopupMenu(hMenu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
            if (cmd == ID_TRAY_EXIT)
            {
                PostQuitMessage(0);
            }
            else if (cmd == ID_TRAY_ABOUT)
            {
                ShowAboutDialog(hwnd);
            }
            else if (cmd == ID_TRAY_STARTUP)
            {
                bool enable = !IsStartupEnabled();
                SetStartup(enable);
            }
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_TRAY_EXIT:
            PostQuitMessage(0);
            break;
        case ID_TRAY_ABOUT:
            ShowAboutDialog(hwnd);
            break;
        }
        break;
    case WM_DESTROY:
    {
        NOTIFYICONDATA nid = {};
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd;
        nid.uID = ID_TRAY_APP_ICON;
        Shell_NotifyIcon(NIM_DELETE, &nid);
    }
    RemoveMouseHook();
    PostQuitMessage(0);
    break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}
