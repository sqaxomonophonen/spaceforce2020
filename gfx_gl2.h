#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_GLEXT_PROTOTYPES
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>

#include "common.h"

#include "stb_sprintf.h"

#define EXPAND_QUAD_TO_TRIS(verts) do { memcpy(&(verts)[4], &(verts)[0], sizeof((verts)[0])); memcpy(&(verts)[5], &(verts)[2], sizeof((verts)[0])); } while(0);

#define FLOAT (1)
#define INT   (2)
#define BYTE  (3)

struct vertex_attr {
	int type;
	int do_normalize;
	char* name;
	int element_count;
	int stride;
	void* offset;
};
#define ATTR_FLOATS(t,m) {FLOAT, 0, #m, MEMBER_SIZE(t,m)/sizeof(float), sizeof(t), MEMBER_OFFSET(t,m)}
#define ATTR_BYTES(t,m)  {BYTE,  1, #m, MEMBER_SIZE(t,m)/sizeof(u8), sizeof(t), MEMBER_OFFSET(t,m)}
#define ATTR_END {0}

static void chkgl(const char* file, const int line)
{
	GLenum err = glGetError();
	if (err != GL_NO_ERROR) {
		fprintf(stderr, "OPENGL ERROR 0x%.4x in %s:%d\n", err, file, line);
		abort();
	}
}
#define CHKGL chkgl(__FILE__, __LINE__);

struct gb_texture {
	GLuint texture;
	GLenum format;
};

struct uniform {
	char* name;
	int type;
	int element_count;
	void* offset;
	GLint location;
};

#define UNIFORM_FLOATS(t,m) {#m, FLOAT, MEMBER_SIZE(t,m)/sizeof(float), MEMBER_OFFSET(t,m)}
#define UNIFORM_INTS(t,m)   {#m, INT, MEMBER_SIZE(t,m)/sizeof(float), MEMBER_OFFSET(t,m)}

struct prg {
	GLuint program;
	struct vertex_attr* attrs;
	struct uniform* uniforms;
};

static GLuint create_shader(GLenum type, const char* header, const char* src)
{
	GLuint shader = glCreateShader(type); CHKGL;

	const char* sources[2];
	sources[0] = header;
	sources[1] = src;
	glShaderSource(shader, ARRAY_LENGTH(sources), sources, NULL); CHKGL;
	glCompileShader(shader); CHKGL;

	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status == GL_FALSE) {
		GLint msglen;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &msglen);
		GLchar* msg = (GLchar*) malloc(msglen + 1);
		assert(msg != NULL);
		glGetShaderInfoLog(shader, msglen, NULL, msg);
		const char* stype = type == GL_VERTEX_SHADER ? "VERTEX" : type == GL_FRAGMENT_SHADER ? "FRAGMENT" : "???";
		fprintf(stderr, "%s GLSL COMPILE ERROR: %s in\n\n%s%s\n", stype, msg, header, src);
		abort();
	}

	return shader;
}

static void prg_init(struct prg* prg, const char* header, const char* vert_src, const char* frag_src, struct vertex_attr* attrs, struct uniform* uniforms)
{
	GLuint vs = create_shader(GL_VERTEX_SHADER, header, vert_src);
	GLuint fs = create_shader(GL_FRAGMENT_SHADER, header, frag_src);

	GLuint program = glCreateProgram(); CHKGL;
	glAttachShader(program, vs); CHKGL;
	glAttachShader(program, fs); CHKGL;

	struct vertex_attr* p = attrs;
	int index = 0;
	while (p->type) {
		glBindAttribLocation(program, index++, p->name); CHKGL;
		p++;
	}
	glLinkProgram(program); CHKGL;

	GLint status;
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (status == GL_FALSE) {
		GLint msglen;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &msglen);
		GLchar* msg = (GLchar*) malloc(msglen + 1);
		glGetProgramInfoLog(program, msglen, NULL, msg);
		fprintf(stderr, "shader link error: %s\n", msg);
		abort();
	}

	glDeleteShader(vs); CHKGL;
	glDeleteShader(fs); CHKGL;

	for (struct uniform* u = uniforms; u->name != NULL; u++) {
		u->location = glGetUniformLocation(program, u->name); CHKGL;
	}

	prg->program = program;
	prg->attrs = attrs;
	prg->uniforms = uniforms;
}

static void prg_set_uniforms(struct prg* prg, void* data)
{
	for (struct uniform* u = prg->uniforms; u->name != NULL; u++) {
		GLint loc = u->location;
		int n = u->element_count;
		if (loc < 0) continue;
		void* base = data + (size_t)u->offset;
		if (u->type == FLOAT) {
			GLfloat* x = (GLfloat*)base;
			const int count = 1;
			switch (n) {
			case 1: glUniform1fv(loc, count, x); CHKGL; break;
			case 2: glUniform2fv(loc, count, x); CHKGL; break;
			case 3: glUniform3fv(loc, count, x); CHKGL; break;
			case 4: glUniform4fv(loc, count, x); CHKGL; break;
			default: assert(!"unhandled u->element_count");
			}
		} else if (u->type == INT) {
			GLint* x = (GLint*)base;
			const int count = 1;
			switch (n) {
			case 1: glUniform1iv(loc, count, x); CHKGL; break;
			case 2: glUniform2iv(loc, count, x); CHKGL; break;
			case 3: glUniform3iv(loc, count, x); CHKGL; break;
			case 4: glUniform4iv(loc, count, x); CHKGL; break;
			default: assert(!"unhandled u->element_count");
			}
		} else {
			assert(!"unhandled u->type");
		}
	}
}

static void prg_use(struct prg* prg)
{
	glUseProgram(prg->program); CHKGL;
	for (struct vertex_attr* p = prg->attrs; p->type; p++) {
		GLuint index = p - prg->attrs;
		glEnableVertexAttribArray(index); CHKGL;
	}
	for (struct vertex_attr* p = prg->attrs; p->type; p++) {
		GLuint index = p - prg->attrs;
		GLenum type = 0;
		GLboolean normalized = GL_FALSE;
		switch (p->type) {
		case FLOAT:
			type = GL_FLOAT;
			normalized = GL_FALSE;
			break;
		case INT:
			type = GL_INT;
			normalized = GL_FALSE;
			break;
		case BYTE:
			type = GL_UNSIGNED_BYTE;
			normalized = GL_TRUE;
			break;
		default: BANG;
		}
		glVertexAttribPointer(
			index,
			p->element_count,
			type,
			normalized,
			p->stride,
			(const GLvoid*)p->offset); CHKGL;
	}
}

static void prg_end(struct prg* prg)
{
	for (struct vertex_attr* p = prg->attrs; p->type; p++) {
		GLuint index = p - prg->attrs;
		glDisableVertexAttribArray(index); CHKGL;
	}
}

struct px_vertex {
	float a_index;
};

struct px_uniforms {
	float u_src_resolution[2];
	float u_dst_resolution[2];
	int u_src_texture;
};

struct px {
	struct prg prg;
	struct px_vertex vertices[6];
	GLuint vertices_buf;
	struct px_uniforms uniforms;
	GLuint texture;
	int iteration;
};


static struct vertex_attr px_vertex_attrs[] = {
	ATTR_FLOATS(struct px_vertex, a_index),
	ATTR_END
};

static void px_init(struct px* px)
{
	memset(px, 0, sizeof *px);

	glGenTextures(1, &px->texture); CHKGL;

	glGenBuffers(1, &px->vertices_buf); CHKGL;
	glBindBuffer(GL_ARRAY_BUFFER, px->vertices_buf); CHKGL;
	for (int i = 0; i < 4; i++) px->vertices[i].a_index = (float)i;
	EXPAND_QUAD_TO_TRIS(px->vertices);
	glBufferData(GL_ARRAY_BUFFER, sizeof(px->vertices), px->vertices, GL_STATIC_DRAW); CHKGL;

	char header[4096];

	stbsp_snprintf(header, sizeof header, "");

	const static char* vert_src =
	"uniform vec2 u_src_resolution;\n"
	"uniform vec2 u_dst_resolution;\n"
	"\n"
	"attribute float a_index;\n"
	"\n"
	"varying vec2 v_uv;\n"
	"varying float v_scale;\n"
	"\n"
	"void main(void)\n"
	"{\n"
	"	float src_aspect = u_src_resolution.x / u_src_resolution.y;\n"
	"	float dst_aspect = u_dst_resolution.x / u_dst_resolution.y;\n"
	"	float dx0, dy0, dx1, dy1, scale, margin, margin_norm;\n"
	"	if (src_aspect > dst_aspect) {\n"
	"		dx0 = -1.0;\n"
	"		dx1 = 1.0;\n"
	"		scale = u_dst_resolution.x / u_src_resolution.x;\n"
	"		margin = (u_dst_resolution.y - u_src_resolution.y*scale);\n"
	"		margin_norm = margin / u_dst_resolution.y;\n"
	"		dy0 = -1.0 + margin_norm;\n"
	"		dy1 = 1.0 - margin_norm;\n"
	"	} else {\n"
	"		dy0 = -1.0;\n"
	"		dy1 = 1.0;\n"
	"		scale = u_dst_resolution.y / u_src_resolution.y;\n"
	"		margin = (u_dst_resolution.x - u_src_resolution.x*scale);\n"
	"		margin_norm = margin / u_dst_resolution.x;\n"
	"		dx0 = -1.0 + margin_norm;\n"
	"		dx1 = 1.0 - margin_norm;\n"
	"	}\n"
	"\n"
	"	v_scale = scale;\n"
	"\n"
	"	vec2 p;\n"
	"	vec2 uv0 = vec2(-0.5, -0.5);\n"
	"	vec2 uv1 = uv0 + u_src_resolution;\n"
	"\n"
	"	if (a_index == 0.0) {\n"
	"		/* bottom-left */\n"
	"		p = vec2(dx0, dy0);\n"
	"		v_uv = vec2(uv0.x, uv1.y);\n"
	"	} else if (a_index == 1.0) {\n"
	"		/* bottom-right */\n"
	"		p = vec2(dx1, dy0);\n"
	"		v_uv = vec2(uv1.x, uv1.y);\n"
	"	} else if (a_index == 2.0) {\n"
	"		/* top-right */\n"
	"		p = vec2(dx1, dy1);\n"
	"		v_uv = vec2(uv1.x, uv0.y);\n"
	"	} else if (a_index == 3.0) {\n"
	"		/* top-left */\n"
	"		p = vec2(dx0, dy1);\n"
	"		v_uv = vec2(uv0.x, uv0.y);\n"
	"	}\n"
	"	gl_Position = vec4(p, 0.0, 1.0);\n"
	"}\n"
	;

	const static char* frag_src =
	//"precision highp float;\n"
	//"\n"
	"uniform vec2 u_src_resolution;\n"
	"\n"
	"uniform sampler2D u_src_texture;\n"
	"\n"
	"varying vec2 v_uv;\n"
	"varying float v_scale;\n"
	"\n"
	"void main(void)\n"
	"{\n"
	"	vec2 uv = floor(v_uv) + 0.5;\n"
	"	uv += 1.0 - clamp((1.0 - fract(v_uv)) * v_scale, 0.0, 1.0);\n"
	"	gl_FragColor = texture2D(u_src_texture, uv / u_src_resolution);\n"
	"}\n"
	;

	static struct uniform uniforms[] = {
		UNIFORM_FLOATS(struct px_uniforms, u_src_resolution),
		UNIFORM_FLOATS(struct px_uniforms, u_dst_resolution),
		UNIFORM_INTS(struct px_uniforms, u_src_texture),
		{0},
	};

	prg_init(&px->prg, header, vert_src, frag_src, px_vertex_attrs, uniforms);
}

static void px_present(struct px* px, int dst_width, int dst_height, int src_width, int src_height, void* src_image)
{
	glBindTexture(GL_TEXTURE_2D, px->texture); CHKGL;
	const GLint internal_format = GL_RGBA;
	const GLenum format = GL_RGBA;

	if (px->iteration == 0) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); CHKGL;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); CHKGL;
		glTexImage2D(GL_TEXTURE_2D, 0, internal_format, src_width, src_height, 0, format, GL_UNSIGNED_BYTE, src_image); CHKGL;
	} else {
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, src_width, src_height, format, GL_UNSIGNED_BYTE, src_image); CHKGL;
	}

	prg_use(&px->prg);

	struct px_uniforms* u = &px->uniforms;
	u->u_src_texture = 0;
	u->u_src_resolution[0] = src_width;
	u->u_src_resolution[1] = src_height;
	u->u_dst_resolution[0] = dst_width;
	u->u_dst_resolution[1] = dst_height;

	prg_set_uniforms(&px->prg, u);


	//glBindTexture(GL_TEXTURE_2D, fb->gl_texture); CHKGL;
	//_gfx_state.texture = NULL; // to be safe.. :)

	glBindBuffer(GL_ARRAY_BUFFER, px->vertices_buf); CHKGL;
	glDrawArrays(GL_TRIANGLES, 0, 6); CHKGL;
	//glBindVertexArray(prg_pxscaler.vao); CHKGL;

	prg_end(&px->prg);

	px->iteration++;
}

struct gfx {
	struct px px;
};

static void gfx_init(struct gfx* gfx)
{
	memset(gfx, 0, sizeof *gfx);
	px_init(&gfx->px);
}
