#include "fitz-internal.h"

#ifdef _WIN32 /* Microsoft Visual C++ */

typedef signed char int8_t;
typedef short int int16_t;
typedef int int32_t;
typedef __int64 int64_t;

typedef unsigned char uint8_t;
typedef unsigned short int uint16_t;
typedef unsigned int uint32_t;

#else
#include <inttypes.h>
#endif

#include <jbig2.h>

typedef struct fz_jbig2d_s fz_jbig2d;

struct fz_jbig2d_s
{
	fz_stream *chain;
	Jbig2Ctx *ctx;
	Jbig2GlobalCtx *gctx;
	Jbig2Image *page;
	int idx;
};

static void
close_jbig2d(fz_context *ctx, void *state_)
{
	fz_jbig2d *state = (fz_jbig2d *)state_;
	if (state->page)
		jbig2_release_page(state->ctx, state->page);
	if (state->gctx)
		jbig2_global_ctx_free(state->gctx);
	jbig2_ctx_free(state->ctx);
	fz_close(state->chain);
	fz_free(ctx, state);
}

static int
read_jbig2d(fz_stream *stm, unsigned char *buf, int len)
{
	fz_jbig2d *state = stm->state;
	unsigned char tmp[4096];
	unsigned char *p = buf;
	unsigned char *ep = buf + len;
	unsigned char *s;
	int x, w, n;

	if (!state->page)
	{
		while (1)
		{
			n = fz_read(state->chain, tmp, sizeof tmp);
			if (n == 0)
				break;
			jbig2_data_in(state->ctx, tmp, n);
		}

		jbig2_complete_page(state->ctx);

		state->page = jbig2_page_out(state->ctx);
		if (!state->page)
			fz_throw(stm->ctx, "jbig2_page_out failed");
	}

	s = state->page->data;
	w = state->page->height * state->page->stride;
	x = state->idx;
	while (p < ep && x < w)
		*p++ = s[x++] ^ 0xff;
	state->idx = x;

	return p - buf;
}

/* SumatraPDF: warn about jbig2dec issues */
static int
error_callback(void *data, const char *msg, Jbig2Severity severity, int32_t seg_idx)
{
	fz_context *ctx = data;
	if (severity == JBIG2_SEVERITY_FATAL)
		fz_warn(ctx, "jbig2dec error: %s (segment %d)", msg, seg_idx);
	else if (severity == JBIG2_SEVERITY_WARNING)
		fz_warn(ctx, "jbig2dec warning: %s (segment %d)", msg, seg_idx);
	return 0;
}

fz_stream *
fz_open_jbig2d(fz_stream *chain, fz_buffer *globals)
{
	fz_jbig2d *state = NULL;
	fz_context *ctx = chain->ctx;

	fz_var(state);

	fz_try(ctx)
	{
		state = fz_malloc_struct(chain->ctx, fz_jbig2d);
		state->ctx = NULL;
		state->gctx = NULL;
		state->chain = chain;
		state->ctx = jbig2_ctx_new(NULL, JBIG2_OPTIONS_EMBEDDED, NULL, error_callback, ctx);
		state->page = NULL;
		state->idx = 0;

		if (globals)
		{
			/* SumatraPDF: TODO: this call leads to memory leaks */
			jbig2_data_in(state->ctx, globals->data, globals->len);
			state->gctx = jbig2_make_global_ctx(state->ctx);
			state->ctx = jbig2_ctx_new(NULL, JBIG2_OPTIONS_EMBEDDED, state->gctx, error_callback, ctx);
		}
	}
	fz_catch(ctx)
	{
		if (state)
		{
			if (state->gctx)
				jbig2_global_ctx_free(state->gctx);
			if (state->ctx)
				jbig2_ctx_free(state->ctx);
		}
		fz_drop_buffer(ctx, globals);
		fz_free(ctx, state);
		fz_close(chain);
		fz_rethrow(ctx);
	}
	fz_drop_buffer(ctx, globals);

	return fz_new_stream(ctx, state, read_jbig2d, close_jbig2d);
}
