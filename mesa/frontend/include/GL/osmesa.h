/*
 * Mesa 3-D graphics library
 * 
 * Copyright (C) 1999-2005  Brian Paul   All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


/*
 * Mesa Off-Screen rendering interface.
 *
 * This is an operating system and window system independent interface to
 * Mesa which allows one to render images into a client-supplied buffer in
 * main memory.  Such images may manipulated or saved in whatever way the
 * client wants.
 *
 * These are the API functions:
 *   OSMesaCreateContext - create a new Off-Screen Mesa rendering context
 *   OSMesaMakeCurrent - bind an OSMesaContext to a client's image buffer
 *                       and make the specified context the current one.
 *   OSMesaDestroyContext - destroy an OSMesaContext
 *   OSMesaGetCurrentContext - return thread's current context ID
 *   OSMesaPixelStore - controls how pixels are stored in image buffer
 *   OSMesaGetIntegerv - return OSMesa state parameters
 *
 *
 * The limits on the width and height of an image buffer can be retrieved
 * via OSMesaGetIntegerv(OSMESA_MAX_WIDTH/OSMESA_MAX_HEIGHT).
 */


#ifndef OSMESA_H
#define OSMESA_H


#ifdef __cplusplus
extern "C" {
#endif


#include <GL/gl.h>


#define OSMESA_MAJOR_VERSION 11
#define OSMESA_MINOR_VERSION 2
#define OSMESA_PATCH_VERSION 0



/*
 * Values for the format parameter of OSMesaCreateContext()
 * New in version 2.0.
 */
#define OSMESA_COLOR_INDEX	GL_COLOR_INDEX
#define OSMESA_RGBA		GL_RGBA
#define OSMESA_BGRA		0x1
#define OSMESA_ARGB		0x2
#define OSMESA_RGB		GL_RGB
#define OSMESA_BGR		0x4
#define OSMESA_RGB_565		0x5


/*
 * OSMesaPixelStore() parameters:
 * New in version 2.0.
 */
#define OSMESA_ROW_LENGTH	0x10
#define OSMESA_Y_UP		0x11


/*
 * Accepted by OSMesaGetIntegerv:
 */
#define OSMESA_WIDTH		0x20
#define OSMESA_HEIGHT		0x21
#define OSMESA_FORMAT		0x22
#define OSMESA_TYPE		0x23
#define OSMESA_MAX_WIDTH	0x24  /* new in 4.0 */
#define OSMESA_MAX_HEIGHT	0x25  /* new in 4.0 */

/*
 * Accepted in OSMesaCreateContextAttrib's attribute list.
 */
#define OSMESA_DEPTH_BITS            0x30
#define OSMESA_STENCIL_BITS          0x31
#define OSMESA_ACCUM_BITS            0x32
#define OSMESA_PROFILE               0x33
#define OSMESA_CORE_PROFILE          0x34
#define OSMESA_COMPAT_PROFILE        0x35
#define OSMESA_CONTEXT_MAJOR_VERSION 0x36
#define OSMESA_CONTEXT_MINOR_VERSION 0x37


typedef struct osmesa_context *OSMesaContext;


/*
 * Create an Off-Screen Mesa rendering context.  The only attribute needed is
 * an RGBA vs Color-Index mode flag.
 *
 * Input:  format - one of OSMESA_COLOR_INDEX, OSMESA_RGBA, OSMESA_BGRA,
 *                  OSMESA_ARGB, OSMESA_RGB, or OSMESA_BGR.
 *         sharelist - specifies another OSMesaContext with which to share
 *                     display lists.  NULL indicates no sharing.
 * Return:  an OSMesaContext or 0 if error
 */
GLAPI OSMesaContext APIENTRY
OSMesaCreateContext( GLenum format, OSMesaContext sharelist );



/*
 * Create an Off-Screen Mesa rendering context and specify desired
 * size of depth buffer, stencil buffer and accumulation buffer.
 * If you specify zero for depthBits, stencilBits, accumBits you
 * can save some memory.
 *
 * New in Mesa 3.5
 */
GLAPI OSMesaContext APIENTRY
OSMesaCreateContextExt( GLenum format, GLint depthBits, GLint stencilBits,
                        GLint accumBits, OSMesaContext sharelist);


/*
 * Create an Off-Screen Mesa rendering context with attribute list.
 * The list is composed of (attribute, value) pairs and terminated with
 * attribute==0.  Supported Attributes:
 *
 * Attributes                    Values
 * --------------------------------------------------------------------------
 * OSMESA_FORMAT                 OSMESA_RGBA*, OSMESA_BGRA, OSMESA_ARGB, etc.
 * OSMESA_DEPTH_BITS             0*, 16, 24, 32
 * OSMESA_STENCIL_BITS           0*, 8
 * OSMESA_ACCUM_BITS             0*, 16
 * OSMESA_PROFILE                OSMESA_COMPAT_PROFILE*, OSMESA_CORE_PROFILE
 * OSMESA_CONTEXT_MAJOR_VERSION  1*, 2, 3
 * OSMESA_CONTEXT_MINOR_VERSION  0+
 *
 * Note: * = default value
 *
 * We return a context version >= what's specified by OSMESA_CONTEXT_MAJOR/
 * MINOR_VERSION for the given profile.  For example, if you request a GL 1.4
 * compat profile, you might get a GL 3.0 compat profile.
 * Otherwise, null is returned if the version/profile is not supported.
 *
 * New in Mesa 11.2
 */
GLAPI OSMesaContext APIENTRY
OSMesaCreateContextAttribs( const int *attribList, OSMesaContext sharelist );



/*
 * Destroy an Off-Screen Mesa rendering context.
 *
 * Input:  ctx - the context to destroy
 */
GLAPI void APIENTRY
OSMesaDestroyContext( OSMesaContext ctx );



/*
 * Bind an OSMesaContext to an image buffer.  The image buffer is just a
 * block of memory which the client provides.  Its size must be at least
 * as large as width*height*sizeof(type).  Its address should be a multiple
 * of 4 if using RGBA mode.
 *
 * Image data is stored in the order of glDrawPixels:  row-major order
 * with the lower-left image pixel stored in the first array position
 * (ie. bottom-to-top).
 *
 * Since the only type initially supported is GL_UNSIGNED_BYTE, if the
 * context is in RGBA mode, each pixel will be stored as a 4-byte RGBA
 * value.  If the context is in color indexed mode, each pixel will be
 * stored as a 1-byte value.
 *
 * If the context's viewport hasn't been initialized yet, it will now be
 * initialized to (0,0,width,height).
 *
 * Input:  ctx - the rendering context
 *         buffer - the image buffer memory
 *         type - data type for pixel components, only GL_UNSIGNED_BYTE
 *                supported now
 *         width, height - size of image buffer in pixels, at least 1
 * Return:  GL_TRUE if success, GL_FALSE if error because of invalid ctx,
 *          invalid buffer address, type!=GL_UNSIGNED_BYTE, width<1, height<1,
 *          width>internal limit or height>internal limit.
 */
GLAPI GLboolean APIENTRY
OSMesaMakeCurrent( OSMesaContext ctx, void *buffer, GLenum type,
                   GLsizei width, GLsizei height );




/*
 * Return the current Off-Screen Mesa rendering context handle.
 */
GLAPI OSMesaContext APIENTRY
OSMesaGetCurrentContext( void );



/*
 * Set pixel store/packing parameters for the current context.
 * This is similar to glPixelStore.
 * Input:  pname - OSMESA_ROW_LENGTH
 *                    specify actual pixels per row in image buffer
 *                    0 = same as image width (default)
 *                 OSMESA_Y_UP
 *                    zero = Y coordinates increase downward
 *                    non-zero = Y coordinates increase upward (default)
 *         value - the value for the parameter pname
 *
 * New in version 2.0.
 */
GLAPI void APIENTRY
OSMesaPixelStore( GLint pname, GLint value );


/*
 * osx-gpu: copy only these rectangles of the color buffer to the user's
 * buffer when the context is flushed, and leave the rest of the buffer
 * alone. Each is x, y, width, height in pixels, with y counted from the
 * first row of the user's buffer. A count of zero copies everything again
 * (the default).
 */
GLAPI void APIENTRY
OSMesaReadbackRects( OSMesaContext ctx, GLint count, const GLint *rects );


/*
 * osx-gpu: bind the context to a surface that already exists in the
 * device, named by `handle` for the driver's resource_from_handle, of the
 * given size in pixels, row length in bytes and byte offset. The context
 * draws on a surface of its own and, when it is flushed, the device copies
 * that to the named surface, top row first: all of it, or the rectangles
 * of OSMesaReadbackRects. There is no user buffer.
 */
GLAPI GLboolean APIENTRY
OSMesaMakeCurrentDirect( OSMesaContext ctx, GLuint handle, GLsizei width,
                         GLsizei height, GLsizei stride, GLuint offset );


/*
 * osx-gpu: the same, for a drawable of width x height pixels that is
 * shown with its top left corner at x, y of the named surface (a window on
 * the screen). OSMesaReadbackRects' rectangles are in the drawable's own
 * coordinates. Calling it again with another x, y moves the drawable
 * without losing its contents.
 */
/*
 * osx-gpu: memory of the device (named by `handle`, with this row length
 * in bytes and byte offset) into which every finished picture of the next
 * OSMesaMakeCurrentSurface's drawable is copied as well, top row first, so
 * that others can be told where to find it. Handle 0 (the default): none.
 */
GLAPI void APIENTRY
OSMesaSurfaceStorage( OSMesaContext ctx, GLuint handle, GLsizei stride,
                      GLuint offset );


/*
 * osx-gpu: copy rectangles of such a memory picture (width x height
 * pixels; the rectangles in its own coordinates) to the target surface of
 * the context's current drawable, the picture's top left corner at x, y
 * there. This is how one context shows what another one drew.
 */
/*
 * osx-gpu: copy a part (sx, sy, sw x sh) of such a memory picture into the
 * context's current drawable itself, its corner at dx, dy from the
 * drawable's top left, after everything drawn so far and before what is
 * drawn next.
 */
GLAPI void APIENTRY
OSMesaDrawStore( OSMesaContext ctx, GLuint handle, GLsizei stride,
                 GLuint offset, GLsizei width, GLsizei height, GLint sx,
                 GLint sy, GLsizei sw, GLsizei sh, GLint dx, GLint dy );


/*
 * osx-gpu: contexts and buffers made after this call are multisampled
 * with this many samples per pixel, as far as the device can (1: not).
 */
GLAPI void GLAPIENTRY
OSMesaSetSamples( GLint samples );

/*
 * osx-gpu: with glthread, the most bytes glBufferData or glBufferSubData
 * can be given in one call without the program's thread waiting for the
 * other one to finish everything before it. 0: the context has no
 * glthread, and no call waits.
 */
GLAPI GLint GLAPIENTRY
OSMesaAsyncDataLimit( OSMesaContext ctx );

/*
 * osx-gpu: make such a memory picture the image of the texture bound to
 * `target` in the context, without copying: the texture shows whatever is
 * in that memory when it is used. Opaque. The memory's first row is the
 * texture's row at t = 0, so a window's picture, which has its top row
 * first, is upside down in the texture. A context must be bound to a
 * drawable. OSMesaTexStoreImage is the same with more choices.
 */
GLAPI GLboolean APIENTRY
OSMesaTexStore( OSMesaContext ctx, GLenum target, GLuint handle,
                GLsizei stride, GLuint offset, GLsizei width, GLsizei height );


/*
 * osx-gpu: flags of OSMesaMakeCurrentStore and OSMesaTexStoreImage.
 *
 * OSMESA_STORE_BOTTOM_UP  the store's first row is the picture's bottom
 *                         row, as in a texture image, so that the store
 *                         can be a texture's image the right way up.
 *                         Without it the first row is the top row, as in
 *                         the stores of OSMesaSurfaceStorage and on the
 *                         screen.
 * OSMESA_STORE_COPY       the context draws on a surface of its own and
 *                         the device copies it to the store when the
 *                         context is flushed (what OSMesaSurfaceStorage
 *                         does), instead of drawing into the store.
 * OSMESA_STORE_ALPHA      the texture has the store's alpha channel.
 *                         Without it the texture is opaque (alpha 1).
 */
#define OSMESA_STORE_BOTTOM_UP	0x1
#define OSMESA_STORE_COPY	0x2
#define OSMESA_STORE_ALPHA	0x4
/*
 * The store's pixels are not 32 bits of 8 bits a channel but 64 (four
 * 16-bit unsigned normalized channels, OSMESA_STORE_RGBA16, or four half
 * floats, OSMESA_STORE_FLOAT16) or 128 (four floats, OSMESA_STORE_FLOAT32),
 * in the device's byte order, red first. The row length must be at least
 * width times that many bytes. OSMesaTexStoreImage takes the same flags
 * for the same memory. Float drawing is not clamped.
 */
#define OSMESA_STORE_RGBA16	0x8
#define OSMESA_STORE_FLOAT16	0x10
#define OSMESA_STORE_FLOAT32	0x20


/*
 * osx-gpu: bind the context to a drawable whose colour buffer is such a
 * memory picture (a store): memory of the device named by `handle`, with
 * this row length in bytes and byte offset, width x height pixels of 32
 * bits in the context's format. Nothing is shown on any screen and nothing
 * is read back; depth, stencil and accumulation buffers are the context's
 * own. This is a pbuffer.
 *
 * The context draws into the store itself: it holds what has been drawn as
 * soon as the commands have run, and is never half copied. With
 * OSMESA_STORE_COPY, or when the buffers are multisampled
 * (OSMesaSetSamples), the context draws elsewhere and the store gets every
 * finished picture, resolved, when the context is flushed. Either way the
 * store is complete for every command given to the device after the
 * context's glFlush has returned (also with glthread), by this context or
 * any other, and for the CPU after its glFinish; with the copy after a
 * second glFinish, because the copy follows what the first waits for.
 *
 * The row order is OSMESA_STORE_BOTTOM_UP's; `flags` takes that and
 * OSMESA_STORE_COPY. A store that was drawn in one order looks upside down
 * to a context bound to it in the other.
 *
 * The memory stays the caller's, and the device wants of it what it wants
 * of any linear render target: a row length that is a multiple of 64
 * pixels (256 bytes) and an offset that is a multiple of 256 bytes. The
 * context uses it until it is bound to another drawable or destroyed.
 * Binding it elsewhere gives the device all that was drawn into the store
 * and does not wait; a glFinish in the context after that does, and so
 * does destroying the context. Only then give the memory back, or the last
 * commands write to whoever has it next. Returns GL_FALSE if the device
 * does not take the memory as a surface.
 */
GLAPI GLboolean APIENTRY
OSMesaMakeCurrentStore( OSMesaContext ctx, GLuint handle, GLsizei stride,
                        GLuint offset, GLsizei width, GLsizei height,
                        GLuint flags );


/*
 * osx-gpu: OSMesaTexStore for a store that is not a window's. `target` is
 * GL_TEXTURE_2D or GL_TEXTURE_RECTANGLE; the image is level 0 of the
 * texture bound to it in `ctx`, which must be the calling thread's current
 * context. With OSMESA_STORE_ALPHA in `flags` the texture has the store's
 * alpha channel, without it the texture is opaque. The pixels are in the
 * context's format, as OSMesaMakeCurrentStore draws them (any context of
 * the same format, sharing or not).
 *
 * The store's first row is the texture's row at t = 0. A store drawn with
 * OSMESA_STORE_BOTTOM_UP is therefore the picture as OpenGL has a texture
 * image; one with the top row first (a window's) is upside down, and is to
 * be drawn with t reversed.
 *
 * Nothing is copied, then or later: the texture shows what is in the
 * memory when it is used, so a context that draws into the store must have
 * been flushed before. There is one level; a GL_TEXTURE_2D needs a
 * minifying filter without mipmaps to be complete. The texture holds no
 * claim on the memory. Before the memory is given back, take the image
 * away with handle 0 (the other arguments are ignored), delete the texture
 * or give it another image; a texture left with the store samples whatever
 * the memory holds next, and writes nothing. Textures are shared, so one
 * context of a share list does this for all.
 */
GLAPI GLboolean APIENTRY
OSMesaTexStoreImage( OSMesaContext ctx, GLenum target, GLuint handle,
                     GLsizei stride, GLuint offset, GLsizei width,
                     GLsizei height, GLuint flags );


/*
 * osx-gpu: make the part x, y, width x height (in pixels, from the top left
 * of the context's current drawable, the way the picture is kept in
 * memory) the image of the texture bound to `target` in the context, as a
 * copy of what has been drawn so far: the texture's row 0 is the part's top
 * row. Returns GL_FALSE if there is no drawable or no memory.
 */
GLAPI GLboolean APIENTRY
OSMesaTexCopyDrawable( OSMesaContext ctx, GLenum target, GLint x, GLint y,
                       GLsizei width, GLsizei height );


GLAPI void APIENTRY
OSMesaShowStore( OSMesaContext ctx, GLuint handle, GLsizei stride,
                 GLuint offset, GLsizei width, GLsizei height, GLint x,
                 GLint y, GLint count, const GLint *rects );


GLAPI GLboolean APIENTRY
OSMesaMakeCurrentSurface( OSMesaContext ctx, GLuint handle,
                          GLsizei target_width, GLsizei target_height,
                          GLsizei stride, GLuint offset, GLint x, GLint y,
                          GLsizei width, GLsizei height );



/*
 * Return an integer value like glGetIntegerv.
 * Input:  pname -
 *                 OSMESA_WIDTH  return current image width
 *                 OSMESA_HEIGHT  return current image height
 *                 OSMESA_FORMAT  return image format
 *                 OSMESA_TYPE  return color component data type
 *                 OSMESA_ROW_LENGTH return row length in pixels
 *                 OSMESA_Y_UP returns 1 or 0 to indicate Y axis direction
 *         value - pointer to integer in which to return result.
 */
GLAPI void APIENTRY
OSMesaGetIntegerv( GLint pname, GLint *value );



/*
 * Return the depth buffer associated with an OSMesa context.
 * Input:  c - the OSMesa context
 * Output:  width, height - size of buffer in pixels
 *          bytesPerValue - bytes per depth value (2 or 4)
 *          buffer - pointer to depth buffer values
 * Return:  GL_TRUE or GL_FALSE to indicate success or failure.
 *
 * New in Mesa 2.4.
 */
GLAPI GLboolean APIENTRY
OSMesaGetDepthBuffer( OSMesaContext c, GLint *width, GLint *height,
                      GLint *bytesPerValue, void **buffer );



/*
 * Return the color buffer associated with an OSMesa context.
 * Input:  c - the OSMesa context
 * Output:  width, height - size of buffer in pixels
 *          format - buffer format (OSMESA_FORMAT)
 *          buffer - pointer to depth buffer values
 * Return:  GL_TRUE or GL_FALSE to indicate success or failure.
 *
 * New in Mesa 3.3.
 */
GLAPI GLboolean APIENTRY
OSMesaGetColorBuffer( OSMesaContext c, GLint *width, GLint *height,
                      GLint *format, void **buffer );



/**
 * This typedef is new in Mesa 6.3.
 */
typedef void (*OSMESAproc)();


/*
 * Return pointer to the named function.
 * New in Mesa 4.1
 * Return OSMESAproc in 6.3.
 */
GLAPI OSMESAproc APIENTRY
OSMesaGetProcAddress( const char *funcName );



/**
 * Enable/disable color clamping, off by default.
 * New in Mesa 6.4.2
 */
GLAPI void APIENTRY
OSMesaColorClamp(GLboolean enable);


/**
 * Enable/disable Gallium post-process filters.
 * This should be called after a context is created, but before it is
 * made current for the first time.  After a context has been made
 * current, this function has no effect.
 * If the enable_value param is zero, the filter is disabled.  Otherwise
 * the filter is enabled, and the value may control the filter's quality.
 * New in Mesa 10.0
 */
GLAPI void APIENTRY
OSMesaPostprocess(OSMesaContext osmesa, const char *filter,
                  unsigned enable_value);


#ifdef __cplusplus
}
#endif


#endif
