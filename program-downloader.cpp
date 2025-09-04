#define UNICODE
#define _UNICODE
#include <algorithm>
#include <windows.h>
#include <wininet.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <regex>
#include <windowsx.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

struct AppEntry {
    std::wstring name;
    std::wstring url;
};

HINSTANCE g_hInst = nullptr;
std::vector<AppEntry> g_apps;
std::wstring g_downloadsFolder;
static HBRUSH hStaticBgBrush = nullptr;
//static HWND hTitleStatic = nullptr;
//static HWND hSubStatic = nullptr;

#define BTN_ID_BASE           1000
#define WM_DL_PROGRESS        (WM_USER + 101)
#define WM_DL_DONE            (WM_USER + 102)
#define IDM_EXIT              40001
#define BUTTON_HEIGHT 40
#define BUTTON_SPACING_Y 10
#define WIN_W 520
#define WIN_H 520

COLORREF CLR_BG = RGB(18, 18, 18);
COLORREF CLR_FG = RGB(240, 240, 240);
COLORREF CLR_SUB = RGB(180, 180, 180);
COLORREF CLR_ACC = RGB(0, 120, 212);
COLORREF CLR_BTN = RGB(30, 30, 30);
COLORREF CLR_BTN_HOT = RGB(45, 45, 45);
COLORREF CLR_BTN_PRESS = RGB(60, 60, 60);
COLORREF CLR_OUTLINE = RGB(55, 55, 55);

HFONT g_hFontTitle = nullptr;
HFONT g_hFontText = nullptr;
HFONT g_hFontButton = nullptr;

static int scrollOffset = 0;
static int scrollMax = 0;
static int scrollPageSize = 0;
static int contentHeight = 0;

static void EnableImmersiveDarkMode(HWND hWnd) {
    BOOL useDark = TRUE;
    DwmSetWindowAttribute(hWnd, 20, &useDark, sizeof(useDark));
    DwmSetWindowAttribute(hWnd, 19, &useDark, sizeof(useDark));
}

static HFONT MakeFont(int pts, bool bold = false) {
    HDC hdc = GetDC(nullptr);
    int logpix = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);
    int height = -MulDiv(pts, logpix, 72);
    return CreateFontW(height, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

static std::wstring GetDownloadsFolder() {
    PWSTR pathTmp = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &pathTmp))) {
        std::wstring path(pathTmp);
        CoTaskMemFree(pathTmp);
        return path;
    }
    wchar_t buf[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, buf))) {
        return std::wstring(buf) + L"\\Downloads";
    }
    return L".";
}

static bool StartsWithI(const std::wstring& s, const std::wstring& prefix) {
    if (s.size() < prefix.size()) return false;
    return _wcsnicmp(s.c_str(), prefix.c_str(), prefix.size()) == 0;
}

static bool ValidHttpUrl(const std::wstring& url) {
    return StartsWithI(url, L"http://") || StartsWithI(url, L"https://");
}

static std::wstring FileNameFromURL(const std::wstring& url, const std::wstring& fallbackBase) {
    size_t q = url.find(L'?');
    size_t h = url.find(L'#');
    size_t cut = min(q == std::wstring::npos ? url.size() : q,
        h == std::wstring::npos ? url.size() : h);

    std::wstring path = url.substr(0, cut);
    size_t slash = path.find_last_of(L"/\\");
    std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    if (name.empty()) name = fallbackBase;

    for (auto& ch : name) {
        if (wcschr(L"<>:\"/\\|?*", ch)) ch = L'_';
    }
    if (name == L"." || name == L"..") name = fallbackBase;
    if (name.find(L'.') == std::wstring::npos) name += L".exe";
    return name;
}

static void LoadApps() {
    g_apps.clear();
    g_apps.push_back({ L"Notepad++", L"https://github.com/notepad-plus-plus/notepad-plus-plus/releases/latest/download/npp.8.6.7.Installer.x64.exe" });
    g_apps.push_back({ L"7-Zip",     L"https://www.7-zip.org/a/7z2408-x64.exe" });

    g_downloadsFolder.clear();

    std::ifstream f("custom.txt", std::ios::binary);
    if (!f.is_open()) {
        g_downloadsFolder = GetDownloadsFolder();
        return;
    }

    std::string line;
    std::wstring currentName, currentUrl;
    auto pushIfReady = [&]() {
        if (!currentName.empty() && !currentUrl.empty() && ValidHttpUrl(currentUrl)) {
            g_apps.push_back({ currentName, currentUrl });
        }
        currentName.clear();
        currentUrl.clear();
        };

    while (std::getline(f, line)) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, nullptr, 0);
        if (wlen <= 0) continue;
        std::wstring wline(wlen - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, line.c_str(), -1, &wline[0], wlen);

        wline.erase(0, wline.find_first_not_of(L" \t\r\n"));
        wline.erase(wline.find_last_not_of(L" \t\r\n") + 1);
        if (wline.empty() || wline[0] == L';' || wline[0] == L'#') continue;

        if (StartsWithI(wline, L"folder:")) {
            std::wstring folder = wline.substr(7);
            folder.erase(0, folder.find_first_not_of(L" \t"));
            folder.erase(folder.find_last_not_of(L" \t") + 1);

            if (!folder.empty()) {
                g_downloadsFolder = folder;
            }
            continue;
        }

        if (wline.front() == L'[' && wline.back() == L']') {
            pushIfReady();
            currentName = wline.substr(1, wline.size() - 2);
            currentName.erase(0, currentName.find_first_not_of(L" \t"));
            currentName.erase(currentName.find_last_not_of(L" \t") + 1);
        }
        else if (StartsWithI(wline, L"url:")) {
            currentUrl = wline.substr(4);
            currentUrl.erase(0, currentUrl.find_first_not_of(L" \t"));
            currentUrl.erase(currentUrl.find_last_not_of(L" \t") + 1);
        }
    }
    pushIfReady();

    if (g_downloadsFolder.empty()) {
        g_downloadsFolder = GetDownloadsFolder();
    }
}

static void DrawRoundedRect(HDC hdc, const RECT& rc, int radius, COLORREF fill, COLORREF outline) {
    HBRUSH hFill = CreateSolidBrush(fill);
    HPEN hPen = CreatePen(PS_SOLID, 1, outline);
    HGDIOBJ o1 = SelectObject(hdc, hFill);
    HGDIOBJ o2 = SelectObject(hdc, hPen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, o2);
    SelectObject(hdc, o1);
    DeleteObject(hFill);
    DeleteObject(hPen);
}

static void DrawTextCentered(HDC hdc, const RECT& rc, const std::wstring& text, HFONT font, COLORREF color) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    HGDIOBJ of = SelectObject(hdc, font);
    DrawTextW(hdc, text.c_str(), (int)text.size(), (LPRECT)&rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, of);
}

struct DarkProgressState {
    int percent = 0;
    ULONGLONG bytes = 0;
    ULONGLONG total = 0;
    UINT timerId = 0;
    int marqueePos = 0;
};

static LRESULT CALLBACK DarkProgressProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    DarkProgressState* st = (DarkProgressState*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        st = new DarkProgressState();
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)st);
        st->timerId = 1;
        SetTimer(hWnd, st->timerId, 30, nullptr);
    } return 0;
    case WM_TIMER: {
        if (st && st->percent < 0) {
            st->marqueePos = (st->marqueePos + 4) % 400;
            InvalidateRect(hWnd, nullptr, FALSE);
        }
    } return 0;
    case WM_DL_PROGRESS: {
        if (!st) break;
        st->percent = (int)wParam;
        st->bytes = (ULONGLONG)lParam & 0xFFFFFFFFULL;
        st->total = ((ULONGLONG)lParam >> 32) & 0xFFFFFFFFULL;
        InvalidateRect(hWnd, nullptr, FALSE);
    } return 0;

    case WM_DESTROY: {
        if (st) {
            if (st->timerId) KillTimer(hWnd, st->timerId);
            delete st;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, 0);
        }
    } break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        HBRUSH bg = CreateSolidBrush(CLR_BTN);
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        RECT track = rc; InflateRect(&track, -4, -4);
        DrawRoundedRect(hdc, track, 10, CLR_BG, CLR_OUTLINE);

        RECT fill = track;
        if (st && st->percent >= 0) {
            int w = (track.right - track.left);
            fill.right = track.left + (int)(w * (st->percent / 100.0));
            DrawRoundedRect(hdc, fill, 10, CLR_ACC, CLR_ACC);
        }
        else {
            int w = (track.right - track.left);
            int chunk = w / 3;
            int x = track.left + (st ? st->marqueePos % (w + chunk) - chunk : 0);
            RECT m = { x, track.top, x + chunk, track.bottom };
            IntersectRect(&m, &m, &track);
            DrawRoundedRect(hdc, m, 10, CLR_ACC, CLR_ACC);
        }

        EndPaint(hWnd, &ps);
    } return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

struct DownloadTask {
    HWND hOwner = nullptr;
    HWND hDlg = nullptr;
    HWND hProg = nullptr;
    HWND hStat = nullptr;
    std::wstring url;
    std::wstring savePath;
};

static DWORD WINAPI DownloadThread(LPVOID p) {
    DownloadTask* t = (DownloadTask*)p;
    auto postFail = [&]() {
        PostMessageW(t->hOwner, WM_DL_DONE, 0, (LPARAM)t);
        };

    if (!ValidHttpUrl(t->url)) return postFail(), 0;

    HINTERNET hInet = InternetOpenW(L"Downloader/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInet) return postFail(), 0;

    HINTERNET hUrl = InternetOpenUrlW(hInet, t->url.c_str(), NULL, 0, INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE, 0);
    if (!hUrl) {
        InternetCloseHandle(hInet);
        return postFail(), 0;
    }

    DWORD contentLen = 0, len = sizeof(contentLen);
    HttpQueryInfoW(hUrl, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLen, &len, NULL);
    bool hasLength = (contentLen > 0);

    HANDLE hFile = CreateFileW(t->savePath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        InternetCloseHandle(hUrl);
        InternetCloseHandle(hInet);
        return postFail(), 0;
    }

    BYTE buf[64 * 1024];
    DWORD totalRead = 0;
    for (;;) {
        DWORD rd = 0;
        if (!InternetReadFile(hUrl, buf, sizeof(buf), &rd)) {
            CloseHandle(hFile);
            DeleteFileW(t->savePath.c_str());
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInet);
            return postFail(), 0;
        }
        if (rd == 0) break;

        DWORD wr = 0;
        if (!WriteFile(hFile, buf, rd, &wr, NULL) || wr != rd) {
            CloseHandle(hFile);
            DeleteFileW(t->savePath.c_str());
            InternetCloseHandle(hUrl);
            InternetCloseHandle(hInet);
            return postFail(), 0;
        }

        totalRead += rd;

        int percent = -1;
        if (hasLength && contentLen > 0) {
            percent = (int)((double)totalRead * 100.0 / (double)contentLen);
            if (percent > 100) percent = 100;
        }
        LPARAM sizes = ((LPARAM)contentLen << 32) | (LPARAM)totalRead;
        PostMessageW(t->hProg, WM_DL_PROGRESS, (WPARAM)percent, sizes);

        std::wstringstream ss;
        ss << L"Downloading " << (totalRead / 1024) << L" KB";
        if (hasLength) ss << L" / " << (contentLen / 1024) << L" KB";
        SetWindowTextW(t->hStat, ss.str().c_str());
    }

    CloseHandle(hFile);
    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInet);

    PostMessageW(t->hOwner, WM_DL_DONE, 1, (LPARAM)t);
    return 0;
}

LRESULT CALLBACK DownloadDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, RGB(255, 255, 255));
        SetBkColor(hdcStatic, CLR_BG);
        static HBRUSH hBrush = CreateSolidBrush(CLR_BG);
        return (INT_PTR)hBrush;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        HBRUSH hBrush = CreateSolidBrush(CLR_BG);
        FillRect(hdc, &rc, hBrush);
        DeleteObject(hBrush);
        return 1;
    }
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

void CreateDarkProgressClass() {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DarkProgressProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = L"DarkProgress";
    wc.hbrBackground = CreateSolidBrush(CLR_BG);
    RegisterClassW(&wc);
}

static void UpdateScrollbar(HWND hWnd) {
    SCROLLINFO si = {};
    si.cbSize = sizeof(SCROLLINFO);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = contentHeight - 1;
    si.nPage = scrollPageSize;
    si.nPos = scrollOffset;
    SetScrollInfo(hWnd, SB_VERT, &si, TRUE);
}

static void ScrollContent(HWND hWnd, int newOffset) {
    newOffset = max(0, min(newOffset, scrollMax));

    if (newOffset != scrollOffset) {
        scrollOffset = newOffset;
      
        HWND hTitle = GetDlgItem(hWnd, 5000);
        HWND hSub = GetDlgItem(hWnd, 5001);
        if (hTitle && hSub) {
            if (scrollOffset == 0) {
                ShowWindow(hTitle, SW_SHOW);
                ShowWindow(hSub, SW_SHOW);
            }
            else {
                ShowWindow(hTitle, SW_HIDE);
                ShowWindow(hSub, SW_HIDE);
            }
        }

        int y = 70 - scrollOffset;
        for (int i = 0; i < (int)g_apps.size(); i++) {
            HWND hBtn = GetDlgItem(hWnd, BTN_ID_BASE + i);
            if (hBtn) {
                SetWindowPos(hBtn, NULL, 20, y, 460, 40, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            y += 50;
        }

        UpdateScrollbar(hWnd);
        InvalidateRect(hWnd, NULL, TRUE);
    }
}

static void CreateDownloadDialog(HWND hOwner, const AppEntry& app) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DownloadDlgProc;
    wc.hInstance = g_hInst;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"DownloadDialogClass";
    RegisterClassW(&wc);

    std::wstring fileName = FileNameFromURL(app.url, app.name);
    std::wstring savePath = g_downloadsFolder + L"\\" + fileName;

    HWND hDlg = CreateWindowExW(WS_EX_APPWINDOW, L"DownloadDialogClass", L"",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 200,
        hOwner, nullptr, g_hInst, nullptr);

    EnableImmersiveDarkMode(hDlg);

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    HWND hHdr = CreateWindowW(L"STATIC", (L"Downloading: " + app.name).c_str(),
        WS_CHILD | WS_VISIBLE, 20, 50, 480, 24, hDlg, nullptr, g_hInst, nullptr);
    SendMessageW(hHdr, WM_SETFONT, (WPARAM)g_hFontText, TRUE);

    CreateDarkProgressClass();
    HWND hProg = CreateWindowW(L"DarkProgress", L"", WS_CHILD | WS_VISIBLE,
        20, 85, 480, 24, hDlg, (HMENU)0, g_hInst, nullptr);

    HWND hStat = CreateWindowW(L"STATIC", L"Connecting…", WS_CHILD | WS_VISIBLE,
        20, 120, 480, 20, hDlg, nullptr, g_hInst, nullptr);
    SendMessageW(hStat, WM_SETFONT, (WPARAM)g_hFontText, TRUE);

    auto* task = new DownloadTask();
    task->hOwner = hOwner;
    task->hDlg = hDlg;
    task->hProg = hProg;
    task->hStat = hStat;
    task->url = app.url;
    task->savePath = savePath;

    PostMessageW(hProg, WM_DL_PROGRESS, (WPARAM)0, 0);

    HANDLE hThread = CreateThread(nullptr, 0, DownloadThread, task, 0, nullptr);
    if (!hThread) {
        SetWindowTextW(hStat, L"Failed to create download thread.");
    }
    else {
        CloseHandle(hThread);
    }
}

static void DrawButton(LPDRAWITEMSTRUCT dis, const std::wstring& text, bool hot, bool pressed, bool focused) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    HBRUSH hBg = CreateSolidBrush(CLR_BG);
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);

    COLORREF fill = CLR_BTN;
    if (pressed) fill = CLR_BTN_PRESS;
    else if (hot) fill = CLR_BTN_HOT;

    DrawRoundedRect(hdc, rc, 12, fill, CLR_OUTLINE);

    RECT rtxt = rc;
    InflateRect(&rtxt, -12, -4);
    DrawTextCentered(hdc, rtxt, text, g_hFontButton, CLR_FG);

    if (focused) {
        RECT rf = rc;
        InflateRect(&rf, -3, -3);
        HPEN pen = CreatePen(PS_DOT, 1, CLR_ACC);
        HGDIOBJ op = SelectObject(hdc, pen);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, rf.left, rf.top, rf.right, rf.bottom, 10, 10);
        SelectObject(hdc, op);
        DeleteObject(pen);
    }
}

static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static int hoverId = -1;

    switch (msg) {
    case WM_CREATE: {
        EnableImmersiveDarkMode(hWnd);
        SetClassLongPtrW(hWnd, GCLP_HBRBACKGROUND, (LONG_PTR)CreateSolidBrush(CLR_BG));

        hStaticBgBrush = CreateSolidBrush(CLR_BG);

        g_hFontTitle = MakeFont(16, true);
        g_hFontText = MakeFont(11, false);
        g_hFontButton = MakeFont(12, true);

        g_downloadsFolder = GetDownloadsFolder();
        LoadApps();


        HWND hTitle = CreateWindowW(L"STATIC", L"Simple App Downloader",
            WS_CHILD | WS_VISIBLE,
            20, 6, 360, 40,   
            hWnd, (HMENU)5000, g_hInst, nullptr);
        SendMessageW(hTitle, WM_SETFONT, (WPARAM)g_hFontTitle, TRUE);

        HWND hSub = CreateWindowW(L"STATIC", L"By github.com/Floristicseas",
            WS_CHILD | WS_VISIBLE,
            20, 35, 480, 22,  
            hWnd, (HMENU)5001, g_hInst, nullptr);
        SendMessageW(hSub, WM_SETFONT, (WPARAM)g_hFontText, TRUE);

        int y = 70;
        for (size_t i = 0; i < g_apps.size(); ++i) {
            HWND hBtn = CreateWindowW(L"BUTTON", g_apps[i].name.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                20, y, 460, 40, hWnd, (HMENU)(BTN_ID_BASE + (int)i), g_hInst, nullptr);
            SendMessageW(hBtn, WM_SETFONT, (WPARAM)g_hFontButton, TRUE);
            y += 50;
        }

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        contentHeight = 70 + (int)g_apps.size() * 50;
        scrollPageSize = rcClient.bottom;
        scrollMax = max(0, contentHeight - scrollPageSize);

        if (scrollMax > 0) {
            UpdateScrollbar(hWnd);
        }

#ifdef _DEBUG
        HMENU hMenu = CreateMenu();
        AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"&Exit");
        SetMenu(hWnd, hMenu);
#endif // DEBUG
    } return 0;

    case WM_SIZE: {
        RECT rcClient;
        GetClientRect(hWnd, &rcClient);
        scrollPageSize = rcClient.bottom;
        scrollMax = max(0, contentHeight - scrollPageSize);

        if (scrollOffset > scrollMax) {
            scrollOffset = scrollMax;
        }

        HWND hTitle = GetDlgItem(hWnd, 5000);
        HWND hSub = GetDlgItem(hWnd, 5001);
        if (hTitle && hSub) {
            if (scrollOffset == 0) {
                ShowWindow(hTitle, SW_SHOW);
                ShowWindow(hSub, SW_SHOW);
            }
            else {
                ShowWindow(hTitle, SW_HIDE);
                ShowWindow(hSub, SW_HIDE);
            }
        }

        UpdateScrollbar(hWnd);

        int y = 70 - scrollOffset;
        for (int i = 0; i < (int)g_apps.size(); i++) {
            HWND hBtn = GetDlgItem(hWnd, BTN_ID_BASE + i);
            if (hBtn) {
                SetWindowPos(hBtn, NULL, 20, y, 460, 40, SWP_NOZORDER | SWP_NOACTIVATE);
            }
            y += 50;
        }

        InvalidateRect(hWnd, NULL, TRUE);
    } break;

    case WM_VSCROLL: {
        int newPos = scrollOffset;
        int scrollCode = LOWORD(wParam);

        switch (scrollCode) {
        case SB_LINEUP:        newPos = scrollOffset - 20; break;
        case SB_LINEDOWN:      newPos = scrollOffset + 20; break;
        case SB_PAGEUP:        newPos = scrollOffset - scrollPageSize; break;
        case SB_PAGEDOWN:      newPos = scrollOffset + scrollPageSize; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: newPos = HIWORD(wParam); break;
        case SB_TOP:           newPos = 0; break;
        case SB_BOTTOM:        newPos = scrollMax; break;
        default: break;
        }

        ScrollContent(hWnd, newPos);
    } break;

    case WM_MOUSEWHEEL: {
        int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        int scrollLines;
        SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &scrollLines, 0);
        if (scrollLines == 0) scrollLines = 3;

        int scrollAmount = -zDelta * scrollLines * 20 / WHEEL_DELTA;
        ScrollContent(hWnd, scrollOffset + scrollAmount);
    } break;

    case WM_MEASUREITEM: {
        return TRUE;
    }

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
        if (dis->CtlType == ODT_BUTTON) {
            int id = (int)dis->CtlID;
            int idx = id - BTN_ID_BASE;
            if (idx >= 0 && idx < (int)g_apps.size()) {
                wchar_t text[256];
                GetWindowTextW(dis->hwndItem, text, 256);
                bool pressed = (dis->itemState & ODS_SELECTED) != 0;
                bool focused = (dis->itemState & ODS_FOCUS) != 0;
                bool hot = (hoverId == id);
                DrawButton(dis, text, hot, pressed, focused);
                return TRUE;
            }
        }
    } break;

    case WM_MOUSEMOVE: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        HWND hChild = ChildWindowFromPoint(hWnd, pt);
        int newHover = -1;
        if (hChild) {
            int id = GetDlgCtrlID(hChild);
            if (id >= BTN_ID_BASE && id < BTN_ID_BASE + (int)g_apps.size()) newHover = id;
        }
        if (newHover != hoverId) {
            int old = hoverId;
            hoverId = newHover;
            if (old != -1) InvalidateRect(GetDlgItem(hWnd, old), nullptr, FALSE);
            if (hoverId != -1) InvalidateRect(GetDlgItem(hWnd, hoverId), nullptr, FALSE);
        }
    } break;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDM_EXIT) {
            DestroyWindow(hWnd);
            break;
        }

        if (id >= BTN_ID_BASE && id < BTN_ID_BASE + (int)g_apps.size()) {
            int index = id - BTN_ID_BASE;
            const auto& app = g_apps[index];

            if (!ValidHttpUrl(app.url)) {
                MessageBoxW(hWnd, L"Invalid URL. Use http:// or https://", L"Error", MB_ICONERROR);
                break;
            }

            std::wstring msg = L"Download and run \"" + app.name + L"\"?";
           
            if (MessageBoxW(hWnd, msg.c_str(), L"Confirm", MB_ICONQUESTION | MB_YESNO) == IDYES) {
                CreateDownloadDialog(hWnd, app);
            }
        }
    } break;

    case WM_DL_DONE: {
        DownloadTask* t = (DownloadTask*)lParam;
        bool ok = (wParam == 1);
        if (ok) {
            SetWindowTextW(t->hStat, L"Download complete. Launching…");
            ShellExecuteW(hWnd, L"open", t->savePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        else {
            SetWindowTextW(t->hStat, L"Download failed. Please try again.");
            MessageBoxW(hWnd, L"Download failed. Check your connection or URL.", L"Error", MB_ICONERROR);
        }
        SetTimer(t->hDlg, 99, 700, nullptr);
        SetWindowLongPtrW(t->hDlg, GWLP_WNDPROC, (LONG_PTR)+[](HWND h, UINT m, WPARAM w, LPARAM l)->LRESULT {
            if (m == WM_TIMER && w == 99) { KillTimer(h, 99); DestroyWindow(h); return 0; }
            return DefWindowProcW(h, m, w, l);
            });
        delete t;
    } break;

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hWnd, &rc);
        HBRUSH bg = CreateSolidBrush(CLR_BG);
        FillRect(hdc, &rc, bg); DeleteObject(bg);
        return TRUE;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        //separator 
        //RECT rc; GetClientRect(hWnd, &rc);
        //HPEN pen = CreatePen(PS_SOLID, 1, CLR_OUTLINE);
        //HGDIOBJ op = SelectObject(hdc, pen);
        //MoveToEx(hdc, 14, 61, nullptr);
        //LineTo(hdc, rc.right - 14, 61);
        //SelectObject(hdc, op);
        //DeleteObject(pen);

        EndPaint(hWnd, &ps);
    } return 0;

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        HWND hwndStatic = (HWND)lParam;
        int ctrlId = GetDlgCtrlID(hwndStatic);
   
        if (ctrlId == 5000 || ctrlId == 5001) {
            SetTextColor(hdcStatic, CLR_FG);
            SetBkColor(hdcStatic, CLR_BG);   
            return (LRESULT)hStaticBgBrush;
        }
        break;
    }

    case WM_DESTROY: {
        if (hStaticBgBrush) {
            DeleteObject(hStaticBgBrush);
            hStaticBgBrush = nullptr;
        }
        if (g_hFontTitle) DeleteObject(g_hFontTitle);
        if (g_hFontText) DeleteObject(g_hFontText);
        if (g_hFontButton) DeleteObject(g_hFontButton);
        PostQuitMessage(0);
    } return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static ATOM RegisterMainClass() {
    WNDCLASSW wc = {};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"DownloaderMainWin";
    wc.hbrBackground = CreateSolidBrush(CLR_BG);
    return RegisterClassW(&wc);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    g_hInst = hInstance;
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    RegisterMainClass();

    HWND hWnd = CreateWindowExW(WS_EX_APPWINDOW, L"DownloaderMainWin", L"Simple App Downloader",
       WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VSCROLL, CW_USEDEFAULT, CW_USEDEFAULT, 520, 520,
    nullptr, nullptr, g_hInst, nullptr
    );


    EnableImmersiveDarkMode(hWnd);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}