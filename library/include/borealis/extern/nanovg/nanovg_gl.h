//
// Copyright (c) 2009-2013 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//
#ifndef NANOVG_GL_H
#define NANOVG_GL_H

#ifdef __cplusplus
extern "C" {
#endif

// Create flags

enum NVGcreateFlags {
	// Flag indicating if geometry based anti-aliasing is used (may not be needed when using MSAA).
	NVG_ANTIALIAS 		= 1<<0,
	// Flag indicating if strokes should be drawn using stencil buffer. The rendering will be a little
	// slower, but path overlaps (i.e. self-intersecting or sharp turns) will be drawn just once.
	NVG_STENCIL_STROKES	= 1<<1,
	// Flag indicating that additional debug checks are done.
	NVG_DEBUG 			= 1<<2,
};

#if defined NANOVG_GL2_IMPLEMENTATION
#  define NANOVG_GL2 1
#  define NANOVG_GL_IMPLEMENTATION 1
#elif defined NANOVG_GL3_IMPLEMENTATION
#  define NANOVG_GL3 1
#  define NANOVG_GL_IMPLEMENTATION 1
#  define NANOVG_GL_USE_UNIFORMBUFFER 1
#elif defined NANOVG_GLES2_IMPLEMENTATION
#  define NANOVG_GLES2 1
#  define NANOVG_GL_IMPLEMENTATION 1
#elif defined NANOVG_GLES3_IMPLEMENTATION
#  define NANOVG_GLES3 1
#  define NANOVG_GL_IMPLEMENTATION 1
#endif

#define NANOVG_GL_USE_STATE_FILTER (1)

// Creates NanoVG contexts for different OpenGL (ES) versions.
// Flags should be combination of the create flags above.

#if defined NANOVG_GL2

NVGcontext* nvgCreateGL2(int flags);
void nvgDeleteGL2(NVGcontext* ctx);

int nvglCreateImageFromHandleGL2(NVGcontext* ctx, GLuint textureId, int w, int h, int flags);
GLuint nvglImageHandleGL2(NVGcontext* ctx, int image);

#endif

#if defined NANOVG_GL3

NVGcontext* nvgCreateGL3(int flags);
void nvgDeleteGL3(NVGcontext* ctx);

int nvglCreateImageFromHandleGL3(NVGcontext* ctx, GLuint textureId, int w, int h, int flags);
GLuint nvglImageHandleGL3(NVGcontext* ctx, int image);
#if defined(PS5_NATIVE_GPU)
// draw calls queued for the next flush (nvgEndFrame).
int nvglPendingCallCountGL3(NVGcontext* ctx);
#endif
#if defined(PS5_NATIVE_GPU)
// bounds (minX, minY, maxX, maxY) of every queued vertex and the
// view size, in NanoVG units. Returns 0 when nothing is queued.
int nvglPendingBoundsGL3(NVGcontext* ctx, float bounds[4], float view[2]);
#endif
#if defined(PS5_NATIVE_GPU)
// PQ-encoded output for drawing straight into the ten-bit window.
void nvglSetHdrPqOutputGL3(NVGcontext* ctx, int enabled);
// True when no queued call needs the stencil buffer, so the window's own
// stencil contents cannot affect the result.
int nvglPendingStencilFreeGL3(NVGcontext* ctx);
#endif

#endif

#if defined NANOVG_GLES2

NVGcontext* nvgCreateGLES2(int flags);
void nvgDeleteGLES2(NVGcontext* ctx);

int nvglCreateImageFromHandleGLES2(NVGcontext* ctx, GLuint textureId, int w, int h, int flags);
GLuint nvglImageHandleGLES2(NVGcontext* ctx, int image);

#endif

#if defined NANOVG_GLES3

NVGcontext* nvgCreateGLES3(int flags);
void nvgDeleteGLES3(NVGcontext* ctx);

int nvglCreateImageFromHandleGLES3(NVGcontext* ctx, GLuint textureId, int w, int h, int flags);
GLuint nvglImageHandleGLES3(NVGcontext* ctx, int image);

#endif

// These are additional flags on top of NVGimageFlags.
enum NVGimageFlagsGL {
	NVG_IMAGE_NODELETE			= 1<<16,	// Do not delete GL texture handle.
};

#ifdef PS4
#include <nanovg_ps4.h>
#endif

#ifdef __cplusplus
}
#endif

#endif /* NANOVG_GL_H */

#ifdef NANOVG_GL_IMPLEMENTATION

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "nanovg.h"

enum GLNVGuniformLoc {
	GLNVG_LOC_VIEWSIZE,
	GLNVG_LOC_TEX,
	GLNVG_LOC_FRAG,
#if defined(PS5_NATIVE_GPU)
	GLNVG_LOC_HDRPQ,
#endif
	GLNVG_MAX_LOCS
};

enum GLNVGshaderType {
	NSVG_SHADER_FILLGRAD,
	NSVG_SHADER_FILLIMG,
	NSVG_SHADER_SIMPLE,
	NSVG_SHADER_IMG
};

#if NANOVG_GL_USE_UNIFORMBUFFER
enum GLNVGuniformBindings {
	GLNVG_FRAG_BINDING = 0,
};
#endif

struct GLNVGshader {
	GLuint prog;
	GLuint frag;
	GLuint vert;
	GLint loc[GLNVG_MAX_LOCS];
};
typedef struct GLNVGshader GLNVGshader;

struct GLNVGtexture {
	int id;
	GLuint tex;
	int width, height;
	int type;
	int flags;
};
typedef struct GLNVGtexture GLNVGtexture;

struct GLNVGblend
{
	GLenum srcRGB;
	GLenum dstRGB;
	GLenum srcAlpha;
	GLenum dstAlpha;
};
typedef struct GLNVGblend GLNVGblend;

enum GLNVGcallType {
	GLNVG_NONE = 0,
	GLNVG_FILL,
	GLNVG_CONVEXFILL,
	GLNVG_STROKE,
	GLNVG_TRIANGLES,
	GLNVG_CONVEXFILL_STENCIL,
	GLNVG_CONVEXFILL_STENCIL_CLEAR,
};

struct GLNVGcall {
	int type;
	int image;
	int pathOffset;
	int pathCount;
	int triangleOffset;
	int triangleCount;
	int uniformOffset;
	GLNVGblend blendFunc;
#ifdef PS5_NATIVE_GPU
	int nativeTriangles;
	int nativeRing;
	int nativeStrokeOffset, nativeStrokeCount;
#endif
};
typedef struct GLNVGcall GLNVGcall;

struct GLNVGpath {
	int fillOffset;
	int fillCount;
	int strokeOffset;
	int strokeCount;
};
typedef struct GLNVGpath GLNVGpath;

struct GLNVGfragUniforms {
	#if NANOVG_GL_USE_UNIFORMBUFFER
		float scissorMat[12]; // matrices are actually 3 vec4s
		float paintMat[12];
		struct NVGcolor innerCol;
		struct NVGcolor outerCol;
		float scissorExt[2];
		float scissorScale[2];
		float extent[2];
		float radius;
		float feather;
		float strokeMult;
		float strokeThr;
		int texType;
		int type;
	#else
		// note: after modifying layout or size of uniform array,
		// don't forget to also update the fragment shader source!
		#define NANOVG_GL_UNIFORMARRAY_SIZE 11
		union {
			struct {
				float scissorMat[12]; // matrices are actually 3 vec4s
				float paintMat[12];
				struct NVGcolor innerCol;
				struct NVGcolor outerCol;
				float scissorExt[2];
				float scissorScale[2];
				float extent[2];
				float radius;
				float feather;
				float strokeMult;
				float strokeThr;
				float texType;
				float type;
			};
			float uniformArray[NANOVG_GL_UNIFORMARRAY_SIZE][4];
		};
	#endif
};
typedef struct GLNVGfragUniforms GLNVGfragUniforms;

struct GLNVGcontext {
#if defined(PS5_NATIVE_GPU)
	/* encode UI output as PQ for direct window drawing. */
	int hdrPqOutput;
#endif
#ifdef PS5_NATIVE_GPU
	int nativeExpandedVerts;
#endif
	GLNVGshader shader;
	GLNVGtexture* textures;
	float view[2];
	int ntextures;
	int ctextures;
	int textureId;
	GLuint vertBuf;
#if defined NANOVG_GL3
	GLuint vertArr;
#endif
#if NANOVG_GL_USE_UNIFORMBUFFER
	GLuint fragBuf;
#endif
	int fragSize;
	int flags;

	// Per frame buffers
	GLNVGcall* calls;
	int ccalls;
	int ncalls;
	GLNVGpath* paths;
	int cpaths;
	int npaths;
	struct NVGvertex* verts;
	int cverts;
	int nverts;
	unsigned char* uniforms;
	int cuniforms;
	int nuniforms;

	// cached state
	#if NANOVG_GL_USE_STATE_FILTER
	GLuint boundTexture;
	GLuint stencilMask;
	GLenum stencilFunc;
	GLint stencilFuncRef;
	GLuint stencilFuncMask;
	GLNVGblend blendFunc;
	#endif

	int dummyTex;
#ifdef PS5_NATIVE_GPU
	GLenum textureUpdateError, textureUpdateCleanupError;
	int textureUpdateStage;
	unsigned int textureUpdateReports;
	int textureBindingUnknown;
#endif
};
typedef struct GLNVGcontext GLNVGcontext;

static int glnvg__maxi(int a, int b) { return a > b ? a : b; }

#ifdef NANOVG_GLES2
static unsigned int glnvg__nearestPow2(unsigned int num)
{
	unsigned n = num > 0 ? num - 1 : 0;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n++;
	return n;
}
#endif

static void glnvg__bindTexture(GLNVGcontext* gl, GLuint tex)
{
#if NANOVG_GL_USE_STATE_FILTER
	if (
#ifdef PS5_NATIVE_GPU
		gl->textureBindingUnknown ||
#endif
		gl->boundTexture != tex) {
		gl->boundTexture = tex;
		glBindTexture(GL_TEXTURE_2D, tex);
	}
#else
	glBindTexture(GL_TEXTURE_2D, tex);
#endif
}

static void glnvg__stencilMask(GLNVGcontext* gl, GLuint mask)
{
#if NANOVG_GL_USE_STATE_FILTER
	if (gl->stencilMask != mask) {
		gl->stencilMask = mask;
		glStencilMask(mask);
	}
#else
	glStencilMask(mask);
#endif
}

static void glnvg__stencilFunc(GLNVGcontext* gl, GLenum func, GLint ref, GLuint mask)
{
#if NANOVG_GL_USE_STATE_FILTER
	if ((gl->stencilFunc != func) ||
		(gl->stencilFuncRef != ref) ||
		(gl->stencilFuncMask != mask)) {

		gl->stencilFunc = func;
		gl->stencilFuncRef = ref;
		gl->stencilFuncMask = mask;
		glStencilFunc(func, ref, mask);
	}
#else
	glStencilFunc(func, ref, mask);
#endif
}
static void glnvg__blendFuncSeparate(GLNVGcontext* gl, const GLNVGblend* blend)
{
#if NANOVG_GL_USE_STATE_FILTER
	if ((gl->blendFunc.srcRGB != blend->srcRGB) ||
		(gl->blendFunc.dstRGB != blend->dstRGB) ||
		(gl->blendFunc.srcAlpha != blend->srcAlpha) ||
		(gl->blendFunc.dstAlpha != blend->dstAlpha)) {

		gl->blendFunc = *blend;
		glBlendFuncSeparate(blend->srcRGB, blend->dstRGB, blend->srcAlpha,blend->dstAlpha);
	}
#else
	glBlendFuncSeparate(blend->srcRGB, blend->dstRGB, blend->srcAlpha,blend->dstAlpha);
#endif
}

static GLNVGtexture* glnvg__allocTexture(GLNVGcontext* gl)
{
	GLNVGtexture* tex = NULL;
	int i;

	for (i = 0; i < gl->ntextures; i++) {
		if (gl->textures[i].id == 0) {
			tex = &gl->textures[i];
			break;
		}
	}
	if (tex == NULL) {
		if (gl->ntextures+1 > gl->ctextures) {
			GLNVGtexture* textures;
			int ctextures = glnvg__maxi(gl->ntextures+1, 4) +  gl->ctextures/2; // 1.5x Overallocate
			textures = (GLNVGtexture*)realloc(gl->textures, sizeof(GLNVGtexture)*ctextures);
			if (textures == NULL) return NULL;
			gl->textures = textures;
			gl->ctextures = ctextures;
		}
		tex = &gl->textures[gl->ntextures++];
	}

	memset(tex, 0, sizeof(*tex));
	tex->id = ++gl->textureId;

	return tex;
}

static GLNVGtexture* glnvg__findTexture(GLNVGcontext* gl, int id)
{
	int i;
	for (i = 0; i < gl->ntextures; i++)
		if (gl->textures[i].id == id)
			return &gl->textures[i];
	return NULL;
}

static int glnvg__deleteTexture(GLNVGcontext* gl, int id)
{
	int i;
	for (i = 0; i < gl->ntextures; i++) {
		if (gl->textures[i].id == id) {
			if (gl->textures[i].tex != 0 && (gl->textures[i].flags & NVG_IMAGE_NODELETE) == 0)
				glDeleteTextures(1, &gl->textures[i].tex);
			memset(&gl->textures[i], 0, sizeof(gl->textures[i]));
			return 1;
		}
	}
	return 0;
}

static void glnvg__dumpShaderError(GLuint shader, const char* name, const char* type)
{
	GLchar str[512+1];
	GLsizei len = 0;
	glGetShaderInfoLog(shader, 512, &len, str);
	if (len > 512) len = 512;
	str[len] = '\0';
	printf("Shader %s/%s error:\n%s\n", name, type, str);
}

static void glnvg__dumpProgramError(GLuint prog, const char* name)
{
	GLchar str[512+1];
	GLsizei len = 0;
	glGetProgramInfoLog(prog, 512, &len, str);
	if (len > 512) len = 512;
	str[len] = '\0';
	printf("Program %s error:\n%s\n", name, str);
}

static void glnvg__checkError(GLNVGcontext* gl, const char* str)
{
	GLenum err;
	if ((gl->flags & NVG_DEBUG) == 0) return;
	err = glGetError();
	if (err != GL_NO_ERROR) {
		printf("Error %08x after %s\n", err, str);
		return;
	}
}

static int glnvg__createShader(GLNVGshader* shader, const char* name, const char* header, const char* opts, const char* vshader, const char* fshader)
{
	GLint status;
	GLuint prog, vert, frag;
	const char* str[3];
	str[0] = header;
	str[1] = opts != NULL ? opts : "";

	memset(shader, 0, sizeof(*shader));

	prog = glCreateProgram();
	vert = glCreateShader(GL_VERTEX_SHADER);
	frag = glCreateShader(GL_FRAGMENT_SHADER);
#ifdef PS4
        glShaderBinary(1, &vert, 2, PS4_SHADER_VERT, PS4_SHADER_VERT_LENGTH);
        glShaderBinary(1, &frag, 2, PS4_SHADER_FRAG, PS4_SHADER_FRAG_LENGTH);
#else
	str[2] = vshader;
	glShaderSource(vert, 3, str, 0);
	str[2] = fshader;
	glShaderSource(frag, 3, str, 0);

	glCompileShader(vert);
	glGetShaderiv(vert, GL_COMPILE_STATUS, &status);
	if (status != GL_TRUE) {
		glnvg__dumpShaderError(vert, name, "vert");
		return 0;
	}

	glCompileShader(frag);
	glGetShaderiv(frag, GL_COMPILE_STATUS, &status);
	if (status != GL_TRUE) {
		glnvg__dumpShaderError(frag, name, "frag");
		return 0;
	}
#endif

	glAttachShader(prog, vert);
	glAttachShader(prog, frag);

	glBindAttribLocation(prog, 0, "vertex");
	glBindAttribLocation(prog, 1, "tcoord");

	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &status);
	if (status != GL_TRUE) {
		glnvg__dumpProgramError(prog, name);
		return 0;
	}

	shader->prog = prog;
	shader->vert = vert;
	shader->frag = frag;

	return 1;
}

static void glnvg__deleteShader(GLNVGshader* shader)
{
	if (shader->prog != 0)
		glDeleteProgram(shader->prog);
	if (shader->vert != 0)
		glDeleteShader(shader->vert);
	if (shader->frag != 0)
		glDeleteShader(shader->frag);
}

static void glnvg__getUniforms(GLNVGshader* shader)
{
	shader->loc[GLNVG_LOC_VIEWSIZE] = glGetUniformLocation(shader->prog, "viewSize");
	shader->loc[GLNVG_LOC_TEX] = glGetUniformLocation(shader->prog, "tex");
#if defined(PS5_NATIVE_GPU)
	shader->loc[GLNVG_LOC_HDRPQ] = glGetUniformLocation(shader->prog, "hdrUiPq");
#endif

#if NANOVG_GL_USE_UNIFORMBUFFER
	shader->loc[GLNVG_LOC_FRAG] = glGetUniformBlockIndex(shader->prog, "frag");
#else
	shader->loc[GLNVG_LOC_FRAG] = glGetUniformLocation(shader->prog, "frag");
#endif
}

static int glnvg__renderCreateTexture(void* uptr, int type, int w, int h, int imageFlags, const unsigned char* data);

static int glnvg__renderCreate(void* uptr)
{
	GLNVGcontext* gl = (GLNVGcontext*)uptr;
	int align = 4;

	// TODO: mediump float may not be enough for GLES2 in iOS.
	// see the following discussion: https://github.com/memononen/nanovg/issues/46
	static const char* shaderHeader =
#if defined NANOVG_GL2
		"#define NANOVG_GL2 1\n"
#elif defined NANOVG_GL3
		"#version 150 core\n"
		"#define NANOVG_GL3 1\n"
#elif defined NANOVG_GLES2
		"#version 100\n"
		"#define NANOVG_GL2 1\n"
#elif defined NANOVG_GLES3
		"#version 300 es\n"
		"#define NANOVG_GL3 1\n"
#endif

#if NANOVG_GL_USE_UNIFORMBUFFER
	"#define USE_UNIFORMBUFFER 1\n"
#else
	"#define UNIFORMARRAY_SIZE 11\n"
#endif
	"\n";

	static const char* fillVertShader =
		"#ifdef NANOVG_GL3\n"
		"	uniform vec2 viewSize;\n"
		"	in vec2 vertex;\n"
		"	in vec2 tcoord;\n"
		"	out vec2 ftcoord;\n"
		"	out vec2 fpos;\n"
		"#else\n"
		"	uniform vec2 viewSize;\n"
		"	attribute vec2 vertex;\n"
		"	attribute vec2 tcoord;\n"
		"	varying vec2 ftcoord;\n"
		"	varying vec2 fpos;\n"
		"#endif\n"
		"void main(void) {\n"
		"	ftcoord = tcoord;\n"
		"	fpos = vertex;\n"
		"	gl_Position = vec4(2.0*vertex.x/viewSize.x - 1.0, 1.0 - 2.0*vertex.y/viewSize.y, 0, 1);\n"
		"}\n";

	static const char* fillFragShader =
		"#ifdef GL_ES\n"
		"#if defined(GL_FRAGMENT_PRECISION_HIGH) || defined(NANOVG_GL3)\n"
		" precision highp float;\n"
		"#else\n"
		" precision mediump float;\n"
		"#endif\n"
		"#endif\n"
		"#ifdef NANOVG_GL3\n"
		"#ifdef USE_UNIFORMBUFFER\n"
		"	layout(std140) uniform frag {\n"
		"		mat3 scissorMat;\n"
		"		mat3 paintMat;\n"
		"		vec4 innerCol;\n"
		"		vec4 outerCol;\n"
		"		vec2 scissorExt;\n"
		"		vec2 scissorScale;\n"
		"		vec2 extent;\n"
		"		float radius;\n"
		"		float feather;\n"
		"		float strokeMult;\n"
		"		float strokeThr;\n"
		"		int texType;\n"
		"		int type;\n"
		"	};\n"
		"#else\n" // NANOVG_GL3 && !USE_UNIFORMBUFFER
		"	uniform vec4 frag[UNIFORMARRAY_SIZE];\n"
		"#endif\n"
		"	uniform sampler2D tex;\n"
		"	in vec2 ftcoord;\n"
		"	in vec2 fpos;\n"
		"	out vec4 outColor;\n"
		"#else\n" // !NANOVG_GL3
		"	uniform vec4 frag[UNIFORMARRAY_SIZE];\n"
		"	uniform sampler2D tex;\n"
		"	varying vec2 ftcoord;\n"
		"	varying vec2 fpos;\n"
		"#endif\n"
		"#ifndef USE_UNIFORMBUFFER\n"
		"	#define scissorMat mat3(frag[0].xyz, frag[1].xyz, frag[2].xyz)\n"
		"	#define paintMat mat3(frag[3].xyz, frag[4].xyz, frag[5].xyz)\n"
		"	#define innerCol frag[6]\n"
		"	#define outerCol frag[7]\n"
		"	#define scissorExt frag[8].xy\n"
		"	#define scissorScale frag[8].zw\n"
		"	#define extent frag[9].xy\n"
		"	#define radius frag[9].z\n"
		"	#define feather frag[9].w\n"
		"	#define strokeMult frag[10].x\n"
		"	#define strokeThr frag[10].y\n"
		"	#define texType int(frag[10].z)\n"
		"	#define type int(frag[10].w)\n"
		"#endif\n"
		"\n"
		"float sdroundrect(vec2 pt, vec2 ext, float rad) {\n"
		"	vec2 ext2 = ext - vec2(rad,rad);\n"
		"	vec2 d = abs(pt) - ext2;\n"
		"	return min(max(d.x,d.y),0.0) + length(max(d,0.0)) - rad;\n"
		"}\n"
		"\n"
		"// Scissoring\n"
		"float scissorMask(vec2 p) {\n"
		"	vec2 sc = (abs((scissorMat * vec3(p,1.0)).xy) - scissorExt);\n"
		"	sc = vec2(0.5,0.5) - sc * scissorScale;\n"
		"	return clamp(sc.x,0.0,1.0) * clamp(sc.y,0.0,1.0);\n"
		"}\n"
		"#ifdef EDGE_AA\n"
		"// Stroke - from [0..1] to clipped pyramid, where the slope is 1px.\n"
		"float strokeMask() {\n"
		"	return min(1.0, (1.0-abs(ftcoord.x*2.0-1.0))*strokeMult) * min(1.0, ftcoord.y);\n"
		"}\n"
		"#endif\n"
		"\n"
#ifdef PS5_NATIVE_HDR
		"// Applied after NanoVG's original encoded paint/image evaluation and coverage.\n"
		"// RGB is linear BT.2020 absolute nits, premultiplied by alpha for source-over.\n"
		"const float hdrUiPaperWhite = 203.0 / 10000.0;\n"
		"vec4 hdrUiTransfer(vec4 encoded) {\n"
		"    if (encoded.a <= 0.0) return vec4(0.0);\n"
		"    vec3 s = clamp(encoded.rgb / encoded.a, 0.0, 1.0);\n"
		"    vec3 l = mix(s / 12.92, pow((s + 0.055) / 1.055, vec3(2.4)),\n"
		"                 greaterThan(s, vec3(0.04045)));\n"
		"    vec3 bt2020 = vec3(\n"
		"        dot(l, vec3(0.6274040, 0.3292820, 0.0433136)),\n"
		"        dot(l, vec3(0.0690970, 0.9195400, 0.0113612)),\n"
		"        dot(l, vec3(0.0163916, 0.0880132, 0.8955950)));\n"
		"    return vec4(bt2020 * hdrUiPaperWhite * encoded.a, encoded.a);\n"
		"}\n"
#if defined(PS5_NATIVE_GPU)
		"// the same colour, encoded PQ for drawing straight into the window.\n"
		"uniform int hdrUiPq;\n"
		"vec4 hdrUiTransferPq(vec4 encoded) {\n"
		"    vec4 linear = hdrUiTransfer(encoded);\n"
		"    if (linear.a <= 0.0) return vec4(0.0);\n"
		"    vec3 p = pow(clamp(linear.rgb / linear.a, 0.0, 1.0), vec3(2610.0/16384.0));\n"
		"    vec3 pq = pow((3424.0/4096.0 + 2413.0/128.0*p) / (1.0 + 2392.0/128.0*p), vec3(2523.0/32.0));\n"
		"    return vec4(pq * linear.a, linear.a);\n"
		"}\n"
#endif
#endif
		"void main(void) {\n"
		"   vec4 result;\n"
		"	float scissor = scissorMask(fpos);\n"
		"#ifdef EDGE_AA\n"
		"	float strokeAlpha = strokeMask();\n"
		"	if (strokeAlpha < strokeThr) discard;\n"
		"#else\n"
		"	float strokeAlpha = 1.0;\n"
		"#endif\n"
		"	if (type == 0) {			// Gradient\n"
		"		// Calculate gradient color using box gradient\n"
		"		vec2 pt = (paintMat * vec3(fpos,1.0)).xy;\n"
		"		float d = clamp((sdroundrect(pt, extent, radius) + feather*0.5) / feather, 0.0, 1.0);\n"
		"		vec4 color = mix(innerCol,outerCol,d);\n"
		"		// Combine alpha\n"
		"		color *= strokeAlpha * scissor;\n"
		"		result = color;\n"
		"	} else if (type == 1) {		// Image\n"
		"		// Calculate color fron texture\n"
		"		vec2 pt = (paintMat * vec3(fpos,1.0)).xy / extent;\n"
		"#ifdef NANOVG_GL3\n"
		"		vec4 color = texture(tex, pt);\n"
		"#else\n"
		"		vec4 color = texture2D(tex, pt);\n"
		"#endif\n"
		"		if (texType == 1) color = vec4(color.xyz*color.w,color.w);"
		"		if (texType == 2) color = vec4(color.x);"
#ifndef __PSV__
		"		if (texType == 3 && color.a == 1.0) discard;"
#endif
		"		// Apply color tint and alpha.\n"
		"		color *= innerCol;\n"
		"		// Combine alpha\n"
		"		color *= strokeAlpha * scissor;\n"
		"		result = color;\n"
		"	} else if (type == 2) {		// Stencil fill\n"
		"		result = vec4(1,1,1,1);\n"
		"	} else if (type == 3) {		// Textured tris\n"
		"#ifdef NANOVG_GL3\n"
		"		vec4 color = texture(tex, ftcoord);\n"
		"#else\n"
		"		vec4 color = texture2D(tex, ftcoord);\n"
		"#endif\n"
		"		if (texType == 1) color = vec4(color.xyz*color.w,color.w);"
		"		if (texType == 2) color = vec4(color.x);"
		"		color *= scissor;\n"
		"		result = color * innerCol;\n"
		"	}\n"
		"#ifdef NANOVG_GL3\n"
#ifdef PS5_NATIVE_HDR
#if defined(PS5_NATIVE_GPU)
		"	outColor = type == 2 ? result : (hdrUiPq != 0 ? hdrUiTransferPq(result) : hdrUiTransfer(result));\n"
#else
		"	outColor = type == 2 ? result : hdrUiTransfer(result);\n"
#endif
#else
		"	outColor = result;\n"
#endif
		"#else\n"
		"	gl_FragColor = result;\n"
		"#endif\n"
		"}\n";

	glnvg__checkError(gl, "init");

	if (gl->flags & NVG_ANTIALIAS) {
		if (glnvg__createShader(&gl->shader, "shader", shaderHeader, "#define EDGE_AA 1\n", fillVertShader, fillFragShader) == 0)
			return 0;
	} else {
		if (glnvg__createShader(&gl->shader, "shader", shaderHeader, NULL, fillVertShader, fillFragShader) == 0)
			return 0;
	}

	glnvg__checkError(gl, "uniform locations");
	glnvg__getUniforms(&gl->shader);

	// Create dynamic vertex array
#if defined NANOVG_GL3
	glGenVertexArrays(1, &gl->vertArr);
#endif
	glGenBuffers(1, &gl->vertBuf);

#if NANOVG_GL_USE_UNIFORMBUFFER
	// Create UBOs
	glUniformBlockBinding(gl->shader.prog, gl->shader.loc[GLNVG_LOC_FRAG], GLNVG_FRAG_BINDING);
	glGenBuffers(1, &gl->fragBuf);
	glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
#endif
	gl->fragSize = sizeof(GLNVGfragUniforms) + align - sizeof(GLNVGfragUniforms) % align;

	// Some platforms does not allow to have samples to unset textures.
	// Create empty one which is bound when there's no texture specified.
	gl->dummyTex = glnvg__renderCreateTexture(gl, NVG_TEXTURE_ALPHA, 1, 1, 0, NULL);
#ifdef PS5_NATIVE_GPU
	if (gl->dummyTex == 0) return 0;
#endif

	glnvg__checkError(gl, "create done");

	glFinish();

	return 1;
}

static int glnvg__renderCreateTexture(void* uptr, int type, int w, int h, int imageFlags, const unsigned char* data)
{
	GLNVGcontext* gl = (GLNVGcontext*)uptr;
#ifdef PS5_NATIVE_GPU
	// A pending error cannot be attributed to this upload. Refuse the attempt
	// before allocating a name; the caller retains its previous image.
	if (w <= 0 || h <= 0 || glGetError() != GL_NO_ERROR) return 0;
#endif
	GLNVGtexture* tex = glnvg__allocTexture(gl);

	if (tex == NULL) return 0;

#ifdef NANOVG_GLES2
	// Check for non-power of 2.
	if (glnvg__nearestPow2(w) != (unsigned int)w || glnvg__nearestPow2(h) != (unsigned int)h) {
		// No repeat
		if ((imageFlags & NVG_IMAGE_REPEATX) != 0 || (imageFlags & NVG_IMAGE_REPEATY) != 0) {
			printf("Repeat X/Y is not supported for non power-of-two textures (%d x %d)\n", w, h);
			imageFlags &= ~(NVG_IMAGE_REPEATX | NVG_IMAGE_REPEATY);
		}
		// No mips.
		if (imageFlags & NVG_IMAGE_GENERATE_MIPMAPS) {
			printf("Mip-maps is not support for non power-of-two textures (%d x %d)\n", w, h);
			imageFlags &= ~NVG_IMAGE_GENERATE_MIPMAPS;
		}
	}
#endif

	glGenTextures(1, &tex->tex);
#ifdef PS5_NATIVE_GPU
	{
		const GLenum error = glGetError();
		if (tex->tex == 0 || error != GL_NO_ERROR) {
			glnvg__deleteTexture(gl, tex->id);
			return 0;
		}
	}
#endif
	tex->width = w;
	tex->height = h;
	tex->type = type;
	tex->flags = imageFlags;
	glnvg__bindTexture(gl, tex->tex);
#ifdef PS5_NATIVE_GPU
	// A failed bind can leave another image bound. Never upload into it.
	if (glGetError() != GL_NO_ERROR) goto native_texture_failure;
#endif

	glPixelStorei(GL_UNPACK_ALIGNMENT,1);
#ifndef NANOVG_GLES2
	glPixelStorei(GL_UNPACK_ROW_LENGTH, tex->width);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
#endif

#if defined (NANOVG_GL2)
	// GL 1.4 and later has support for generating mipmaps using a tex parameter.
	if (imageFlags & NVG_IMAGE_GENERATE_MIPMAPS) {
		glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
	}
#endif

#ifdef PS5_NATIVE_GPU
	// Failed unpack setup must not make GL read pixels with stale strides.
	if (glGetError() != GL_NO_ERROR) goto native_texture_failure;
#endif
	if (type == NVG_TEXTURE_RGBA)
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	else
#if defined(NANOVG_GLES2) || defined (NANOVG_GL2)
		glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w, h, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, data);
#elif defined(PS5_NATIVE_GPU)
		// The native renderer can batch single-level RGBA8 samples. Keep the alpha atlas and
		// uploads one byte per pixel: the shader consumes only the red channel.
		// Bound the expanded backing to 4 MiB per image; larger atlases retain
		// the existing compact format and synchronous rendering path.
		glTexImage2D(GL_TEXTURE_2D, 0, w <= 1024 && h <= 1024 ? GL_RGBA8 : GL_RED,
			w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data);
#elif defined(NANOVG_GLES3)
		glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data);
#else
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, data);
#endif
#ifdef PS5_NATIVE_GPU
	if (glGetError() != GL_NO_ERROR) goto native_texture_failure;
#endif

	if (imageFlags & NVG_IMAGE_GENERATE_MIPMAPS) {
		if (imageFlags & NVG_IMAGE_NEAREST) {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
		} else {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		}
	} else {
		if (imageFlags & NVG_IMAGE_NEAREST) {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		} else {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		}
	}

	if (imageFlags & NVG_IMAGE_NEAREST) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	} else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}

	if (imageFlags & NVG_IMAGE_REPEATX)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	else
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

	if (imageFlags & NVG_IMAGE_REPEATY)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	else
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#ifndef NANOVG_GLES2
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
#endif

	// The new way to build mipmaps on GLES and GL3
#if !defined(NANOVG_GL2)
	if (imageFlags & NVG_IMAGE_GENERATE_MIPMAPS) {
		glGenerateMipmap(GL_TEXTURE_2D);
	}
#endif

#ifdef PS5_NATIVE_GPU
	// Cached dimensions describe the request, not successful GL storage.
	// Restore the normal binding/unpack state before reporting any failure.
	glnvg__bindTexture(gl, 0);
	if (glGetError() != GL_NO_ERROR) goto native_texture_failure;
	gl->textureBindingUnknown = 0;
#else
	glnvg__checkError(gl, "create tex");
	glnvg__bindTexture(gl, 0);
#endif

	return tex->id;
#ifdef PS5_NATIVE_GPU
native_texture_failure:
	// Cleanup is best effort; force future binds until a checked operation
	// establishes a known binding. Do not consume and hide cleanup errors.
	gl->textureBindingUnknown = 1;
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#ifndef NANOVG_GLES2
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
#endif
	// The binding cache may describe a failed GL call. Force the cleanup bind.
	glBindTexture(GL_TEXTURE_2D, 0);
#if NANOVG_GL_USE_STATE_FILTER
	gl->boundTexture = 0;
#endif
	// This fresh name was never published, so NODELETE cannot transfer it.
	tex->flags &= ~NVG_IMAGE_NODELETE;
	glnvg__deleteTexture(gl, tex->id);
	return 0;
#endif
}


static int glnvg__renderDeleteTexture(void* uptr, int image)
{
	GLNVGcontext* gl = (GLNVGcontext*)uptr;
	return glnvg__deleteTexture(gl, image);
}

#ifdef PS5_NATIVE_GPU
// Stages: 1 pre-existing GL error, 2 bind, 3 unpack, 4 upload, 5 restore.
static int glnvg__nativeUpdateFailure(GLNVGcontext* gl, int stage, GLenum error, GLenum cleanup)
{
	if (gl->textureUpdateReports < 16 &&
		(gl->textureUpdateStage != stage || gl->textureUpdateError != error || gl->textureUpdateCleanupError != cleanup)) {
		fprintf(stderr, "PS5 texture update failed stage=%d error=0x%04x cleanup=0x%04x\n", stage, (unsigned)error, (unsigned)cleanup);
		gl->textureUpdateReports++;
	}
	gl->textureUpdateStage = stage;
	gl->textureUpdateError = error;
	gl->textureUpdateCleanupError = cleanup;
	return 0;
}

static int glnvg__renderUpdateTexture(void* uptr, int image, int x, int y, int w, int h, const unsigned char* data)
{
	GLNVGcontext* gl = (GLNVGcontext*)uptr;
	GLNVGtexture* tex = glnvg__findTexture(gl, image);
	GLenum error, cleanup;
	int stage = 2;
	if (tex == NULL || tex->tex == 0 || data == NULL || tex->width <= 0 || tex->height <= 0 ||
		x < 0 || y < 0 || w <= 0 || h <= 0 || x > tex->width || y > tex->height ||
		w > tex->width-x || h > tex->height-y) return 0;
	// Do not drain and ignore unrelated errors: reject before changing GL state,
	// and retain their numeric identity separately from this upload's stages.
	error = glGetError();
	if (error != GL_NO_ERROR) return glnvg__nativeUpdateFailure(gl, 1, error, GL_NO_ERROR);
	glnvg__bindTexture(gl, tex->tex);
	error = glGetError();
	if (error != GL_NO_ERROR) goto native_update_restore;
	stage = 3;
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
#ifndef NANOVG_GLES2
	glPixelStorei(GL_UNPACK_ROW_LENGTH, tex->width);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, x);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, y);
#else
	data += (size_t)y * (size_t)tex->width * (tex->type == NVG_TEXTURE_RGBA ? 4 : 1);
	x = 0; w = tex->width;
#endif
	error = glGetError();
	if (error != GL_NO_ERROR) goto native_update_restore;
	stage = 4;
	if (tex->type == NVG_TEXTURE_RGBA)
		glTexSubImage2D(GL_TEXTURE_2D, 0, x,y, w,h, GL_RGBA, GL_UNSIGNED_BYTE, data);
	else
#if defined(NANOVG_GLES2) || defined(NANOVG_GL2)
		glTexSubImage2D(GL_TEXTURE_2D, 0, x,y, w,h, GL_LUMINANCE, GL_UNSIGNED_BYTE, data);
#else
		glTexSubImage2D(GL_TEXTURE_2D, 0, x,y, w,h, GL_RED, GL_UNSIGNED_BYTE, data);
#endif
	error = glGetError();
native_update_restore:
	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#ifndef NANOVG_GLES2
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
#endif
	// A failed bind may invalidate the cache. Restore without trusting it.
	glBindTexture(GL_TEXTURE_2D, 0);
#if NANOVG_GL_USE_STATE_FILTER
	gl->boundTexture = 0;
#endif
	cleanup = glGetError();
	// Generic cached binds cannot prove a failed driver call was repaired.
	// Keep forcing them until this or another checked upload establishes zero.
	gl->textureBindingUnknown = cleanup != GL_NO_ERROR;
	if (error != GL_NO_ERROR || cleanup != GL_NO_ERROR)
		return glnvg__nativeUpdateFailure(gl, error != GL_NO_ERROR ? stage : 5, error, cleanup);
	gl->textureUpdateStage = 0;
	gl->textureUpdateError = gl->textureUpdateCleanupError = GL_NO_ERROR;
	return 1;
}
#else
static int glnvg__renderUpdateTexture(void* uptr, int image, int x, int y, int w, int h, const unsigned char* data)
{
	GLNVGcontext* gl = (GLNVGcontext*)uptr;
	GLNVGtexture* tex = glnvg__findTexture(gl, image);

	if (tex == NULL) return 0;
	glnvg__bindTexture(gl, tex->tex);

	glPixelStorei(GL_UNPACK_ALIGNMENT,1);

#ifndef NANOVG_GLES2
	glPixelStorei(GL_UNPACK_ROW_LENGTH, tex->width);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, x);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, y);
#else
	// No support for all of skip, need to update a whole row at a time.
	if (tex->type == NVG_TEXTURE_RGBA)
		data += y*tex->width*4;
	else
		data += y*tex->width;
	x = 0;
	w = tex->width;
#endif

	if (tex->type == NVG_TEXTURE_RGBA)
		glTexSubImage2D(GL_TEXTURE_2D, 0, x,y, w,h, GL_RGBA, GL_UNSIGNED_BYTE, data);
	else
#if defined(NANOVG_GLES2) || defined(NANOVG_GL2)
		glTexSubImage2D(GL_TEXTURE_2D, 0, x,y, w,h, GL_LUMINANCE, GL_UNSIGNED_BYTE, data);
#else
		glTexSubImage2D(GL_TEXTURE_2D, 0, x,y, w,h, GL_RED, GL_UNSIGNED_BYTE, data);
#endif

	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
#ifndef NANOVG_GLES2
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
#endif

	glnvg__bindTexture(gl, 0);

	return 1;
}

#endif

static int glnvg__renderGetTextureSize(void* uptr, int image, int* w, int* h)
{
	GLNVGcontext* gl = (GLNVGcontext*)uptr;
	GLNVGtexture* tex = glnvg__findTexture(gl, image);
	if (tex == NULL) return 0;
	*w = tex->width;
	*h = tex->height;
	return 1;
}

static void glnvg__xformToMat3x4(float* m3, float* t)
{
	m3[0] = t[0];
	m3[1] = t[1];
	m3[2] = 0.0f;
	m3[3] = 0.0f;
	m3[4] = t[2];
	m3[5] = t[3];
	m3[6] = 0.0f;
	m3[7] = 0.0f;
	m3[8] = t[4];
	m3[9] = t[5];
	m3[10] = 1.0f;
	m3[11] = 0.0f;
}

static NVGcolor glnvg__premulColor(NVGcolor c)
{
	c.r *= c.a;
	c.g *= c.a;
	c.b *= c.a;
	return c;
}

static int glnvg__convertPaint(GLNVGcontext* gl, GLNVGfragUniforms* frag, NVGpaint* paint,
							   NVGscissor* scissor, float width, float fringe, float strokeThr)
{
	GLNVGtexture* tex = NULL;
	float invxform[6];

	memset(frag, 0, sizeof(*frag));

	frag->innerCol = glnvg__premulColor(paint->innerColor);
	frag->outerCol = glnvg__premulColor(paint->outerColor);

	if (scissor->extent[0] < -0.5f || scissor->extent[1] < -0.5f) {
		memset(frag->scissorMat, 0, sizeof(frag->scissorMat));
		frag->scissorExt[0] = 1.0f;
		frag->scissorExt[1] = 1.0f;
		frag->scissorScale[0] = 1.0f;
		frag->scissorScale[1] = 1.0f;
	} else {
		nvgTransformInverse(invxform, scissor->xform);
		glnvg__xformToMat3x4(frag->scissorMat, invxform);
		frag->scissorExt[0] = scissor->extent[0];
		frag->scissorExt[1] = scissor->extent[1];
		frag->scissorScale[0] = sqrtf(scissor->xform[0]*scissor->xform[0] + scissor->xform[2]*scissor->xform[2]) / fringe;
		frag->scissorScale[1] = sqrtf(scissor->xform[1]*scissor->xform[1] + scissor->xform[3]*scissor->xform[3]) / fringe;
	}

	memcpy(frag->extent, paint->extent, sizeof(frag->extent));
	frag->strokeMult = (width*0.5f + fringe*0.5f) / fringe;
	frag->strokeThr = strokeThr;

	if (paint->image != 0) {
		tex = glnvg__findTexture(gl, paint->image);
		if (tex == NULL) return 0;
		if ((tex->flags & NVG_IMAGE_FLIPY) != 0) {
			float m1[6], m2[6];
			nvgTransformTranslate(m1, 0.0f, frag->extent[1] * 0.5f);
			nvgTransformMultiply(m1, paint->xform);
			nvgTransformScale(m2, 1.0f, -1.0f);
			nvgTransformMultiply(m2, m1);
			nvgTransformTranslate(m1, 0.0f, -frag->extent[1] * 0.5f);
			nvgTransformMultiply(m1, m2);
			nvgTransformInverse(invxform, m1);
		} else {
			nvgTransformInverse(invxform, paint->xform);
		}
		frag->type = NSVG_SHADER_FILLIMG;

		#if NANOVG_GL_USE_UNIFORMBUFFER
		if (tex->type == NVG_TEXTURE_RGBA)
			if (scissor->stencilFlag)
				frag->texType = 3;
			else
				frag->texType = (tex->flags & NVG_IMAGE_PREMULTIPLIED) ? 0 : 1;
		else
			frag->texType = 2;
		#else
		if (tex->type == NVG_TEXTURE_RGBA)
			if (scissor->stencilFlag)
				frag->texType = 3.0f;
			else
				frag->texType = (tex->flags & NVG_IMAGE_PREMULTIPLIED) ? 0.0f : 1.0f;
		else
			frag->texType = 2.0f;
		#endif
//		printf("frag->texType = %d\n", frag->texType);
	} else {
		frag->type = NSVG_SHADER_FILLGRAD;
		frag->radius = paint->radius;
		frag->feather = paint->feather;
		nvgTransformInverse(invxform, paint->xform);
	}

	glnvg__xformToMat3x4(frag->paintMat, invxform);

	return 1;
}

static GLNVGfragUniforms* nvg__fragUniformPtr(GLNVGcontext* gl, int i);

static void glnvg__setUniforms(GLNVGcontext* gl, int uniformOffset, int image)
{
	GLNVGtexture* tex = NULL;
#if NANOVG_GL_USE_UNIFORMBUFFER
	glBindBufferRange(GL_UNIFORM_BUFFER, GLNVG_FRAG_BINDING, gl->fragBuf, uniformOffset, sizeof(GLNVGfragUniforms));
#else
	GLNVGfragUniforms* frag = nvg__fragUniformPtr(gl, uniformOffset);
	glUniform4fv(gl->shader.loc[GLNVG_LOC_FRAG], NANOVG_GL_UNIFORMARRAY_SIZE, &(frag->uniformArray[0][0]));
#endif

	if (image != 0) {
		tex = glnvg__findTexture(gl, image);
	}
	// If no image is set, use empty texture
	if (tex == NULL) {
		tex = glnvg__findTexture(gl, gl->dummyTex);
	}
	glnvg__bindTexture(gl, tex != NULL ? tex->tex : 0);
	glnvg__checkError(gl, "tex paint tex");
}

static void glnvg__renderViewport(void* uptr, float width, float height, float devicePixelRatio)
{
	NVG_NOTUSED(devicePixelRatio);
	GLNVGcontext* gl = (GLNVGcontext*)uptr;
	gl->view[0] = width;
	gl->view[1] = height;
}

static void glnvg__fill(GLNVGcontext* gl, GLNVGcall* call)
{
	GLNVGpath* paths = &gl->paths[call->pathOffset];
	int i, npaths = call->pathCount;

	// Draw shapes
	glEnable(GL_STENCIL_TEST);
	glnvg__stencilMask(gl, 0xff);
	glnvg__stencilFunc(gl, GL_ALWAYS, 0, 0xff);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	// set bindpoint for solid loc
	glnvg__setUniforms(gl, call->uniformOffset, 0);
	glnvg__checkError(gl, "fill simple");

	glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP);
	glStencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR_WRAP);
	glDisable(GL_CULL_FACE);
	for (i = 0; i < npaths; i++)
		glDrawArrays(GL_TRIANGLE_FAN, paths[i].fillOffset, paths[i].fillCount);
	glEnable(GL_CULL_FACE);

	// Draw anti-aliased pixels
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	glnvg__setUniforms(gl, call->uniformOffset + gl->fragSize, call->image);
	glnvg__checkError(gl, "fill fill");

	if (gl->flags & NVG_ANTIALIAS) {
		glnvg__stencilFunc(gl, GL_EQUAL, 0x00, 0xff);
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
		// Draw fringes
		for (i = 0; i < npaths; i++)
			glDrawArrays(GL_TRIANGLE_STRIP, paths[i].strokeOffset, paths[i].strokeCount);
	}

	// Draw fill
	glnvg__stencilFunc(gl, GL_NOTEQUAL, 0x0, 0xff);
	glStencilOp(GL_ZERO, GL_ZERO, GL_ZERO);
	glDrawArrays(GL_TRIANGLE_STRIP, call->triangleOffset, call->triangleCount);

	glDisable(GL_STENCIL_TEST);
}

static void glnvg__convexFill(GLNVGcontext* gl, GLNVGcall* call)
{
	GLNVGpath* paths = &gl->paths[call->pathOffset];
	int i, npaths = call->pathCount;

	glnvg__setUniforms(gl, call->uniformOffset, call->image);
	glnvg__checkError(gl, "convex fill");

	for (i = 0; i < npaths; i++) {
#ifdef PS5_NATIVE_GPU
		// The native expansion stores independent fill triangles followed by
		// independent fringe triangles. Both use exactly the same GL state.
		// Submit that ordered range once; do not merge fans, strips or stencil
		// commands that retained the original representation.
		if (call->type == GLNVG_CONVEXFILL && call->nativeTriangles &&
			paths[i].fillCount >= 0 && paths[i].fillCount <= 65536 &&
			paths[i].strokeCount >= 0 && paths[i].strokeCount <= 65536 - paths[i].fillCount &&
			paths[i].fillOffset >= 0 && paths[i].fillOffset <= gl->nverts &&
			paths[i].fillCount + paths[i].strokeCount <= gl->nverts - paths[i].fillOffset &&
			paths[i].strokeOffset == paths[i].fillOffset + paths[i].fillCount) {
			glDrawArrays(GL_TRIANGLES, paths[i].fillOffset, paths[i].fillCount + paths[i].strokeCount);
			continue;
		}
#endif
		glDrawArrays(
#ifdef PS5_NATIVE_GPU
			call->nativeTriangles ? GL_TRIANGLES :
#endif
			GL_TRIANGLE_FAN, paths[i].fillOffset, paths[i].fillCount);
		// Draw fringes
		if (paths[i].strokeCount > 0) {
			glDrawArrays(
#ifdef PS5_NATIVE_GPU
				call->nativeTriangles ? GL_TRIANGLES :
#endif
				GL_TRIANGLE_STRIP, paths[i].strokeOffset, paths[i].strokeCount);
		}
	}
}

static void glnvg__convexFillStencil(GLNVGcontext* gl, GLNVGcall* call)
{
	glEnable(GL_STENCIL_TEST);
	glnvg__stencilFunc(gl, GL_ALWAYS, 1, 0xFF);
	glStencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

	glnvg__convexFill(gl, call);

	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glnvg__stencilFunc(gl, GL_EQUAL, 0, 0xFF);
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

static void glnvg__convexFillStencilClear(GLNVGcontext* gl, GLNVGcall* call)
{
	glClear(GL_STENCIL_BUFFER_BIT);
	glDisable(GL_STENCIL_TEST);
}

static void glnvg__stroke(GLNVGcontext* gl, GLNVGcall* call)
{
	GLNVGpath* paths = &gl->paths[call->pathOffset];
	int npaths = call->pathCount, i;

#if defined(PS5_NATIVE_GPU)
	if (call->nativeTriangles) {
		// nvgStencil resets its scissor flag immediately after enqueueing the
		// mask. Inspect actual dispatch state, including masks across frames.
		if (!glIsEnabled(GL_STENCIL_TEST)) {
			glnvg__setUniforms(gl, call->uniformOffset, call->image);
			glDrawArrays(GL_TRIANGLES, paths[0].strokeOffset, paths[0].strokeCount);
			return;
		}
		paths[0].strokeOffset = call->nativeStrokeOffset;
		paths[0].strokeCount = call->nativeStrokeCount;
		call->nativeTriangles = 0;
	}
#endif

	if (gl->flags & NVG_STENCIL_STROKES) {

		glEnable(GL_STENCIL_TEST);
		glnvg__stencilMask(gl, 0xff);

		// Fill the stroke base without overlap
		glnvg__stencilFunc(gl, GL_EQUAL, 0x0, 0xff);
		glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
		glnvg__setUniforms(gl, call->uniformOffset + gl->fragSize, call->image);
		glnvg__checkError(gl, "stroke fill 0");
		for (i = 0; i < npaths; i++)
			glDrawArrays(GL_TRIANGLE_STRIP, paths[i].strokeOffset, paths[i].strokeCount);
		// Draw anti-aliased pixels.
		glnvg__setUniforms(gl, call->uniformOffset, call->image);
		glnvg__stencilFunc(gl, GL_EQUAL, 0x00, 0xff);
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
		for (i = 0; i < npaths; i++)
			glDrawArrays(GL_TRIANGLE_STRIP, paths[i].strokeOffset, paths[i].strokeCount);
		// Clear stencil buffer.
		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glnvg__stencilFunc(gl, GL_ALWAYS, 0x0, 0xff);
		glStencilOp(GL_ZERO, GL_ZERO, GL_ZERO);
		glnvg__checkError(gl, "stroke fill 1");
		for (i = 0; i < npaths; i++)
			glDrawArrays(GL_TRIANGLE_STRIP, paths[i].strokeOffset, paths[i].strokeCount);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

		glDisable(GL_STENCIL_TEST);

//		glnvg__convertPaint(gl, nvg__fragUniformPtr(gl, call->uniformOffset + gl->fragSize), paint, scissor, strokeWidth, fringe, 1.0f - 0.5f/255.0f);

	} else {
		glnvg__setUniforms(gl, call->uniformOffset, call->image);
		glnvg__checkError(gl, "stroke fill");
		// Draw Strokes
		for (i = 0; i < npaths; i++)
			glDrawArrays(GL_TRIANGLE_STRIP, paths[i].strokeOffset, paths[i].strokeCount);
	}
}

static void glnvg__triangles(GLNVGcontext* gl, GLNVGcall* call)
{
	glnvg__setUniforms(gl, call->uniformOffset, call->image);
	glnvg__checkError(gl, "triangles fill");

	glDrawArrays(GL_TRIANGLES, call->triangleOffset, call->triangleCount);
}

static void glnvg__renderCancel(void* uptr) {
	GLNVGcontext* gl = (GLNVGcontext*)uptr;
	gl->nverts = 0;
#ifdef PS5_NATIVE_GPU
	gl->nativeExpandedVerts = 0;
#endif
	gl->npaths = 0;
	gl->ncalls = 0;
	gl->nuniforms = 0;
}

static GLenum glnvg_convertBlendFuncFactor(int factor)
{
	if (factor == NVG_ZERO)
		return GL_ZERO;
	if (factor == NVG_ONE)
		return GL_ONE;
	if (factor == NVG_SRC_COLOR)
		return GL_SRC_COLOR;
	if (factor == NVG_ONE_MINUS_SRC_COLOR)
		return GL_ONE_MINUS_SRC_COLOR;
	if (factor == NVG_DST_COLOR)
		return GL_DST_COLOR;
	if (factor == NVG_ONE_MINUS_DST_COLOR)
		return GL_ONE_MINUS_DST_COLOR;
	if (factor == NVG_SRC_ALPHA)
		return GL_SRC_ALPHA;
	if (factor == NVG_ONE_MINUS_SRC_ALPHA)
		return GL_ONE_MINUS_SRC_ALPHA;
	if (factor == NVG_DST_ALPHA)
		return GL_DST_ALPHA;
	if (factor == NVG_ONE_MINUS_DST_ALPHA)
		return GL_ONE_MINUS_DST_ALPHA;
	if (factor == NVG_SRC_ALPHA_SATURATE)
		return GL_SRC_ALPHA_SATURATE;
	return GL_INVALID_ENUM;
}

static GLNVGblend glnvg__blendCompositeOperation(NVGcompositeOperationState op)
{
	GLNVGblend blend;
	blend.srcRGB = glnvg_convertBlendFuncFactor(op.srcRGB);
	blend.dstRGB = glnvg_convertBlendFuncFactor(op.dstRGB);
	blend.srcAlpha = glnvg_convertBlendFuncFactor(op.srcAlpha);
	blend.dstAlpha = glnvg_convertBlendFuncFactor(op.dstAlpha);
	if (blend.srcRGB == GL_INVALID_ENUM || blend.dstRGB == GL_INVALID_ENUM || blend.srcAlpha == GL_INVALID_ENUM || blend.dstAlpha == GL_INVALID_ENUM)
	{
		blend.srcRGB = GL_ONE;
		blend.dstRGB = GL_ONE_MINUS_SRC_ALPHA;
		blend.srcAlpha = GL_ONE;
		blend.dstAlpha = GL_ONE_MINUS_SRC_ALPHA;
	}
	return blend;
}

static void glnvg__renderFlush(void* uptr)
{
	GLNVGcontext* gl = (GLNVGcontext*)uptr;
	int i;

	if (gl->ncalls > 0) {

		// Setup require GL state.
		glUseProgram(gl->shader.prog);
#if defined(PS5_NATIVE_GPU)
		if (gl->shader.loc[GLNVG_LOC_HDRPQ] >= 0)
			glUniform1i(gl->shader.loc[GLNVG_LOC_HDRPQ], gl->hdrPqOutput);
#endif

		glEnable(GL_CULL_FACE);
		glCullFace(GL_BACK);
		glFrontFace(GL_CCW);
		glEnable(GL_BLEND);
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_SCISSOR_TEST);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glStencilMask(0xffffffff);
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
		glStencilFunc(GL_ALWAYS, 0, 0xffffffff);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, 0);
		#if NANOVG_GL_USE_STATE_FILTER
		gl->boundTexture = 0;
		gl->stencilMask = 0xffffffff;
		gl->stencilFunc = GL_ALWAYS;
		gl->stencilFuncRef = 0;
		gl->stencilFuncMask = 0xffffffff;
		gl->blendFunc.srcRGB = GL_INVALID_ENUM;
		gl->blendFunc.srcAlpha = GL_INVALID_ENUM;
		gl->blendFunc.dstRGB = GL_INVALID_ENUM;
		gl->blendFunc.dstAlpha = GL_INVALID_ENUM;
		#endif

#if NANOVG_GL_USE_UNIFORMBUFFER
		// Upload ubo for frag shaders
		glBindBuffer(GL_UNIFORM_BUFFER, gl->fragBuf);
		glBufferData(GL_UNIFORM_BUFFER, gl->nuniforms * gl->fragSize, gl->uniforms, GL_STREAM_DRAW);
#endif

		// Upload vertex data
#if defined NANOVG_GL3
		glBindVertexArray(gl->vertArr);
#endif
		glBindBuffer(GL_ARRAY_BUFFER, gl->vertBuf);
		glBufferData(GL_ARRAY_BUFFER, gl->nverts * sizeof(NVGvertex), gl->verts, GL_STREAM_DRAW);
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(NVGvertex), (const GLvoid*)(size_t)0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(NVGvertex), (const GLvoid*)(0 + 2*sizeof(float)));

		// Set view and texture just once per frame.
		glUniform1i(gl->shader.loc[GLNVG_LOC_TEX], 0);
		glUniform2fv(gl->shader.loc[GLNVG_LOC_VIEWSIZE], 1, gl->view);

#if NANOVG_GL_USE_UNIFORMBUFFER
		glBindBuffer(GL_UNIFORM_BUFFER, gl->fragBuf);
#endif

		for (i = 0; i < gl->ncalls; i++) {
			GLNVGcall* call = &gl->calls[i];
			glnvg__blendFuncSeparate(gl,&call->blendFunc);
			if (call->type == GLNVG_FILL)
				glnvg__fill(gl, call);
			else if (call->type == GLNVG_CONVEXFILL)
				glnvg__convexFill(gl, call);
			else if (call->type == GLNVG_STROKE)
				glnvg__stroke(gl, call);
			else if (call->type == GLNVG_TRIANGLES)
				glnvg__triangles(gl, call);
			else if (call->type == GLNVG_CONVEXFILL_STENCIL)
				glnvg__convexFillStencil(gl, call);
			else if (call->type == GLNVG_CONVEXFILL_STENCIL_CLEAR)
				glnvg__convexFillStencilClear(gl, call);
		}

		glDisableVertexAttribArray(0);
		glDisableVertexAttribArray(1);
#if defined NANOVG_GL3
		glBindVertexArray(0);
#endif
		glDisable(GL_CULL_FACE);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		glUseProgram(0);
		glnvg__bindTexture(gl, 0);
	}

	// Reset calls
	gl->nverts = 0;
#ifdef PS5_NATIVE_GPU
	gl->nativeExpandedVerts = 0;
#endif
	gl->npaths = 0;
	gl->ncalls = 0;
	gl->nuniforms = 0;
}

static int glnvg__maxVertCount(const NVGpath* paths, int npaths)
{
	int i, count = 0;
	for (i = 0; i < npaths; i++) {
		count += paths[i].nfill;
		count += paths[i].nstroke;
	}
	return count;
}

static GLNVGcall* glnvg__allocCall(GLNVGcontext* gl)
{
	GLNVGcall* ret = NULL;
	if (gl->ncalls+1 > gl->ccalls) {
		GLNVGcall* calls;
		int ccalls = glnvg__maxi(gl->ncalls+1, 128) + gl->ccalls/2; // 1.5x Overallocate
		calls = (GLNVGcall*)realloc(gl->calls, sizeof(GLNVGcall) * ccalls);
		if (calls == NULL) return NULL;
		gl->calls = calls;
		gl->ccalls = ccalls;
	}
	ret = &gl->calls[gl->ncalls++];
	memset(ret, 0, sizeof(GLNVGcall));
	return ret;
}

static int glnvg__allocPaths(GLNVGcontext* gl, int n)
{
	int ret = 0;
	if (gl->npaths+n > gl->cpaths) {
		GLNVGpath* paths;
		int cpaths = glnvg__maxi(gl->npaths + n, 128) + gl->cpaths/2; // 1.5x Overallocate
		paths = (GLNVGpath*)realloc(gl->paths, sizeof(GLNVGpath) * cpaths);
		if (paths == NULL) return -1;
		gl->paths = paths;
		gl->cpaths = cpaths;
	}
	ret = gl->npaths;
	gl->npaths += n;
	return ret;
}

static int glnvg__allocVerts(GLNVGcontext* gl, int n)
{
	int ret = 0;
	if (gl->nverts+n > gl->cverts) {
		NVGvertex* verts;
		int cverts = glnvg__maxi(gl->nverts + n, 4096) + gl->cverts/2; // 1.5x Overallocate
		verts = (NVGvertex*)realloc(gl->verts, sizeof(NVGvertex) * cverts);
		if (verts == NULL) return -1;
		gl->verts = verts;
		gl->cverts = cverts;
	}
	ret = gl->nverts;
	gl->nverts += n;
	return ret;
}

static int glnvg__allocFragUniforms(GLNVGcontext* gl, int n)
{
	int ret = 0, structSize = gl->fragSize;
	if (gl->nuniforms+n > gl->cuniforms) {
		unsigned char* uniforms;
		int cuniforms = glnvg__maxi(gl->nuniforms+n, 128) + gl->cuniforms/2; // 1.5x Overallocate
		uniforms = (unsigned char*)realloc(gl->uniforms, structSize * cuniforms);
		if (uniforms == NULL) return -1;
		gl->uniforms = uniforms;
		gl->cuniforms = cuniforms;
	}
	ret = gl->nuniforms * structSize;
	gl->nuniforms += n;
	return ret;
}

static GLNVGfragUniforms* nvg__fragUniformPtr(GLNVGcontext* gl, int i)
{
	return (GLNVGfragUniforms*)&gl->uniforms[i];
}

static void glnvg__vset(NVGvertex* vtx, float x, float y, float u, float v)
{
	vtx->x = x;
	vtx->y = y;
	vtx->u = u;
	vtx->v = v;
}


#ifdef PS5_NATIVE_GPU
/* The native retained batch supports independent triangles. Adapt only ordinary
 * convex fills: stencil paths and strokes retain their original implementation.
 * Appending at most 65536 vertices (1 MiB) per frame bounds extra vertex data.
 * Budget/allocation failure leaves the original vertices and draw intact. */
static void glnvg__nativeConvexTriangles(GLNVGcontext* gl, GLNVGcall* call)
{
	GLNVGpath* path;
	int fill, stroke, total, offset, i, output;
	if (call->type != GLNVG_CONVEXFILL || call->nativeTriangles ||
		call->pathCount != 1 || call->pathOffset < 0 || call->pathOffset >= gl->npaths ||
		gl->nverts < 0 || gl->cverts < 0 || gl->nativeExpandedVerts < 0 ||
		gl->nativeExpandedVerts > 65536) return;
	path = &gl->paths[call->pathOffset];
	if (path->fillCount < 0 || path->strokeCount < 0 ||
		path->fillOffset < 0 || path->strokeOffset < 0 ||
		path->fillOffset > gl->nverts || path->strokeOffset > gl->nverts ||
		path->fillCount > gl->nverts - path->fillOffset ||
		path->strokeCount > gl->nverts - path->strokeOffset) return;
	/* Bound before multiplying, including degenerate 0/1/2-vertex paths. */
	if (path->fillCount > 21847 || path->strokeCount > 21847) return;
	fill = path->fillCount < 3 ? 0 : (path->fillCount - 2) * 3;
	stroke = path->strokeCount < 3 ? 0 : (path->strokeCount - 2) * 3;
	total = fill + stroke;
	if (total == 0 || total > 65536 - gl->nativeExpandedVerts) return;
	/* glnvg__allocVerts uses signed capacities and 1.5x growth. */
	if (gl->nverts > 0x7fffffff - total ||
		gl->cverts / 2 > 0x7fffffff - glnvg__maxi(gl->nverts + total, 4096)) return;
	offset = glnvg__allocVerts(gl, total);
	if (offset < 0) {
		return;
	}
	output = offset;
	/* Read offsets after realloc; the old array pointer may have moved. Fan
	 * and strip expansion preserve winding and the last provoking vertex. */
	for (i = 0; i < path->fillCount - 2; i++) {
		gl->verts[output++] = gl->verts[path->fillOffset];
		gl->verts[output++] = gl->verts[path->fillOffset + i + 1];
		gl->verts[output++] = gl->verts[path->fillOffset + i + 2];
	}
	for (i = 0; i < path->strokeCount - 2; i++) {
		gl->verts[output++] = gl->verts[path->strokeOffset + i + (i & 1)];
		gl->verts[output++] = gl->verts[path->strokeOffset + i + 1 - (i & 1)];
		gl->verts[output++] = gl->verts[path->strokeOffset + i + 2];
	}
	path->fillOffset = offset;
	path->fillCount = fill;
	path->strokeOffset = offset + fill;
	path->strokeCount = stroke;
	call->nativeTriangles = 1;
	gl->nativeExpandedVerts += total;
}
#endif

#ifdef PS5_NATIVE_GPU
// Avoid uploading and submitting ordinary draws whose complete tessellated
// geometry lies beyond one viewport edge. Include AA fringe vertices. This is
// hardware clip-space rejection, independent of paint, blending and scissoring;
// it must never suppress stencil construction/clear or compound path commands.
static int glnvg__nativeOutsideViewport(const GLNVGcontext* gl,
	const NVGvertex* first, int nfirst, const NVGvertex* second, int nsecond)
{
	int part, i, mask = 15;
	float right, bottom;
	if (!gl || !isfinite(gl->view[0]) || !isfinite(gl->view[1]) ||
		gl->view[0] < 1.0f || gl->view[1] < 1.0f ||
		gl->view[0] > 65536.0f || gl->view[1] > 65536.0f ||
		nfirst < 0 || nsecond < 0 || nsecond > 65536 || nfirst > 65536 - nsecond ||
		(nfirst && !first) || (nsecond && !second) || nfirst + nsecond == 0)
		return 0;
	// Retain a full logical pixel beyond every edge for floating-point rounding.
	right = gl->view[0] + 1.0f;
	bottom = gl->view[1] + 1.0f;
	for (part = 0; part < 2; ++part) {
		const NVGvertex* vertices = part ? second : first;
		int count = part ? nsecond : nfirst;
		for (i = 0; i < count; ++i) {
			float x = vertices[i].x, y = vertices[i].y;
			int outside = 0;
			if (!isfinite(x) || !isfinite(y) || fabsf(x) > 1048576.0f || fabsf(y) > 1048576.0f)
				return 0;
			if (x < -1.0f) outside |= 1;
			if (x > right) outside |= 2;
			if (y < -1.0f) outside |= 4;
			if (y > bottom) outside |= 8;
			mask &= outside;
			if (!mask) return 0;
		}
	}
	return mask != 0;
}

// Cull only ordinary source-over geometry outside an axis-aligned scissor.
// NanoVG's shader has a half-fringe ramp outside the rectangle; keep that ramp
// plus a logical pixel of rounding margin. Rotated/sheared/singular scissors,
// stencil commands and non-source-over blends retain the original path. In
// particular NVG_COPY must still write transparent pixels outside its scissor.
static int glnvg__nativeOutsideScissor(const NVGscissor* scissor,
	NVGcompositeOperationState op, float fringe,
	const NVGvertex* first, int nfirst, const NVGvertex* second, int nsecond)
{
	int part, i, mask = 15;
	float left, right, top, bottom, width, height, margin;
	if (!scissor || scissor->stencilFlag != NVG_STENCIL_DEFAULT ||
		op.srcRGB != NVG_ONE || op.srcAlpha != NVG_ONE ||
		op.dstRGB != NVG_ONE_MINUS_SRC_ALPHA || op.dstAlpha != NVG_ONE_MINUS_SRC_ALPHA ||
		!isfinite(fringe) || fringe <= 0 || fringe > 16 ||
		nfirst < 0 || nsecond < 0 || nsecond > 65536 || nfirst > 65536 - nsecond ||
		(nfirst && !first) || (nsecond && !second) || nfirst + nsecond == 0)
		return 0;
	for (i = 0; i < 6; ++i)
		if (!isfinite(scissor->xform[i]) || fabsf(scissor->xform[i]) > 1048576.0f) return 0;
	if (scissor->xform[1] != 0 || scissor->xform[2] != 0 ||
		fabsf(scissor->xform[0]) < 0.01f || fabsf(scissor->xform[3]) < 0.01f)
		return 0;
	for (i = 0; i < 2; ++i)
		if (!isfinite(scissor->extent[i]) || scissor->extent[i] < 0 || scissor->extent[i] > 65536) return 0;
	width = fabsf(scissor->xform[0]) * scissor->extent[0];
	height = fabsf(scissor->xform[3]) * scissor->extent[1];
	if (width > 1048576 || height > 1048576) return 0;
	margin = fringe * 0.5f + 1.0f;
	left = scissor->xform[4] - width - margin;
	right = scissor->xform[4] + width + margin;
	top = scissor->xform[5] - height - margin;
	bottom = scissor->xform[5] + height + margin;
	for (part = 0; part < 2; ++part) {
		const NVGvertex* vertices = part ? second : first;
		int count = part ? nsecond : nfirst;
		for (i = 0; i < count; ++i) {
			float x = vertices[i].x, y = vertices[i].y;
			int outside = 0;
			if (!isfinite(x) || !isfinite(y) || fabsf(x) > 1048576 || fabsf(y) > 1048576) return 0;
			if (x < left) outside |= 1;
			if (x > right) outside |= 2;
			if (y < top) outside |= 4;
			if (y > bottom) outside |= 8;
			mask &= outside;
			if (!mask) return 0;
		}
	}
	return mask != 0;
}

// Adjacent text runs commonly share atlas, clip and paint. Join only complete
// contiguous triangle lists with byte-identical initialized uniforms and blend
// state. No sorting, vertex copies, extra allocations or stencil crossings.
static void glnvg__nativeMergeTriangles(GLNVGcontext* gl)
{
	GLNVGcall *a, *b;
	if (gl->ncalls < 2) return;
	a = &gl->calls[gl->ncalls - 2];
	b = &gl->calls[gl->ncalls - 1];
	if (a->type != GLNVG_TRIANGLES || b->type != GLNVG_TRIANGLES || a->image != b->image ||
		a->triangleCount <= 0 || b->triangleCount <= 0 ||
		a->triangleCount > 65536 || b->triangleCount > 65536 - a->triangleCount ||
		a->triangleCount % 3 || b->triangleCount % 3 ||
		a->triangleOffset < 0 || a->triangleOffset > gl->nverts ||
		a->triangleCount > gl->nverts - a->triangleOffset ||
		b->triangleOffset != a->triangleOffset + a->triangleCount ||
		b->triangleCount > gl->nverts - b->triangleOffset ||
		a->blendFunc.srcRGB != b->blendFunc.srcRGB || a->blendFunc.dstRGB != b->blendFunc.dstRGB ||
		a->blendFunc.srcAlpha != b->blendFunc.srcAlpha || a->blendFunc.dstAlpha != b->blendFunc.dstAlpha ||
		memcmp(nvg__fragUniformPtr(gl, a->uniformOffset), nvg__fragUniformPtr(gl, b->uniformOffset),
			sizeof(GLNVGfragUniforms)) != 0)
		return;
	a->triangleCount += b->triangleCount;
	// Called immediately after successfully constructing the final call/uniform.
	--gl->ncalls;
	--gl->nuniforms;
}
#endif

static void glnvg__renderFill(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation, NVGscissor* scissor, float fringe,
							  const float* bounds, const NVGpath* paths, int npaths)
{
	GLNVGcontext* gl = (GLNVGcontext*)uptr;
#ifdef PS5_NATIVE_GPU
	if (npaths == 1 && paths && paths[0].convex && scissor->stencilFlag == NVG_STENCIL_DEFAULT &&
		(glnvg__nativeOutsideViewport(gl, paths[0].fill, paths[0].nfill, paths[0].stroke, paths[0].nstroke) ||
		 glnvg__nativeOutsideScissor(scissor, compositeOperation, fringe,
			paths[0].fill, paths[0].nfill, paths[0].stroke, paths[0].nstroke))) {
		return;
	}
#endif
	GLNVGcall* call = glnvg__allocCall(gl);
	NVGvertex* quad;
	GLNVGfragUniforms* frag;
	int i, maxverts, offset;

	if (call == NULL) return;

	call->type = GLNVG_FILL;
	call->triangleCount = 4;
	call->pathOffset = glnvg__allocPaths(gl, npaths);
	if (call->pathOffset == -1) goto error;
	call->pathCount = npaths;
	call->image = paint->image;
	call->blendFunc = glnvg__blendCompositeOperation(compositeOperation);

	if (npaths == 1 && paths[0].convex)
	{
		if (scissor->stencilFlag == NVG_STENCIL_DEFAULT)
			call->type = GLNVG_CONVEXFILL;
		else if (scissor->stencilFlag == NVG_STENCIL_ENABLE)
			call->type = GLNVG_CONVEXFILL_STENCIL;
		else if (scissor->stencilFlag == NVG_STENCIL_CLEAR)
			call->type = GLNVG_CONVEXFILL_STENCIL_CLEAR;
		call->triangleCount = 0;	// Bounding box fill quad not needed for convex fill
	}

	// Allocate vertices for all the paths.
	maxverts = glnvg__maxVertCount(paths, npaths) + call->triangleCount;
	offset = glnvg__allocVerts(gl, maxverts);
	if (offset == -1) goto error;

	for (i = 0; i < npaths; i++) {
		GLNVGpath* copy = &gl->paths[call->pathOffset + i];
		const NVGpath* path = &paths[i];
		memset(copy, 0, sizeof(GLNVGpath));
		if (path->nfill > 0) {
			copy->fillOffset = offset;
			copy->fillCount = path->nfill;
			memcpy(&gl->verts[offset], path->fill, sizeof(NVGvertex) * path->nfill);
			offset += path->nfill;
		}
		if (path->nstroke > 0) {
			copy->strokeOffset = offset;
			copy->strokeCount = path->nstroke;
			memcpy(&gl->verts[offset], path->stroke, sizeof(NVGvertex) * path->nstroke);
			offset += path->nstroke;
		}
	}

	// Setup uniforms for draw calls
	if (call->type == GLNVG_FILL) {
		// Quad
		call->triangleOffset = offset;
		quad = &gl->verts[call->triangleOffset];
		glnvg__vset(&quad[0], bounds[2], bounds[3], 0.5f, 1.0f);
		glnvg__vset(&quad[1], bounds[2], bounds[1], 0.5f, 1.0f);
		glnvg__vset(&quad[2], bounds[0], bounds[3], 0.5f, 1.0f);
		glnvg__vset(&quad[3], bounds[0], bounds[1], 0.5f, 1.0f);

		call->uniformOffset = glnvg__allocFragUniforms(gl, 2);
		if (call->uniformOffset == -1) goto error;
		// Simple shader for stencil
		frag = nvg__fragUniformPtr(gl, call->uniformOffset);
		memset(frag, 0, sizeof(*frag));
		frag->strokeThr = -1.0f;
		frag->type = NSVG_SHADER_SIMPLE;
		// Fill shader
		glnvg__convertPaint(gl, nvg__fragUniformPtr(gl, call->uniformOffset + gl->fragSize), paint, scissor, fringe, fringe, -1.0f);
	} else {
		call->uniformOffset = glnvg__allocFragUniforms(gl, 1);
		if (call->uniformOffset == -1) goto error;
		// Fill shader
		glnvg__convertPaint(gl, nvg__fragUniformPtr(gl, call->uniformOffset), paint, scissor, fringe, fringe, -1.0f);
	}

#ifdef PS5_NATIVE_GPU
	glnvg__nativeConvexTriangles(gl, call);
#endif
	return;

error:
	// We get here if call alloc was ok, but something else is not.
	// Roll back the last call to prevent drawing it.
	if (gl->ncalls > 0) gl->ncalls--;
}

#if defined(PS5_NATIVE_GPU)
static double glnvg__nativeStrokeCross(const NVGvertex* a, const NVGvertex* b, const NVGvertex* c)
{
	return ((double)b->x-a->x)*((double)c->y-a->y) -
		((double)b->y-a->y)*((double)c->x-a->x);
}

/* Certify the actual AA strip, not just its convex centerline. A simple
 * annulus with two strictly convex nested boundaries and consistently oriented
 * triangles covers its interior once. Overlapping/beveled joins, small inner
 * rings and uncertain geometry retain the stencil implementation. This also
 * keeps translucent/fading outlines correct. At most 128 vertex pairs and no
 * temporary allocation; expansion shares the existing 1 MiB/frame budget. */
static int glnvg__nativeStrokeRingReason(const NVGpath* path)
{
	const NVGvertex* v;
	int count, i, j, ring;
	double sign;
	/* Every geometry property the conversion relies on is checked below. */
	if (!path || !path->closed || !path->convex) return 2;
	if (path->nbevel) return 3;
	/* Admission checks are quadratic in the point count, so keep a small bound. */
	if (path->count < 3 || path->count > 128 || !path->stroke ||
		path->nstroke != 2*(path->count+1)) return 4;
	v = path->stroke;
	count = path->count;
	for (i = 0; i < path->nstroke; ++i) {
		if (!isfinite(v[i].x) || !isfinite(v[i].y) || !isfinite(v[i].u) || !isfinite(v[i].v) ||
			fabsf(v[i].x) > 65536 || fabsf(v[i].y) > 65536) return 5;
	}
	if (memcmp(v, v+2*count, 2*sizeof(NVGvertex)) != 0) return 5;
	sign = glnvg__nativeStrokeCross(v, v+2, v+4) > 0 ? 1 : -1;
	// Each boundary must be simple and strictly convex, with the same winding.
	for (ring = 0; ring < 2; ++ring) for (i = 0; i < count; ++i) {
		int next = (i+1)%count;
		for (j = 0; j < count; ++j) {
			if (j == i || j == next) continue;
			if (sign*glnvg__nativeStrokeCross(v+2*i+ring, v+2*next+ring, v+2*j+ring) <= 0.001) return 6;
		}
	}
	// NanoVG's first vertex in each pair is the inner boundary for this winding.
	// Require strict containment; do not infer safety from a radius or width.
	for (i = 0; i < count; ++i) for (j = 0; j < count; ++j)
		if (sign*glnvg__nativeStrokeCross(v+2*i+1, v+2*((i+1)%count)+1, v+2*j) <= 0.001) return 7;
	// Preserve strip winding, including the closing pair. Folded connecting
	// edges would create a reversed or degenerate triangle and are rejected.
	for (i = 0; i < path->nstroke-2; ++i)
		if (sign*glnvg__nativeStrokeCross(v+i+(i&1), v+i+1-(i&1), v+i+2) <= 0.001) return 8;
	return 0;
}


static void glnvg__nativeStrokeTriangles(GLNVGcontext* gl, GLNVGcall* call, const NVGpath* source)
{
	GLNVGpath* path;
	int total, offset, i, output;
	if (!call->nativeRing || call->nativeTriangles || call->type != GLNVG_STROKE ||
		call->pathCount != 1 || call->pathOffset < 0 || call->pathOffset >= gl->npaths ||
		gl->nverts < 0 || gl->cverts < 0 || gl->nativeExpandedVerts < 0 ||
		gl->nativeExpandedVerts > 65536) {
		return;
	}
	if (glnvg__nativeStrokeRingReason(source)) return;
	path = &gl->paths[call->pathOffset];
	if (path->strokeCount != source->nstroke || path->strokeOffset < 0 ||
		path->strokeOffset > gl->nverts || path->strokeCount > gl->nverts-path->strokeOffset) return;
	total = (path->strokeCount-2)*3;
	if (total > 65536-gl->nativeExpandedVerts || gl->nverts > 0x7fffffff-total ||
		gl->cverts/2 > 0x7fffffff-glnvg__maxi(gl->nverts+total,4096)) return;
	offset = glnvg__allocVerts(gl, total);
	if (offset < 0) {
		return;
	}
	output = offset;
	for (i = 0; i < path->strokeCount-2; ++i) {
		gl->verts[output++] = gl->verts[path->strokeOffset+i+(i&1)];
		gl->verts[output++] = gl->verts[path->strokeOffset+i+1-(i&1)];
		gl->verts[output++] = gl->verts[path->strokeOffset+i+2];
	}
	call->nativeStrokeOffset = path->strokeOffset;
	call->nativeStrokeCount = path->strokeCount;
	path->strokeOffset = offset;
	path->strokeCount = total;
	call->nativeTriangles = 1;
	gl->nativeExpandedVerts += total;
}
#endif

static void glnvg__renderStroke(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation, NVGscissor* scissor, float fringe,
								float strokeWidth, const NVGpath* paths, int npaths)
{
	GLNVGcontext* gl = (GLNVGcontext*)uptr;
	GLNVGcall* call = glnvg__allocCall(gl);
	int i, maxverts, offset;

	if (call == NULL) return;

	call->type = GLNVG_STROKE;
#ifdef PS5_NATIVE_GPU
	call->nativeRing = npaths == 1;
#endif
	call->pathOffset = glnvg__allocPaths(gl, npaths);
	if (call->pathOffset == -1) goto error;
	call->pathCount = npaths;
	call->image = paint->image;
	call->blendFunc = glnvg__blendCompositeOperation(compositeOperation);

	// Allocate vertices for all the paths.
	maxverts = glnvg__maxVertCount(paths, npaths);
	offset = glnvg__allocVerts(gl, maxverts);
	if (offset == -1) goto error;

	for (i = 0; i < npaths; i++) {
		GLNVGpath* copy = &gl->paths[call->pathOffset + i];
		const NVGpath* path = &paths[i];
		memset(copy, 0, sizeof(GLNVGpath));
		if (path->nstroke) {
			copy->strokeOffset = offset;
			copy->strokeCount = path->nstroke;
			memcpy(&gl->verts[offset], path->stroke, sizeof(NVGvertex) * path->nstroke);
			offset += path->nstroke;
		}
	}

	if (gl->flags & NVG_STENCIL_STROKES) {
		// Fill shader
		call->uniformOffset = glnvg__allocFragUniforms(gl, 2);
		if (call->uniformOffset == -1) goto error;

		glnvg__convertPaint(gl, nvg__fragUniformPtr(gl, call->uniformOffset), paint, scissor, strokeWidth, fringe, -1.0f);
		glnvg__convertPaint(gl, nvg__fragUniformPtr(gl, call->uniformOffset + gl->fragSize), paint, scissor, strokeWidth, fringe, 1.0f - 0.5f/255.0f);

	} else {
		// Fill shader
		call->uniformOffset = glnvg__allocFragUniforms(gl, 1);
		if (call->uniformOffset == -1) goto error;
		glnvg__convertPaint(gl, nvg__fragUniformPtr(gl, call->uniformOffset), paint, scissor, strokeWidth, fringe, -1.0f);
	}

#if defined(PS5_NATIVE_GPU)
	// Explicit stencil clips and non-source-over paints retain all original state.
	if (scissor->stencilFlag == NVG_STENCIL_DEFAULT &&
		compositeOperation.srcRGB == NVG_ONE && compositeOperation.srcAlpha == NVG_ONE &&
		compositeOperation.dstRGB == NVG_ONE_MINUS_SRC_ALPHA && compositeOperation.dstAlpha == NVG_ONE_MINUS_SRC_ALPHA)
		glnvg__nativeStrokeTriangles(gl, call, npaths == 1 ? paths : NULL);
#endif

	return;

error:
	// We get here if call alloc was ok, but something else is not.
	// Roll back the last call to prevent drawing it.
	if (gl->ncalls > 0) gl->ncalls--;
}

static void glnvg__renderTriangles(void* uptr, NVGpaint* paint, NVGcompositeOperationState compositeOperation, NVGscissor* scissor,
								   const NVGvertex* verts, int nverts, float fringe)
{
	GLNVGcontext* gl = (GLNVGcontext*)uptr;
#ifdef PS5_NATIVE_GPU
	if (glnvg__nativeOutsideViewport(gl, verts, nverts, NULL, 0) ||
		glnvg__nativeOutsideScissor(scissor, compositeOperation, fringe, verts, nverts, NULL, 0)) {
		return;
	}
#endif
	GLNVGcall* call = glnvg__allocCall(gl);
	GLNVGfragUniforms* frag;

	if (call == NULL) return;

	call->type = GLNVG_TRIANGLES;
	call->image = paint->image;
	call->blendFunc = glnvg__blendCompositeOperation(compositeOperation);

	// Allocate vertices for all the paths.
	call->triangleOffset = glnvg__allocVerts(gl, nverts);
	if (call->triangleOffset == -1) goto error;
	call->triangleCount = nverts;

	memcpy(&gl->verts[call->triangleOffset], verts, sizeof(NVGvertex) * nverts);

	// Fill shader
	call->uniformOffset = glnvg__allocFragUniforms(gl, 1);
	if (call->uniformOffset == -1) goto error;
	frag = nvg__fragUniformPtr(gl, call->uniformOffset);
	glnvg__convertPaint(gl, frag, paint, scissor, 1.0f, fringe, -1.0f);
	frag->type = NSVG_SHADER_IMG;

#ifdef PS5_NATIVE_GPU
	glnvg__nativeMergeTriangles(gl);
#endif

	return;

error:
	// We get here if call alloc was ok, but something else is not.
	// Roll back the last call to prevent drawing it.
	if (gl->ncalls > 0) gl->ncalls--;
}

static void glnvg__renderDelete(void* uptr)
{
	GLNVGcontext* gl = (GLNVGcontext*)uptr;
	int i;
	if (gl == NULL) return;

	glnvg__deleteShader(&gl->shader);

#if NANOVG_GL3
#if NANOVG_GL_USE_UNIFORMBUFFER
	if (gl->fragBuf != 0)
		glDeleteBuffers(1, &gl->fragBuf);
#endif
	if (gl->vertArr != 0)
		glDeleteVertexArrays(1, &gl->vertArr);
#endif
	if (gl->vertBuf != 0)
		glDeleteBuffers(1, &gl->vertBuf);

	for (i = 0; i < gl->ntextures; i++) {
		if (gl->textures[i].tex != 0 && (gl->textures[i].flags & NVG_IMAGE_NODELETE) == 0)
			glDeleteTextures(1, &gl->textures[i].tex);
	}
	free(gl->textures);

	free(gl->paths);
	free(gl->verts);
	free(gl->uniforms);
	free(gl->calls);

	free(gl);
}


#if defined NANOVG_GL2
NVGcontext* nvgCreateGL2(int flags)
#elif defined NANOVG_GL3
NVGcontext* nvgCreateGL3(int flags)
#elif defined NANOVG_GLES2
NVGcontext* nvgCreateGLES2(int flags)
#elif defined NANOVG_GLES3
NVGcontext* nvgCreateGLES3(int flags)
#endif
{
	NVGparams params;
	NVGcontext* ctx = NULL;
	GLNVGcontext* gl = (GLNVGcontext*)malloc(sizeof(GLNVGcontext));
	if (gl == NULL) goto error;
	memset(gl, 0, sizeof(GLNVGcontext));

	memset(&params, 0, sizeof(params));
	params.renderCreate = glnvg__renderCreate;
	params.renderCreateTexture = glnvg__renderCreateTexture;
	params.renderDeleteTexture = glnvg__renderDeleteTexture;
	params.renderUpdateTexture = glnvg__renderUpdateTexture;
	params.renderGetTextureSize = glnvg__renderGetTextureSize;
	params.renderViewport = glnvg__renderViewport;
	params.renderCancel = glnvg__renderCancel;
	params.renderFlush = glnvg__renderFlush;
	params.renderFill = glnvg__renderFill;
	params.renderStroke = glnvg__renderStroke;
	params.renderTriangles = glnvg__renderTriangles;
	params.renderDelete = glnvg__renderDelete;
	params.userPtr = gl;
	params.edgeAntiAlias = flags & NVG_ANTIALIAS ? 1 : 0;

	gl->flags = flags;

	ctx = nvgCreateInternal(&params);
	if (ctx == NULL) goto error;

	return ctx;

error:
	// 'gl' is freed by nvgDeleteInternal.
	if (ctx != NULL) nvgDeleteInternal(ctx);
	return NULL;
}

#if defined NANOVG_GL2
void nvgDeleteGL2(NVGcontext* ctx)
#elif defined NANOVG_GL3
void nvgDeleteGL3(NVGcontext* ctx)
#elif defined NANOVG_GLES2
void nvgDeleteGLES2(NVGcontext* ctx)
#elif defined NANOVG_GLES3
void nvgDeleteGLES3(NVGcontext* ctx)
#endif
{
	nvgDeleteInternal(ctx);
}

#if defined NANOVG_GL2
int nvglCreateImageFromHandleGL2(NVGcontext* ctx, GLuint textureId, int w, int h, int imageFlags)
#elif defined NANOVG_GL3
int nvglCreateImageFromHandleGL3(NVGcontext* ctx, GLuint textureId, int w, int h, int imageFlags)
#elif defined NANOVG_GLES2
int nvglCreateImageFromHandleGLES2(NVGcontext* ctx, GLuint textureId, int w, int h, int imageFlags)
#elif defined NANOVG_GLES3
int nvglCreateImageFromHandleGLES3(NVGcontext* ctx, GLuint textureId, int w, int h, int imageFlags)
#endif
{
	GLNVGcontext* gl = (GLNVGcontext*)nvgInternalParams(ctx)->userPtr;
	GLNVGtexture* tex = glnvg__allocTexture(gl);

	if (tex == NULL) return 0;

	tex->type = NVG_TEXTURE_RGBA;
	tex->tex = textureId;
	tex->flags = imageFlags;
	tex->width = w;
	tex->height = h;

	return tex->id;
}

#if defined NANOVG_GL2
GLuint nvglImageHandleGL2(NVGcontext* ctx, int image)
#elif defined NANOVG_GL3
GLuint nvglImageHandleGL3(NVGcontext* ctx, int image)
#elif defined NANOVG_GLES2
GLuint nvglImageHandleGLES2(NVGcontext* ctx, int image)
#elif defined NANOVG_GLES3
GLuint nvglImageHandleGLES3(NVGcontext* ctx, int image)
#endif
{
	GLNVGcontext* gl = (GLNVGcontext*)nvgInternalParams(ctx)->userPtr;
	GLNVGtexture* tex = glnvg__findTexture(gl, image);
	return tex->tex;
}

#if defined(PS5_NATIVE_GPU)
#if defined NANOVG_GL3
int nvglPendingCallCountGL3(NVGcontext* ctx)
{
	GLNVGcontext* gl = (GLNVGcontext*)nvgInternalParams(ctx)->userPtr;
	return gl->ncalls;
}
#endif
#endif
#if defined(PS5_NATIVE_GPU)
#if defined NANOVG_GL3
int nvglPendingBoundsGL3(NVGcontext* ctx, float bounds[4], float view[2])
{
	GLNVGcontext* gl = (GLNVGcontext*)nvgInternalParams(ctx)->userPtr;
	int i;
	if (gl->ncalls <= 0 || gl->nverts <= 0) return 0;
	bounds[0] = bounds[2] = gl->verts[0].x;
	bounds[1] = bounds[3] = gl->verts[0].y;
	for (i = 1; i < gl->nverts; i++) {
		const NVGvertex* v = &gl->verts[i];
		if (v->x < bounds[0]) bounds[0] = v->x;
		if (v->x > bounds[2]) bounds[2] = v->x;
		if (v->y < bounds[1]) bounds[1] = v->y;
		if (v->y > bounds[3]) bounds[3] = v->y;
	}
	view[0] = gl->view[0];
	view[1] = gl->view[1];
	return 1;
}
#endif
#endif
#if defined(PS5_NATIVE_GPU)
#if defined NANOVG_GL3
void nvglSetHdrPqOutputGL3(NVGcontext* ctx, int enabled)
{
	GLNVGcontext* gl = (GLNVGcontext*)nvgInternalParams(ctx)->userPtr;
	gl->hdrPqOutput = enabled ? 1 : 0;
}

int nvglPendingStencilFreeGL3(NVGcontext* ctx)
{
	GLNVGcontext* gl = (GLNVGcontext*)nvgInternalParams(ctx)->userPtr;
	int i;
	for (i = 0; i < gl->ncalls; i++) {
		const int type = gl->calls[i].type;
		if (type != GLNVG_CONVEXFILL && type != GLNVG_TRIANGLES) return 0;
	}
	return 1;
}
#endif
#endif

#endif /* NANOVG_GL_IMPLEMENTATION */
