#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <string.h>
#include "glHooks.hpp"
#include "../graphics/shaderPatches.h"
#include "../patching/patchResolution.h"
#include "../graphics/crossHair.h"


extern "C" void __attribute__((cdecl)) wrap_glBegin(GLenum mode)
{
    if (glad_glBegin)
        glad_glBegin(mode);
}

extern "C" void __attribute__((cdecl)) wrap_glEnd()
{
    if (glad_glEnd)
        glad_glEnd();
}

extern "C" void __attribute__((cdecl)) wrap_glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    if (glad_glVertex3f)
        glad_glVertex3f(x, y, z);
}

extern "C" void __attribute__((cdecl)) wrap_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    if (glad_glColor4f)
        glad_glColor4f(r, g, b, a);
}

extern "C" void __attribute__((cdecl)) wrap_glDisable(GLenum cap)
{
    if (glad_glDisable)
        glad_glDisable(cap);
}

extern "C" void __attribute__((cdecl)) wrap_glMatrixMode(GLenum mode)
{
    if (glad_glMatrixMode)
        glad_glMatrixMode(mode);
}

extern "C" void __attribute__((cdecl)) wrap_glLoadIdentity()
{
    if (glad_glLoadIdentity)
        glad_glLoadIdentity();
}

extern "C" void __attribute__((cdecl)) wrap_glPushMatrix()
{
    if (glad_glPushMatrix)
        glad_glPushMatrix();
}

extern "C" void __attribute__((cdecl)) wrap_glPopMatrix()
{
    if (glad_glPopMatrix)
        glad_glPopMatrix();
}

extern "C" void __attribute__((cdecl)) wrap_glTranslated(GLdouble x, GLdouble y, GLdouble z)
{
    if (glad_glTranslated)
        glad_glTranslated(x, y, z);
}

extern "C" void __attribute__((cdecl)) wrap_glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    if (glad_glTranslatef)
        glad_glTranslatef(x, y, z);
}

extern "C" void __attribute__((cdecl)) wrap_glRotatef(GLfloat a, GLfloat x, GLfloat y, GLfloat z)
{
    if (glad_glRotatef)
        glad_glRotatef(a, x, y, z);
}

extern "C" void __attribute__((cdecl)) wrap_glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    if (glad_glScalef)
        glad_glScalef(x, y, z);
}

extern "C" void __attribute__((cdecl)) wrap_glDepthRange(GLclampd n, GLclampd f)
{
    if (glad_glDepthRange)
        glad_glDepthRange(n, f);
}

extern "C" void __attribute__((cdecl)) wrap_glPolygonOffset(GLfloat factor, GLfloat units)
{
    if (glad_glPolygonOffset)
        glad_glPolygonOffset(factor, units);
}

extern "C" void __attribute__((cdecl)) wrap_glFlush()
{
    if (glad_glFlush)
        glad_glFlush();
}

extern "C" void __attribute__((cdecl)) wrap_glFinish()
{
    if (glad_glFinish)
        glad_glFinish();
}

extern "C" void __attribute__((cdecl)) wrap_glClear(GLbitfield mask)
{
    if (glad_glClear)
        glad_glClear(mask);
}

extern "C" void __attribute__((cdecl)) wrap_glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a)
{
    if (glad_glClearColor)
        glad_glClearColor(r, g, b, a);
}

extern "C" void __attribute__((cdecl)) wrap_glDeleteTextures(GLsizei n, const GLuint *textures)
{
    if (glad_glDeleteTextures)
        glad_glDeleteTextures(n, textures);
}

extern "C" void __attribute__((cdecl)) wrap_glGenTextures(GLsizei n, GLuint *textures)
{
    if (glad_glGenTextures)
        glad_glGenTextures(n, textures);
}

extern "C" void __attribute__((cdecl)) wrap_glGetLightfv(GLenum light, GLenum pname, GLfloat *params)
{
    if (glad_glGetLightfv)
        glad_glGetLightfv(light, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glCullFace(GLenum mode)
{
    if (glad_glCullFace)
        glad_glCullFace(mode);
}

extern "C" void __attribute__((cdecl)) wrap_glFrontFace(GLenum mode)
{
    if (glad_glFrontFace)
        glad_glFrontFace(mode);
}

extern "C" void __attribute__((cdecl)) wrap_glHint(GLenum target, GLenum mode)
{
    if (glad_glHint)
        glad_glHint(target, mode);
}

extern "C" void __attribute__((cdecl)) wrap_glLineWidth(GLfloat width)
{
    if (glad_glLineWidth)
        glad_glLineWidth(width);
}

extern "C" void __attribute__((cdecl)) wrap_glPointSize(GLfloat size)
{
    if (glad_glPointSize)
        glad_glPointSize(size);
}

extern "C" void __attribute__((cdecl)) wrap_glPolygonMode(GLenum face, GLenum mode)
{
    if (glad_glPolygonMode)
        glad_glPolygonMode(face, mode);
}

extern "C" void __attribute__((cdecl)) wrap_glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    if (glad_glScissor)
        glad_glScissor(x, y, width, height);
}

extern "C" void __attribute__((cdecl)) wrap_glShadeModel(GLenum mode)
{
    if (glad_glShadeModel)
        glad_glShadeModel(mode);
}

extern "C" void __attribute__((cdecl)) wrap_glPixelStorei(GLenum pname, GLint param)
{
    if (glad_glPixelStorei)
        glad_glPixelStorei(pname, param);
}

extern "C" void __attribute__((cdecl)) wrap_glPixelStoref(GLenum pname, GLfloat param)
{
    if (glad_glPixelStoref)
        glad_glPixelStoref(pname, param);
}

extern "C" void __attribute__((cdecl)) wrap_glGetBooleanv(GLenum pname, GLboolean *params)
{
    if (glad_glGetBooleanv)
        glad_glGetBooleanv(pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glGetDoublev(GLenum pname, GLdouble *params)
{
    if (glad_glGetDoublev)
        glad_glGetDoublev(pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glGetFloatv(GLenum pname, GLfloat *params)
{
    if (glad_glGetFloatv)
        glad_glGetFloatv(pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glGetIntegerv(GLenum pname, GLint *params)
{
    if (glad_glGetIntegerv)
        glad_glGetIntegerv(pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glPushAttrib(GLbitfield mask)
{
    if (glad_glPushAttrib)
        glad_glPushAttrib(mask);
}

extern "C" void __attribute__((cdecl)) wrap_glPopAttrib()
{
    if (glad_glPopAttrib)
        glad_glPopAttrib();
}

extern "C" void __attribute__((cdecl)) wrap_glPushClientAttrib(GLbitfield mask)
{
    if (glad_glPushClientAttrib)
        glad_glPushClientAttrib(mask);
}

extern "C" void __attribute__((cdecl)) wrap_glPopClientAttrib()
{
    if (glad_glPopClientAttrib)
        glad_glPopClientAttrib();
}

extern "C" void __attribute__((cdecl)) wrap_glAlphaFunc(GLenum func, GLclampf ref)
{
    if (glad_glAlphaFunc)
        glad_glAlphaFunc(func, ref);
}

extern "C" void __attribute__((cdecl)) wrap_glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    if (glad_glBlendFunc)
        glad_glBlendFunc(sfactor, dfactor);
}

extern "C" void __attribute__((cdecl)) wrap_glLogicOp(GLenum opcode)
{
    if (glad_glLogicOp)
        glad_glLogicOp(opcode);
}

extern "C" void __attribute__((cdecl)) wrap_glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
    if (glad_glStencilFunc)
        glad_glStencilFunc(func, ref, mask);
}

extern "C" void __attribute__((cdecl)) wrap_glStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
    if (glad_glStencilOp)
        glad_glStencilOp(fail, zfail, zpass);
}

extern "C" void __attribute__((cdecl)) wrap_glDepthFunc(GLenum func)
{
    if (glad_glDepthFunc)
        glad_glDepthFunc(func);
}

extern "C" void __attribute__((cdecl)) wrap_glDepthMask(GLboolean flag)
{
    if (glad_glDepthMask)
        glad_glDepthMask(flag);
}

extern "C" void __attribute__((cdecl)) wrap_glLightfv(GLenum light, GLenum pname, const GLfloat *params)
{
    if (glad_glLightfv)
        glad_glLightfv(light, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glLightf(GLenum light, GLenum pname, GLfloat param)
{
    if (glad_glLightf)
        glad_glLightf(light, pname, param);
}

extern "C" void __attribute__((cdecl)) wrap_glLightModelfv(GLenum pname, const GLfloat *params)
{
    if (glad_glLightModelfv)
        glad_glLightModelfv(pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glMaterialfv(GLenum face, GLenum pname, const GLfloat *params)
{
    if (glad_glMaterialfv)
        glad_glMaterialfv(face, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz)
{
    if (glad_glNormal3f)
        glad_glNormal3f(nx, ny, nz);
}

extern "C" void __attribute__((cdecl)) wrap_glTexCoord2f(GLfloat s, GLfloat t)
{
    if (glad_glTexCoord2f)
        glad_glTexCoord2f(s, t);
}

extern "C" void __attribute__((cdecl)) wrap_glTexCoord2fv(const GLfloat *v)
{
    if (glad_glTexCoord2fv)
        glad_glTexCoord2fv(v);
}

extern "C" void __attribute__((cdecl)) wrap_glActiveTexture(GLenum texture)
{
    if (glad_glActiveTexture)
        glad_glActiveTexture(texture);
}

extern "C" void __attribute__((cdecl)) wrap_glMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t)
{
    if (glad_glMultiTexCoord2f)
        glad_glMultiTexCoord2f(target, s, t);
}

extern "C" void __attribute__((cdecl)) wrap_glMultiTexCoord2fv(GLenum target, const GLfloat *v)
{
    if (glad_glMultiTexCoord2fv)
        glad_glMultiTexCoord2fv(target, v);
}

extern "C" void __attribute__((cdecl)) wrap_glActiveTextureARB(GLenum texture)
{
    if (glad_glActiveTextureARB)
        glad_glActiveTextureARB(texture);
}

extern "C" void __attribute__((cdecl)) wrap_glMultiTexCoord2fARB(GLenum target, GLfloat s, GLfloat t)
{
    if (glad_glMultiTexCoord2fARB)
        glad_glMultiTexCoord2fARB(target, s, t);
}

extern "C" void __attribute__((cdecl)) wrap_glMultiTexCoord2fvARB(GLenum target, const GLfloat *v)
{
    if (glad_glMultiTexCoord2fvARB)
        glad_glMultiTexCoord2fvARB(target, v);
}

extern "C" void __attribute__((cdecl)) wrap_glVertex2d(GLdouble x, GLdouble y)
{
    if (glad_glVertex2d)
        glad_glVertex2d(x, y);
}

extern "C" void __attribute__((cdecl)) wrap_glVertex2f(GLfloat x, GLfloat y)
{
    if (glad_glVertex2f)
        glad_glVertex2f(x, y);
}

extern "C" void __attribute__((cdecl)) wrap_glVertex2i(GLint x, GLint y)
{
    if (glad_glVertex2i)
        glad_glVertex2i(x, y);
}

extern "C" void __attribute__((cdecl)) wrap_glVertex3fv(const GLfloat *v)
{
    if (glad_glVertex3fv)
        glad_glVertex3fv(v);
}

extern "C" void __attribute__((cdecl)) wrap_glRasterPos2i(GLint x, GLint y)
{
    if (glad_glRasterPos2i)
        glad_glRasterPos2i(x, y);
}

extern "C" void __attribute__((cdecl)) wrap_glRasterPos3f(GLfloat x, GLfloat y, GLfloat z)
{
    if (glad_glRasterPos3f)
        glad_glRasterPos3f(x, y, z);
}

extern "C" void __attribute__((cdecl)) wrap_glColor3f(GLfloat r, GLfloat g, GLfloat b)
{
    if (glad_glColor3f)
        glad_glColor3f(r, g, b);
}

extern "C" void __attribute__((cdecl)) wrap_glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{
    if (glad_glColor4ub)
        glad_glColor4ub(r, g, b, a);
}

extern "C" void __attribute__((cdecl)) wrap_glColor4fv(const GLfloat *v)
{
    if (glad_glColor4fv)
        glad_glColor4fv(v);
}

extern "C" void __attribute__((cdecl)) wrap_glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
    if (glad_glRectf)
        glad_glRectf(x1, y1, x2, y2);
}

extern "C" void __attribute__((cdecl)) wrap_glRecti(GLint x1, GLint y1, GLint x2, GLint y2)
{
    if (glad_glRecti)
        glad_glRecti(x1, y1, x2, y2);
}

extern "C" void __attribute__((cdecl)) wrap_glNewList(GLuint list, GLenum mode)
{
    if (glad_glNewList)
        glad_glNewList(list, mode);
}

extern "C" void __attribute__((cdecl)) wrap_glEndList()
{
    if (glad_glEndList)
        glad_glEndList();
}

extern "C" void __attribute__((cdecl)) wrap_glCallList(GLuint list)
{
    if (glad_glCallList)
        glad_glCallList(list);
}

extern "C" void __attribute__((cdecl)) wrap_glDeleteLists(GLuint list, GLsizei range)
{
    if (glad_glDeleteLists)
        glad_glDeleteLists(list, range);
}

extern "C" void __attribute__((cdecl)) wrap_glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear,
                                                      GLdouble zFar)
{
    if (glad_glFrustum)
        glad_glFrustum(left, right, bottom, top, zNear, zFar);
}

extern "C" void __attribute__((cdecl)) wrap_glEnableClientState(GLenum array)
{
    if (glad_glEnableClientState)
        glad_glEnableClientState(array);
}

extern "C" void __attribute__((cdecl)) wrap_glDisableClientState(GLenum array)
{
    if (glad_glDisableClientState)
        glad_glDisableClientState(array);
}

extern "C" void __attribute__((cdecl)) wrap_glClientActiveTexture(GLenum texture)
{
    if (glad_glClientActiveTexture)
        glad_glClientActiveTexture(texture);
}


extern "C" void __attribute__((cdecl)) wrap_glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    if (glad_glNormalPointer)
        glad_glNormalPointer(type, stride, pointer);
}

extern "C" void __attribute__((cdecl)) wrap_glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    if (glad_glColorPointer)
        glad_glColorPointer(size, type, stride, pointer);
}

extern "C" void __attribute__((cdecl)) wrap_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if (glad_glDrawArrays)
        glad_glDrawArrays(mode, first, count);
}

extern "C" void __attribute__((cdecl)) wrap_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    if (glad_glDrawElements)
        glad_glDrawElements(mode, count, type, indices);
}

extern "C" void __attribute__((cdecl)) wrap_glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    if (glad_glTexEnvf)
        glad_glTexEnvf(target, pname, param);
}

extern "C" void __attribute__((cdecl)) wrap_glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    if (glad_glTexEnvi)
        glad_glTexEnvi(target, pname, param);
}

extern "C" void __attribute__((cdecl)) wrap_glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    if (glad_glTexEnvfv)
        glad_glTexEnvfv(target, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glTexEnviv(GLenum target, GLenum pname, const GLint *params)
{
    if (glad_glTexEnviv)
        glad_glTexEnviv(target, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glColor3ub(GLubyte red, GLubyte green, GLubyte blue)
{
    if (glad_glColor3ub)
        glad_glColor3ub(red, green, blue);
}

extern "C" void __attribute__((cdecl)) wrap_glColorMaterial(GLenum face, GLenum mode)
{
    if (glad_glColorMaterial)
        glad_glColorMaterial(face, mode);
}

extern "C" void __attribute__((cdecl)) wrap_glFogf(GLenum pname, GLfloat param)
{
    if (glad_glFogf)
        glad_glFogf(pname, param);
}

extern "C" void __attribute__((cdecl)) wrap_glFogfv(GLenum pname, const GLfloat *params)
{
    if (glad_glFogfv)
        glad_glFogfv(pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glFogi(GLenum pname, GLint param)
{
    if (glad_glFogi)
        glad_glFogi(pname, param);
}

extern "C" void __attribute__((cdecl)) wrap_glMaterialf(GLenum face, GLenum pname, GLfloat param)
{
    if (glad_glMaterialf)
        glad_glMaterialf(face, pname, param);
}

extern "C" void __attribute__((cdecl)) wrap_glLightModeli(GLenum pname, GLint param)
{
    if (glad_glLightModeli)
        glad_glLightModeli(pname, param);
}

extern "C" void __attribute__((cdecl)) wrap_glSecondaryColor3ub(GLubyte red, GLubyte green, GLubyte blue)
{
    if (glad_glSecondaryColor3ub)
        glad_glSecondaryColor3ub(red, green, blue);
}

extern "C" void __attribute__((cdecl)) wrap_glBlendColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    if (glad_glBlendColor)
        glad_glBlendColor(red, green, blue, alpha);
}

extern "C" void __attribute__((cdecl)) wrap_glBlendEquation(GLenum mode)
{
    if (glad_glBlendEquation)
        glad_glBlendEquation(mode);
}

extern "C" void __attribute__((cdecl)) wrap_glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    if (glad_glTexCoord4f)
        glad_glTexCoord4f(s, t, r, q);
}

extern "C" void __attribute__((cdecl)) wrap_glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
    if (glad_glTexParameterfv)
        glad_glTexParameterfv(target, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glPointParameterf(GLenum pname, GLfloat param)
{
    if (glad_glPointParameterf)
        glad_glPointParameterf(pname, param);
}

extern "C" void __attribute__((cdecl)) wrap_glPointParameterfv(GLenum pname, const GLfloat *params)
{
    if (glad_glPointParameterfv)
        glad_glPointParameterfv(pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    if (glad_glVertex4f)
        glad_glVertex4f(x, y, z, w);
}

extern "C" void __attribute__((cdecl)) wrap_glMultMatrixf(const GLfloat *m)
{
    if (glad_glMultMatrixf)
        glad_glMultMatrixf(m);
}

extern "C" void __attribute__((cdecl)) wrap_glGenFramebuffersEXT(GLsizei n, GLuint *framebuffers)
{
    if (glad_glGenFramebuffersEXT)
        glad_glGenFramebuffersEXT(n, framebuffers);
}

extern "C" void __attribute__((cdecl)) wrap_glBindFramebufferEXT(GLenum target, GLuint framebuffer)
{
    if (glad_glBindFramebufferEXT)
        glad_glBindFramebufferEXT(target, framebuffer);
}

extern "C" void __attribute__((cdecl)) wrap_glFramebufferTexture2DEXT(GLenum target, GLenum attachment, GLenum textarget, GLuint texture,
                                                                      GLint level)
{
    if (glad_glFramebufferTexture2DEXT)
        glad_glFramebufferTexture2DEXT(target, attachment, textarget, texture, level);
}

extern "C" void __attribute__((cdecl)) wrap_glDeleteFramebuffersEXT(GLsizei n, const GLuint *framebuffers)
{
    if (glad_glDeleteFramebuffersEXT)
        glad_glDeleteFramebuffersEXT(n, framebuffers);
}

extern "C" void __attribute__((cdecl)) wrap_glGenProgramsARB(GLsizei n, GLuint *programs)
{
    if (glad_glGenProgramsARB)
        glad_glGenProgramsARB(n, programs);
}

extern "C" void __attribute__((cdecl)) wrap_glBindProgramARB(GLenum target, GLuint program)
{
    if (glad_glBindProgramARB)
        glad_glBindProgramARB(target, program);
}

extern "C" void __attribute__((cdecl)) wrap_glProgramStringARB(GLenum target, GLenum format, GLsizei len, const GLvoid *string)
{
    if (glad_glProgramStringARB)
        glad_glProgramStringARB(target, format, len, string);
}

extern "C" void __attribute__((cdecl)) wrap_glProgramLocalParameter4fARB(GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z,
                                                                         GLfloat w)
{
    if (glad_glProgramLocalParameter4fARB)
        glad_glProgramLocalParameter4fARB(target, index, x, y, z, w);
}

extern "C" void __attribute__((cdecl)) wrap_glProgramLocalParameter4fvARB(GLenum target, GLuint index, const GLfloat *params)
{
    if (glad_glProgramLocalParameter4fvARB)
        glad_glProgramLocalParameter4fvARB(target, index, params);
}

extern "C" void __attribute__((cdecl)) wrap_glGetProgramivARB(GLenum target, GLenum pname, GLint *params)
{
    if (glad_glGetProgramivARB)
        glad_glGetProgramivARB(target, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glClientActiveTextureARB(GLenum texture)
{
    if (glad_glClientActiveTextureARB)
        glad_glClientActiveTextureARB(texture);
}

extern "C" void __attribute__((cdecl)) wrap_glEnableVertexAttribArrayARB(GLuint index)
{
    if (glad_glEnableVertexAttribArrayARB)
        glad_glEnableVertexAttribArrayARB(index);
}

extern "C" void __attribute__((cdecl)) wrap_glDisableVertexAttribArrayARB(GLuint index)
{
    if (glad_glDisableVertexAttribArrayARB)
        glad_glDisableVertexAttribArrayARB(index);
}

extern "C" void __attribute__((cdecl)) wrap_glVertexAttribPointerARB(GLuint index, GLint size, GLenum type, GLboolean normalized,
                                                                     GLsizei stride, const GLvoid *pointer)
{
    if (glad_glVertexAttribPointerARB)
        glad_glVertexAttribPointerARB(index, size, type, normalized, stride, pointer);
}

extern "C" void __attribute__((cdecl)) wrap_glBindBuffer(GLenum target, GLuint buffer)
{
    if (glad_glBindBuffer)
        glad_glBindBuffer(target, buffer);
}

extern "C" void __attribute__((cdecl)) wrap_glDeleteBuffers(GLsizei n, const GLuint *buffers)
{
    if (glad_glDeleteBuffers)
        glad_glDeleteBuffers(n, buffers);
}

extern "C" void __attribute__((cdecl)) wrap_glGenBuffers(GLsizei n, GLuint *buffers)
{
    if (glad_glGenBuffers)
        glad_glGenBuffers(n, buffers);
}

extern "C" void __attribute__((cdecl)) wrap_glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage)
{
    if (glad_glBufferData)
        glad_glBufferData(target, size, data, usage);
}

extern "C" void __attribute__((cdecl)) wrap_glGenFramebuffers(GLsizei n, GLuint *framebuffers)
{
    if (glad_glGenFramebuffers)
        glad_glGenFramebuffers(n, framebuffers);
}

extern "C" void __attribute__((cdecl)) wrap_glBindFramebuffer(GLenum target, GLuint framebuffer)
{
    if (glad_glBindFramebuffer)
        glad_glBindFramebuffer(target, framebuffer);
}

extern "C" void __attribute__((cdecl)) wrap_glFramebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget, GLuint texture,
                                                                   GLint level)
{
    if (glad_glFramebufferTexture2D)
        glad_glFramebufferTexture2D(target, attachment, textarget, texture, level);
}

extern "C" void __attribute__((cdecl)) wrap_glDeleteFramebuffers(GLsizei n, const GLuint *framebuffers)
{
    if (glad_glDeleteFramebuffers)
        glad_glDeleteFramebuffers(n, framebuffers);
}

extern "C" void __attribute__((cdecl)) wrap_glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum renderbuffertarget,
                                                                      GLuint renderbuffer)
{
    if (glad_glFramebufferRenderbuffer)
        glad_glFramebufferRenderbuffer(target, attachment, renderbuffertarget, renderbuffer);
}

extern "C" void __attribute__((cdecl)) wrap_glGenRenderbuffers(GLsizei n, GLuint *renderbuffers)
{
    if (glad_glGenRenderbuffers)
        glad_glGenRenderbuffers(n, renderbuffers);
}

extern "C" void __attribute__((cdecl)) wrap_glBindRenderbuffer(GLenum target, GLuint renderbuffer)
{
    if (glad_glBindRenderbuffer)
        glad_glBindRenderbuffer(target, renderbuffer);
}

extern "C" void __attribute__((cdecl)) wrap_glRenderbufferStorage(GLenum target, GLenum internalformat, GLsizei width, GLsizei height)
{
    if (glad_glRenderbufferStorage)
        glad_glRenderbufferStorage(target, internalformat, width, height);
}

extern "C" void __attribute__((cdecl)) wrap_glDeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers)
{
    if (glad_glDeleteRenderbuffers)
        glad_glDeleteRenderbuffers(n, renderbuffers);
}

extern "C" void __attribute__((cdecl)) wrap_glDrawBuffers(GLsizei n, const GLenum *bufs)
{
    if (glad_glDrawBuffers)
        glad_glDrawBuffers(n, bufs);
}

extern "C" void __attribute__((cdecl)) wrap_glPixelMapusv(GLenum map, GLsizei size, const GLushort *values)
{
    if (glad_glPixelMapusv)
        glad_glPixelMapusv(map, size, values);
}

extern "C" void __attribute__((cdecl)) wrap_glPixelTransferf(GLenum pname, GLfloat param)
{
    if (glad_glPixelTransferf)
        glad_glPixelTransferf(pname, param);
}

extern "C" void __attribute__((cdecl)) wrap_glPixelTransferi(GLenum pname, GLint param)
{
    if (glad_glPixelTransferi)
        glad_glPixelTransferi(pname, param);
}

extern "C" void __attribute__((cdecl)) wrap_glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                                                         GLvoid *pixels)
{
    if (glad_glReadPixels)
        glad_glReadPixels(x, y, width, height, format, type, pixels);
}

extern "C" void __attribute__((cdecl)) wrap_glShaderSource(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)
{
    if (glad_glShaderSource)
        glad_glShaderSource(shader, count, string, length);
}

extern "C" void __attribute__((cdecl)) wrap_glCompileShader(GLuint shader)
{
    if (glad_glCompileShader)
        glad_glCompileShader(shader);
}

extern "C" void __attribute__((cdecl)) wrap_glGetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
    if (glad_glGetShaderiv)
        glad_glGetShaderiv(shader, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
    if (glad_glGetShaderInfoLog)
        glad_glGetShaderInfoLog(shader, bufSize, length, infoLog);
}

extern "C" void __attribute__((cdecl)) wrap_glDeleteShader(GLuint shader)
{
    if (glad_glDeleteShader)
        glad_glDeleteShader(shader);
}

extern "C" void __attribute__((cdecl)) wrap_glAttachShader(GLuint program, GLuint shader)
{
    if (glad_glAttachShader)
        glad_glAttachShader(program, shader);
}

extern "C" void __attribute__((cdecl)) wrap_glLinkProgram(GLuint program)
{
    if (glad_glLinkProgram)
        glad_glLinkProgram(program);
}

extern "C" void __attribute__((cdecl)) wrap_glGetProgramiv(GLuint program, GLenum pname, GLint *params)
{
    if (glad_glGetProgramiv)
        glad_glGetProgramiv(program, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog)
{
    if (glad_glGetProgramInfoLog)
        glad_glGetProgramInfoLog(program, bufSize, length, infoLog);
}

extern "C" void __attribute__((cdecl)) wrap_glUseProgram(GLuint program)
{
    if (glad_glUseProgram)
        glad_glUseProgram(program);
}

extern "C" void __attribute__((cdecl)) wrap_glDeleteProgram(GLuint program)
{
    if (glad_glDeleteProgram)
        glad_glDeleteProgram(program);
}

extern "C" void __attribute__((cdecl)) wrap_glUniform1f(GLint location, GLfloat v0)
{
    if (glad_glUniform1f)
        glad_glUniform1f(location, v0);
}

extern "C" void __attribute__((cdecl)) wrap_glUniform1fv(GLint location, GLsizei count, const GLfloat *value)
{
    if (glad_glUniform1fv)
        glad_glUniform1fv(location, count, value);
}

extern "C" void __attribute__((cdecl)) wrap_glUniform1i(GLint location, GLint v0)
{
    if (glad_glUniform1i)
        glad_glUniform1i(location, v0);
}

extern "C" void __attribute__((cdecl)) wrap_glUniform1iv(GLint location, GLsizei count, const GLint *value)
{
    if (glad_glUniform1iv)
        glad_glUniform1iv(location, count, value);
}

extern "C" void __attribute__((cdecl)) wrap_glUniform2fv(GLint location, GLsizei count, const GLfloat *value)
{
    if (glad_glUniform2fv)
        glad_glUniform2fv(location, count, value);
}

extern "C" void __attribute__((cdecl)) wrap_glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
{
    if (glad_glUniform3f)
        glad_glUniform3f(location, v0, v1, v2);
}

extern "C" void __attribute__((cdecl)) wrap_glUniform2iv(GLint location, GLsizei count, const GLint *value)
{
    if (glad_glUniform2iv)
        glad_glUniform2iv(location, count, value);
}

extern "C" void __attribute__((cdecl)) wrap_glUniform3fv(GLint location, GLsizei count, const GLfloat *value)
{
    if (glad_glUniform3fv)
        glad_glUniform3fv(location, count, value);
}

extern "C" void __attribute__((cdecl)) wrap_glUniform3iv(GLint location, GLsizei count, const GLint *value)
{
    if (glad_glUniform3iv)
        glad_glUniform3iv(location, count, value);
}

extern "C" void __attribute__((cdecl)) wrap_glUniform4fv(GLint location, GLsizei count, const GLfloat *value)
{
    if (glad_glUniform4fv)
        glad_glUniform4fv(location, count, value);
}

extern "C" void __attribute__((cdecl)) wrap_glUniform4iv(GLint location, GLsizei count, const GLint *value)
{
    if (glad_glUniform4iv)
        glad_glUniform4iv(location, count, value);
}

extern "C" void __attribute__((cdecl)) wrap_glUniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    if (glad_glUniformMatrix2fv)
        glad_glUniformMatrix2fv(location, count, transpose, value);
}

extern "C" void __attribute__((cdecl)) wrap_glUniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    if (glad_glUniformMatrix3fv)
        glad_glUniformMatrix3fv(location, count, transpose, value);
}

extern "C" void __attribute__((cdecl)) wrap_glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    if (glad_glUniformMatrix4fv)
        glad_glUniformMatrix4fv(location, count, transpose, value);
}

extern "C" void __attribute__((cdecl)) wrap_glGetActiveUniform(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size,
                                                               GLenum *type, GLchar *name)
{
    if (glad_glGetActiveUniform)
        glad_glGetActiveUniform(program, index, bufSize, length, size, type, name);
}

extern "C" void __attribute__((cdecl)) wrap_glEnableVertexAttribArray(GLuint index)
{
    if (glad_glEnableVertexAttribArray)
        glad_glEnableVertexAttribArray(index);
}

extern "C" void __attribute__((cdecl)) wrap_glDisableVertexAttribArray(GLuint index)
{
    if (glad_glDisableVertexAttribArray)
        glad_glDisableVertexAttribArray(index);
}

extern "C" void __attribute__((cdecl)) wrap_glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                                                                  GLsizei stride, const GLvoid *pointer)
{
    if (glad_glVertexAttribPointer)
        glad_glVertexAttribPointer(index, size, type, normalized, stride, pointer);
}

extern "C" void __attribute__((cdecl)) wrap_glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    if (glad_glTexParameterf)
        glad_glTexParameterf(target, pname, param);
}

extern "C" void __attribute__((cdecl)) wrap_glInterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer)
{
    if (glad_glInterleavedArrays)
        glad_glInterleavedArrays(format, stride, pointer);
}

extern "C" void __attribute__((cdecl)) wrap_glSetFenceNV(GLuint fence, GLenum condition)
{
    if (glad_glSetFenceNV)
        glad_glSetFenceNV(fence, condition);
}

extern "C" void __attribute__((cdecl)) wrap_glMultTransposeMatrixf(const GLfloat *m)
{
    if (glad_glMultTransposeMatrixf)
        glad_glMultTransposeMatrixf(m);
}

extern "C" void __attribute__((cdecl)) wrap_glColor4ubv(const GLubyte *v)
{
    if (glad_glColor4ubv)
        glad_glColor4ubv(v);
}

extern "C" void __attribute__((cdecl)) wrap_glFinishFenceNV(GLuint fence)
{
    if (glad_glFinishFenceNV)
        glad_glFinishFenceNV(fence);
}

extern "C" void __attribute__((cdecl)) wrap_glVertexAttrib1f(GLuint index, GLfloat x)
{
    if (glad_glVertexAttrib1f)
        glad_glVertexAttrib1f(index, x);
}

extern "C" void __attribute__((cdecl)) wrap_glClampColorARB(GLenum target, GLenum clamp)
{
    if (glad_glClampColorARB)
        glad_glClampColorARB(target, clamp);
}

extern "C" void __attribute__((cdecl)) wrap_glBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha,
                                                                GLenum dfactorAlpha)
{
    if (glad_glBlendFuncSeparate)
        glad_glBlendFuncSeparate(sfactorRGB, dfactorRGB, sfactorAlpha, dfactorAlpha);
}

extern "C" void __attribute__((cdecl)) wrap_glGetBufferParameterivARB(GLenum target, GLenum pname, GLint *params)
{
    if (glad_glGetBufferParameterivARB)
        glad_glGetBufferParameterivARB(target, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glDeleteProgramsARB(GLsizei n, const GLuint *programs)
{
    if (glad_glDeleteProgramsARB)
        glad_glDeleteProgramsARB(n, programs);
}

extern "C" void __attribute__((cdecl)) wrap_glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params)
{
    if (glad_glGetTexLevelParameteriv)
        glad_glGetTexLevelParameteriv(target, level, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glRasterPos2f(GLfloat x, GLfloat y)
{
    if (glad_glRasterPos2f)
        glad_glRasterPos2f(x, y);
}

extern "C" void __attribute__((cdecl)) wrap_glDeleteRenderbuffersEXT(GLsizei n, const GLuint *renderbuffers)
{
    if (glad_glDeleteRenderbuffersEXT)
        glad_glDeleteRenderbuffersEXT(n, renderbuffers);
}

extern "C" void __attribute__((cdecl)) wrap_glDeleteBuffersARB(GLsizei n, const GLuint *buffers)
{
    if (glad_glDeleteBuffersARB)
        glad_glDeleteBuffersARB(n, buffers);
}

extern "C" void __attribute__((cdecl)) wrap_glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y,
                                                                GLsizei width, GLsizei height)
{
    if (glad_glCopyTexSubImage2D)
        glad_glCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width, height);
}

extern "C" void __attribute__((cdecl)) wrap_glBufferDataARB(GLenum target, GLsizeiptrARB size, const GLvoid *data, GLenum usage)
{
    if (glad_glBufferDataARB)
        glad_glBufferDataARB(target, size, data, usage);
}

extern "C" void __attribute__((cdecl)) wrap_glBindBufferARB(GLenum target, GLuint buffer)
{
    if (glad_glBindBufferARB)
        glad_glBindBufferARB(target, buffer);
}

extern "C" void __attribute__((cdecl)) wrap_glLoadTransposeMatrixf(const GLfloat *m)
{
    if (glad_glLoadTransposeMatrixf)
        glad_glLoadTransposeMatrixf(m);
}

extern "C" void __attribute__((cdecl)) wrap_glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params)
{
    if (glad_glGetMaterialfv)
        glad_glGetMaterialfv(face, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glProgramEnvParameter4fARB(GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z,
                                                                       GLfloat w)
{
    if (glad_glProgramEnvParameter4fARB)
        glad_glProgramEnvParameter4fARB(target, index, x, y, z, w);
}

extern "C" void __attribute__((cdecl)) wrap_glReadBuffer(GLenum mode)
{
    if (glad_glReadBuffer)
        glad_glReadBuffer(mode);
}

extern "C" void __attribute__((cdecl)) wrap_glProgramEnvParameter4fvARB(GLenum target, GLuint index, const GLfloat *params)
{
    if (glad_glProgramEnvParameter4fvARB)
        glad_glProgramEnvParameter4fvARB(target, index, params);
}

extern "C" void __attribute__((cdecl)) wrap_glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    if (glad_glColorMask)
        glad_glColorMask(red, green, blue, alpha);
}

extern "C" void __attribute__((cdecl)) wrap_glClearDepth(GLclampd depth)
{
    if (glad_glClearDepth)
        glad_glClearDepth(depth);
}

extern "C" void __attribute__((cdecl)) wrap_glClearStencil(GLint s)
{
    if (glad_glClearStencil)
        glad_glClearStencil(s);
}

extern "C" void __attribute__((cdecl)) wrap_glStencilMask(GLuint mask)
{
    if (glad_glStencilMask)
        glad_glStencilMask(mask);
}

extern "C" void __attribute__((cdecl)) wrap_glLoadMatrixf(const GLfloat *m)
{
    if (glad_glLoadMatrixf)
        glad_glLoadMatrixf(m);
}

extern "C" void __attribute__((cdecl)) wrap_glDrawBuffer(GLenum mode)
{
    if (glad_glDrawBuffer)
        glad_glDrawBuffer(mode);
}

extern "C" void __attribute__((cdecl)) wrap_glCheckFramebufferStatusEXT(GLenum target)
{
    if (glad_glCheckFramebufferStatusEXT)
        glad_glCheckFramebufferStatusEXT(target);
}

extern "C" void __attribute__((cdecl)) wrap_glGenBuffersARB(GLsizei n, GLuint *buffers)
{
    if (glad_glGenBuffersARB)
        glad_glGenBuffersARB(n, buffers);
}

extern "C" void __attribute__((cdecl)) wrap_glBufferSubDataARB(GLenum target, GLintptrARB offset, GLsizeiptrARB size, const GLvoid *data)
{
    if (glad_glBufferSubDataARB)
        glad_glBufferSubDataARB(target, offset, size, data);
}

extern "C" void __attribute__((cdecl)) wrap_glShaderSourceARB(GLhandleARB shader, GLsizei count, const GLcharARB **string,
                                                              const GLint *length)
{
    if (glad_glShaderSourceARB)
        glad_glShaderSourceARB(shader, count, string, length);
}

extern "C" void __attribute__((cdecl)) wrap_glCompileShaderARB(GLhandleARB shader)
{
    if (glad_glCompileShaderARB)
        glad_glCompileShaderARB(shader);
}

extern "C" void __attribute__((cdecl)) wrap_glUseProgramObjectARB(GLhandleARB program)
{
    if (glad_glUseProgramObjectARB)
        glad_glUseProgramObjectARB(program);
}

extern "C" void __attribute__((cdecl)) wrap_glLinkProgramARB(GLhandleARB program)
{
    if (glad_glLinkProgramARB)
        glad_glLinkProgramARB(program);
}

extern "C" void __attribute__((cdecl)) wrap_glDeleteObjectARB(GLhandleARB obj)
{
    if (glad_glDeleteObjectARB)
        glad_glDeleteObjectARB(obj);
}

extern "C" void __attribute__((cdecl)) wrap_glGetObjectParameterivARB(GLhandleARB obj, GLenum pname, GLint *params)
{
    if (glad_glGetObjectParameterivARB)
        glad_glGetObjectParameterivARB(obj, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glGetInfoLogARB(GLhandleARB obj, GLsizei maxLength, GLsizei *length, GLcharARB *infoLog)
{
    if (glad_glGetInfoLogARB)
        glad_glGetInfoLogARB(obj, maxLength, length, infoLog);
}

extern "C" void __attribute__((cdecl)) wrap_glGenQueriesARB(GLsizei n, GLuint *ids)
{
    if (glad_glGenQueriesARB)
        glad_glGenQueriesARB(n, ids);
}

extern "C" void __attribute__((cdecl)) wrap_glDeleteQueriesARB(GLsizei n, const GLuint *ids)
{
    if (glad_glDeleteQueriesARB)
        glad_glDeleteQueriesARB(n, ids);
}

extern "C" void __attribute__((cdecl)) wrap_glBeginQueryARB(GLenum target, GLuint id)
{
    if (glad_glBeginQueryARB)
        glad_glBeginQueryARB(target, id);
}

extern "C" void __attribute__((cdecl)) wrap_glEndQueryARB(GLenum target)
{
    if (glad_glEndQueryARB)
        glad_glEndQueryARB(target);
}

extern "C" void __attribute__((cdecl)) wrap_glGetQueryObjectuivARB(GLuint id, GLenum pname, GLuint *params)
{
    if (glad_glGetQueryObjectuivARB)
        glad_glGetQueryObjectuivARB(id, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glBlendFuncSeparateEXT(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha,
                                                                   GLenum dfactorAlpha)
{
    if (glad_glBlendFuncSeparateEXT)
        glad_glBlendFuncSeparateEXT(sfactorRGB, dfactorRGB, sfactorAlpha, dfactorAlpha);
}

extern "C" void __attribute__((cdecl)) wrap_glBlendEquationEXT(GLenum mode)
{
    if (glad_glBlendEquationEXT)
        glad_glBlendEquationEXT(mode);
}

extern "C" void __attribute__((cdecl)) wrap_glBlendColorEXT(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    if (glad_glBlendColorEXT)
        glad_glBlendColorEXT(red, green, blue, alpha);
}

extern "C" void __attribute__((cdecl)) wrap_glBlendEquationSeparateEXT(GLenum modeRGB, GLenum modeAlpha)
{
    if (glad_glBlendEquationSeparateEXT)
        glad_glBlendEquationSeparateEXT(modeRGB, modeAlpha);
}

extern "C" void __attribute__((cdecl)) wrap_glGenRenderbuffersEXT(GLsizei n, GLuint *renderbuffers)
{
    if (glad_glGenRenderbuffersEXT)
        glad_glGenRenderbuffersEXT(n, renderbuffers);
}

extern "C" void __attribute__((cdecl)) wrap_glBindRenderbufferEXT(GLenum target, GLuint renderbuffer)
{
    if (glad_glBindRenderbufferEXT)
        glad_glBindRenderbufferEXT(target, renderbuffer);
}

extern "C" void __attribute__((cdecl)) wrap_glRenderbufferStorageEXT(GLenum target, GLenum internalformat, GLsizei width, GLsizei height)
{
    if (glad_glRenderbufferStorageEXT)
        glad_glRenderbufferStorageEXT(target, internalformat, width, height);
}

extern "C" void __attribute__((cdecl)) wrap_glFramebufferRenderbufferEXT(GLenum target, GLenum attachment, GLenum renderbuffertarget,
                                                                         GLuint renderbuffer)
{
    if (glad_glFramebufferRenderbufferEXT)
        glad_glFramebufferRenderbufferEXT(target, attachment, renderbuffertarget, renderbuffer);
}

extern "C" void __attribute__((cdecl)) wrap_glWindowPos2sARB(GLshort x, GLshort y)
{
    if (glad_glWindowPos2sARB)
        glad_glWindowPos2sARB(x, y);
}

extern "C" void __attribute__((cdecl)) wrap_glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                                                                const GLvoid *indices)
{
    if (glad_glDrawRangeElements)
        glad_glDrawRangeElements(mode, start, end, count, type, indices);
}

extern "C" void __attribute__((cdecl)) wrap_glCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type)
{
    if (glad_glCopyPixels)
        glad_glCopyPixels(x, y, width, height, type);
}

extern "C" void __attribute__((cdecl)) wrap_glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels)
{
    if (glad_glGetTexImage)
        glad_glGetTexImage(target, level, format, type, pixels);
}

extern "C" void __attribute__((cdecl)) wrap_glPrimitiveRestartIndexNV(GLuint index)
{
    if (glad_glPrimitiveRestartIndexNV)
        glad_glPrimitiveRestartIndexNV(index);
}

extern "C" void __attribute__((cdecl)) wrap_glClipPlane(GLenum plane, const GLdouble *equation)
{
    if (glad_glClipPlane)
        glad_glClipPlane(plane, equation);
}

extern "C" void __attribute__((cdecl)) wrap_glNormal3dv(const GLdouble *v)
{
    if (glad_glNormal3dv)
        glad_glNormal3dv(v);
}

extern "C" void __attribute__((cdecl)) wrap_glNormal3d(GLdouble nx, GLdouble ny, GLdouble nz)
{
    if (glad_glNormal3d)
        glad_glNormal3d(nx, ny, nz);
}

extern "C" void __attribute__((cdecl)) wrap_glLightModeliv(GLenum pname, const GLint *params)
{
    if (glad_glLightModeliv)
        glad_glLightModeliv(pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glVertex4dv(const GLdouble *v)
{
    if (glad_glVertex4dv)
        glad_glVertex4dv(v);
}

extern "C" void __attribute__((cdecl)) wrap_glVertex3dv(const GLdouble *v)
{
    if (glad_glVertex3dv)
        glad_glVertex3dv(v);
}

extern "C" void __attribute__((cdecl)) wrap_glVertex2dv(const GLdouble *v)
{
    if (glad_glVertex2dv)
        glad_glVertex2dv(v);
}

extern "C" void __attribute__((cdecl)) wrap_glTexGeni(GLenum coord, GLenum pname, GLint param)
{
    if (glad_glTexGeni)
        glad_glTexGeni(coord, pname, param);
}

extern "C" void __attribute__((cdecl)) wrap_glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params)
{
    if (glad_glTexGenfv)
        glad_glTexGenfv(coord, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glLineStipple(GLint factor, GLushort pattern)
{
    if (glad_glLineStipple)
        glad_glLineStipple(factor, pattern);
}

extern "C" void __attribute__((cdecl)) wrap_glColor3dv(const GLdouble *v)
{
    if (glad_glColor3dv)
        glad_glColor3dv(v);
}

extern "C" void __attribute__((cdecl)) wrap_glColor3d(GLdouble red, GLdouble green, GLdouble blue)
{
    if (glad_glColor3d)
        glad_glColor3d(red, green, blue);
}

extern "C" void __attribute__((cdecl)) wrap_glColor4dv(const GLdouble *v)
{
    if (glad_glColor4dv)
        glad_glColor4dv(v);
}

extern "C" void __attribute__((cdecl)) wrap_glGetUniformLocationARB(GLint program, const GLchar *name)
{
    if (glad_glGetUniformLocationARB)
        glad_glGetUniformLocationARB(program, name);
}

extern "C" void __attribute__((cdecl)) wrap_glUniform1fvARB(GLint location, GLsizei count, const GLfloat *value)
{
    if (glad_glUniform1fvARB)
        glad_glUniform1fvARB(location, count, value);
}

extern "C" void __attribute__((cdecl)) wrap_glSecondaryColor3fv(const GLfloat *v)
{
    if (glad_glSecondaryColor3fv)
        glad_glSecondaryColor3fv(v);
}

extern "C" void __attribute__((cdecl)) wrap_glUniformMatrix3fvARB(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    if (glad_glUniformMatrix3fvARB)
        glad_glUniformMatrix3fvARB(location, count, transpose, value);
}

extern "C" void __attribute__((cdecl)) wrap_glUniform4fvARB(GLint location, GLsizei count, const GLfloat *value)
{
    if (glad_glUniform4fvARB)
        glad_glUniform4fvARB(location, count, value);
}

extern "C" void __attribute__((cdecl)) wrap_glUniform1fARB(GLint location, GLfloat value)
{
    if (glad_glUniform1fARB)
        glad_glUniform1fARB(location, value);
}

extern "C" void __attribute__((cdecl)) wrap_glSecondaryColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    if (glad_glSecondaryColorPointer)
        glad_glSecondaryColorPointer(size, type, stride, pointer);
}

extern "C" void __attribute__((cdecl)) wrap_glLockArraysEXT(GLint first, GLsizei count)
{
    if (glad_glLockArraysEXT)
        glad_glLockArraysEXT(first, count);
}

extern "C" void __attribute__((cdecl)) wrap_glMultiDrawElements(GLenum mode, const GLsizei *count, GLenum type,
                                                                const GLvoid *const *indices, GLsizei drawcount)
{
    if (glad_glMultiDrawElements)
        glad_glMultiDrawElements(mode, count, type, indices, drawcount);
}

extern "C" void __attribute__((cdecl)) wrap_glUnlockArraysEXT()
{
    if (glad_glUnlockArraysEXT)
        glad_glUnlockArraysEXT();
}

extern "C" void __attribute__((cdecl)) wrap_glUniform1iARB(GLint location, GLint value)
{
    if (glad_glUniform1iARB)
        glad_glUniform1iARB(location, value);
}

extern "C" void __attribute__((cdecl)) wrap_glVertexAttrib3fARB(GLuint index, GLfloat x, GLfloat y, GLfloat z)
{
    if (glad_glVertexAttrib3fARB)
        glad_glVertexAttrib3fARB(index, x, y, z);
}

extern "C" void __attribute__((cdecl)) wrap_glDetachObjectARB(GLhandleARB containerObj, GLhandleARB attachedObj)
{
    if (glad_glDetachObjectARB)
        glad_glDetachObjectARB(containerObj, attachedObj);
}

extern "C" void __attribute__((cdecl)) wrap_glAttachObjectARB(GLhandleARB containerObj, GLhandleARB obj)
{
    if (glad_glAttachObjectARB)
        glad_glAttachObjectARB(containerObj, obj);
}

extern "C" void __attribute__((cdecl)) wrap_glCreateProgramObjectARB()
{
    if (glad_glCreateProgramObjectARB)
        glad_glCreateProgramObjectARB();
}

extern "C" void __attribute__((cdecl)) wrap_glBindAttribLocationARB(GLhandleARB programObj, GLuint index, const GLchar *name)
{
    if (glad_glBindAttribLocationARB)
        glad_glBindAttribLocationARB(programObj, index, name);
}

extern "C" void __attribute__((cdecl)) wrap_glUniformMatrix4fvARB(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
    if (glad_glUniformMatrix4fvARB)
        glad_glUniformMatrix4fvARB(location, count, transpose, value);
}

extern "C" void __attribute__((cdecl)) wrap_glColor3fv(const GLfloat *v)
{
    if (glad_glColor3fv)
        glad_glColor3fv(v);
}

extern "C" void __attribute__((cdecl)) wrap_glGenProgramsNV(GLsizei n, GLuint *programs)
{
    if (glad_glGenProgramsNV)
        glad_glGenProgramsNV(n, programs);
}

extern "C" void __attribute__((cdecl)) wrap_glDeleteProgramsNV(GLsizei n, const GLuint *programs)
{
    if (glad_glDeleteProgramsNV)
        glad_glDeleteProgramsNV(n, programs);
}

extern "C" void __attribute__((cdecl)) wrap_glBindProgramNV(GLenum target, GLuint program)
{
    if (glad_glBindProgramNV)
        glad_glBindProgramNV(target, program);
}

extern "C" void __attribute__((cdecl)) wrap_glLoadProgramNV(GLenum target, GLuint id, GLsizei len, const GLubyte *program)
{
    if (glad_glLoadProgramNV)
        glad_glLoadProgramNV(target, id, len, program);
}

extern "C" void __attribute__((cdecl)) wrap_glIsProgramNV(GLuint program)
{
    if (glad_glIsProgramNV)
        glad_glIsProgramNV(program);
}

extern "C" void __attribute__((cdecl)) wrap_glTrackMatrixNV(GLenum target, GLuint address, GLenum matrix, GLenum transform)
{
    if (glad_glTrackMatrixNV)
        glad_glTrackMatrixNV(target, address, matrix, transform);
}

extern "C" void __attribute__((cdecl)) wrap_glProgramParameter4fNV(GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    if (glad_glProgramParameter4fNV)
        glad_glProgramParameter4fNV(target, index, x, y, z, w);
}

extern "C" void __attribute__((cdecl)) wrap_glProgramParameter4fvNV(GLenum target, GLuint index, const GLfloat *v)
{
    if (glad_glProgramParameter4fvNV)
        glad_glProgramParameter4fvNV(target, index, v);
}

extern "C" void __attribute__((cdecl)) wrap_glProgramParameters4fvNV(GLenum target, GLuint index, GLsizei count, const GLfloat *v)
{
    if (glad_glProgramParameters4fvNV)
        glad_glProgramParameters4fvNV(target, index, count, v);
}

extern "C" void __attribute__((cdecl)) wrap_glGenOcclusionQueriesNV(GLsizei n, GLuint *ids)
{
    if (glad_glGenOcclusionQueriesNV)
        glad_glGenOcclusionQueriesNV(n, ids);
}

extern "C" void __attribute__((cdecl)) wrap_glBeginOcclusionQueryNV(GLuint id)
{
    if (glad_glBeginOcclusionQueryNV)
        glad_glBeginOcclusionQueryNV(id);
}

extern "C" void __attribute__((cdecl)) wrap_glEndOcclusionQueryNV()
{
    if (glad_glEndOcclusionQueryNV)
        glad_glEndOcclusionQueryNV();
}

extern "C" void __attribute__((cdecl)) wrap_glGetOcclusionQueryuivNV(GLuint id, GLenum pname, GLuint *params)
{
    if (glad_glGetOcclusionQueryuivNV)
        glad_glGetOcclusionQueryuivNV(id, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glProgramEnvParameters4fvEXT(GLenum target, GLuint index, GLsizei count, const GLfloat *params)
{
    if (glad_glProgramEnvParameters4fvEXT)
        glad_glProgramEnvParameters4fvEXT(target, index, count, params);
}

extern "C" void __attribute__((cdecl)) wrap_glProgramLocalParameters4fvEXT(GLenum target, GLuint index, GLsizei count,
                                                                           const GLfloat *params)
{
    if (glad_glProgramLocalParameters4fvEXT)
        glad_glProgramLocalParameters4fvEXT(target, index, count, params);
}

extern "C" void __attribute__((cdecl)) wrap_glNormal3fv(const GLfloat *v)
{
    if (glad_glNormal3fv)
        glad_glNormal3fv(v);
}

extern "C" void __attribute__((cdecl)) wrap_glCopyTexImage2D(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y,
                                                             GLsizei width, GLsizei height, GLint border)
{
    if (glad_glCopyTexImage2D)
        glad_glCopyTexImage2D(target, level, internalFormat, x, y, width, height, border);
}

extern "C" void __attribute__((cdecl)) wrap_glTexCoord3f(GLfloat s, GLfloat t, GLfloat r)
{
    if (glad_glTexCoord3f)
        glad_glTexCoord3f(s, t, r);
}

extern "C" void __attribute__((cdecl)) wrap_glArrayElement(GLint i)
{
    if (glad_glArrayElement)
        glad_glArrayElement(i);
}

extern "C" void __attribute__((cdecl)) wrap_glPointParameterfARB(GLenum pname, GLfloat param)
{
    if (glad_glPointParameterfARB)
        glad_glPointParameterfARB(pname, param);
}

extern "C" void __attribute__((cdecl)) wrap_glGetQueryObjectivARB(GLuint id, GLenum pname, GLint *params)
{
    if (glad_glGetQueryObjectivARB)
        glad_glGetQueryObjectivARB(id, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glDrawArraysEXT(GLenum mode, GLint first, GLsizei count)
{
    if (glad_glDrawArraysEXT)
        glad_glDrawArraysEXT(mode, first, count);
}

extern "C" void __attribute__((cdecl)) wrap_glGenFencesNV(GLsizei n, GLuint *fences)
{
    if (glad_glGenFencesNV)
        glad_glGenFencesNV(n, fences);
}

extern "C" void __attribute__((cdecl)) wrap_glDeleteFencesNV(GLsizei n, const GLuint *fences)
{
    if (glad_glDeleteFencesNV)
        glad_glDeleteFencesNV(n, fences);
}

extern "C" int __attribute__((cdecl)) wrap_glIsEnabled(GLenum cap)
{
    if (glad_glIsEnabled)
        return glad_glIsEnabled(cap);
    return (int)0;
}

extern "C" GLuint __attribute__((cdecl)) wrap_glGenLists(GLsizei range)
{
    if (glad_glGenLists)
        return glad_glGenLists(range);
    return (GLuint)0;
}

extern "C" GLboolean __attribute__((cdecl)) wrap_glIsList(GLuint list)
{
    if (glad_glIsList)
        return glad_glIsList(list);
    return (GLboolean)0;
}

extern "C" const GLubyte *__attribute__((cdecl)) wrap_glGetString(GLenum name)
{
    if (glad_glGetString)
        return glad_glGetString(name);
    return (const GLubyte *)0;
}

extern "C" GLenum __attribute__((cdecl)) wrap_glGetError()
{
    if (glad_glGetError)
        return glad_glGetError();
    return (GLenum)0;
}

extern "C" GLboolean __attribute__((cdecl)) wrap_glIsProgramARB(GLuint program)
{
    if (glad_glIsProgramARB)
        return glad_glIsProgramARB(program);
    return (GLboolean)0;
}

extern "C" GLenum __attribute__((cdecl)) wrap_glCheckFramebufferStatus(GLenum target)
{
    if (glad_glCheckFramebufferStatus)
        return glad_glCheckFramebufferStatus(target);
    return (GLenum)0;
}

extern "C" GLuint __attribute__((cdecl)) wrap_glCreateShader(GLenum type)
{
    if (glad_glCreateShader)
        return glad_glCreateShader(type);
    return (GLuint)0;
}

extern "C" GLuint __attribute__((cdecl)) wrap_glCreateProgram()
{
    if (glad_glCreateProgram)
        return glad_glCreateProgram();
    return (GLuint)0;
}

extern "C" GLint __attribute__((cdecl)) wrap_glGetUniformLocation(GLuint program, const GLchar *name)
{
    if (glad_glGetUniformLocation)
        return glad_glGetUniformLocation(program, name);
    return (GLint)0;
}

extern "C" GLint __attribute__((cdecl)) wrap_glGetAttribLocation(GLuint program, const GLchar *name)
{
    if (glad_glGetAttribLocation)
        return glad_glGetAttribLocation(program, name);
    return (GLint)0;
}

extern "C" GLboolean __attribute__((cdecl)) wrap_glIsTexture(GLuint texture)
{
    if (glad_glIsTexture)
        return glad_glIsTexture(texture);
    return (GLboolean)0;
}

extern "C" GLvoid *__attribute__((cdecl)) wrap_glMapBuffer(GLenum target, GLenum access)
{
    if (glad_glMapBuffer)
        return glad_glMapBuffer(target, access);
    return (GLvoid *)0;
}

extern "C" GLboolean __attribute__((cdecl)) wrap_glIsBufferARB(GLuint buffer)
{
    if (glad_glIsBufferARB)
        return glad_glIsBufferARB(buffer);
    return (GLboolean)0;
}

extern "C" GLboolean __attribute__((cdecl)) wrap_glUnmapBuffer(GLenum target)
{
    if (glad_glUnmapBuffer)
        return glad_glUnmapBuffer(target);
    return (GLboolean)0;
}

extern "C" GLboolean __attribute__((cdecl)) wrap_glIsFenceNV(GLuint fence)
{
    if (glad_glIsFenceNV)
        return glad_glIsFenceNV(fence);
    return (GLboolean)0;
}

extern "C" GLvoid *__attribute__((cdecl)) wrap_glMapBufferARB(GLenum target, GLenum access)
{
    if (glad_glMapBufferARB)
        return glad_glMapBufferARB(target, access);
    return (GLvoid *)0;
}

extern "C" GLboolean __attribute__((cdecl)) wrap_glUnmapBufferARB(GLenum target)
{
    if (glad_glUnmapBufferARB)
        return glad_glUnmapBufferARB(target);
    return (GLboolean)0;
}

extern "C" GLhandleARB __attribute__((cdecl)) wrap_glCreateShaderObjectARB(GLenum shaderType)
{
    if (glad_glCreateShaderObjectARB)
        return glad_glCreateShaderObjectARB(shaderType);
    return (GLhandleARB)0;
}

extern "C" void __attribute__((cdecl)) wrap_glCompressedTexImage2DARB(GLenum target, GLint level, GLenum internalformat, GLsizei width,
                                                                      GLsizei height, GLint border, GLsizei imageSize, const void *data)
{
    if (glad_glCompressedTexImage2DARB)
        glad_glCompressedTexImage2DARB(target, level, internalformat, width, height, border, imageSize, data);
}

extern "C" void __attribute__((cdecl)) wrap_glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                                                            GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    if (glad_glTexSubImage2D)
        glad_glTexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
}

extern "C" void __attribute__((cdecl)) wrap_glCompressedTexSubImage2DARB(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                                                         GLsizei width, GLsizei height, GLenum format, GLsizei imageSize,
                                                                         const void *data)
{
    if (glad_glCompressedTexSubImage2DARB)
        glad_glCompressedTexSubImage2DARB(target, level, xoffset, yoffset, width, height, format, imageSize, data);
}

extern "C" void __attribute__((cdecl)) wrap_glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width,
                                                                   GLsizei height, GLint border, GLsizei imageSize, const void *data)
{
    if (glad_glCompressedTexImage2D)
        glad_glCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data);
}

extern "C" void __attribute__((cdecl)) wrap_glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border,
                                                         GLenum format, GLenum type, const GLvoid *pixels)
{
    if (glad_glTexImage1D)
        glad_glTexImage1D(target, level, internalformat, width, border, format, type, pixels);
}

extern "C" void __attribute__((cdecl)) wrap_glBeginQuery(GLenum target, GLuint id)
{
    if (glad_glBeginQuery)
        glad_glBeginQuery(target, id);
}

extern "C" void __attribute__((cdecl)) wrap_glEndQuery(GLenum target)
{
    if (glad_glEndQuery)
        glad_glEndQuery(target);
}

extern "C" void __attribute__((cdecl)) wrap_glGetQueryObjectiv(GLuint id, GLenum pname, GLint *params)
{
    if (glad_glGetQueryObjectiv)
        glad_glGetQueryObjectiv(id, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                                                      GLsizei width, GLsizei height, GLenum format, GLsizei imageSize,
                                                                      const void *data)
{
    if (glad_glCompressedTexSubImage2D)
        glad_glCompressedTexSubImage2D(target, level, xoffset, yoffset, width, height, format, imageSize, data);
}

extern "C" void __attribute__((cdecl)) wrap_glGetCompressedTexImage(GLenum target, GLint level, void *img)
{
    if (glad_glGetCompressedTexImage)
        glad_glGetCompressedTexImage(target, level, img);
}

extern "C" void __attribute__((cdecl)) wrap_glGetQueryObjectuiv(GLuint id, GLenum pname, GLuint *params)
{
    if (glad_glGetQueryObjectuiv)
        glad_glGetQueryObjectuiv(id, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glGetProgramEnvParameterfvARB(GLhandleARB program, GLuint index, GLfloat *params)
{
    if (glad_glGetProgramEnvParameterfvARB)
        glad_glGetProgramEnvParameterfvARB(program, index, params);
}

extern "C" void __attribute__((cdecl)) wrap_glGetTexParameteriv(GLenum target, GLenum pname, GLint *params)
{
    if (glad_glGetTexParameteriv)
        glad_glGetTexParameteriv(target, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    if (glad_glMultiTexCoord4f)
        glad_glMultiTexCoord4f(target, s, t, r, q);
}

extern "C" void __attribute__((cdecl)) wrap_glVertexAttrib4fv(GLuint index, const GLfloat *v)
{
    if (glad_glVertexAttrib4fv)
        glad_glVertexAttrib4fv(index, v);
}

extern "C" void __attribute__((cdecl)) wrap_glDeleteQueries(GLsizei n, const GLuint *ids)
{
    if (glad_glDeleteQueries)
        glad_glDeleteQueries(n, ids);
}

extern "C" void __attribute__((cdecl)) wrap_glPointParameteri(GLenum target, GLint param)
{
    if (glad_glPointParameteri)
        glad_glPointParameteri(target, param);
}

extern "C" void __attribute__((cdecl)) wrap_glGenQueries(GLsizei n, GLuint *ids)
{
    if (glad_glGenQueries)
        glad_glGenQueries(n, ids);
}

extern "C" void __attribute__((cdecl)) wrap_glSecondaryColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
    if (glad_glSecondaryColor3f)
        glad_glSecondaryColor3f(red, green, blue);
}

extern "C" void __attribute__((cdecl)) wrap_glUniform3fvARB(GLhandleARB location, GLsizei count, const GLfloat *v)
{
    if (glad_glUniform3fvARB)
        glad_glUniform3fvARB(location, count, v);
}

extern "C" void __attribute__((cdecl)) wrap_glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels)
{
    if (glad_glDrawPixels)
        glad_glDrawPixels(width, height, format, type, pixels);
}

extern "C" void __attribute__((cdecl)) wrap_glGetTexEnvfv(GLenum target, GLenum pname, GLfloat *params)
{
    if (glad_glGetTexEnvfv)
        glad_glGetTexEnvfv(target, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glGetTexEnviv(GLenum target, GLenum pname, GLint *params)
{
    if (glad_glGetTexEnviv)
        glad_glGetTexEnviv(target, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
    if (glad_glGetTexParameterfv)
        glad_glGetTexParameterfv(target, pname, params);
}

extern "C" void __attribute__((cdecl)) wrap_glMultMatrixd(const GLdouble *m)
{
    if (glad_glMultMatrixd)
        glad_glMultMatrixd(m);
}

extern "C" void __attribute__((cdecl)) wrap_glGenerateMipmapEXT(GLenum target)
{
    if (glad_glGenerateMipmapEXT)
        glad_glGenerateMipmapEXT(target);
}
extern "C" void __attribute__((cdecl)) wrap_glTexCoord2d(GLdouble u, GLdouble v)
{
    if (glad_glTexCoord2d)
        glad_glTexCoord2d(u, v);
}

extern "C" void __attribute__((cdecl)) wrap_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data)
{
    if (glad_glBufferSubData)
        glad_glBufferSubData(target, offset, size, data);
}

// The interceptor router mapping string names to our safe __cdecl wrappers
void *GLHooks_GetProcAddress(const char *procName)
{
    if (strcmp(procName, "glOrtho") == 0)
        return (void *)&bridgeglOrtho;
    if (strcmp(procName, "glViewport") == 0)
        return (void *)&bridgeglViewport;
    if (strcmp(procName, "glTexImage2D") == 0)
        return (void *)&bridgeglTexImage2D;
    if (strcmp(procName, "glTexParameteri") == 0)
        return (void *)&bridgeglTexParameteri;
    if (strcmp(procName, "glEnable") == 0)
        return (void *)&bridgeglEnable;
    if (strcmp(procName, "glDisable") == 0)
        return (void *)&bridgeglDisable;
    if (strcmp(procName, "glGenFencesNV") == 0)
        return (void *)&bridgeglGenFencesNV;
    if (strcmp(procName, "glDeleteFencesNV") == 0)
        return (void *)&bridgeglDeleteFencesNV;
    if (strcmp(procName, "glSetFenceNV") == 0)
        return (void *)&bridgeglSetFenceNV;
    if (strcmp(procName, "glTestFenceNV") == 0)
        return (void *)&bridgeglTestFenceNV;
    if (strcmp(procName, "glFinishFenceNV") == 0)
        return (void *)&bridgeglFinishFenceNV;
    if (strcmp(procName, "glIsFenceNV") == 0)
        return (void *)&bridgeglIsFenceNV;
    if (strcmp(procName, "glBindTexture") == 0)
        return (void *)&bridgeglBindTexture;
    if (strcmp(procName, "glTexCoordPointer") == 0)
        return (void *)&bridgeglTexCoordPointer;
    if (strcmp(procName, "glVertexPointer") == 0)
        return (void *)&bridgeglVertexPointer;

    if (strcmp(procName, "glDrawArrays") == 0)
        return (void *)&wrap_glDrawArrays;
    if (strcmp(procName, "glTexSubImage2D") == 0)
        return (void *)&wrap_glTexSubImage2D;
    if (strcmp(procName, "glCompressedTexSubImage2DARB") == 0)
        return (void *)&wrap_glCompressedTexSubImage2DARB;
    if (strcmp(procName, "glCompressedTexImage2D") == 0)
        return (void *)&wrap_glCompressedTexImage2D;
    if (strcmp(procName, "glActiveTexture") == 0)
        return (void *)&wrap_glActiveTexture;
    if (strcmp(procName, "glActiveTextureARB") == 0)
        return (void *)&wrap_glActiveTextureARB;
    if (strcmp(procName, "glAlphaFunc") == 0)
        return (void *)&wrap_glAlphaFunc;
    if (strcmp(procName, "glArrayElement") == 0)
        return (void *)&wrap_glArrayElement;
    if (strcmp(procName, "glAttachObjectARB") == 0)
        return (void *)&wrap_glAttachObjectARB;
    if (strcmp(procName, "glAttachShader") == 0)
        return (void *)&wrap_glAttachShader;
    if (strcmp(procName, "glBegin") == 0)
        return (void *)&wrap_glBegin;
    if (strcmp(procName, "glBeginOcclusionQueryNV") == 0)
        return (void *)&wrap_glBeginOcclusionQueryNV;
    if (strcmp(procName, "glBeginQueryARB") == 0)
        return (void *)&wrap_glBeginQueryARB;
    if (strcmp(procName, "glBindAttribLocationARB") == 0)
        return (void *)&wrap_glBindAttribLocationARB;
    if (strcmp(procName, "glBindBuffer") == 0)
        return (void *)&wrap_glBindBuffer;
    if (strcmp(procName, "glBindBufferARB") == 0)
        return (void *)&wrap_glBindBufferARB;
    if (strcmp(procName, "glBindFramebuffer") == 0)
        return (void *)&wrap_glBindFramebuffer;
    if (strcmp(procName, "glBindFramebufferEXT") == 0)
        return (void *)&wrap_glBindFramebufferEXT;
    if (strcmp(procName, "glBindProgramARB") == 0)
        return (void *)&wrap_glBindProgramARB;
    if (strcmp(procName, "glBindProgramNV") == 0)
        return (void *)&wrap_glBindProgramNV;
    if (strcmp(procName, "glBindRenderbuffer") == 0)
        return (void *)&wrap_glBindRenderbuffer;
    if (strcmp(procName, "glBindRenderbufferEXT") == 0)
        return (void *)&wrap_glBindRenderbufferEXT;
    if (strcmp(procName, "glBlendColor") == 0)
        return (void *)&wrap_glBlendColor;
    if (strcmp(procName, "glBlendColorEXT") == 0)
        return (void *)&wrap_glBlendColorEXT;
    if (strcmp(procName, "glBlendEquation") == 0)
        return (void *)&wrap_glBlendEquation;
    if (strcmp(procName, "glBlendEquationEXT") == 0)
        return (void *)&wrap_glBlendEquationEXT;
    if (strcmp(procName, "glBlendEquationSeparateEXT") == 0)
        return (void *)&wrap_glBlendEquationSeparateEXT;
    if (strcmp(procName, "glBlendFunc") == 0)
        return (void *)&wrap_glBlendFunc;
    if (strcmp(procName, "glBlendFuncSeparate") == 0)
        return (void *)&wrap_glBlendFuncSeparate;
    if (strcmp(procName, "glBlendFuncSeparateEXT") == 0)
        return (void *)&wrap_glBlendFuncSeparateEXT;
    if (strcmp(procName, "glBufferData") == 0)
        return (void *)&wrap_glBufferData;
    if (strcmp(procName, "glBufferDataARB") == 0)
        return (void *)&wrap_glBufferDataARB;
    if (strcmp(procName, "glBufferSubDataARB") == 0)
        return (void *)&wrap_glBufferSubDataARB;
    if (strcmp(procName, "glCallList") == 0)
        return (void *)&wrap_glCallList;
    if (strcmp(procName, "glCheckFramebufferStatus") == 0)
        return (void *)&wrap_glCheckFramebufferStatus;
    if (strcmp(procName, "glCheckFramebufferStatusEXT") == 0)
        return (void *)&wrap_glCheckFramebufferStatusEXT;
    if (strcmp(procName, "glClampColorARB") == 0)
        return (void *)&wrap_glClampColorARB;
    if (strcmp(procName, "glClear") == 0)
        return (void *)&wrap_glClear;
    if (strcmp(procName, "glClearColor") == 0)
        return (void *)&wrap_glClearColor;
    if (strcmp(procName, "glClearDepth") == 0)
        return (void *)&wrap_glClearDepth;
    if (strcmp(procName, "glClearStencil") == 0)
        return (void *)&wrap_glClearStencil;
    if (strcmp(procName, "glClientActiveTexture") == 0)
        return (void *)&wrap_glClientActiveTexture;
    if (strcmp(procName, "glClientActiveTextureARB") == 0)
        return (void *)&wrap_glClientActiveTextureARB;
    if (strcmp(procName, "glClipPlane") == 0)
        return (void *)&wrap_glClipPlane;
    if (strcmp(procName, "glColor3d") == 0)
        return (void *)&wrap_glColor3d;
    if (strcmp(procName, "glColor3dv") == 0)
        return (void *)&wrap_glColor3dv;
    if (strcmp(procName, "glColor3f") == 0)
        return (void *)&wrap_glColor3f;
    if (strcmp(procName, "glColor3fv") == 0)
        return (void *)&wrap_glColor3fv;
    if (strcmp(procName, "glColor3ub") == 0)
        return (void *)&wrap_glColor3ub;
    if (strcmp(procName, "glColor4dv") == 0)
        return (void *)&wrap_glColor4dv;
    if (strcmp(procName, "glColor4f") == 0)
        return (void *)&wrap_glColor4f;
    if (strcmp(procName, "glColor4fv") == 0)
        return (void *)&wrap_glColor4fv;
    if (strcmp(procName, "glColor4ub") == 0)
        return (void *)&wrap_glColor4ub;
    if (strcmp(procName, "glColor4ubv") == 0)
        return (void *)&wrap_glColor4ubv;
    if (strcmp(procName, "glColorMask") == 0)
        return (void *)&wrap_glColorMask;
    if (strcmp(procName, "glColorMaterial") == 0)
        return (void *)&wrap_glColorMaterial;
    if (strcmp(procName, "glColorPointer") == 0)
        return (void *)&wrap_glColorPointer;
    if (strcmp(procName, "glCompileShader") == 0)
        return (void *)&wrap_glCompileShader;
    if (strcmp(procName, "glCompileShaderARB") == 0)
        return (void *)&wrap_glCompileShaderARB;
    if (strcmp(procName, "glCopyPixels") == 0)
        return (void *)&wrap_glCopyPixels;
    if (strcmp(procName, "glCompressedTexImage2DARB") == 0)
        return (void *)&wrap_glCompressedTexImage2DARB;
    if (strcmp(procName, "glCopyTexImage2D") == 0)
        return (void *)&wrap_glCopyTexImage2D;
    if (strcmp(procName, "glCopyTexSubImage2D") == 0)
        return (void *)&wrap_glCopyTexSubImage2D;
    if (strcmp(procName, "glCreateProgram") == 0)
        return (void *)&wrap_glCreateProgram;
    if (strcmp(procName, "glCreateProgramObjectARB") == 0)
        return (void *)&wrap_glCreateProgramObjectARB;
    if (strcmp(procName, "glCreateShader") == 0)
        return (void *)&wrap_glCreateShader;
    if (strcmp(procName, "glCreateShaderObjectARB") == 0)
        return (void *)&wrap_glCreateShaderObjectARB;
    if (strcmp(procName, "glCullFace") == 0)
        return (void *)&wrap_glCullFace;
    if (strcmp(procName, "glDeleteBuffers") == 0)
        return (void *)&wrap_glDeleteBuffers;
    if (strcmp(procName, "glDeleteBuffersARB") == 0)
        return (void *)&wrap_glDeleteBuffersARB;
    if (strcmp(procName, "glDeleteFencesNV") == 0)
        return (void *)&wrap_glDeleteFencesNV;
    if (strcmp(procName, "glDeleteFramebuffers") == 0)
        return (void *)&wrap_glDeleteFramebuffers;
    if (strcmp(procName, "glDeleteFramebuffersEXT") == 0)
        return (void *)&wrap_glDeleteFramebuffersEXT;
    if (strcmp(procName, "glDeleteLists") == 0)
        return (void *)&wrap_glDeleteLists;
    if (strcmp(procName, "glDeleteObjectARB") == 0)
        return (void *)&wrap_glDeleteObjectARB;
    if (strcmp(procName, "glDeleteProgram") == 0)
        return (void *)&wrap_glDeleteProgram;
    if (strcmp(procName, "glDeleteProgramsARB") == 0)
        return (void *)&wrap_glDeleteProgramsARB;
    if (strcmp(procName, "glDeleteProgramsNV") == 0)
        return (void *)&wrap_glDeleteProgramsNV;
    if (strcmp(procName, "glDeleteQueriesARB") == 0)
        return (void *)&wrap_glDeleteQueriesARB;
    if (strcmp(procName, "glDeleteRenderbuffers") == 0)
        return (void *)&wrap_glDeleteRenderbuffers;
    if (strcmp(procName, "glDeleteRenderbuffersEXT") == 0)
        return (void *)&wrap_glDeleteRenderbuffersEXT;
    if (strcmp(procName, "glDeleteShader") == 0)
        return (void *)&wrap_glDeleteShader;
    if (strcmp(procName, "glDeleteTextures") == 0)
        return (void *)&wrap_glDeleteTextures;
    if (strcmp(procName, "glDepthFunc") == 0)
        return (void *)&wrap_glDepthFunc;
    if (strcmp(procName, "glDepthMask") == 0)
        return (void *)&wrap_glDepthMask;
    if (strcmp(procName, "glDepthRange") == 0)
        return (void *)&wrap_glDepthRange;
    if (strcmp(procName, "glDetachObjectARB") == 0)
        return (void *)&wrap_glDetachObjectARB;
    if (strcmp(procName, "glDisableClientState") == 0)
        return (void *)&wrap_glDisableClientState;
    if (strcmp(procName, "glDisableVertexAttribArray") == 0)
        return (void *)&wrap_glDisableVertexAttribArray;
    if (strcmp(procName, "glDisableVertexAttribArrayARB") == 0)
        return (void *)&wrap_glDisableVertexAttribArrayARB;
    if (strcmp(procName, "glDrawArraysEXT") == 0)
        return (void *)&wrap_glDrawArraysEXT;
    if (strcmp(procName, "glDrawBuffer") == 0)
        return (void *)&wrap_glDrawBuffer;
    if (strcmp(procName, "glDrawBuffers") == 0)
        return (void *)&wrap_glDrawBuffers;
    if (strcmp(procName, "glDrawElements") == 0)
        return (void *)&wrap_glDrawElements;
    if (strcmp(procName, "glDrawRangeElements") == 0)
        return (void *)&wrap_glDrawRangeElements;
    if (strcmp(procName, "glEnableClientState") == 0)
        return (void *)&wrap_glEnableClientState;
    if (strcmp(procName, "glEnableVertexAttribArray") == 0)
        return (void *)&wrap_glEnableVertexAttribArray;
    if (strcmp(procName, "glEnableVertexAttribArrayARB") == 0)
        return (void *)&wrap_glEnableVertexAttribArrayARB;
    if (strcmp(procName, "glEnd") == 0)
        return (void *)&wrap_glEnd;
    if (strcmp(procName, "glEndList") == 0)
        return (void *)&wrap_glEndList;
    if (strcmp(procName, "glEndOcclusionQueryNV") == 0)
        return (void *)&wrap_glEndOcclusionQueryNV;
    if (strcmp(procName, "glEndQueryARB") == 0)
        return (void *)&wrap_glEndQueryARB;
    if (strcmp(procName, "glFinish") == 0)
        return (void *)&wrap_glFinish;
    if (strcmp(procName, "glFinishFenceNV") == 0)
        return (void *)&wrap_glFinishFenceNV;
    if (strcmp(procName, "glFlush") == 0)
        return (void *)&wrap_glFlush;
    if (strcmp(procName, "glFogf") == 0)
        return (void *)&wrap_glFogf;
    if (strcmp(procName, "glFogfv") == 0)
        return (void *)&wrap_glFogfv;
    if (strcmp(procName, "glFogi") == 0)
        return (void *)&wrap_glFogi;
    if (strcmp(procName, "glFramebufferRenderbuffer") == 0)
        return (void *)&wrap_glFramebufferRenderbuffer;
    if (strcmp(procName, "glFramebufferRenderbufferEXT") == 0)
        return (void *)&wrap_glFramebufferRenderbufferEXT;
    if (strcmp(procName, "glFramebufferTexture2D") == 0)
        return (void *)&wrap_glFramebufferTexture2D;
    if (strcmp(procName, "glFramebufferTexture2DEXT") == 0)
        return (void *)&wrap_glFramebufferTexture2DEXT;
    if (strcmp(procName, "glFrontFace") == 0)
        return (void *)&wrap_glFrontFace;
    if (strcmp(procName, "glFrustum") == 0)
        return (void *)&wrap_glFrustum;
    if (strcmp(procName, "glGenBuffers") == 0)
        return (void *)&wrap_glGenBuffers;
    if (strcmp(procName, "glGenBuffersARB") == 0)
        return (void *)&wrap_glGenBuffersARB;
    if (strcmp(procName, "glGenFencesNV") == 0)
        return (void *)&wrap_glGenFencesNV;
    if (strcmp(procName, "glGenFramebuffers") == 0)
        return (void *)&wrap_glGenFramebuffers;
    if (strcmp(procName, "glGenFramebuffersEXT") == 0)
        return (void *)&wrap_glGenFramebuffersEXT;
    if (strcmp(procName, "glGenLists") == 0)
        return (void *)&wrap_glGenLists;
    if (strcmp(procName, "glGenOcclusionQueriesNV") == 0)
        return (void *)&wrap_glGenOcclusionQueriesNV;
    if (strcmp(procName, "glGenProgramsARB") == 0)
        return (void *)&wrap_glGenProgramsARB;
    if (strcmp(procName, "glGenProgramsNV") == 0)
        return (void *)&wrap_glGenProgramsNV;
    if (strcmp(procName, "glGenQueriesARB") == 0)
        return (void *)&wrap_glGenQueriesARB;
    if (strcmp(procName, "glGenRenderbuffers") == 0)
        return (void *)&wrap_glGenRenderbuffers;
    if (strcmp(procName, "glGenRenderbuffersEXT") == 0)
        return (void *)&wrap_glGenRenderbuffersEXT;
    if (strcmp(procName, "glGenTextures") == 0)
        return (void *)&wrap_glGenTextures;
    if (strcmp(procName, "glGetActiveUniform") == 0)
        return (void *)&wrap_glGetActiveUniform;
    if (strcmp(procName, "glGetAttribLocation") == 0)
        return (void *)&wrap_glGetAttribLocation;
    if (strcmp(procName, "glGetBooleanv") == 0)
        return (void *)&wrap_glGetBooleanv;
    if (strcmp(procName, "glGetBufferParameterivARB") == 0)
        return (void *)&wrap_glGetBufferParameterivARB;
    if (strcmp(procName, "glGetDoublev") == 0)
        return (void *)&wrap_glGetDoublev;
    if (strcmp(procName, "glGetError") == 0)
        return (void *)&wrap_glGetError;
    if (strcmp(procName, "glGetFloatv") == 0)
        return (void *)&wrap_glGetFloatv;
    if (strcmp(procName, "glGetInfoLogARB") == 0)
        return (void *)&wrap_glGetInfoLogARB;
    if (strcmp(procName, "glGetIntegerv") == 0)
        return (void *)&wrap_glGetIntegerv;
    if (strcmp(procName, "glGetLightfv") == 0)
        return (void *)&wrap_glGetLightfv;
    if (strcmp(procName, "glGetMaterialfv") == 0)
        return (void *)&wrap_glGetMaterialfv;
    if (strcmp(procName, "glGetObjectParameterivARB") == 0)
        return (void *)&wrap_glGetObjectParameterivARB;
    if (strcmp(procName, "glGetOcclusionQueryuivNV") == 0)
        return (void *)&wrap_glGetOcclusionQueryuivNV;
    if (strcmp(procName, "glGetProgramInfoLog") == 0)
        return (void *)&wrap_glGetProgramInfoLog;
    if (strcmp(procName, "glGetProgramiv") == 0)
        return (void *)&wrap_glGetProgramiv;
    if (strcmp(procName, "glGetProgramivARB") == 0)
        return (void *)&wrap_glGetProgramivARB;
    if (strcmp(procName, "glGetQueryObjectivARB") == 0)
        return (void *)&wrap_glGetQueryObjectivARB;
    if (strcmp(procName, "glGetQueryObjectuivARB") == 0)
        return (void *)&wrap_glGetQueryObjectuivARB;
    if (strcmp(procName, "glGetShaderInfoLog") == 0)
        return (void *)&wrap_glGetShaderInfoLog;
    if (strcmp(procName, "glGetShaderiv") == 0)
        return (void *)&wrap_glGetShaderiv;
    if (strcmp(procName, "glGetString") == 0)
        return (void *)&wrap_glGetString;
    if (strcmp(procName, "glGetTexImage") == 0)
        return (void *)&wrap_glGetTexImage;
    if (strcmp(procName, "glGetTexLevelParameteriv") == 0)
        return (void *)&wrap_glGetTexLevelParameteriv;
    if (strcmp(procName, "glGetUniformLocation") == 0)
        return (void *)&wrap_glGetUniformLocation;
    if (strcmp(procName, "glGetUniformLocationARB") == 0)
        return (void *)&wrap_glGetUniformLocationARB;
    if (strcmp(procName, "glHint") == 0)
        return (void *)&wrap_glHint;
    if (strcmp(procName, "glInterleavedArrays") == 0)
        return (void *)&wrap_glInterleavedArrays;
    if (strcmp(procName, "glIsBufferARB") == 0)
        return (void *)&wrap_glIsBufferARB;
    if (strcmp(procName, "glIsEnabled") == 0)
        return (void *)&wrap_glIsEnabled;
    if (strcmp(procName, "glIsFenceNV") == 0)
        return (void *)&wrap_glIsFenceNV;
    if (strcmp(procName, "glIsList") == 0)
        return (void *)&wrap_glIsList;
    if (strcmp(procName, "glIsProgramARB") == 0)
        return (void *)&wrap_glIsProgramARB;
    if (strcmp(procName, "glIsProgramNV") == 0)
        return (void *)&wrap_glIsProgramNV;
    if (strcmp(procName, "glIsTexture") == 0)
        return (void *)&wrap_glIsTexture;
    if (strcmp(procName, "glLightModelfv") == 0)
        return (void *)&wrap_glLightModelfv;
    if (strcmp(procName, "glLightModeli") == 0)
        return (void *)&wrap_glLightModeli;
    if (strcmp(procName, "glLightModeliv") == 0)
        return (void *)&wrap_glLightModeliv;
    if (strcmp(procName, "glLightf") == 0)
        return (void *)&wrap_glLightf;
    if (strcmp(procName, "glLightfv") == 0)
        return (void *)&wrap_glLightfv;
    if (strcmp(procName, "glLineStipple") == 0)
        return (void *)&wrap_glLineStipple;
    if (strcmp(procName, "glLineWidth") == 0)
        return (void *)&wrap_glLineWidth;
    if (strcmp(procName, "glLinkProgram") == 0)
        return (void *)&wrap_glLinkProgram;
    if (strcmp(procName, "glLinkProgramARB") == 0)
        return (void *)&wrap_glLinkProgramARB;
    if (strcmp(procName, "glLoadIdentity") == 0)
        return (void *)&wrap_glLoadIdentity;
    if (strcmp(procName, "glLoadMatrixf") == 0)
        return (void *)&wrap_glLoadMatrixf;
    if (strcmp(procName, "glLoadProgramNV") == 0)
        return (void *)&wrap_glLoadProgramNV;
    if (strcmp(procName, "glLoadTransposeMatrixf") == 0)
        return (void *)&wrap_glLoadTransposeMatrixf;
    if (strcmp(procName, "glLockArraysEXT") == 0)
        return (void *)&wrap_glLockArraysEXT;
    if (strcmp(procName, "glLogicOp") == 0)
        return (void *)&wrap_glLogicOp;
    if (strcmp(procName, "glMapBuffer") == 0)
        return (void *)&wrap_glMapBuffer;
    if (strcmp(procName, "glMapBufferARB") == 0)
        return (void *)&wrap_glMapBufferARB;
    if (strcmp(procName, "glMaterialf") == 0)
        return (void *)&wrap_glMaterialf;
    if (strcmp(procName, "glMaterialfv") == 0)
        return (void *)&wrap_glMaterialfv;
    if (strcmp(procName, "glMatrixMode") == 0)
        return (void *)&wrap_glMatrixMode;
    if (strcmp(procName, "glMultMatrixf") == 0)
        return (void *)&wrap_glMultMatrixf;
    if (strcmp(procName, "glMultTransposeMatrixf") == 0)
        return (void *)&wrap_glMultTransposeMatrixf;
    if (strcmp(procName, "glMultiDrawElements") == 0)
        return (void *)&wrap_glMultiDrawElements;
    if (strcmp(procName, "glMultiTexCoord2f") == 0)
        return (void *)&wrap_glMultiTexCoord2f;
    if (strcmp(procName, "glMultiTexCoord2fARB") == 0)
        return (void *)&wrap_glMultiTexCoord2fARB;
    if (strcmp(procName, "glMultiTexCoord2fv") == 0)
        return (void *)&wrap_glMultiTexCoord2fv;
    if (strcmp(procName, "glMultiTexCoord2fvARB") == 0)
        return (void *)&wrap_glMultiTexCoord2fvARB;
    if (strcmp(procName, "glNewList") == 0)
        return (void *)&wrap_glNewList;
    if (strcmp(procName, "glNormal3d") == 0)
        return (void *)&wrap_glNormal3d;
    if (strcmp(procName, "glNormal3dv") == 0)
        return (void *)&wrap_glNormal3dv;
    if (strcmp(procName, "glNormal3f") == 0)
        return (void *)&wrap_glNormal3f;
    if (strcmp(procName, "glNormal3fv") == 0)
        return (void *)&wrap_glNormal3fv;
    if (strcmp(procName, "glNormalPointer") == 0)
        return (void *)&wrap_glNormalPointer;
    if (strcmp(procName, "glPixelMapusv") == 0)
        return (void *)&wrap_glPixelMapusv;
    if (strcmp(procName, "glPixelStoref") == 0)
        return (void *)&wrap_glPixelStoref;
    if (strcmp(procName, "glPixelStorei") == 0)
        return (void *)&wrap_glPixelStorei;
    if (strcmp(procName, "glPixelTransferf") == 0)
        return (void *)&wrap_glPixelTransferf;
    if (strcmp(procName, "glPixelTransferi") == 0)
        return (void *)&wrap_glPixelTransferi;
    if (strcmp(procName, "glPointParameterf") == 0)
        return (void *)&wrap_glPointParameterf;
    if (strcmp(procName, "glPointParameterfARB") == 0)
        return (void *)&wrap_glPointParameterfARB;
    if (strcmp(procName, "glPointParameterfv") == 0)
        return (void *)&wrap_glPointParameterfv;
    if (strcmp(procName, "glPointSize") == 0)
        return (void *)&wrap_glPointSize;
    if (strcmp(procName, "glPolygonMode") == 0)
        return (void *)&wrap_glPolygonMode;
    if (strcmp(procName, "glPolygonOffset") == 0)
        return (void *)&wrap_glPolygonOffset;
    if (strcmp(procName, "glPopAttrib") == 0)
        return (void *)&wrap_glPopAttrib;
    if (strcmp(procName, "glPopClientAttrib") == 0)
        return (void *)&wrap_glPopClientAttrib;
    if (strcmp(procName, "glPopMatrix") == 0)
        return (void *)&wrap_glPopMatrix;
    if (strcmp(procName, "glPrimitiveRestartIndexNV") == 0)
        return (void *)&wrap_glPrimitiveRestartIndexNV;
    if (strcmp(procName, "glProgramEnvParameter4fARB") == 0)
        return (void *)&wrap_glProgramEnvParameter4fARB;
    if (strcmp(procName, "glProgramEnvParameter4fvARB") == 0)
        return (void *)&wrap_glProgramEnvParameter4fvARB;
    if (strcmp(procName, "glProgramEnvParameters4fvEXT") == 0)
        return (void *)&wrap_glProgramEnvParameters4fvEXT;
    if (strcmp(procName, "glProgramLocalParameter4fARB") == 0)
        return (void *)&wrap_glProgramLocalParameter4fARB;
    if (strcmp(procName, "glProgramLocalParameter4fvARB") == 0)
        return (void *)&wrap_glProgramLocalParameter4fvARB;
    if (strcmp(procName, "glProgramLocalParameters4fvEXT") == 0)
        return (void *)&wrap_glProgramLocalParameters4fvEXT;
    if (strcmp(procName, "glProgramParameter4fNV") == 0)
        return (void *)&wrap_glProgramParameter4fNV;
    if (strcmp(procName, "glProgramParameter4fvNV") == 0)
        return (void *)&wrap_glProgramParameter4fvNV;
    if (strcmp(procName, "glProgramParameters4fvNV") == 0)
        return (void *)&wrap_glProgramParameters4fvNV;
    if (strcmp(procName, "glProgramStringARB") == 0)
        return (void *)&wrap_glProgramStringARB;
    if (strcmp(procName, "glPushAttrib") == 0)
        return (void *)&wrap_glPushAttrib;
    if (strcmp(procName, "glPushClientAttrib") == 0)
        return (void *)&wrap_glPushClientAttrib;
    if (strcmp(procName, "glPushMatrix") == 0)
        return (void *)&wrap_glPushMatrix;
    if (strcmp(procName, "glRasterPos2f") == 0)
        return (void *)&wrap_glRasterPos2f;
    if (strcmp(procName, "glRasterPos2i") == 0)
        return (void *)&wrap_glRasterPos2i;
    if (strcmp(procName, "glRasterPos3f") == 0)
        return (void *)&wrap_glRasterPos3f;
    if (strcmp(procName, "glReadBuffer") == 0)
        return (void *)&wrap_glReadBuffer;
    if (strcmp(procName, "glReadPixels") == 0)
        return (void *)&wrap_glReadPixels;
    if (strcmp(procName, "glRectf") == 0)
        return (void *)&wrap_glRectf;
    if (strcmp(procName, "glRecti") == 0)
        return (void *)&wrap_glRecti;
    if (strcmp(procName, "glRenderbufferStorage") == 0)
        return (void *)&wrap_glRenderbufferStorage;
    if (strcmp(procName, "glRenderbufferStorageEXT") == 0)
        return (void *)&wrap_glRenderbufferStorageEXT;
    if (strcmp(procName, "glRotatef") == 0)
        return (void *)&wrap_glRotatef;
    if (strcmp(procName, "glScalef") == 0)
        return (void *)&wrap_glScalef;
    if (strcmp(procName, "glScissor") == 0)
        return (void *)&wrap_glScissor;
    if (strcmp(procName, "glSecondaryColor3fv") == 0)
        return (void *)&wrap_glSecondaryColor3fv;
    if (strcmp(procName, "glSecondaryColor3ub") == 0)
        return (void *)&wrap_glSecondaryColor3ub;
    if (strcmp(procName, "glSecondaryColorPointer") == 0)
        return (void *)&wrap_glSecondaryColorPointer;
    if (strcmp(procName, "glSetFenceNV") == 0)
        return (void *)&wrap_glSetFenceNV;
    if (strcmp(procName, "glShadeModel") == 0)
        return (void *)&wrap_glShadeModel;
    if (strcmp(procName, "glShaderSource") == 0)
        return (void *)&wrap_glShaderSource;
    if (strcmp(procName, "glShaderSourceARB") == 0)
        return (void *)&wrap_glShaderSourceARB;
    if (strcmp(procName, "glStencilFunc") == 0)
        return (void *)&wrap_glStencilFunc;
    if (strcmp(procName, "glStencilMask") == 0)
        return (void *)&wrap_glStencilMask;
    if (strcmp(procName, "glStencilOp") == 0)
        return (void *)&wrap_glStencilOp;
    if (strcmp(procName, "glTexCoord2f") == 0)
        return (void *)&wrap_glTexCoord2f;
    if (strcmp(procName, "glTexCoord2fv") == 0)
        return (void *)&wrap_glTexCoord2fv;
    if (strcmp(procName, "glTexCoord3f") == 0)
        return (void *)&wrap_glTexCoord3f;
    if (strcmp(procName, "glTexCoord4f") == 0)
        return (void *)&wrap_glTexCoord4f;
    if (strcmp(procName, "glTexEnvf") == 0)
        return (void *)&wrap_glTexEnvf;
    if (strcmp(procName, "glTexEnvfv") == 0)
        return (void *)&wrap_glTexEnvfv;
    if (strcmp(procName, "glTexEnvi") == 0)
        return (void *)&wrap_glTexEnvi;
    if (strcmp(procName, "glTexEnviv") == 0)
        return (void *)&wrap_glTexEnviv;
    if (strcmp(procName, "glTexGenfv") == 0)
        return (void *)&wrap_glTexGenfv;
    if (strcmp(procName, "glTexGeni") == 0)
        return (void *)&wrap_glTexGeni;
    if (strcmp(procName, "glTexParameterf") == 0)
        return (void *)&wrap_glTexParameterf;
    if (strcmp(procName, "glTexParameterfv") == 0)
        return (void *)&wrap_glTexParameterfv;
    if (strcmp(procName, "glTrackMatrixNV") == 0)
        return (void *)&wrap_glTrackMatrixNV;
    if (strcmp(procName, "glTranslated") == 0)
        return (void *)&wrap_glTranslated;
    if (strcmp(procName, "glTranslatef") == 0)
        return (void *)&wrap_glTranslatef;
    if (strcmp(procName, "glUniform1f") == 0)
        return (void *)&wrap_glUniform1f;
    if (strcmp(procName, "glUniform1fARB") == 0)
        return (void *)&wrap_glUniform1fARB;
    if (strcmp(procName, "glUniform1fv") == 0)
        return (void *)&wrap_glUniform1fv;
    if (strcmp(procName, "glUniform1fvARB") == 0)
        return (void *)&wrap_glUniform1fvARB;
    if (strcmp(procName, "glUniform1i") == 0)
        return (void *)&wrap_glUniform1i;
    if (strcmp(procName, "glUniform1iARB") == 0)
        return (void *)&wrap_glUniform1iARB;
    if (strcmp(procName, "glUniform1iv") == 0)
        return (void *)&wrap_glUniform1iv;
    if (strcmp(procName, "glUniform2fv") == 0)
        return (void *)&wrap_glUniform2fv;
    if (strcmp(procName, "glUniform2iv") == 0)
        return (void *)&wrap_glUniform2iv;
    if (strcmp(procName, "glUniform3f") == 0)
        return (void *)&wrap_glUniform3f;
    if (strcmp(procName, "glUniform3fv") == 0)
        return (void *)&wrap_glUniform3fv;
    if (strcmp(procName, "glUniform3iv") == 0)
        return (void *)&wrap_glUniform3iv;
    if (strcmp(procName, "glUniform4fv") == 0)
        return (void *)&wrap_glUniform4fv;
    if (strcmp(procName, "glUniform4fvARB") == 0)
        return (void *)&wrap_glUniform4fvARB;
    if (strcmp(procName, "glUniform4iv") == 0)
        return (void *)&wrap_glUniform4iv;
    if (strcmp(procName, "glUniformMatrix2fv") == 0)
        return (void *)&wrap_glUniformMatrix2fv;
    if (strcmp(procName, "glUniformMatrix3fv") == 0)
        return (void *)&wrap_glUniformMatrix3fv;
    if (strcmp(procName, "glUniformMatrix3fvARB") == 0)
        return (void *)&wrap_glUniformMatrix3fvARB;
    if (strcmp(procName, "glUniformMatrix4fv") == 0)
        return (void *)&wrap_glUniformMatrix4fv;
    if (strcmp(procName, "glUniformMatrix4fvARB") == 0)
        return (void *)&wrap_glUniformMatrix4fvARB;
    if (strcmp(procName, "glUnlockArraysEXT") == 0)
        return (void *)&wrap_glUnlockArraysEXT;
    if (strcmp(procName, "glUnmapBuffer") == 0)
        return (void *)&wrap_glUnmapBuffer;
    if (strcmp(procName, "glUnmapBufferARB") == 0)
        return (void *)&wrap_glUnmapBufferARB;
    if (strcmp(procName, "glUseProgram") == 0)
        return (void *)&wrap_glUseProgram;
    if (strcmp(procName, "glUseProgramObjectARB") == 0)
        return (void *)&wrap_glUseProgramObjectARB;
    if (strcmp(procName, "glVertex2d") == 0)
        return (void *)&wrap_glVertex2d;
    if (strcmp(procName, "glVertex2dv") == 0)
        return (void *)&wrap_glVertex2dv;
    if (strcmp(procName, "glVertex2f") == 0)
        return (void *)&wrap_glVertex2f;
    if (strcmp(procName, "glVertex2i") == 0)
        return (void *)&wrap_glVertex2i;
    if (strcmp(procName, "glVertex3dv") == 0)
        return (void *)&wrap_glVertex3dv;
    if (strcmp(procName, "glVertex3f") == 0)
        return (void *)&wrap_glVertex3f;
    if (strcmp(procName, "glVertex3fv") == 0)
        return (void *)&wrap_glVertex3fv;
    if (strcmp(procName, "glVertex4dv") == 0)
        return (void *)&wrap_glVertex4dv;
    if (strcmp(procName, "glVertex4f") == 0)
        return (void *)&wrap_glVertex4f;
    if (strcmp(procName, "glVertexAttrib1f") == 0)
        return (void *)&wrap_glVertexAttrib1f;
    if (strcmp(procName, "glVertexAttrib3fARB") == 0)
        return (void *)&wrap_glVertexAttrib3fARB;
    if (strcmp(procName, "glVertexAttribPointer") == 0)
        return (void *)&wrap_glVertexAttribPointer;
    if (strcmp(procName, "glVertexAttribPointerARB") == 0)
        return (void *)&wrap_glVertexAttribPointerARB;
    if (strcmp(procName, "glWindowPos2sARB") == 0)
        return (void *)&wrap_glWindowPos2sARB;
    if (strcmp(procName, "glTexImage1D") == 0)
        return (void *)&wrap_glTexImage1D;
    if (strcmp(procName, "glBeginQuery") == 0)
        return (void *)&wrap_glBeginQuery;
    if (strcmp(procName, "glEndQuery") == 0)
        return (void *)&wrap_glEndQuery;
    if (strcmp(procName, "glGetQueryObjectiv") == 0)
        return (void *)&wrap_glGetQueryObjectiv;
    if (strcmp(procName, "glCompressedTexSubImage2D") == 0)
        return (void *)&wrap_glCompressedTexSubImage2D;
    if (strcmp(procName, "glGetCompressedTexImage") == 0)
        return (void *)&wrap_glGetCompressedTexImage;
    if (strcmp(procName, "glGetQueryObjectuiv") == 0)
        return (void *)&wrap_glGetQueryObjectuiv;
    if (strcmp(procName, "glGetProgramEnvParameterfvARB") == 0)
        return (void *)&wrap_glGetProgramEnvParameterfvARB;
    if (strcmp(procName, "glGetTexParameteriv") == 0)
        return (void *)&wrap_glGetTexParameteriv;
    if (strcmp(procName, "glMultiTexCoord4f") == 0)
        return (void *)&wrap_glMultiTexCoord4f;
    if (strcmp(procName, "glVertexAttrib4fv") == 0)
        return (void *)&wrap_glVertexAttrib4fv;
    if (strcmp(procName, "glDeleteQueries") == 0)
        return (void *)&wrap_glDeleteQueries;
    if (strcmp(procName, "glPointParameteri") == 0)
        return (void *)&wrap_glPointParameteri;
    if (strcmp(procName, "glGenQueries") == 0)
        return (void *)&wrap_glGenQueries;
    if (strcmp(procName, "glSecondaryColor3f") == 0)
        return (void *)&wrap_glSecondaryColor3f;
    if (strcmp(procName, "glUniform3fvARB") == 0)
        return (void *)&wrap_glUniform3fvARB;
    if (strcmp(procName, "glDrawPixels") == 0)
        return (void *)&wrap_glDrawPixels;
    if (strcmp(procName, "glGetTexEnvfv") == 0)
        return (void *)&wrap_glGetTexEnvfv;
    if (strcmp(procName, "glGetTexEnviv") == 0)
        return (void *)&wrap_glGetTexEnviv;
    if (strcmp(procName, "glGetTexParameterfv") == 0)
        return (void *)&wrap_glGetTexParameterfv;
    if (strcmp(procName, "glMultMatrixd") == 0)
        return (void *)&wrap_glMultMatrixd;
    if (strcmp(procName, "glGenerateMipmapEXT") == 0)
        return (void *)&wrap_glGenerateMipmapEXT;        
    if (strcmp(procName, "glBufferSubData") == 0)
        return (void *)&wrap_glBufferSubData;
    if (strcmp(procName, "glTexCoord2d") == 0)
        return (void *)&wrap_glTexCoord2d;
    void *proc = (void *)SDL_GL_GetProcAddress(procName);
    if (proc)
        return proc;

    return NULL;
}