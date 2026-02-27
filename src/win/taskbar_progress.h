#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <stdbool.h>

/* ── Progress states ──────────────────────────────────────────────────────── */
typedef enum {
    TASKBAR_PROGRESS_NONE          = 0x0,   /* hide the progress indicator   */
    TASKBAR_PROGRESS_INDETERMINATE = 0x1,   /* animated marquee (no value)   */
    TASKBAR_PROGRESS_NORMAL        = 0x2,   /* green bar                     */
    TASKBAR_PROGRESS_ERROR         = 0x4,   /* red bar                       */
    TASKBAR_PROGRESS_PAUSED        = 0x8,   /* yellow bar                    */
} TaskbarProgressState;

/* ── Lifecycle ────────────────────────────────────────────────────────────── */

/*  Initialize the taskbar progress system.
 *  Must be called after the application window has been created.
 *  Safe to call multiple times – subsequent calls are no-ops.
 *  Returns false if COM initialisation or interface creation failed.        */
bool taskbar_progress_init(HWND hwnd);

/*  Release all COM resources and hide the progress indicator.
 *  Safe to call even if init was never called or failed.                    */
void taskbar_progress_destroy(void);

/* ── Control ──────────────────────────────────────────────────────────────── */

/*  Set the visual state of the progress indicator.
 *  TASKBAR_PROGRESS_INDETERMINATE and TASKBAR_PROGRESS_NONE ignore value.  */
void taskbar_progress_set_state(TaskbarProgressState state);

/*  Set progress to a normalised value in [0.0, 1.0].
 *  Automatically switches to TASKBAR_PROGRESS_NORMAL state.                */
void taskbar_progress_set_value(float value);

/*  Convenience: show an indeterminate spinner.                              */
void taskbar_progress_set_indeterminate(void);

/*  Convenience: hide the indicator entirely.                                */
void taskbar_progress_hide(void);

/*  Convenience: show a red error bar at the current value.                  */
void taskbar_progress_set_error(float value);

/*  Convenience: show a yellow paused bar at the current value.              */
void taskbar_progress_set_paused(float value);
