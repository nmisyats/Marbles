#pragma once

#include <windows.h>

void start_capture(HWND hwnd);
void finish_capture(HWND hwnd);
void capture_frame(HWND hwnd);
void save_audio(short* buffer, DWORD bytes, HWND hwnd);