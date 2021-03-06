/*
 * Copyright 1993-2010 NVIDIA Corporation.  All rights reserved.
 *
 * Please refer to the NVIDIA end user license agreement (EULA) associated
 * with this source code for terms and conditions that govern your use of
 * this software. Any use, reproduction, disclosure, or distribution of
 * this software and related documentation outside the terms of the EULA
 * is strictly prohibited.
 *
 */
 
 /*
    Class to represent an array in GPU and CPU memory
*/

#include <stdlib.h>
#include <stdio.h>
#include <GL/glew.h>
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
//#include <cutil_inline.h>

extern int devID;
extern int renderDevID;

typedef unsigned int uint;

#define cutilSafeCall(err)           __cudaSafeCall      (err, __FILE__, __LINE__)

inline void __cudaSafeCall(cudaError err, const char *file, const int line)
{
    if (cudaSuccess != err) {
        fprintf(stderr, "%s(%i) : cudaSafeCall() Runtime API error %d: %s.\n",
                 file, line, (int)err, cudaGetErrorString(err));
        exit(-1);
    }
}

template <class T> 
class GpuArray {
public:
    GpuArray();
    ~GpuArray();

    enum Direction
    {
        HOST_TO_DEVICE,
	    DEVICE_TO_HOST,
    };

    // allocate and free
    void alloc(size_t size, bool vbo=false, bool doubleBuffer=false, bool useElementArray=false);
    void free();

    // swap buffers for double buffering
    void swap();

    // when using vbo, must map before getting device ptr
    void map();
    void unmap();

    void copy(Direction dir, uint start=0, uint count=0);
    void memset(T value, uint start=0, uint count=0);

    T *getDevicePtr() { return m_dptr[m_currentRead]; }
    GLuint getVbo() { return m_vbo[m_currentRead]; }

    T *getDeviceWritePtr() { return m_dptr[m_currentWrite]; }
    GLuint getWriteVbo() { return m_vbo[m_currentWrite]; }

    T *getHostPtr() { return m_hptr; }

    size_t getSize() const { return m_size; }

private:
    GLuint createVbo(size_t size, bool useElementArray);

    void allocDevice();
    void allocVbo(bool useElementArray);
    void allocHost();

    void freeDevice();
    void freeVbo();
    void freeHost();

    size_t m_size;
    T *m_dptr[2];
    GLuint m_vbo[2];
    T *m_hptr;

    bool m_useVBO;
    bool m_doubleBuffer;
    uint m_currentRead, m_currentWrite;
};

template <class T> 
GpuArray<T>::GpuArray() :
    m_size(0),
    m_hptr(0),
    m_currentRead(0),
    m_currentWrite(0)
{
    m_dptr[0] = 0;
    m_dptr[1] = 0;

    m_vbo[0] = 0;
    m_vbo[1] = 0;
}

template <class T> 
GpuArray<T>::~GpuArray()
{
    free();
}

template <class T> 
void
GpuArray<T>::alloc(size_t size, bool vbo, bool doubleBuffer, bool useElementArray)
{
    m_size = size;

    m_useVBO = vbo;
    m_doubleBuffer = doubleBuffer;
    if (m_doubleBuffer) {
        m_currentWrite = 1;
    }

    allocHost();
    if (vbo) {
        allocVbo(useElementArray);
    } else {
        allocDevice();
    }
}

template <class T> 
void
GpuArray<T>::free()
{
    freeHost();
    if (m_vbo) {
        freeVbo();
    } else {
        freeDevice();
    }
}

template <class T> 
void
GpuArray<T>::allocHost()
{
    m_hptr = (T *) new T [m_size];
}

template <class T> 
void
GpuArray<T>::freeHost()
{
    if (m_hptr) {
        delete [] m_hptr;
        m_hptr = 0;
    }
}

template <class T> 
void
GpuArray<T>::allocDevice()
{
    cutilSafeCall(cudaMalloc((void **) &m_dptr[0], m_size*sizeof(T)));
    if (m_doubleBuffer) {
        cutilSafeCall(cudaMalloc((void **) &m_dptr[1], m_size*sizeof(T)));
    }
}

template <class T> 
void
GpuArray<T>::freeDevice()
{
    if (m_dptr[0]) {
        cutilSafeCall(cudaFree(m_dptr[0]));
        m_dptr[0] = 0;
    }

    if (m_dptr[1]) {
        cutilSafeCall(cudaFree(m_dptr[1]));
        m_dptr[1] = 0;
    }
}

template <class T> 
GLuint
GpuArray<T>::createVbo(size_t size, bool useElementArray)
{
    GLuint vbo;
    glGenBuffers(1, &vbo);

    if (useElementArray) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, 0, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, size, 0, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    cutilSafeCall(cudaGLRegisterBufferObject(vbo));
    cutilSafeCall(cudaGLSetBufferObjectMapFlags(vbo, cudaGLMapFlagsWriteDiscard));    // CUDA writes, GL consumes
    return vbo;
}

template <class T> 
void
GpuArray<T>::allocVbo(bool useElementArray)
{
    m_vbo[0] = createVbo(m_size*sizeof(T), useElementArray);
    if (m_doubleBuffer) {
        m_vbo[1] = createVbo(m_size*sizeof(T), useElementArray);
    }
}

template <class T> 
void
GpuArray<T>::freeVbo()
{
    if (m_vbo[0]) {
        cutilSafeCall(cudaGLUnregisterBufferObject(m_vbo[0]));
        glDeleteBuffers(1, &m_vbo[0]);
        m_vbo[0] = 0;
    }

    if (m_vbo[1]) {
        cutilSafeCall(cudaGLUnregisterBufferObject(m_vbo[1]));
        glDeleteBuffers(1, &m_vbo[1]);
        m_vbo[1] = 0;
    }
}

template <class T> 
void
GpuArray<T>::swap()
{
    std::swap(m_currentRead, m_currentWrite);
}

template <class T> 
void
GpuArray<T>::map()
{
    if (m_vbo[0]) {
        cutilSafeCall(cudaGLMapBufferObject((void **) &m_dptr[0], m_vbo[0]));
    } 
    if (m_doubleBuffer && m_vbo[1]) {
        cutilSafeCall(cudaGLMapBufferObject((void **) &m_dptr[1], m_vbo[1]));
    }
}

template <class T> 
void
GpuArray<T>::unmap()
{
    if (m_vbo[0]) {
        cutilSafeCall(cudaGLUnmapBufferObject(m_vbo[0]));
        m_dptr[0] = 0;
    }
    if (m_doubleBuffer && m_vbo[1]) {
        cutilSafeCall(cudaGLUnmapBufferObject(m_vbo[1]));
        m_dptr[1] = 0;
    }
}

template <class T> 
void
GpuArray<T>::copy(Direction dir, uint start, uint count)
{
    if (count==0) {
        count = (uint) m_size;
    }

	//assert(start < m_size);

    cudaSetDevice(renderDevID);

    map();
    switch(dir) {
    case HOST_TO_DEVICE:
	    cutilSafeCall(cudaMemcpy((void *) (m_dptr[m_currentRead] + start), (void *) (m_hptr + start), count*sizeof(T), cudaMemcpyHostToDevice));
        break;

    case DEVICE_TO_HOST:
        cutilSafeCall(cudaMemcpy((void *) (m_hptr + start), (void *) (m_dptr[m_currentRead] + start), count*sizeof(T), cudaMemcpyDeviceToHost));
        break;
    }
    unmap();

    cudaSetDevice(devID);
}


template <class T> 
void
GpuArray<T>::memset(T value, uint start, uint count)
{
    
}
