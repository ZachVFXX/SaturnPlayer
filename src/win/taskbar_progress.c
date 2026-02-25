/*  win/taskbar_progress.c
 *  ──────────────────────────────────────────────────────────────────────────
 *  Taskbar progress-bar API.
 *
 *  Zero system headers beyond what raylib already pulled in.  We forward-
 *  declare the three ole32 functions we call ourselves, sidestepping the
 *  objbase → ole2 → oleidl → MSG chain that breaks under NOUSER.
 *
 *  Link with:  -lole32
 *  Include once:  #include "win/taskbar_progress.c"   (inside #ifdef _WIN32)
 *  ──────────────────────────────────────────────────────────────────────────
 */

/* raylib has already included <windows.h> with NOUSER/NOGDI.  That gives us
 * HWND, HRESULT, BOOL, DWORD, ULONG, ULONGLONG, RECT, LPCWSTR, IID, CLSID,
 * STDMETHODCALLTYPE – everything we need.  We add nothing that pulls in MSG. */

#include "taskbar_progress.h"

/* ── ole32 imports (avoids objbase.h → ole2.h → oleidl.h → MSG) ────────── */

#define COINIT_APARTMENTTHREADED_ 0x2
#define CLSCTX_INPROC_SERVER_     0x1

/* S_OK / S_FALSE / FAILED / SUCCEEDED / RPC_E_CHANGED_MODE are in winerror.h
 * which windows.h always includes regardless of NOUSER. */

__declspec(dllimport) HRESULT __stdcall
CoInitializeEx(void* reserved, DWORD dwCoInit);

__declspec(dllimport) HRESULT __stdcall
CoCreateInstance(const CLSID* rclsid, void* pUnkOuter,
                 DWORD dwClsContext, const IID* riid, void** ppv);

__declspec(dllimport) void __stdcall
CoUninitialize(void);

/* ═══════════════════════════════════════════════════════════════════════════
 * Minimal ITaskbarList3 – no shobjidl.h needed
 * GUIDs and vtable order from the public Windows SDK (stable ABI).
 * ═══════════════════════════════════════════════════════════════════════════ */

static const CLSID CLSID_TaskbarList_ =
    {0x56FDF344,0xFD6D,0x11d0,{0x95,0x8A,0x00,0x60,0x97,0xC9,0xA0,0x90}};

static const IID IID_ITaskbarList3_ =
    {0xEA1AFB91,0x9E28,0x4B86,{0x90,0xE9,0x9E,0x9F,0x8A,0x5E,0xEF,0xAF}};

typedef enum {
    TBPF_NOPROGRESS_    = 0,
    TBPF_INDETERMINATE_ = 0x1,
    TBPF_NORMAL_        = 0x2,
    TBPF_ERROR_         = 0x4,
    TBPF_PAUSED_        = 0x8,
} TBPFLAG_;

typedef struct ITaskbarList3Vtbl_ {
    /* IUnknown */
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void*, const IID*, void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)        (void*);
    ULONG   (STDMETHODCALLTYPE *Release)       (void*);
    /* ITaskbarList */
    HRESULT (STDMETHODCALLTYPE *HrInit)        (void*);
    HRESULT (STDMETHODCALLTYPE *AddTab)        (void*, HWND);
    HRESULT (STDMETHODCALLTYPE *DeleteTab)     (void*, HWND);
    HRESULT (STDMETHODCALLTYPE *ActivateTab)   (void*, HWND);
    HRESULT (STDMETHODCALLTYPE *SetActiveAlt)  (void*, HWND);
    /* ITaskbarList2 */
    HRESULT (STDMETHODCALLTYPE *MarkFullscreenWindow)(void*, HWND, BOOL);
    /* ITaskbarList3 */
    HRESULT (STDMETHODCALLTYPE *SetProgressValue)     (void*, HWND, ULONGLONG, ULONGLONG);
    HRESULT (STDMETHODCALLTYPE *SetProgressState)     (void*, HWND, TBPFLAG_);
    HRESULT (STDMETHODCALLTYPE *RegisterTab)          (void*, HWND, HWND);
    HRESULT (STDMETHODCALLTYPE *UnregisterTab)        (void*, HWND);
    HRESULT (STDMETHODCALLTYPE *SetTabOrder)          (void*, HWND, HWND);
    HRESULT (STDMETHODCALLTYPE *SetTabActive)         (void*, HWND, HWND, DWORD);
    HRESULT (STDMETHODCALLTYPE *ThumbBarAddButtons)   (void*, HWND, UINT, void*);
    HRESULT (STDMETHODCALLTYPE *ThumbBarUpdateButtons)(void*, HWND, UINT, void*);
    HRESULT (STDMETHODCALLTYPE *ThumbBarSetImageList) (void*, HWND, void*);
    HRESULT (STDMETHODCALLTYPE *SetOverlayIcon)       (void*, HWND, void*, LPCWSTR);
    HRESULT (STDMETHODCALLTYPE *SetThumbnailTooltip)  (void*, HWND, LPCWSTR);
    HRESULT (STDMETHODCALLTYPE *SetThumbnailClip)     (void*, HWND, RECT*);
} ITaskbarList3Vtbl_;

typedef struct { ITaskbarList3Vtbl_* lpVtbl; } ITaskbarList3_;

#define TBL3_HrInit(p)                 (p)->lpVtbl->HrInit((p))
#define TBL3_Release(p)                (p)->lpVtbl->Release((p))
#define TBL3_SetProgressState(p, h, f) (p)->lpVtbl->SetProgressState((p),(h),(f))
#define TBL3_SetProgressValue(p,h,c,t) (p)->lpVtbl->SetProgressValue((p),(h),(c),(t))

/* ═══════════════════════════════════════════════════════════════════════════
 * Internal state
 * ═══════════════════════════════════════════════════════════════════════════ */

#define TASKBAR_TOTAL_STEPS 10000uLL

static struct {
    ITaskbarList3_*      list;
    HWND                 hwnd;
    bool                 com_owned;
    TaskbarProgressState state;
    ULONGLONG            completed;
} g_tb = {0};

static TBPFLAG_ state_to_flag(TaskbarProgressState s) {
    switch (s) {
        case TASKBAR_PROGRESS_INDETERMINATE: return TBPF_INDETERMINATE_;
        case TASKBAR_PROGRESS_NORMAL:        return TBPF_NORMAL_;
        case TASKBAR_PROGRESS_ERROR:         return TBPF_ERROR_;
        case TASKBAR_PROGRESS_PAUSED:        return TBPF_PAUSED_;
        default:                             return TBPF_NOPROGRESS_;
    }
}

static void tb_apply_state(void) {
    if (!g_tb.list) return;
    TBL3_SetProgressState(g_tb.list, g_tb.hwnd, state_to_flag(g_tb.state));
}

static void tb_apply_value(void) {
    if (!g_tb.list) return;
    TBL3_SetProgressValue(g_tb.list, g_tb.hwnd, g_tb.completed, TASKBAR_TOTAL_STEPS);
}

static ULONGLONG clamp_to_steps(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return (ULONGLONG)(v * (float)TASKBAR_TOTAL_STEPS);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Lifecycle
 * ═══════════════════════════════════════════════════════════════════════════ */

bool taskbar_progress_init(HWND hwnd) {
    if (g_tb.list) return true;

    g_tb.hwnd = hwnd;

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED_);
    if (hr == RPC_E_CHANGED_MODE) {
        g_tb.com_owned = false;
    } else if (SUCCEEDED(hr)) {
        g_tb.com_owned = (hr == S_OK); /* S_FALSE = already initialised */
    } else {
        return false;
    }

    hr = CoCreateInstance(&CLSID_TaskbarList_, NULL,
                          CLSCTX_INPROC_SERVER_,
                          &IID_ITaskbarList3_,
                          (void**)&g_tb.list);
    if (FAILED(hr)) goto fail;

    hr = TBL3_HrInit(g_tb.list);
    if (FAILED(hr)) {
        TBL3_Release(g_tb.list);
        g_tb.list = NULL;
        goto fail;
    }

    TBL3_SetProgressState(g_tb.list, hwnd, TBPF_NOPROGRESS_);
    g_tb.state     = TASKBAR_PROGRESS_NONE;
    g_tb.completed = 0;
    return true;

fail:
    if (g_tb.com_owned) { CoUninitialize(); g_tb.com_owned = false; }
    return false;
}

void taskbar_progress_destroy(void) {
    if (g_tb.list) {
        TBL3_SetProgressState(g_tb.list, g_tb.hwnd, TBPF_NOPROGRESS_);
        TBL3_Release(g_tb.list);
        g_tb.list = NULL;
    }
    if (g_tb.com_owned) { CoUninitialize(); g_tb.com_owned = false; }
    g_tb.hwnd      = NULL;
    g_tb.state     = TASKBAR_PROGRESS_NONE;
    g_tb.completed = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Control
 * ═══════════════════════════════════════════════════════════════════════════ */

void taskbar_progress_set_state(TaskbarProgressState state) {
    if (!g_tb.list) return;
    g_tb.state = state;
    tb_apply_state();
}

void taskbar_progress_set_value(float value) {
    if (!g_tb.list) return;
    g_tb.completed = clamp_to_steps(value);
    if (g_tb.state == TASKBAR_PROGRESS_NONE ||
        g_tb.state == TASKBAR_PROGRESS_INDETERMINATE) {
        g_tb.state = TASKBAR_PROGRESS_NORMAL;
        tb_apply_state();
    }
    tb_apply_value();
}

void taskbar_progress_set_indeterminate(void) {
    taskbar_progress_set_state(TASKBAR_PROGRESS_INDETERMINATE);
}

void taskbar_progress_hide(void) {
    taskbar_progress_set_state(TASKBAR_PROGRESS_NONE);
}

void taskbar_progress_set_error(float value) {
    if (!g_tb.list) return;
    g_tb.completed = clamp_to_steps(value);
    g_tb.state     = TASKBAR_PROGRESS_ERROR;
    tb_apply_state();
    tb_apply_value();
}

void taskbar_progress_set_paused(float value) {
    if (!g_tb.list) return;
    g_tb.completed = clamp_to_steps(value);
    g_tb.state     = TASKBAR_PROGRESS_PAUSED;
    tb_apply_state();
    tb_apply_value();
}
