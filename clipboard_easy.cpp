// clipboard_easy.cpp - Windows clipboard manager (dark UI)
//
// Build (MinGW):
//   windres app.rc -O coff -o app.res
//   g++ -std=c++17 -mwindows -O2 -static -static-libgcc -static-libstdc++ clipboard_easy.cpp app.res -o ClipboardEasy.exe -lcomctl32 -lmsimg32
// Build (MSVC):
//   rc /fo app.res app.rc
//   cl /EHsc /O2 clipboard_easy.cpp app.res /FeClipboardEasy.exe /link /SUBSYSTEM:WINDOWS comctl32.lib user32.lib gdi32.lib msimg32.lib

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <algorithm>
#include <shellapi.h>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "msimg32.lib")   // GradientFill
#endif

// ══════════════════════════════════════════════════════════════════
//  Color palette - dark theme
// ══════════════════════════════════════════════════════════════════
static const COLORREF C_BG      = RGB(13,  13,  23);   // main background
static const COLORREF C_SURFACE = RGB(20,  20,  36);   // header top
static const COLORREF C_SURFACE2= RGB(26,  26,  44);   // header bottom
static const COLORREF C_CARD    = RGB(26,  26,  44);   // even row
static const COLORREF C_CARD2   = RGB(22,  22,  38);   // odd row
static const COLORREF C_CARDHOV = RGB(42,  42,  68);   // hover
static const COLORREF C_CARDSEL = RGB(79,  82, 210);   // selected row background
static const COLORREF C_ACCENT  = RGB(99, 102, 241);   // main accent
static const COLORREF C_ACCENTP = RGB(72,  75, 200);   // pressed accent
static const COLORREF C_RED     = RGB(192,  40,  40);  // destructive action
static const COLORREF C_REDP    = RGB(155,  30,  30);  // pressed destructive action
static const COLORREF C_TEXT    = RGB(228, 228, 252);  // primary text
static const COLORREF C_SUBTEXT = RGB(120, 120, 165);  // secondary text
static const COLORREF C_BORDER  = RGB(44,  44,  72);   // thin dividers
static const COLORREF C_ACCBAR  = RGB(155, 158, 255);  // selected left bar

// ══════════════════════════════════════════════════════════════════
//  Control IDs and constants
// ══════════════════════════════════════════════════════════════════
static const int IDC_LIST   = 101;
static const int IDC_MODE_COPY  = 102;
static const int IDC_MODE_PASTE = 103;
static const int IDC_DELETE     = 104;
static const int IDC_CLEAR      = 105;
static const int IDC_STATUS     = 106;

static const int  MAX_ITEMS = 100;
static const UINT WM_PASTEND = WM_USER + 1;
static const UINT WM_TRAYICON= WM_USER + 2;
static const int  HDR_H  = 66;   // header height in pixels
static const int  BTM_H  = 52;   // bottom bar height
static const int  ITEM_H = 50;   // list item height
static const int  MIN_W  = 530;  // minimum window width
static const int  MIN_H  = 420;  // minimum window height

// ══════════════════════════════════════════════════════════════════
//  Global state
// ══════════════════════════════════════════════════════════════════
static HWND    g_hwnd       = nullptr;
static HWND    g_list       = nullptr;
static HWND    g_status     = nullptr;
static HWND    g_lastTarget = nullptr;
static DWORD   g_myPid      = 0;
static bool    g_pasting    = false;
static bool    g_selfSet    = false;
static bool    g_pasteEnter = true;
static WNDPROC g_listProc   = nullptr;
static int     g_hoverIdx   = -1;   // item index under the mouse
static std::vector<std::wstring> g_items;
static wchar_t g_histPath[MAX_PATH];

// Fonts are created in WM_CREATE and released in WM_DESTROY.
static HFONT g_fNorm = nullptr;   // 17px - buttons
static HFONT g_fBold = nullptr;   // 24px bold - title
static HFONT g_fList = nullptr;   // 23px - list text
static HFONT g_fTiny = nullptr;   // 13px - subtitle/status

// Brushes
static HBRUSH g_brBg   = nullptr;
static HBRUSH g_brCard = nullptr;
static HICON  g_hIconUI= nullptr;

// ══════════════════════════════════════════════════════════════════
//  Encoding: UTF-8 to/from wide characters
// ══════════════════════════════════════════════════════════════════
static std::wstring U8W(const char* s, int len = -1)
{
    if (!s || len == 0) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, len, nullptr, 0);
    if (n <= 0) return {};
    std::wstring o(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s, len, &o[0], n);
    while (!o.empty() && o.back() == 0) o.pop_back();
    return o;
}
static std::string WU8(const std::wstring& s)
{
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string o(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), &o[0], n, nullptr, nullptr);
    return o;
}

// ══════════════════════════════════════════════════════════════════
//  Simple dependency-free JSON
// ══════════════════════════════════════════════════════════════════
static std::wstring JEsc(const std::wstring& s)
{
    std::wstring o;
    for (wchar_t c : s) {
        if      (c == L'"')  o += L"\\\"";
        else if (c == L'\\') o += L"\\\\";
        else if (c == L'\n') o += L"\\n";
        else if (c == L'\r') o += L"\\r";
        else if (c == L'\t') o += L"\\t";
        else if (c < 0x20) { wchar_t b[8]; swprintf(b,8,L"\\u%04x",(UINT)c); o+=b; }
        else o += c;
    }
    return o;
}
static std::vector<std::wstring> JParse(const std::wstring& j)
{
    std::vector<std::wstring> v;
    size_t p = j.find(L'[');
    if (p == std::wstring::npos) return v;
    p++;
    while (p < j.size() && (int)v.size() < MAX_ITEMS) {
        p = j.find(L'"', p);
        if (p == std::wstring::npos) break;
        p++;
        std::wstring s;
        while (p < j.size()) {
            wchar_t c = j[p++];
            if (c == L'\\' && p < j.size()) {
                wchar_t e = j[p++];
                switch (e) {
                case L'"':  s += L'"';  break; case L'\\': s += L'\\'; break;
                case L'n':  s += L'\n'; break; case L'r':  s += L'\r'; break;
                case L't':  s += L'\t'; break;
                case L'u':
                    if (p+4 <= j.size()) { s += (wchar_t)wcstol(j.substr(p,4).c_str(),nullptr,16); p+=4; }
                    break;
                default: s += e;
                }
            } else if (c == L'"') break;
            else s += c;
        }
        if (!s.empty()) v.push_back(s);
    }
    return v;
}

// ══════════════════════════════════════════════════════════════════
//  File operations
// ══════════════════════════════════════════════════════════════════
static void LoadHist()
{
    g_items.clear();
    HANDLE h = CreateFileW(g_histPath, GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD sz = GetFileSize(h, nullptr);
    if (!sz || sz == INVALID_FILE_SIZE) { CloseHandle(h); return; }
    std::vector<char> buf(sz);
    DWORD rd = 0; ReadFile(h, buf.data(), sz, &rd, nullptr); CloseHandle(h);
    int start = (rd>=3 && (BYTE)buf[0]==0xEF && (BYTE)buf[1]==0xBB && (BYTE)buf[2]==0xBF) ? 3 : 0;
    g_items = JParse(U8W(buf.data()+start, (int)rd-start));
}
static void SaveHist()
{
    std::wstring j = L"[\n";
    for (size_t i = 0; i < g_items.size(); i++) {
        j += L"  \"" + JEsc(g_items[i]) + L"\"";
        if (i+1 < g_items.size()) j += L",";
        j += L"\n";
    }
    j += L"]\n";
    std::string u = WU8(j);
    HANDLE h = CreateFileW(g_histPath, GENERIC_WRITE, 0,
                           nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0; WriteFile(h, u.c_str(), (DWORD)u.size(), &wr, nullptr); CloseHandle(h);
}

// ══════════════════════════════════════════════════════════════════
//  Clipboard operations
// ══════════════════════════════════════════════════════════════════
static std::wstring GetClip()
{
    if (!OpenClipboard(nullptr)) return {};
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    std::wstring r;
    if (h) { auto* p=(wchar_t*)GlobalLock(h); if(p){r=p;GlobalUnlock(h);} }
    CloseClipboard(); return r;
}
static bool SetClip(const std::wstring& s)
{
    size_t bytes = (s.size()+1)*sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!h) return false;
    auto* p = (wchar_t*)GlobalLock(h);
    if (!p) { GlobalFree(h); return false; }
    memcpy(p, s.c_str(), bytes);
    GlobalUnlock(h);
    g_selfSet = true;
    if (OpenClipboard(g_hwnd)) {
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT,h)) {
            GlobalFree(h);
            g_selfSet = false;
            CloseClipboard();
            return false;
        }
        CloseClipboard();
        return true;
    } else {
        GlobalFree(h);
        g_selfSet = false;
        return false;
    }
}

// ══════════════════════════════════════════════════════════════════
//  List helpers
// ══════════════════════════════════════════════════════════════════
static std::wstring Short(const std::wstring& s, int lim = 88)
{
    std::wstring o; bool sp = true;
    for (wchar_t c : s) {
        if (c==L'\n'||c==L'\r'||c==L'\t') c=L' ';
        if (c==L' '&&sp) continue;
        o+=c;
        sp=(c==L' ');
    }
    while (!o.empty() && o.back()==L' ') o.pop_back();
    if ((int)o.size() > lim) o = o.substr(0,lim-3) + L"...";
    return o;
}
static bool IsOurs(HWND hw)
{
    if (!hw) return false;
    DWORD pid=0; GetWindowThreadProcessId(hw,&pid); return pid==g_myPid;
}
static void Refresh()
{
    SendMessage(g_list, LB_RESETCONTENT, 0, 0);
    if (g_items.empty()) {
        SendMessage(g_list, LB_ADDSTRING, 0, (LPARAM)L"No copied text yet");
    } else {
        for (auto& it : g_items)
            SendMessage(g_list, LB_ADDSTRING, 0, (LPARAM)Short(it).c_str());
        SendMessage(g_list, LB_SETCURSEL, 0, 0);
    }
}
static void AddItem(const std::wstring& raw)
{
    std::wstring t = raw;
    while (!t.empty() && (unsigned)t.front()<=L' ') t.erase(t.begin());
    while (!t.empty() && (unsigned)t.back() <=L' ') t.pop_back();
    if (t.empty()) return;
    g_items.erase(std::remove(g_items.begin(),g_items.end(),t), g_items.end());
    g_items.insert(g_items.begin(), t);
    if ((int)g_items.size() > MAX_ITEMS) g_items.resize(MAX_ITEMS);
    SaveHist(); Refresh();
    wchar_t m[64]; swprintf(m,64,L"Saved  (%d)",(int)g_items.size());
    SetWindowText(g_status, m);
}

static void RefreshModeButtons(HWND hw = nullptr)
{
    if (!hw) hw = g_hwnd;
    HWND copy = GetDlgItem(hw, IDC_MODE_COPY);
    HWND paste = GetDlgItem(hw, IDC_MODE_PASTE);
    if (copy) InvalidateRect(copy, nullptr, TRUE);
    if (paste) InvalidateRect(paste, nullptr, TRUE);
}

// ══════════════════════════════════════════════════════════════════
//  Keystroke sending (SendInput)
// ══════════════════════════════════════════════════════════════════
static void SndCtrlV()
{
    INPUT in[4]={};
    in[0].type=in[1].type=in[2].type=in[3].type=INPUT_KEYBOARD;
    in[0].ki.wVk=VK_CONTROL; in[1].ki.wVk='V';
    in[2].ki.wVk='V'; in[2].ki.dwFlags=KEYEVENTF_KEYUP;
    in[3].ki.wVk=VK_CONTROL; in[3].ki.dwFlags=KEYEVENTF_KEYUP;
    SendInput(4,in,sizeof(INPUT));
}
static void SndEnter()
{
    INPUT in[2]={};
    in[0].type=in[1].type=INPUT_KEYBOARD;
    in[0].ki.wVk=VK_RETURN;
    in[1].ki.wVk=VK_RETURN; in[1].ki.dwFlags=KEYEVENTF_KEYUP;
    SendInput(2,in,sizeof(INPUT));
}

// ══════════════════════════════════════════════════════════════════
//  Paste on a worker thread so the UI stays responsive.
// ══════════════════════════════════════════════════════════════════
struct PTask { HWND tgt; bool sw; };
static DWORD WINAPI PasteThread(LPVOID p)
{
    auto* t = (PTask*)p;
    if (t->sw) {
        Sleep(200);
        HWND fw = t->tgt;
        if (!fw||!IsWindow(fw)) fw=GetForegroundWindow();
        if (fw) { SetForegroundWindow(fw); Sleep(100); }
    } else Sleep(60);
    SndCtrlV(); Sleep(80); SndEnter();
    PostMessage(g_hwnd, WM_PASTEND, t->sw?1:0, 0);
    delete t; return 0;
}
static void Paste(int idx)
{
    if (idx<0||idx>=(int)g_items.size()||g_pasting) return;
    g_pasting = true;
    if (!SetClip(g_items[idx])) {
        SetWindowText(g_status, L"Clipboard unavailable");
        g_pasting = false;
        return;
    }
    SetWindowText(g_status, L"Pasting...");
    bool sw = IsOurs(GetForegroundWindow());
    auto* t = new PTask{g_lastTarget, sw};
    if (sw) ShowWindow(g_hwnd, SW_HIDE);
    HANDLE h = CreateThread(nullptr,0,PasteThread,t,0,nullptr);
    if (h) CloseHandle(h);
    else { delete t; if(sw) ShowWindow(g_hwnd,SW_SHOWNOACTIVATE); g_pasting=false; }
}
static void UseItem(int idx)
{
    if (idx<0||idx>=(int)g_items.size()||g_pasting) return;
    if (g_pasteEnter) {
        Paste(idx);
        return;
    }
    SetWindowText(g_status, SetClip(g_items[idx]) ? L"Copied" : L"Copy failed");
}

// ══════════════════════════════════════════════════════════════════
//  GDI helpers
// ══════════════════════════════════════════════════════════════════

// Filled rounded rectangle without an outline.
static void FillRR(HDC dc, RECT rc, int r, COLORREF clr)
{
    HBRUSH br = CreateSolidBrush(clr);
    HPEN   np = CreatePen(PS_NULL, 0, 0);
    auto* ob = (HBRUSH)SelectObject(dc, br);
    auto* op = (HPEN)SelectObject(dc, np);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, r, r);
    SelectObject(dc,ob); SelectObject(dc,op);
    DeleteObject(br); DeleteObject(np);
}

// Horizontal line
static void HLine(HDC dc, int x0, int x1, int y, COLORREF clr)
{
    HPEN p = CreatePen(PS_SOLID, 1, clr);
    auto* op = (HPEN)SelectObject(dc, p);
    MoveToEx(dc,x0,y,nullptr); LineTo(dc,x1,y);
    SelectObject(dc,op); DeleteObject(p);
}

// Vertical gradient rectangle
static void GradRect(HDC dc, int x,int y,int w,int h, COLORREF top, COLORREF bot)
{
    TRIVERTEX vx[2];
    vx[0] = {x,   y,   (COLOR16)(GetRValue(top)<<8),(COLOR16)(GetGValue(top)<<8),(COLOR16)(GetBValue(top)<<8),0};
    vx[1] = {x+w, y+h, (COLOR16)(GetRValue(bot)<<8),(COLOR16)(GetGValue(bot)<<8),(COLOR16)(GetBValue(bot)<<8),0};
    GRADIENT_RECT gr = {0,1};
    GradientFill(dc, vx, 2, &gr, 1, GRADIENT_FILL_RECT_V);
}

// ══════════════════════════════════════════════════════════════════
//  ListBox subclass - single-click paste and hover tracking
// ══════════════════════════════════════════════════════════════════
static LRESULT CALLBACK ListProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;

    // Hover takibi
    if (msg == WM_MOUSEMOVE) {
        int x=(short)LOWORD(lp), y=(short)HIWORD(lp);
        LRESULT res = SendMessage(hw, LB_ITEMFROMPOINT, 0, MAKELPARAM(x,y));
        int idx = (HIWORD(res)==0) ? (int)LOWORD(res) : -1;
        if (idx != g_hoverIdx) {
            int old = g_hoverIdx; g_hoverIdx = idx;
            auto repaint = [&](int i){
                if (i<0) return;
                RECT r; SendMessage(hw,LB_GETITEMRECT,i,(LPARAM)&r);
                InvalidateRect(hw,&r,FALSE);
            };
            repaint(old); repaint(idx);
        }
        TRACKMOUSEEVENT tme = {sizeof(tme),TME_LEAVE,hw,0};
        TrackMouseEvent(&tme);
    }
    if (msg == WM_MOUSELEAVE) {
        int old = g_hoverIdx; g_hoverIdx = -1;
        if (old >= 0) {
            RECT r; SendMessage(hw,LB_GETITEMRECT,old,(LPARAM)&r);
            InvalidateRect(hw,&r,FALSE);
        }
    }

    // Prevent double-click from pasting twice.
    if (msg == WM_LBUTTONDBLCLK) return 0;

    // Paste on single click.
    if (msg == WM_LBUTTONDOWN && !g_pasting) {
        LRESULT r = CallWindowProcW(g_listProc, hw, msg, wp, lp);
        int idx = (int)SendMessage(hw, LB_GETCURSEL, 0, 0);
        if (idx != LB_ERR && idx < (int)g_items.size()) UseItem(idx);
        return r;
    }
    return CallWindowProcW(g_listProc, hw, msg, wp, lp);
}

// ══════════════════════════════════════════════════════════════════
//  System tray operations
// ══════════════════════════════════════════════════════════════════
static void ShowTrayIcon(HWND hw, bool add)
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hw;
    nid.uID = 1001;
    if (add) {
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_TRAYICON;
        nid.hIcon = (HICON)LoadImageW(GetModuleHandle(nullptr), L"IDI_ICON1", IMAGE_ICON, 16, 16, 0);
        wcscpy_s(nid.szTip, L"Clipboard Easy");
        Shell_NotifyIconW(NIM_ADD, &nid);
        if (nid.hIcon) DestroyIcon(nid.hIcon);
    } else {
        Shell_NotifyIconW(NIM_DELETE, &nid);
    }
}

// ══════════════════════════════════════════════════════════════════
//  Layout - dynamic sizing
// ══════════════════════════════════════════════════════════════════
static void Layout(HWND hw)
{
    RECT rc; GetClientRect(hw,&rc);
    int W=rc.right, H=rc.bottom;
    if (W<100||H<100) return;

    // List fills the area between the header and bottom bar.
    SetWindowPos(g_list, nullptr, 0,HDR_H, W,H-HDR_H-BTM_H, SWP_NOZORDER);

    // Bottom bar controls
    int bY = H - BTM_H + 11;
    int pasteW = 126;
    int copyW = 76;
    int pasteX = W - 12 - pasteW;
    int copyX = pasteX - 8 - copyW;
    SetWindowPos(GetDlgItem(hw,IDC_DELETE),    nullptr,  12, bY,  80,30, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hw,IDC_CLEAR),     nullptr, 102, bY,  80,30, SWP_NOZORDER);
    SetWindowPos(g_status, nullptr, 194,bY+7, copyX-204,16, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hw,IDC_MODE_COPY), nullptr, copyX, bY, copyW,30, SWP_NOZORDER);
    SetWindowPos(GetDlgItem(hw,IDC_MODE_PASTE),nullptr, pasteX,bY, pasteW,30, SWP_NOZORDER);
}

// ══════════════════════════════════════════════════════════════════
//  Main window procedure
// ══════════════════════════════════════════════════════════════════
static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {

    // ── System commands, such as minimize ─────────────────────────
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_MINIMIZE) {
            ShowWindow(hw, SW_HIDE);
            ShowTrayIcon(hw, true);
            return 0; // Cancel the default minimize behavior.
        }
        return DefWindowProcW(hw, msg, wp, lp);

    // ── Tray icon messages ────────────────────────────────────────
    case WM_TRAYICON:
        if (lp == WM_LBUTTONUP || lp == WM_RBUTTONUP) {
            ShowTrayIcon(hw, false);
            ShowWindow(hw, SW_RESTORE);
            SetForegroundWindow(hw);
        }
        break;

    // ── Creation ──────────────────────────────────────────────────
    case WM_CREATE: {
        HINSTANCE hi = ((LPCREATESTRUCT)lp)->hInstance;

        // Create fonts.
        auto mkFont = [](int h, int w, LPCWSTR face) {
            return CreateFontW(h,0,0,0,w,0,0,0,DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
                DEFAULT_PITCH|FF_DONTCARE,face);
        };
        g_fNorm = mkFont(17, FW_NORMAL, L"Segoe UI");
        g_fBold = mkFont(24, FW_BOLD,   L"Segoe UI");
        g_fList = mkFont(23, FW_NORMAL, L"Segoe UI");
        g_fTiny = mkFont(13, FW_NORMAL, L"Segoe UI");

        // Create brushes.
        g_brBg   = CreateSolidBrush(C_BG);
        g_brCard = CreateSolidBrush(C_CARD);
        g_hIconUI= (HICON)LoadImageW(hi, L"IDI_ICON1", IMAGE_ICON, 48, 48, 0);

        // Owner-drawn borderless list box.
        g_list = CreateWindowExW(0, L"LISTBOX", nullptr,
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|
            LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT,
            0,HDR_H,100,100, hw,(HMENU)(UINT_PTR)IDC_LIST,hi,nullptr);
        SendMessage(g_list, LB_SETITEMHEIGHT, 0, ITEM_H);
        g_listProc = (WNDPROC)SetWindowLongPtrW(g_list, GWLP_WNDPROC, (LONG_PTR)ListProc);

        // Owner-drawn buttons.
        auto mkBtn = [&](LPCWSTR txt, int id) {
            return CreateWindowExW(0,L"BUTTON",txt,
                WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                0,0,1,1, hw,(HMENU)(UINT_PTR)id,hi,nullptr);
        };
        mkBtn(L"Copy",          IDC_MODE_COPY);
        mkBtn(L"Paste + Enter", IDC_MODE_PASTE);
        mkBtn(L"Delete",        IDC_DELETE);
        mkBtn(L"Clear",         IDC_CLEAR);

        // Status label.
        g_status = CreateWindowExW(0,L"STATIC",L"Monitoring clipboard",
            WS_CHILD|WS_VISIBLE|SS_RIGHT,
            0,0,1,1, hw,(HMENU)(UINT_PTR)IDC_STATUS,hi,nullptr);
        SendMessage(g_status, WM_SETFONT, (WPARAM)g_fTiny, TRUE);
        RefreshModeButtons(hw);

        AddClipboardFormatListener(hw);
        SetTimer(hw,1,250,nullptr);
        Refresh();
        Layout(hw);
        break;
    }

    case WM_GETMINMAXINFO: {
        auto* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = MIN_W;
        mmi->ptMinTrackSize.y = MIN_H;
        break;
    }

    case WM_SIZE: Layout(hw); break;

    // ── Background: WM_PAINT draws it; this only clears it. ───────
    case WM_ERASEBKGND: {
        HDC dc = (HDC)wp;
        RECT rc; GetClientRect(hw,&rc);
        FillRect(dc,&rc,g_brBg);
        return 1;
    }

    // ── Header and bottom bar painting ────────────────────────────
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hw,&ps);
        RECT rc; GetClientRect(hw,&rc);
        int W=rc.right, H=rc.bottom;

        // Header gradient background.
        GradRect(dc, 0,0,W,HDR_H, C_SURFACE, C_SURFACE2);

        // Left accent strip across the header.
        {
            HBRUSH ab = CreateSolidBrush(C_ACCENT);
            RECT   ar = {0,0,4,HDR_H};
            FillRect(dc,&ar,ab); DeleteObject(ab);
        }

        // Draw the real application icon from app.ico.
        if (g_hIconUI) {
            DrawIconEx(dc, 12, HDR_H/2 - 24, g_hIconUI, 48, 48, 0, nullptr, DI_NORMAL|DI_COMPAT);
        }

        // Title
        SelectObject(dc,g_fBold);
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,C_TEXT);
        RECT tr={64, 8, W-10, HDR_H/2+4};
        DrawTextW(dc,L"Clipboard Easy",-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

        // Subtitle/hint
        SelectObject(dc,g_fTiny);
        SetTextColor(dc,C_SUBTEXT);
        RECT sr={64, HDR_H/2+3, W-10, HDR_H-8};
        DrawTextW(dc,L"1. Copy text   2. Choose action   3. Select saved text",-1,&sr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);

        // Header bottom border.
        HLine(dc,0,W,HDR_H-1,C_BORDER);

        // Bottom bar background.
        RECT bot={0,H-BTM_H,W,H};
        FillRect(dc,&bot,g_brBg);
        HLine(dc,0,W,H-BTM_H,C_BORDER);

        EndPaint(hw,&ps);
        break;
    }

    // ── Prevent stealing focus ────────────────────────────────────
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;

    // ── Owner-draw item height ────────────────────────────────────
    case WM_MEASUREITEM: {
        auto* mi=(MEASUREITEMSTRUCT*)lp;
        if (mi->CtlID==IDC_LIST) mi->itemHeight=ITEM_H;
        break;
    }

    // ── Owner-draw: boyama ────────────────────────────────────────
    case WM_DRAWITEM: {
        auto* di = (DRAWITEMSTRUCT*)lp;
        HDC   dc = di->hDC;
        RECT  rc = di->rcItem;

        // ── List item ─────────────────────────────────────────────
        if (di->CtlID == IDC_LIST) {
            if (di->itemID == (UINT)-1) break;
            int  idx  = (int)di->itemID;
            bool sel  = (di->itemState & ODS_SELECTED) != 0;
            bool hov  = (idx == g_hoverIdx) && !sel;
            bool real = (idx < (int)g_items.size());

            // Background color.
            COLORREF bg;
            if      (sel) bg = C_CARDSEL;
            else if (hov) bg = C_CARDHOV;
            else          bg = (idx%2==0) ? C_CARD : C_CARD2;

            HBRUSH bb = CreateSolidBrush(bg);
            FillRect(dc,&rc,bb); DeleteObject(bb);

            // Left accent bar for selected or hovered rows.
            if (sel || hov) {
                HBRUSH ab = CreateSolidBrush(sel ? C_ACCBAR : C_ACCENT);
                RECT   ar = {rc.left, rc.top, rc.left+3, rc.bottom};
                FillRect(dc,&ar,ab); DeleteObject(ab);
            }

            // Bottom separator.
            HLine(dc, rc.left, rc.right, rc.bottom-1, C_BORDER);

            // Text
            wchar_t buf[512]={};
            SendMessage(di->hwndItem, LB_GETTEXT, idx, (LPARAM)buf);
            SelectObject(dc, g_fList);
            SetBkMode(dc, TRANSPARENT);
            if (!real) {
                SetTextColor(dc, C_SUBTEXT);
                DrawTextW(dc,buf,-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            } else {
                SetTextColor(dc, sel ? RGB(255,255,255) : C_TEXT);
                RECT tr={rc.left+14, rc.top, rc.right-8, rc.bottom};
                DrawTextW(dc,buf,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            }
        }
        // ── Button ────────────────────────────────────────────────
        else if (di->CtlID==IDC_MODE_COPY || di->CtlID==IDC_MODE_PASTE || di->CtlID==IDC_CLEAR || di->CtlID==IDC_DELETE) {
            bool pressed = (di->itemState & ODS_SELECTED) != 0;
            bool modeBtn = (di->CtlID==IDC_MODE_COPY || di->CtlID==IDC_MODE_PASTE);
            bool activeMode = (di->CtlID==IDC_MODE_COPY && !g_pasteEnter) ||
                              (di->CtlID==IDC_MODE_PASTE && g_pasteEnter);

            // Match the button exterior with the window background.
            FillRect(dc,&rc,g_brBg);

            // Rounded button body.
            COLORREF btnClr;
            if (di->CtlID==IDC_CLEAR || di->CtlID==IDC_DELETE)
                btnClr = pressed ? C_REDP : C_RED;
            else if (activeMode)
                btnClr = pressed ? C_ACCENTP : C_ACCENT;
            else
                btnClr = pressed ? C_CARDHOV : C_CARD;
            RECT br2 = {rc.left+1, rc.top+1, rc.right-1, rc.bottom-1};
            FillRR(dc, br2, 9, btnClr);

            // Subtle top highlight.
            RECT shine={br2.left+4, br2.top+1, br2.right-4, br2.top+1+8};
            FillRR(dc, shine, 6, RGB(
                std::min(255,(int)GetRValue(btnClr)+28),
                std::min(255,(int)GetGValue(btnClr)+28),
                std::min(255,(int)GetBValue(btnClr)+28)));

            // Button text.
            wchar_t txt[64]={};
            GetWindowTextW(di->hwndItem,txt,64);
            SelectObject(dc,g_fNorm);
            SetBkMode(dc,TRANSPARENT);
            SetTextColor(dc, modeBtn && !activeMode ? C_TEXT : RGB(255,255,255));
            DrawTextW(dc,txt,-1,&br2,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
        break;
    }

    // ── Static control color for status ───────────────────────────
    case WM_CTLCOLORSTATIC: {
        HDC dc=(HDC)wp;
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,C_SUBTEXT);
        return (LRESULT)g_brBg;
    }

    // ── Listbox background ────────────────────────────────────────
    case WM_CTLCOLORLISTBOX: {
        HDC dc=(HDC)wp;
        SetBkColor(dc,C_CARD);
        return (LRESULT)g_brCard;
    }

    // ── Clipboard changed ─────────────────────────────────────────
    case WM_CLIPBOARDUPDATE:
        if (g_selfSet) { g_selfSet=false; break; }
        { std::wstring t=GetClip(); if(!t.empty()) AddItem(t); }
        break;

    // ── Active window tracking ────────────────────────────────────
    case WM_TIMER: {
        HWND fg=GetForegroundWindow();
        if (fg&&!IsOurs(fg)) g_lastTarget=fg;
        break;
    }

    // ── Paste finished ────────────────────────────────────────────
    case WM_PASTEND:
        if (wp) {
            ShowWindow(hw,SW_SHOWNOACTIVATE);
            SetWindowPos(hw,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        }
        SetWindowText(g_status,L"Monitoring clipboard");
        g_pasting=false;
        break;

    // ── Button clicks ─────────────────────────────────────────────
    case WM_COMMAND: {
        int id=LOWORD(wp);
        if (id==IDC_MODE_COPY) {
            g_pasteEnter = false;
            RefreshModeButtons(hw);
            SetWindowText(g_status, L"Action: Copy");
        } else if (id==IDC_MODE_PASTE) {
            g_pasteEnter = true;
            RefreshModeButtons(hw);
            SetWindowText(g_status, L"Action: Paste + Enter");
        } else if (id==IDC_DELETE) {
            int s=(int)SendMessage(g_list,LB_GETCURSEL,0,0);
            if (s>=0&&s<(int)g_items.size()) {
                g_items.erase(g_items.begin() + s);
                SaveHist(); Refresh();
                SetWindowText(g_status,L"Deleted");
                int newSel = (s >= (int)g_items.size()) ? (int)g_items.size() - 1 : s;
                if (newSel >= 0) SendMessage(g_list,LB_SETCURSEL,newSel,0);
            }
        } else if (id==IDC_CLEAR) {
            if (IDYES==MessageBoxW(hw,L"Delete all history?",L"Clipboard Easy",
                MB_YESNO|MB_ICONQUESTION|MB_TOPMOST|MB_SETFOREGROUND)) {
                g_items.clear(); SaveHist(); Refresh();
                SetWindowText(g_status,L"Cleared");
            }
        }
        break;
    }

    // ── Cleanup ───────────────────────────────────────────────────
    case WM_DESTROY:
        ShowTrayIcon(hw, false);
        RemoveClipboardFormatListener(hw);
        KillTimer(hw,1);
        DeleteObject(g_fNorm); DeleteObject(g_fBold);
        DeleteObject(g_fList); DeleteObject(g_fTiny);
        DeleteObject(g_brBg);  DeleteObject(g_brCard);
        if (g_hIconUI) DestroyIcon(g_hIconUI);
        PostQuitMessage(0);
        break;

    default: return DefWindowProcW(hw,msg,wp,lp);
    }
    return 0;
}

// ══════════════════════════════════════════════════════════════════
//  Entry point
// ══════════════════════════════════════════════════════════════════
int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int)
{
    g_myPid = GetCurrentProcessId();

    // History file path, next to the executable.
    GetModuleFileNameW(nullptr,g_histPath,MAX_PATH);
    wchar_t* sl = wcsrchr(g_histPath,L'\\');
    if (sl) { sl[1]=0; }
    wcscat_s(g_histPath,MAX_PATH,L"clipboard_history.json");
    LoadHist();

    INITCOMMONCONTROLSEX ic = {sizeof(ic),ICC_WIN95_CLASSES};
    InitCommonControlsEx(&ic);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.hInstance     = hi;
    wc.lpszClassName = L"ClipboardEasyC";
    wc.lpfnWndProc   = WndProc;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.hCursor       = LoadCursor(nullptr,IDC_ARROW);
    wc.hIcon         = LoadIconW(hi, L"IDI_ICON1");
    wc.hIconSm       = (HICON)LoadImageW(hi, L"IDI_ICON1", IMAGE_ICON, 16, 16, 0);
    RegisterClassExW(&wc);

    // WS_EX_NOACTIVATE: clicking the window does not steal focus.
    g_hwnd = CreateWindowExW(
        WS_EX_NOACTIVATE | WS_EX_APPWINDOW,
        L"ClipboardEasyC", L"Clipboard Easy",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 530, 660,
        nullptr,nullptr,hi,nullptr);

    ShowWindow(g_hwnd,SW_SHOWNOACTIVATE);
    UpdateWindow(g_hwnd);

    // Set icons.
    HICON hIconBig = LoadIconW(hi, L"IDI_ICON1");
    HICON hIconSmall = (HICON)LoadImageW(hi, L"IDI_ICON1", IMAGE_ICON, 16, 16, 0);
    SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);

    MSG m;
    while (GetMessage(&m,nullptr,0,0)>0) { TranslateMessage(&m); DispatchMessage(&m); }
    return (int)m.wParam;
}
