#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>

void set_window_title_bar_color(void *hwnd, unsigned char r, unsigned char g, unsigned char b) {
    COLORREF color = RGB(r, g, b);
    DwmSetWindowAttribute((HWND)hwnd, DWMWA_CAPTION_COLOR, &color, sizeof(color));
    COLORREF text = RGB(255, 255, 255);
    DwmSetWindowAttribute((HWND)hwnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
}
#endif
