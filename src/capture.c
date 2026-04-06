#include <windows.h>
#include <GL/gl.h>
#include "glext.h"
#include "config.h"

#define FRAME_SIZE 3*XRES*YRES


#define glGenFramebuffers ((PFNGLGENFRAMEBUFFERSPROC)wglGetProcAddress("glGenFramebuffers"))
#define glBindFramebuffer ((PFNGLBINDFRAMEBUFFERPROC)wglGetProcAddress("glBindFramebuffer"))
#define glFramebufferTexture2D ((PFNGLFRAMEBUFFERTEXTURE2DPROC)wglGetProcAddress("glFramebufferTexture2D"))
#define glCheckFramebufferStatus ((PFNGLCHECKFRAMEBUFFERSTATUSPROC)wglGetProcAddress("glCheckFramebufferStatus"))


static GLubyte frame[FRAME_SIZE];
static GLuint fboTexture;
static GLuint fbo;

static HANDLE ffmpegStdinWrite;
static PROCESS_INFORMATION ffmpegPi;


static int write_all(HANDLE hnd, const void* data, DWORD bytes) {
    const BYTE* p = (const BYTE*)data;
    while (bytes > 0) {
        DWORD nbWritten = 0;
        if (!WriteFile(hnd, p, bytes, &nbWritten, NULL)) {
            return 0; // error
        }
        p += nbWritten;
        bytes -= nbWritten;
    }
    return 1; // success
}

void start_capture(HWND hwnd) {
    SECURITY_ATTRIBUTES sa = {0};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE stdinRead = NULL;
    CreatePipe(&stdinRead, &ffmpegStdinWrite, &sa, 0);

    SetHandleInformation(ffmpegStdinWrite, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFO si = {0};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = stdinRead;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    char cmd[1024];
    #ifdef SOUND
    wsprintf(cmd,
        "ffmpeg -y "
        "-f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i - "
        "-i \".\\audio.mp3\" "
        "-map 0:v:0 -map 1:a:0 "
        "-vf vflip "
        "-c:v libx264 -pix_fmt yuv420p "
        "-c:a aac -b:a 192k "
        "-shortest "
        "\".\\capture.mp4\"",
        XRES, YRES, CAPTURE_FRAMERATE);
    #else
    wsprintf(cmd,
        "ffmpeg -y "
        "-f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i - "
        "-c:v libx264 -pix_fmt yuv420p "
        "\".\\capture.mp4\"",
        XRES, YRES, CAPTURE_FRAMERATE);
    #endif

    BOOL ok = CreateProcess(
        NULL, cmd,
        NULL, NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL, NULL,
        &si, &ffmpegPi);
    
    if (!ok) {
        MessageBox(hwnd, "Failed to start ffmpeg for video capture.", "Error", MB_OK);
        ExitProcess(1);
    }

    CloseHandle(stdinRead);

    // Create texture to render into
    glGenTextures(1, &fboTexture);
    glBindTexture(GL_TEXTURE_2D, fboTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, XRES, YRES, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);

    // Create FBO
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTexture, 0);

    // Check FBO is complete
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        MessageBox(hwnd, "FBO creation failed.", "Error", MB_OK);
        ExitProcess(1);
    }

    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glViewport(0, 0, XRES, YRES);
}

void capture_frame(HWND hwnd) {
    glReadPixels(0, 0, XRES, YRES, GL_RGB, GL_UNSIGNED_BYTE, frame);
    
    if (!write_all(ffmpegStdinWrite, frame, FRAME_SIZE)) {
        MessageBox(hwnd, "Failed to pipe frame to ffmpeg.", "Error", MB_OK);
        ExitProcess(1);
    }
}

void finish_capture(HWND hwnd) {
    CloseHandle(ffmpegStdinWrite); // EOF to ffmpeg
    WaitForSingleObject(ffmpegPi.hProcess, INFINITE);
    
    DWORD exitCode = 0;
    GetExitCodeProcess(ffmpegPi.hProcess, &exitCode);
    if (exitCode != 0) {
        MessageBox(hwnd, "Failed to encode video capture.", "Error", MB_OK);
        ExitProcess(1);
    }

    CloseHandle(ffmpegPi.hThread);
    CloseHandle(ffmpegPi.hProcess);
}

void save_audio(short* buffer, DWORD bytes, HWND hwnd) {
    // Save raw buffer to file
    HANDLE file = CreateFile(
        ".\\audio.raw",
        GENERIC_WRITE,
        0,
        NULL, 
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if(file == INVALID_HANDLE_VALUE) {
        MessageBox(hwnd, "Failed to create audio file.", "Error", MB_OK);
        ExitProcess(1);
    }

    if (!write_all(file, buffer, bytes)) {
        CloseHandle(file);
        DeleteFile(".\\audio.raw");
        MessageBox(hwnd, "Failed to write raw audio file.", "Error", MB_OK);
        ExitProcess(1);
    }

    CloseHandle(file);

    // Run ffmpeg to convert raw to mp3
    STARTUPINFOA si = {0};
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi = {0};

    char cmd[1024];
    wsprintf(cmd,
        "ffmpeg -y "
        "-f s16le -ar 44100 -ac 1 -i \"%s\" "
        "-c:a libmp3lame -q:a 2 "
        "\"%s\"",
        ".\\audio.raw",
        ".\\audio.mp3"
    );

    BOOL ok = CreateProcess(
        NULL, cmd,
        NULL, NULL,
        FALSE,
        CREATE_NO_WINDOW,
        NULL, NULL,
        &si, &pi
    );

    if (!ok) {
        DeleteFile(".\\audio.raw");
        MessageBox(hwnd, "Failed to start ffmpeg for MP3 encoding.", "Error", MB_OK);
        ExitProcess(1);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    DeleteFile(".\\audio.raw");

    if (exitCode != 0) {
        MessageBox(hwnd, "ffmpeg MP3 encoding failed.", "Error", MB_OK);
        ExitProcess(1);
    }
}