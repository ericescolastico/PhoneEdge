#include "win_driver.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <winsock2.h>
#include <windows.h>

#include "pe/pe_monitors.h"
#include "util/log.h"
#include "util/thread.h"
#include "util/tick.h"

// Not in older MinGW headers
#ifndef LLMHF_LOWER_IL_INJECTED
# define LLMHF_LOWER_IL_INJECTED 0x02
#endif
#ifndef LLKHF_LOWER_IL_INJECTED
# define LLKHF_LOWER_IL_INJECTED 0x02
#endif

#define PE_WND_CLASS "PhoneEdgeInput"
#define PE_TIMER_ID 1
#define PE_TIMER_MS 4
#define PE_WM_STOP (WM_APP + 1)

static struct {
    struct pe_session *session;
    struct pe_config cfg;

    HANDLE thread;
    DWORD thread_id;
    HWND hwnd;
    HHOOK mouse_hook;
    HHOOK keyboard_hook;
    bool clip_active;

    // Set by the emergency release: the hooks let everything through
    atomic_bool hard_release;
    atomic_bool started;
    atomic_bool ready;
    atomic_bool failed;
} g;

static uint64_t
now_ms(void) {
    return (uint64_t) SC_TICK_TO_MS(sc_tick_now());
}

// ---------------------------------------------------------------------------
// Monitors

struct monitors {
    struct pe_monitor_info info[PE_MAX_MONITORS];
    int count;
};

static BOOL CALLBACK
enum_monitor_proc(HMONITOR hmon, HDC hdc, LPRECT rect, LPARAM lparam) {
    (void) hdc;
    (void) rect;
    struct monitors *m = (struct monitors *) lparam;
    if (m->count >= PE_MAX_MONITORS) {
        return TRUE;
    }
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfo(hmon, &mi)) {
        return TRUE;
    }
    struct pe_monitor_info *info = &m->info[m->count++];
    info->rect.left = mi.rcMonitor.left;
    info->rect.top = mi.rcMonitor.top;
    info->rect.right = mi.rcMonitor.right;
    info->rect.bottom = mi.rcMonitor.bottom;
    info->primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    return TRUE;
}

static int
enumerate_monitors(struct pe_monitor_info *sorted) {
    struct monitors m;
    m.count = 0;
    EnumDisplayMonitors(NULL, NULL, enum_monitor_proc, (LPARAM) &m);
    pe_monitors_sort(m.info, sorted, m.count);
    return m.count;
}

static void
update_monitors(void) {
    struct pe_monitor_info sorted[PE_MAX_MONITORS];
    int count = enumerate_monitors(sorted);

    struct pe_rect rects[PE_MAX_MONITORS];
    for (int i = 0; i < count; ++i) {
        rects[i] = sorted[i].rect;
    }
    int selected = g.cfg.monitor_index;
    if (selected >= count) {
        LOGW("Monitor %d does not exist (%d found): the edge handoff is "
             "disabled until it appears", selected, count);
    }
    pe_session_set_monitors(g.session, rects, count, selected);
}

void
pe_win_print_monitors(void) {
    struct pe_monitor_info sorted[PE_MAX_MONITORS];
    int count = enumerate_monitors(sorted);
    printf("Monitors (use the number as \"monitor\"):\n");
    for (int i = 0; i < count; ++i) {
        const struct pe_rect *r = &sorted[i].rect;
        printf("  %d: %dx%d at (%d,%d)%s\n", i, (int) (r->right - r->left),
               (int) (r->bottom - r->top), (int) r->left, (int) r->top,
               sorted[i].primary ? " [primary]" : "");
    }
}

// ---------------------------------------------------------------------------
// Cursor clipping (optional plan B: freeze the cursor even if a hook fails)

static void
clip_to_cursor(void) {
    POINT p;
    if (GetCursorPos(&p)) {
        RECT r = {p.x, p.y, p.x + 1, p.y + 1};
        ClipCursor(&r);
        g.clip_active = true;
    }
}

static void
unclip(void) {
    if (g.clip_active) {
        ClipCursor(NULL);
        g.clip_active = false;
    }
}

static void
sync_clip(void) {
    if (!g.cfg.clip_cursor) {
        return;
    }
    bool active = pe_session_is_phone_active(g.session);
    if (active && !g.clip_active) {
        clip_to_cursor();
    } else if (!active && g.clip_active) {
        unclip();
    }
}

// ---------------------------------------------------------------------------
// Hooks (run in the capture thread, during its message loop)

static bool
bypass(void) {
    return atomic_load(&g.hard_release);
}

static LRESULT CALLBACK
mouse_hook_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code != HC_ACTION || bypass()) {
        return CallNextHookEx(NULL, code, wparam, lparam);
    }

    const MSLLHOOKSTRUCT *ms = (const MSLLHOOKSTRUCT *) lparam;
    if (ms->flags & (LLMHF_INJECTED | LLMHF_LOWER_IL_INJECTED)) {
        return CallNextHookEx(NULL, code, wparam, lparam);
    }

    uint64_t now = now_ms();
    bool swallow = false;
    switch (wparam) {
        case WM_MOUSEMOVE:
            // The deltas come from Raw Input; here, only freeze the cursor
            swallow = pe_session_is_phone_active(g.session);
            if (LOG_VERBOSE_ENABLED()) {
                LOGV("hook mouse move at %ld,%ld%s", (long) ms->pt.x,
                     (long) ms->pt.y, swallow ? " (swallowed)" : "");
            }
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
            swallow = pe_session_on_button(g.session, now, PE_BTN_LEFT,
                                           wparam == WM_LBUTTONDOWN);
            break;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
            swallow = pe_session_on_button(g.session, now, PE_BTN_RIGHT,
                                           wparam == WM_RBUTTONDOWN);
            break;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
            swallow = pe_session_on_button(g.session, now, PE_BTN_MIDDLE,
                                           wparam == WM_MBUTTONDOWN);
            break;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP: {
            WORD which = HIWORD(ms->mouseData);
            uint8_t button = which == XBUTTON1 ? PE_BTN_X1
                           : which == XBUTTON2 ? PE_BTN_X2 : 0;
            if (button) {
                swallow = pe_session_on_button(g.session, now, button,
                                               wparam == WM_XBUTTONDOWN);
            }
            break;
        }
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {
            int delta = (short) HIWORD(ms->mouseData);
            swallow = pe_session_on_wheel(g.session, now,
                                          wparam == WM_MOUSEHWHEEL, delta);
            break;
        }
        default:
            break;
    }

    sync_clip();

    if (swallow) {
        return 1;
    }
    return CallNextHookEx(NULL, code, wparam, lparam);
}

static LRESULT CALLBACK
keyboard_hook_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code != HC_ACTION || bypass()) {
        return CallNextHookEx(NULL, code, wparam, lparam);
    }

    const KBDLLHOOKSTRUCT *kb = (const KBDLLHOOKSTRUCT *) lparam;
    if (kb->flags & (LLKHF_INJECTED | LLKHF_LOWER_IL_INJECTED)) {
        return CallNextHookEx(NULL, code, wparam, lparam);
    }

    bool down = wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN;
    bool up = wparam == WM_KEYUP || wparam == WM_SYSKEYUP;
    if (!down && !up) {
        return CallNextHookEx(NULL, code, wparam, lparam);
    }

    uint8_t hid = pe_keymap_from_windows((uint16_t) kb->scanCode,
                                         (kb->flags & LLKHF_EXTENDED) != 0,
                                         (uint16_t) kb->vkCode);
    if (LOG_VERBOSE_ENABLED()) {
        LOGV("hook key vk=0x%02lx scan=0x%02lx flags=0x%02lx %s -> hid=0x%02x",
             (unsigned long) kb->vkCode, (unsigned long) kb->scanCode,
             (unsigned long) kb->flags, down ? "down" : "up", hid);
    }
    if (!hid) {
        // Unknown key (media keys...): always left to the PC, so that its
        // release can never be lost
        return CallNextHookEx(NULL, code, wparam, lparam);
    }

    bool swallow = pe_session_on_key(g.session, now_ms(), hid, down);
    sync_clip();

    if (swallow) {
        return 1;
    }
    return CallNextHookEx(NULL, code, wparam, lparam);
}

// ---------------------------------------------------------------------------
// Raw Input (deltas) and the message window

static void
handle_raw_input(HRAWINPUT handle) {
    UINT size = 0;
    if (GetRawInputData(handle, RID_INPUT, NULL, &size,
                        sizeof(RAWINPUTHEADER)) != 0 || !size) {
        return;
    }
    // Mouse events are small: avoid the heap in the hot path
    BYTE buf[128];
    if (size > sizeof(buf)) {
        return;
    }
    if (GetRawInputData(handle, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER))
            != size) {
        return;
    }
    const RAWINPUT *raw = (const RAWINPUT *) buf;
    if (raw->header.dwType != RIM_TYPEMOUSE) {
        return;
    }

    const RAWMOUSE *m = &raw->data.mouse;
    if (m->usFlags & MOUSE_MOVE_ABSOLUTE) {
        // Touch screens, pens, remote desktop... not a relative mouse
        return;
    }
    int dx = (int) m->lLastX;
    int dy = (int) m->lLastY;
    if (!dx && !dy) {
        return;
    }

    POINT p;
    if (!GetCursorPos(&p)) {
        return;
    }
    if (LOG_VERBOSE_ENABLED()) {
        LOGV("raw input dx=%d dy=%d cursor=%ld,%ld", dx, dy, (long) p.x,
             (long) p.y);
    }
    pe_session_on_mouse_move(g.session, now_ms(), dx, dy, p.x, p.y);
    sync_clip();
}

static LRESULT CALLBACK
wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_INPUT:
            if (!bypass()) {
                handle_raw_input((HRAWINPUT) lparam);
            }
            return DefWindowProc(hwnd, msg, wparam, lparam);
        case WM_TIMER:
            if (wparam == PE_TIMER_ID && !bypass()) {
                if (pe_session_is_phone_active(g.session)) {
                    pe_session_on_tick(g.session, now_ms());
                    sync_clip();
                }
            }
            return 0;
        case WM_DISPLAYCHANGE:
            update_monitors();
            return 0;
        case PE_WM_STOP:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(hwnd, msg, wparam, lparam);
    }
}

static DWORD WINAPI
capture_thread(LPVOID param) {
    (void) param;

    HINSTANCE inst = GetModuleHandle(NULL);

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.lpszClassName = PE_WND_CLASS;
    if (!RegisterClassA(&wc)) {
        LOGE("RegisterClass failed (%lu)", GetLastError());
        atomic_store(&g.failed, true);
        atomic_store(&g.ready, true);
        return 1;
    }

    g.hwnd = CreateWindowExA(0, PE_WND_CLASS, "PhoneEdge", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, NULL, inst, NULL);
    if (!g.hwnd) {
        LOGE("CreateWindow failed (%lu)", GetLastError());
        atomic_store(&g.failed, true);
        atomic_store(&g.ready, true);
        return 1;
    }

    RAWINPUTDEVICE rid;
    rid.usUsagePage = 0x01; // generic desktop
    rid.usUsage = 0x02;     // mouse
    rid.dwFlags = RIDEV_INPUTSINK; // also when not in the foreground
    rid.hwndTarget = g.hwnd;
    if (!RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        LOGE("RegisterRawInputDevices failed (%lu)", GetLastError());
        atomic_store(&g.failed, true);
        atomic_store(&g.ready, true);
        DestroyWindow(g.hwnd);
        return 1;
    }

    update_monitors();

    g.mouse_hook = SetWindowsHookExA(WH_MOUSE_LL, mouse_hook_proc, inst, 0);
    g.keyboard_hook = SetWindowsHookExA(WH_KEYBOARD_LL, keyboard_hook_proc,
                                        inst, 0);
    if (!g.mouse_hook || !g.keyboard_hook) {
        LOGE("Could not install the input hooks (%lu)", GetLastError());
        atomic_store(&g.failed, true);
    }

    timeBeginPeriod(1);
    SetTimer(g.hwnd, PE_TIMER_ID, PE_TIMER_MS, NULL);

    atomic_store(&g.ready, true);

    if (!atomic_load(&g.failed)) {
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    // Cleanup: always, whatever the way out
    atomic_store(&g.hard_release, true);
    KillTimer(g.hwnd, PE_TIMER_ID);
    timeEndPeriod(1);
    if (g.mouse_hook) {
        UnhookWindowsHookEx(g.mouse_hook);
        g.mouse_hook = NULL;
    }
    if (g.keyboard_hook) {
        UnhookWindowsHookEx(g.keyboard_hook);
        g.keyboard_hook = NULL;
    }
    unclip();
    DestroyWindow(g.hwnd);
    UnregisterClassA(PE_WND_CLASS, inst);
    return 0;
}

bool
pe_win_driver_start(struct pe_session *session, const struct pe_config *cfg) {
    g.session = session;
    g.cfg = *cfg;
    atomic_store(&g.hard_release, false);
    atomic_store(&g.ready, false);
    atomic_store(&g.failed, false);

    g.thread = CreateThread(NULL, 0, capture_thread, NULL, 0, &g.thread_id);
    if (!g.thread) {
        LOGE("Could not create the capture thread (%lu)", GetLastError());
        return false;
    }
    // Make sure that the hooks are served promptly
    SetThreadPriority(g.thread, THREAD_PRIORITY_TIME_CRITICAL);

    while (!atomic_load(&g.ready)) {
        Sleep(1);
    }
    if (atomic_load(&g.failed)) {
        WaitForSingleObject(g.thread, 2000);
        CloseHandle(g.thread);
        g.thread = NULL;
        return false;
    }
    atomic_store(&g.started, true);
    return true;
}

void
pe_win_driver_stop(void) {
    if (!g.thread) {
        return;
    }
    // Let the hooks go right now, then ask the thread to finish
    atomic_store(&g.hard_release, true);
    if (g.hwnd) {
        PostMessage(g.hwnd, PE_WM_STOP, 0, 0);
    }
    PostThreadMessage(g.thread_id, WM_QUIT, 0, 0);
    if (WaitForSingleObject(g.thread, 3000) == WAIT_TIMEOUT) {
        LOGW("The capture thread did not stop in time");
        pe_win_emergency_release();
    }
    CloseHandle(g.thread);
    g.thread = NULL;
    atomic_store(&g.started, false);
}

void
pe_win_emergency_release(void) {
    atomic_store(&g.hard_release, true);
    // UnhookWindowsHookEx() is safe from any thread of the process
    HHOOK mh = g.mouse_hook;
    HHOOK kh = g.keyboard_hook;
    if (mh) {
        UnhookWindowsHookEx(mh);
    }
    if (kh) {
        UnhookWindowsHookEx(kh);
    }
    if (g.clip_active) {
        ClipCursor(NULL);
    }
}

static BOOL WINAPI
console_ctrl_handler(DWORD type) {
    // Any event: the process is about to end or be asked to
    (void) type;
    pe_win_emergency_release();
    // Not handled: let the other handlers (clean exit on Ctrl+C) run
    return FALSE;
}

static LONG WINAPI
exception_filter(EXCEPTION_POINTERS *info) {
    (void) info;
    pe_win_emergency_release();
    return EXCEPTION_CONTINUE_SEARCH;
}

static void
at_exit_release(void) {
    pe_win_emergency_release();
}

// A click in the console window with QuickEdit enabled freezes the writes to
// it, and so would freeze the hooks if they log: disable it
static void
disable_console_quick_edit(void) {
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    if (in != INVALID_HANDLE_VALUE && GetConsoleMode(in, &mode)) {
        mode &= ~ENABLE_QUICK_EDIT_MODE;
        mode |= ENABLE_EXTENDED_FLAGS;
        SetConsoleMode(in, mode);
    }
}

void
pe_win_install_safety_handlers(void) {
    disable_console_quick_edit();
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
    SetUnhandledExceptionFilter(exception_filter);
    atexit(at_exit_release);
}
