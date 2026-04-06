#include <windows.h>
#include <GL/gl.h>
#include <malloc.h>
#include "glext.h" // contains type definitions for all modern OpenGL functions
#include "shaders.inl"
#include "config.h"
#include "music.h"

// Define the modern OpenGL functions to load from the driver

#define glCreateShaderProgramv ((PFNGLCREATESHADERPROGRAMVPROC)wglGetProcAddress("glCreateShaderProgramv"))
#define glUseProgram ((PFNGLUSEPROGRAMPROC)wglGetProcAddress("glUseProgram"))
#define glUniform4fv ((PFNGLUNIFORM4FVPROC)wglGetProcAddress("glUniform4fv"))
#define glUniform4iv ((PFNGLUNIFORM4IVPROC)wglGetProcAddress("glUniform4iv"))
#define glUniform1i ((PFNGLUNIFORM1IPROC)wglGetProcAddress("glUniform1i"))
#define glGetProgramiv ((PFNGLGETPROGRAMIVPROC)wglGetProcAddress("glGetProgramiv"))
#define glGetProgramInfoLog ((PFNGLGETPROGRAMINFOLOGPROC)wglGetProcAddress("glGetProgramInfoLog"))
#define glDispatchCompute ((PFNGLDISPATCHCOMPUTEPROC)wglGetProcAddress("glDispatchCompute"))
#define glCreateBuffers ((PFNGLCREATEBUFFERSPROC)wglGetProcAddress("glCreateBuffers"))
#define glNamedBufferStorage ((PFNGLNAMEDBUFFERSTORAGEPROC)wglGetProcAddress("glNamedBufferStorage"))
#define glBindBufferBase ((PFNGLBINDBUFFERBASEPROC)wglGetProcAddress("glBindBufferBase"))
#define glMemoryBarrier ((PFNGLMEMORYBARRIERPROC)wglGetProcAddress("glMemoryBarrier"))
#define glGetNamedBufferSubData ((PFNGLGETNAMEDBUFFERSUBDATAPROC)wglGetProcAddress("glGetNamedBufferSubData"))

#define N_PARTICLES (1024*4)
#define MAX_VOXELS (32*32*32)
#define VOXEL_CAPACITY (128)

#define N_BUFFERS 5

static GLuint fragShader, fluidShader;
static GLuint buffers[N_BUFFERS];

static GLsizeiptr bufferSizes[] = {
    // particle buffer = N_PARTICLES*{3*vec4}
    N_PARTICLES*3*(16),
    // bbox buffer = 3*vec4 + ivec4 + MAX_VOXELS*ivec4
    3*(16) + 1*(16) + MAX_VOXELS*(16),
    // voxel buffer = MAX_VOXELS*VOXEL_CAPACITY*int
    MAX_VOXELS*VOXEL_CAPACITY*(4),
    // frame buffer = XRES*YRES*vec4
    XRES*YRES*(16),
    // music buffer = NUM_SAMPLES*float
    NUM_SAMPLES*(4)
};

static float cpuMusicBuffer[NUM_SAMPLES];

// XRES, YRES, frame, sample
static GLint params[4*1] = {XRES, YRES, 0, 0};

#define PARAM_FRAME 2
#define PARAM_SAMPLE 3

#define N_PARAMS (sizeof(params)/(4*sizeof(GLint)))

#define LOC_PARAMS 0
#define LOC_OPTION 1
#define LOC_WHAT_GRID 2

#define CEIL_DIV(x, y) ((x) + (y) - 1) / (y)


void check_shader(GLint shader, HWND hwnd, LPCWSTR title);
void run_fluid_shader(GLuint numInvoc, GLint option);

void intro_init(HWND hwnd) {
    fragShader = glCreateShaderProgramv(GL_FRAGMENT_SHADER, 1, &shader_frag);
    fluidShader = glCreateShaderProgramv(GL_COMPUTE_SHADER, 1, &fluid_comp);

    #ifdef DEBUG
    check_shader(fragShader, hwnd, "Render fragment shader error");
    check_shader(fluidShader, hwnd, "Fluid compute shader error");
    #endif

    glCreateBuffers(N_BUFFERS, buffers);
    for (int i = 0; i < N_BUFFERS; i++) {
        glNamedBufferStorage(buffers[i], bufferSizes[i], NULL, GL_DYNAMIC_STORAGE_BIT);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, buffers[i]);
    }

    glUseProgram(fluidShader);
    glUniform4iv(LOC_PARAMS, N_PARAMS, params);
    run_fluid_shader(N_PARTICLES, 0);
}

void intro_do(GLint sample) {
    params[PARAM_SAMPLE] = sample;

    glUseProgram(fluidShader);
    glUniform4iv(LOC_PARAMS, N_PARAMS, params);

    glUniform1i(LOC_WHAT_GRID, 0); // whatGrid = physics
    for(int i = 0; i < 4; i++){
        // build physics grid
        run_fluid_shader(          1, 4);
        run_fluid_shader( MAX_VOXELS, 5);
        run_fluid_shader(N_PARTICLES, 6);
        // update physics
        run_fluid_shader(N_PARTICLES, 1);
        run_fluid_shader(N_PARTICLES, 2);
        run_fluid_shader(N_PARTICLES, 3);
    }

    glUniform1i(LOC_WHAT_GRID, 1); // whatGrid = render
    run_fluid_shader(          1, 4);
    run_fluid_shader( MAX_VOXELS, 5);
    run_fluid_shader(N_PARTICLES, 6);

    // render into ssbo
    run_fluid_shader( MAX_VOXELS, 7);
    run_fluid_shader(  XRES*YRES, 8);

    // render into frame buffer
    glUseProgram(fragShader);
    glUniform4iv(LOC_PARAMS, N_PARAMS, params);
    glRects(-1, -1, 1, 1);

    params[PARAM_FRAME]++; // frame count
}

void music_init(short* buffer) {
    run_fluid_shader(CEIL_DIV(NUM_SAMPLES, 1024), 9);

    glGetNamedBufferSubData(buffers[4], 0, bufferSizes[4], cpuMusicBuffer);
    for(int i = 0; i < NUM_SAMPLES; i++) {
        float out = cpuMusicBuffer[i];
        buffer[i] = (short)out;
    }
}

static void run_fluid_shader(GLuint numInvoc, GLint option) {
    // glUseProgram(fluidShader);
    glUniform1i(LOC_OPTION, option);
    glDispatchCompute(CEIL_DIV(numInvoc, 64), 1, 1);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
}

static void check_shader(GLint shader, HWND hwnd, LPCWSTR title) {
    GLuint result;
    glGetProgramiv(shader, GL_LINK_STATUS, &result);
    if(!result) {
        GLint infoLength;
        glGetProgramiv(shader, GL_INFO_LOG_LENGTH, &infoLength);
        GLchar* info = (GLchar*)malloc(infoLength * sizeof(GLchar));
        glGetProgramInfoLog(shader, infoLength, NULL, info);
        MessageBox(hwnd, info, title, MB_OK);
        free(info);
        ExitProcess(0);
    }
}