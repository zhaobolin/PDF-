#include "fitz-internal.h"

#define QUANT(x,a) (((int)((x) * (a))) / (a))
#define HSUBPIX 5.0
#define VSUBPIX 5.0

#define STACK_SIZE 96

/* Enable the following to attempt to support knockout and/or isolated
 * blending groups. */
#define ATTEMPT_KNOCKOUT_AND_ISOLATED

/* Enable the following to help debug group blending. */
#undef DUMP_GROUP_BLENDS

typedef struct fz_draw_device_s fz_draw_device;

enum {
	FZ_DRAWDEV_FLAGS_TYPE3 = 1,
};

typedef struct fz_draw_state_s fz_draw_state;

struct fz_draw_state_s {
	fz_bbox scissor;
	fz_pixmap *dest;
	fz_pixmap *mask;
	fz_pixmap *shape;
	int blendmode;
	int luminosity;
	float alpha;
	fz_matrix ctm;
	float xstep, ystep;
	fz_rect area;
};

struct fz_draw_device_s
{
	fz_gel *gel;
	fz_context *ctx;
	int flags;
	int top;
	fz_draw_state *stack;
	int stack_max;
	fz_draw_state init_stack[STACK_SIZE];
};

#ifdef DUMP_GROUP_BLENDS
static int group_dump_count = 0;

static void fz_dump_blend(fz_context *ctx, fz_pixmap *pix, const char *s)
{
	char name[80];

	if (!pix)
		return;

	sprintf(name, "dump%02d.png", group_dump_count);
	if (s)
		printf("%s%02d", s, group_dump_count);
	group_dump_count++;

	fz_write_png(ctx, pix, name, (pix->n > 1));
}

static void dump_spaces(int x, const char *s)
{
	int i;
	for (i = 0; i < x; i++)
		printf(" ");
	printf("%s", s);
}

#endif

static void fz_grow_stack(fz_draw_device *dev)
{
	int max = dev->stack_max * 2;
	fz_draw_state *stack;

	if (dev->stack == &dev->init_stack[0])
	{
		stack = fz_malloc(dev->ctx, sizeof(*stack) * max);
		memcpy(stack, dev->stack, sizeof(*stack) * dev->stack_max);
	}
	else
	{
		stack = fz_resize_array(dev->ctx, dev->stack, max, sizeof(*stack));
	}
	dev->stack = stack;
	dev->stack_max = max;
}

/* 'Push' the stack. Returns a pointer to the current state, with state[1]
 * already having been initialised to contain the same thing. Simply
 * change any contents of state[1] that you want to and continue. */
static fz_draw_state *
push_stack(fz_draw_device *dev)
{
	fz_draw_state *state;

	if (dev->top == dev->stack_max-1)
		fz_grow_stack(dev);
	state = &dev->stack[dev->top];
	dev->top++;
	memcpy(&state[1], state, sizeof(*state));
	return state;
}

static fz_draw_state *
fz_knockout_begin(fz_draw_device *dev)
{
	fz_context *ctx = dev->ctx;
	fz_bbox bbox;
	fz_pixmap *dest, *shape;
	fz_draw_state *state = &dev->stack[dev->top];
	int isolated = state->blendmode & FZ_BLEND_ISOLATED;

	if ((state->blendmode & FZ_BLEND_KNOCKOUT) == 0)
		return state;

	state = push_stack(dev);

	bbox = fz_pixmap_bbox(dev->ctx, state->dest);
	bbox = fz_intersect_bbox(bbox, state->scissor);
	dest = fz_new_pixmap_with_bbox(dev->ctx, state->dest->colorspace, bbox);

	if (isolated)
	{
		fz_clear_pixmap(ctx, dest);
	}
	else
	{
		/* Find the last but one destination to copy */
		int i = dev->top-1; /* i = the one on entry (i.e. the last one) */
		fz_pixmap *prev = state->dest;
		while (i > 0)
		{
			prev = dev->stack[--i].dest;
			if (prev != state->dest)
				break;
		}
		if (prev)
			fz_copy_pixmap_rect(ctx, dest, prev, bbox);
		else
			fz_clear_pixmap(ctx, dest);
	}

	if (state->blendmode == 0 && isolated)
	{
		/* We can render direct to any existing shape plane. If there
		 * isn't one, we don't need to make one. */
		shape = state->shape;
	}
	else
	{
		shape = fz_new_pixmap_with_bbox(dev->ctx, NULL, bbox);
		fz_clear_pixmap(dev->ctx, shape);
	}
#ifdef DUMP_GROUP_BLENDS
	dump_spaces(dev->top-1, "Knockout begin\n");
#endif
	state[1].scissor = bbox;
	state[1].dest = dest;
	state[1].shape = shape;
	state[1].blendmode &= ~FZ_BLEND_MODEMASK;

	return &state[1];
}

static void fz_knockout_end(fz_draw_device *dev)
{
	fz_draw_state *state;
	int blendmode;
	int isolated;
	fz_context *ctx = dev->ctx;

	if (dev->top == 0)
	{
		fz_warn(ctx, "unexpected knockout end");
		return;
	}
	state = &dev->stack[--dev->top];
	if ((state[0].blendmode & FZ_BLEND_KNOCKOUT) == 0)
		return;

	blendmode = state->blendmode & FZ_BLEND_MODEMASK;
	isolated = state->blendmode & FZ_BLEND_ISOLATED;

#ifdef DUMP_GROUP_BLENDS
	dump_spaces(dev->top, "");
	fz_dump_blend(dev->ctx, state[1].dest, "Knockout end: blending ");
	if (state[1].shape)
		fz_dump_blend(dev->ctx, state[1].shape, "/");
	fz_dump_blend(dev->ctx, state[0].dest, " onto ");
	if (state[0].shape)
		fz_dump_blend(dev->ctx, state[0].shape, "/");
	if (blendmode != 0)
		printf(" (blend %d)", blendmode);
	if (isolated != 0)
		printf(" (isolated)");
	printf(" (knockout)");
#endif
	if ((blendmode == 0) && (state[0].shape == state[1].shape))
		fz_paint_pixmap(state[0].dest, state[1].dest, 255);
	else
		fz_blend_pixmap(state[0].dest, state[1].dest, 255, blendmode, isolated, state[1].shape);

	fz_drop_pixmap(dev->ctx, state[1].dest);
	if (state[0].shape != state[1].shape)
	{
		if (state[0].shape)
			fz_paint_pixmap(state[0].shape, state[1].shape, 255);
		fz_drop_pixmap(dev->ctx, state[1].shape);
	}
#ifdef DUMP_GROUP_BLENDS
	fz_dump_blend(dev->ctx, state[0].dest, " to get ");
	if (state[0].shape)
		fz_dump_blend(dev->ctx, state[0].shape, "/");
	printf("\n");
#endif
}

static void
fz_draw_fill_path(fz_device *devp, fz_path *path, int even_odd, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha)//���·��
{
	fz_draw_device *dev = devp->user;
	float expansion = fz_matrix_expansion(ctm);
	float flatness = 0.3f / expansion;
	unsigned char colorbv[FZ_MAX_COLORS + 1];
	float colorfv[FZ_MAX_COLORS];
	fz_bbox bbox;
	int i;
	fz_draw_state *state = &dev->stack[dev->top];
	fz_colorspace *model = state->dest->colorspace;

	fz_reset_gel(dev->gel, state->scissor);
	fz_flatten_fill_path(dev->gel, path, ctm, flatness);
	fz_sort_gel(dev->gel);

	bbox = fz_bound_gel(dev->gel);
	bbox = fz_intersect_bbox(bbox, state->scissor);

	if (fz_is_empty_rect(bbox))//��ͼ����Ϊ��
		return;

	if (state->blendmode & FZ_BLEND_KNOCKOUT)
		state = fz_knockout_begin(dev);

	fz_convert_color(dev->ctx, model, colorfv, colorspace, color);
	for (i = 0; i < model->n; i++)
		colorbv[i] = colorfv[i] * 255;
	colorbv[i] = alpha * 255;

	fz_scan_convert(dev->gel, even_odd, bbox, state->dest, colorbv);
	if (state->shape)
	{
		fz_reset_gel(dev->gel, state->scissor);
		fz_flatten_fill_path(dev->gel, path, ctm, flatness);
		fz_sort_gel(dev->gel);

		colorbv[0] = alpha * 255;
		fz_scan_convert(dev->gel, even_odd, bbox, state->shape, colorbv);
	}

	if (state->blendmode & FZ_BLEND_KNOCKOUT)
		fz_knockout_end(dev);
}

static void
fz_draw_stroke_path(fz_device *devp, fz_path *path, fz_stroke_state *stroke, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_draw_device *dev = devp->user;
	float expansion = fz_matrix_expansion(ctm);
	float flatness = 0.3f / expansion;
	float linewidth = stroke->linewidth;
	unsigned char colorbv[FZ_MAX_COLORS + 1];
	float colorfv[FZ_MAX_COLORS];
	fz_bbox bbox;
	int i;
	fz_draw_state *state = &dev->stack[dev->top];
	fz_colorspace *model = state->dest->colorspace;

	if (linewidth * expansion < 0.1f)
		linewidth = 1 / expansion;

	fz_reset_gel(dev->gel, state->scissor);
	if (stroke->dash_len > 0)
		fz_flatten_dash_path(dev->gel, path, stroke, ctm, flatness, linewidth);
	else
		fz_flatten_stroke_path(dev->gel, path, stroke, ctm, flatness, linewidth);
	fz_sort_gel(dev->gel);

	bbox = fz_bound_gel(dev->gel);
	bbox = fz_intersect_bbox(bbox, state->scissor);

	if (fz_is_empty_rect(bbox))
		return;

	if (state->blendmode & FZ_BLEND_KNOCKOUT)
		state = fz_knockout_begin(dev);

	fz_convert_color(dev->ctx, model, colorfv, colorspace, color);
	for (i = 0; i < model->n; i++)
		colorbv[i] = colorfv[i] * 255;
	colorbv[i] = alpha * 255;

	fz_scan_convert(dev->gel, 0, bbox, state->dest, colorbv);
	if (state->shape)
	{
		fz_reset_gel(dev->gel, state->scissor);
		if (stroke->dash_len > 0)
			fz_flatten_dash_path(dev->gel, path, stroke, ctm, flatness, linewidth);
		else
			fz_flatten_stroke_path(dev->gel, path, stroke, ctm, flatness, linewidth);
		fz_sort_gel(dev->gel);

		colorbv[0] = 255;
		fz_scan_convert(dev->gel, 0, bbox, state->shape, colorbv);
	}

	if (state->blendmode & FZ_BLEND_KNOCKOUT)
		fz_knockout_end(dev);
}

static void
fz_draw_clip_path(fz_device *devp, fz_path *path, fz_rect *rect, int even_odd, fz_matrix ctm)
{
	fz_draw_device *dev = devp->user;
	float expansion = fz_matrix_expansion(ctm);
	float flatness = 0.3f / expansion;
	fz_bbox bbox;
	fz_draw_state *state = push_stack(dev);
	fz_colorspace *model = state->dest->colorspace;

	fz_reset_gel(dev->gel, state->scissor);
	fz_flatten_fill_path(dev->gel, path, ctm, flatness);
	fz_sort_gel(dev->gel);

	bbox = fz_bound_gel(dev->gel);
	bbox = fz_intersect_bbox(bbox, state->scissor);
	if (rect)
		bbox = fz_intersect_bbox(bbox, fz_bbox_covering_rect(*rect));

	if (fz_is_empty_rect(bbox) || fz_is_rect_gel(dev->gel))
	{
		state[1].scissor = bbox;
		state[1].mask = NULL;
#ifdef DUMP_GROUP_BLENDS
		dump_spaces(dev->top-1, "Clip (rectangular) begin\n");
#endif
		return;
	}

	state[1].mask = fz_new_pixmap_with_bbox(dev->ctx, NULL, bbox);
	fz_clear_pixmap(dev->ctx, state[1].mask);
	state[1].dest = fz_new_pixmap_with_bbox(dev->ctx, model, bbox);
	fz_clear_pixmap(dev->ctx, state[1].dest);
	if (state[1].shape)
	{
		state[1].shape = fz_new_pixmap_with_bbox(dev->ctx, NULL, bbox);
		fz_clear_pixmap(dev->ctx, state[1].shape);
	}

	fz_scan_convert(dev->gel, even_odd, bbox, state[1].mask, NULL);

	state[1].blendmode |= FZ_BLEND_ISOLATED;
	state[1].scissor = bbox;
#ifdef DUMP_GROUP_BLENDS
	dump_spaces(dev->top-1, "Clip (non-rectangular) begin\n");
#endif
}

static void
fz_draw_clip_stroke_path(fz_device *devp, fz_path *path, fz_rect *rect, fz_stroke_state *stroke, fz_matrix ctm)
{
	fz_draw_device *dev = devp->user;
	float expansion = fz_matrix_expansion(ctm);
	float flatness = 0.3f / expansion;
	float linewidth = stroke->linewidth;
	fz_bbox bbox;
	fz_draw_state *state = push_stack(dev);
	fz_colorspace *model = state->dest->colorspace;

	if (linewidth * expansion < 0.1f)
		linewidth = 1 / expansion;

	fz_reset_gel(dev->gel, state->scissor);
	if (stroke->dash_len > 0)
		fz_flatten_dash_path(dev->gel, path, stroke, ctm, flatness, linewidth);
	else
		fz_flatten_stroke_path(dev->gel, path, stroke, ctm, flatness, linewidth);
	fz_sort_gel(dev->gel);

	bbox = fz_bound_gel(dev->gel);
	bbox = fz_intersect_bbox(bbox, state->scissor);
	if (rect)
		bbox = fz_intersect_bbox(bbox, fz_bbox_covering_rect(*rect));

	state[1].mask = fz_new_pixmap_with_bbox(dev->ctx, NULL, bbox);
	fz_clear_pixmap(dev->ctx, state[1].mask);
	state[1].dest = fz_new_pixmap_with_bbox(dev->ctx, model, bbox);
	fz_clear_pixmap(dev->ctx, state[1].dest);
	if (state->shape)
	{
		state[1].shape = fz_new_pixmap_with_bbox(dev->ctx, NULL, bbox);
		fz_clear_pixmap(dev->ctx, state[1].shape);
	}

	if (!fz_is_empty_rect(bbox))
		fz_scan_convert(dev->gel, 0, bbox, state[1].mask, NULL);

	state[1].blendmode |= FZ_BLEND_ISOLATED;
	state[1].scissor = bbox;
#ifdef DUMP_GROUP_BLENDS
	dump_spaces(dev->top-1, "Clip (stroke) begin\n");
#endif
}

static void
draw_glyph(unsigned char *colorbv, fz_pixmap *dst, fz_pixmap *msk,
	int xorig, int yorig, fz_bbox scissor)
{
	unsigned char *dp, *mp;
	fz_bbox bbox;
	int x, y, w, h;

	bbox = fz_pixmap_bbox_no_ctx(msk);
	bbox.x0 += xorig;
	bbox.y0 += yorig;
	bbox.x1 += xorig;
	bbox.y1 += yorig;

	bbox = fz_intersect_bbox(bbox, scissor); /* scissor < dst */
	x = bbox.x0;
	y = bbox.y0;
	w = bbox.x1 - bbox.x0;
	h = bbox.y1 - bbox.y0;

	mp = msk->samples + ((y - msk->y - yorig) * msk->w + (x - msk->x - xorig));
	dp = dst->samples + ((y - dst->y) * dst->w + (x - dst->x)) * dst->n;

	assert(msk->n == 1);

	while (h--)
	{
		if (dst->colorspace)
			fz_paint_span_with_color(dp, mp, dst->n, w, colorbv);
		else
			fz_paint_span(dp, mp, 1, w, 255);
		dp += dst->w * dst->n;
		mp += msk->w;
	}
}

static void
fz_draw_fill_text(fz_device *devp, fz_text *text, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_draw_device *dev = devp->user;
	unsigned char colorbv[FZ_MAX_COLORS + 1];
	unsigned char shapebv;
	float colorfv[FZ_MAX_COLORS];
	fz_matrix tm, trm;
	fz_pixmap *glyph;
	int i, x, y, gid;
	fz_draw_state *state = &dev->stack[dev->top];
	fz_colorspace *model = state->dest->colorspace;

	if (state->blendmode & FZ_BLEND_KNOCKOUT)
		state = fz_knockout_begin(dev);

	fz_convert_color(dev->ctx, model, colorfv, colorspace, color);
	for (i = 0; i < model->n; i++)
		colorbv[i] = colorfv[i] * 255;
	colorbv[i] = alpha * 255;
	shapebv = 255;

	tm = text->trm;

	for (i = 0; i < text->len; i++)
	{
		gid = text->items[i].gid;
		if (gid < 0)
			continue;

		tm.e = text->items[i].x;
		tm.f = text->items[i].y;
		trm = fz_concat(tm, ctm);
		x = floorf(trm.e);
		y = floorf(trm.f);
		trm.e = QUANT(trm.e - floorf(trm.e), HSUBPIX);
		trm.f = QUANT(trm.f - floorf(trm.f), VSUBPIX);

		glyph = fz_render_glyph(dev->ctx, text->font, gid, trm, model);
		if (glyph)
		{
			if (glyph->n == 1)
			{
				draw_glyph(colorbv, state->dest, glyph, x, y, state->scissor);
				if (state->shape)
					draw_glyph(&shapebv, state->shape, glyph, x, y, state->scissor);
			}
			else
			{
				fz_matrix ctm = {glyph->w, 0.0, 0.0, glyph->h, x + glyph->x, y + glyph->y};
				fz_paint_image(state->dest, state->scissor, state->shape, glyph, ctm, alpha * 255);
			}
			fz_drop_pixmap(dev->ctx, glyph);
		}
	}

	if (state->blendmode & FZ_BLEND_KNOCKOUT)
		fz_knockout_end(dev);
}

static void
fz_draw_stroke_text(fz_device *devp, fz_text *text, fz_stroke_state *stroke, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_draw_device *dev = devp->user;
	unsigned char colorbv[FZ_MAX_COLORS + 1];
	float colorfv[FZ_MAX_COLORS];
	fz_matrix tm, trm;
	fz_pixmap *glyph;
	int i, x, y, gid;
	fz_draw_state *state = &dev->stack[dev->top];
	fz_colorspace *model = state->dest->colorspace;

	if (state->blendmode & FZ_BLEND_KNOCKOUT)
		state = fz_knockout_begin(dev);

	fz_convert_color(dev->ctx, model, colorfv, colorspace, color);
	for (i = 0; i < model->n; i++)
		colorbv[i] = colorfv[i] * 255;
	colorbv[i] = alpha * 255;

	tm = text->trm;

	for (i = 0; i < text->len; i++)
	{
		gid = text->items[i].gid;
		if (gid < 0)
			continue;

		tm.e = text->items[i].x;
		tm.f = text->items[i].y;
		trm = fz_concat(tm, ctm);
		x = floorf(trm.e);
		y = floorf(trm.f);
		trm.e = QUANT(trm.e - floorf(trm.e), HSUBPIX);
		trm.f = QUANT(trm.f - floorf(trm.f), VSUBPIX);

		glyph = fz_render_stroked_glyph(dev->ctx, text->font, gid, trm, ctm, stroke);
		if (glyph)
		{
			draw_glyph(colorbv, state->dest, glyph, x, y, state->scissor);
			if (state->shape)
				draw_glyph(colorbv, state->shape, glyph, x, y, state->scissor);
			fz_drop_pixmap(dev->ctx, glyph);
		}
	}

	if (state->blendmode & FZ_BLEND_KNOCKOUT)
		fz_knockout_end(dev);
}

static void
fz_draw_clip_text(fz_device *devp, fz_text *text, fz_matrix ctm, int accumulate)
{
	fz_draw_device *dev = devp->user;
	fz_bbox bbox;
	fz_pixmap *mask, *dest, *shape;
	fz_matrix tm, trm;
	fz_pixmap *glyph;
	int i, x, y, gid;
	fz_draw_state *state;
	fz_colorspace *model;

	/* If accumulate == 0 then this text object is guaranteed complete */
	/* If accumulate == 1 then this text object is the first (or only) in a sequence */
	/* If accumulate == 2 then this text object is a continuation */

	state = push_stack(dev);
	model = state->dest->colorspace;

	if (accumulate == 0)
	{
		/* make the mask the exact size needed */
		bbox = fz_bbox_covering_rect(fz_bound_text(dev->ctx, text, ctm));
		bbox = fz_intersect_bbox(bbox, state->scissor);
	}
	else
	{
		/* be conservative about the size of the mask needed */
		bbox = state->scissor;
	}

	if (accumulate == 0 || accumulate == 1)
	{
		mask = fz_new_pixmap_with_bbox(dev->ctx, NULL, bbox);
		fz_clear_pixmap(dev->ctx, mask);
		dest = fz_new_pixmap_with_bbox(dev->ctx, model, bbox);
		fz_clear_pixmap(dev->ctx, dest);
		if (state->shape)
		{
			shape = fz_new_pixmap_with_bbox(dev->ctx, NULL, bbox);
			fz_clear_pixmap(dev->ctx, shape);
		}
		else
			shape = NULL;

		state[1].blendmode |= FZ_BLEND_ISOLATED;
		state[1].scissor = bbox;
		state[1].dest = dest;
		state[1].mask = mask;
		state[1].shape = shape;
#ifdef DUMP_GROUP_BLENDS
		dump_spaces(dev->top-1, "Clip (text) begin\n");
#endif
	}
	else
	{
		mask = state->mask;
		dev->top--;
	}

	if (!fz_is_empty_rect(bbox) && mask)
	{
		tm = text->trm;

		for (i = 0; i < text->len; i++)
		{
			gid = text->items[i].gid;
			if (gid < 0)
				continue;

			tm.e = text->items[i].x;
			tm.f = text->items[i].y;
			trm = fz_concat(tm, ctm);
			x = floorf(trm.e);
			y = floorf(trm.f);
			trm.e = QUANT(trm.e - floorf(trm.e), HSUBPIX);
			trm.f = QUANT(trm.f - floorf(trm.f), VSUBPIX);

			glyph = fz_render_glyph(dev->ctx, text->font, gid, trm, model);
			if (glyph)
			{
				draw_glyph(NULL, mask, glyph, x, y, bbox);
				if (state[1].shape)
					draw_glyph(NULL, state[1].shape, glyph, x, y, bbox);
				fz_drop_pixmap(dev->ctx, glyph);
			}
		}
	}
}

static void
fz_draw_clip_stroke_text(fz_device *devp, fz_text *text, fz_stroke_state *stroke, fz_matrix ctm)
{
	fz_draw_device *dev = devp->user;
	fz_bbox bbox;
	fz_pixmap *mask, *dest, *shape;
	fz_matrix tm, trm;
	fz_pixmap *glyph;
	int i, x, y, gid;
	fz_draw_state *state = push_stack(dev);
	fz_colorspace *model = state->dest->colorspace;

	/* make the mask the exact size needed */
	bbox = fz_bbox_covering_rect(fz_bound_text(dev->ctx, text, ctm));
	bbox = fz_intersect_bbox(bbox, state->scissor);

	mask = fz_new_pixmap_with_bbox(dev->ctx, NULL, bbox);
	fz_clear_pixmap(dev->ctx, mask);
	dest = fz_new_pixmap_with_bbox(dev->ctx, model, bbox);
	fz_clear_pixmap(dev->ctx, dest);
	if (state->shape)
	{
		shape = fz_new_pixmap_with_bbox(dev->ctx, NULL, bbox);
		fz_clear_pixmap(dev->ctx, shape);
	}
	else
		shape = state->shape;

	state[1].blendmode |= FZ_BLEND_ISOLATED;
	state[1].scissor = bbox;
	state[1].dest = dest;
	state[1].shape = shape;
	state[1].mask = mask;
#ifdef DUMP_GROUP_BLENDS
	dump_spaces(dev->top-1, "Clip (stroke text) begin\n");
#endif

	if (!fz_is_empty_rect(bbox))
	{
		tm = text->trm;

		for (i = 0; i < text->len; i++)
		{
			gid = text->items[i].gid;
			if (gid < 0)
				continue;

			tm.e = text->items[i].x;
			tm.f = text->items[i].y;
			trm = fz_concat(tm, ctm);
			x = floorf(trm.e);
			y = floorf(trm.f);
			trm.e = QUANT(trm.e - floorf(trm.e), HSUBPIX);
			trm.f = QUANT(trm.f - floorf(trm.f), VSUBPIX);

			glyph = fz_render_stroked_glyph(dev->ctx, text->font, gid, trm, ctm, stroke);
			if (glyph)
			{
				draw_glyph(NULL, mask, glyph, x, y, bbox);
				if (shape)
					draw_glyph(NULL, shape, glyph, x, y, bbox);
				fz_drop_pixmap(dev->ctx, glyph);
			}
		}
	}
}

static void
fz_draw_ignore_text(fz_device *dev, fz_text *text, fz_matrix ctm)
{
}

static void
fz_draw_fill_shade(fz_device *devp, fz_shade *shade, fz_matrix ctm, float alpha)
{
	fz_draw_device *dev = devp->user;
	fz_rect bounds;
	fz_bbox bbox, scissor;
	fz_pixmap *dest, *shape;
	float colorfv[FZ_MAX_COLORS];
	unsigned char colorbv[FZ_MAX_COLORS + 1];
	fz_draw_state *state = &dev->stack[dev->top];
	fz_colorspace *model = state->dest->colorspace;

	bounds = fz_bound_shade(dev->ctx, shade, ctm);
	scissor = state->scissor;
	bbox = fz_intersect_bbox(fz_bbox_covering_rect(bounds), scissor);

	if (fz_is_empty_rect(bbox))
		return;

	if (!model)
	{
		fz_warn(dev->ctx, "cannot render shading directly to an alpha mask");
		return;
	}

	if (state->blendmode & FZ_BLEND_KNOCKOUT)
		state = fz_knockout_begin(dev);

	dest = state->dest;
	shape = state->shape;

	if (alpha < 1)
	{
		dest = fz_new_pixmap_with_bbox(dev->ctx, state->dest->colorspace, bbox);
		fz_clear_pixmap(dev->ctx, dest);
		if (shape)
		{
			shape = fz_new_pixmap_with_bbox(dev->ctx, NULL, bbox);
			fz_clear_pixmap(dev->ctx, shape);
		}
	}

	if (shade->use_background)
	{
		unsigned char *s;
		int x, y, n, i;
		fz_convert_color(dev->ctx, model, colorfv, shade->colorspace, shade->background);
		for (i = 0; i < model->n; i++)
			colorbv[i] = colorfv[i] * 255;
		colorbv[i] = 255;

		n = dest->n;
		for (y = scissor.y0; y < scissor.y1; y++)
		{
			s = dest->samples + ((scissor.x0 - dest->x) + (y - dest->y) * dest->w) * dest->n;
			for (x = scissor.x0; x < scissor.x1; x++)
			{
				for (i = 0; i < n; i++)
					*s++ = colorbv[i];
			}
		}
		if (shape)
		{
			for (y = scissor.y0; y < scissor.y1; y++)
			{
				s = shape->samples + (scissor.x0 - shape->x) + (y - shape->y) * shape->w;
				for (x = scissor.x0; x < scissor.x1; x++)
				{
					*s++ = 255;
				}
			}
		}
	}

	fz_paint_shade(dev->ctx, shade, ctm, dest, bbox);
	if (shape)
		fz_clear_pixmap_rect_with_value(dev->ctx, shape, 255, bbox);

	if (alpha < 1)
	{
		fz_paint_pixmap(state->dest, dest, alpha * 255);
		fz_drop_pixmap(dev->ctx, dest);
		if (shape)
		{
			fz_paint_pixmap(state->shape, shape, alpha * 255);
			fz_drop_pixmap(dev->ctx, shape);
		}
	}

	if (state->blendmode & FZ_BLEND_KNOCKOUT)
		fz_knockout_end(dev);
}

static fz_pixmap *
fz_transform_pixmap(fz_context *ctx, fz_pixmap *image, fz_matrix *ctm, int x, int y, int dx, int dy, int gridfit, fz_bbox *clip)
{
	fz_pixmap *scaled;

	if (ctm->a != 0 && ctm->b == 0 && ctm->c == 0 && ctm->d != 0)
	{
		/* Unrotated or X-flip or Y-flip or XY-flip */
		fz_matrix m = *ctm;
		if (gridfit)
			fz_gridfit_matrix(&m);
		scaled = fz_scale_pixmap(ctx, image, m.e, m.f, m.a, m.d, clip);
		if (!scaled)
			return NULL;
		ctm->a = scaled->w;
		ctm->d = scaled->h;
		ctm->e = scaled->x;
		ctm->f = scaled->y;
		return scaled;
	}

	if (ctm->a == 0 && ctm->b != 0 && ctm->c != 0 && ctm->d == 0)
	{
		/* Other orthogonal flip/rotation cases */
		fz_matrix m = *ctm;
		fz_bbox rclip;
		if (gridfit)
			fz_gridfit_matrix(&m);
		if (clip)
		{
			rclip.x0 = clip->y0;
			rclip.y0 = clip->x0;
			rclip.x1 = clip->y1;
			rclip.y1 = clip->x1;
		}
		scaled = fz_scale_pixmap(ctx, image, m.f, m.e, m.b, m.c, (clip ? &rclip : 0));
		if (!scaled)
			return NULL;
		ctm->b = scaled->w;
		ctm->c = scaled->h;
		ctm->f = scaled->x;
		ctm->e = scaled->y;
		return scaled;
	}

	/* Downscale, non rectilinear case */
	if (dx > 0 && dy > 0)
	{
		scaled = fz_scale_pixmap(ctx, image, 0, 0, (float)dx, (float)dy, NULL);
		return scaled;
	}

	return NULL;
}

static void
fz_draw_fill_image(fz_device *devp, fz_image *image, fz_matrix ctm, float alpha)
{
	fz_draw_device *dev = devp->user;
	fz_pixmap *converted = NULL;
	fz_pixmap *scaled = NULL;
	fz_pixmap *pixmap;
	fz_pixmap *orig_pixmap;
	int after;
	int dx, dy;
	fz_context *ctx = dev->ctx;
	fz_draw_state *state = &dev->stack[dev->top];
	fz_colorspace *model = state->dest->colorspace;
	fz_bbox clip = fz_pixmap_bbox(ctx, state->dest);

	clip = fz_intersect_bbox(clip, state->scissor);

	fz_var(scaled);

	if (!model)
	{
		fz_warn(dev->ctx, "cannot render image directly to an alpha mask");
		return;
	}

	if (image->w == 0 || image->h == 0)
		return;

	dx = sqrtf(ctm.a * ctm.a + ctm.b * ctm.b);
	dy = sqrtf(ctm.c * ctm.c + ctm.d * ctm.d);

	pixmap = fz_image_to_pixmap(ctx, image, dx, dy);
	orig_pixmap = pixmap;

	/* convert images with more components (cmyk->rgb) before scaling */
	/* convert images with fewer components (gray->rgb after scaling */
	/* convert images with expensive colorspace transforms after scaling */

	fz_try(ctx)
	{
		if (state->blendmode & FZ_BLEND_KNOCKOUT)
			state = fz_knockout_begin(dev);

		after = 0;
		if (pixmap->colorspace == fz_device_gray)
			after = 1;

		if (pixmap->colorspace != model && !after)
		{
			converted = fz_new_pixmap_with_bbox(ctx, model, fz_pixmap_bbox(ctx, pixmap));
			fz_convert_pixmap(ctx, converted, pixmap);
			pixmap = converted;
		}

		if (dx < pixmap->w && dy < pixmap->h)
		{
			int gridfit = alpha == 1.0f && !(dev->flags & FZ_DRAWDEV_FLAGS_TYPE3);
			scaled = fz_transform_pixmap(ctx, pixmap, &ctm, state->dest->x, state->dest->y, dx, dy, gridfit, &clip);
			if (!scaled)
			{
				if (dx < 1)
					dx = 1;
				if (dy < 1)
					dy = 1;
				scaled = fz_scale_pixmap(ctx, pixmap, pixmap->x, pixmap->y, dx, dy, NULL);
			}
			if (scaled)
				pixmap = scaled;
		}

		if (pixmap->colorspace != model)
		{
			if ((pixmap->colorspace == fz_device_gray && model == fz_device_rgb) ||
				(pixmap->colorspace == fz_device_gray && model == fz_device_bgr))
			{
				/* We have special case rendering code for gray -> rgb/bgr */
			}
			else
			{
				converted = fz_new_pixmap_with_bbox(ctx, model, fz_pixmap_bbox(ctx, pixmap));
				fz_convert_pixmap(ctx, converted, pixmap);
				pixmap = converted;
			}
		}

		fz_paint_image(state->dest, state->scissor, state->shape, pixmap, ctm, alpha * 255);

		if (state->blendmode & FZ_BLEND_KNOCKOUT)
			fz_knockout_end(dev);
	}
	fz_always(ctx)
	{
		fz_drop_pixmap(ctx, scaled);
		fz_drop_pixmap(ctx, converted);
		fz_drop_pixmap(ctx, orig_pixmap);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}

static void
fz_draw_fill_image_mask(fz_device *devp, fz_image *image, fz_matrix ctm,
	fz_colorspace *colorspace, float *color, float alpha)
{
	fz_draw_device *dev = devp->user;
	unsigned char colorbv[FZ_MAX_COLORS + 1];
	float colorfv[FZ_MAX_COLORS];
	fz_pixmap *scaled = NULL;
	fz_pixmap *pixmap;
	fz_pixmap *orig_pixmap;
	int dx, dy;
	int i;
	fz_context *ctx = dev->ctx;
	fz_draw_state *state = &dev->stack[dev->top];
	fz_colorspace *model = state->dest->colorspace;
	fz_bbox clip = fz_pixmap_bbox(ctx, state->dest);

	clip = fz_intersect_bbox(clip, state->scissor);

	if (image->w == 0 || image->h == 0)
		return;

	dx = sqrtf(ctm.a * ctm.a + ctm.b * ctm.b);
	dy = sqrtf(ctm.c * ctm.c + ctm.d * ctm.d);
	pixmap = fz_image_to_pixmap(ctx, image, dx, dy);
	orig_pixmap = pixmap;

	fz_try(ctx)
	{
		if (state->blendmode & FZ_BLEND_KNOCKOUT)
			state = fz_knockout_begin(dev);

		if (dx < pixmap->w && dy < pixmap->h)
		{
			int gridfit = alpha == 1.0f && !(dev->flags & FZ_DRAWDEV_FLAGS_TYPE3);
			scaled = fz_transform_pixmap(dev->ctx, pixmap, &ctm, state->dest->x, state->dest->y, dx, dy, gridfit, &clip);
			if (!scaled)
			{
				if (dx < 1)
					dx = 1;
				if (dy < 1)
					dy = 1;
				scaled = fz_scale_pixmap(dev->ctx, pixmap, pixmap->x, pixmap->y, dx, dy, NULL);
			}
			if (scaled)
				pixmap = scaled;
		}

		fz_convert_color(dev->ctx, model, colorfv, colorspace, color);
		for (i = 0; i < model->n; i++)
			colorbv[i] = colorfv[i] * 255;
		colorbv[i] = alpha * 255;

		fz_paint_image_with_color(state->dest, state->scissor, state->shape, pixmap, ctm, colorbv);

		if (scaled)
			fz_drop_pixmap(dev->ctx, scaled);

		if (state->blendmode & FZ_BLEND_KNOCKOUT)
			fz_knockout_end(dev);
	}
	fz_always(ctx)
	{
		fz_drop_pixmap(dev->ctx, orig_pixmap);
	}
	fz_catch(ctx)
	{
		fz_rethrow(ctx);
	}
}

static void
fz_draw_clip_image_mask(fz_device *devp, fz_image *image, fz_rect *rect, fz_matrix ctm)
{
	fz_draw_device *dev = devp->user;
	fz_context *ctx = dev->ctx;
	fz_bbox bbox;
	fz_pixmap *mask = NULL;
	fz_pixmap *dest = NULL;
	fz_pixmap *shape = NULL;
	fz_pixmap *scaled = NULL;
	fz_pixmap *pixmap;
	fz_pixmap *orig_pixmap;
	int dx, dy;
	fz_draw_state *state = push_stack(dev);
	fz_colorspace *model = state->dest->colorspace;
	fz_bbox clip = fz_pixmap_bbox(ctx, state->dest);

	clip = fz_intersect_bbox(clip, state->scissor);

	fz_var(mask);
	fz_var(dest);
	fz_var(shape);

	if (image->w == 0 || image->h == 0)
	{
#ifdef DUMP_GROUP_BLENDS
		dump_spaces(dev->top-1, "Clip (image mask) (empty) begin\n");
#endif
		state[1].scissor = fz_empty_bbox;
		state[1].mask = NULL;
		return;
	}

#ifdef DUMP_GROUP_BLENDS
	dump_spaces(dev->top-1, "Clip (image mask) begin\n");
#endif

	bbox = fz_bbox_covering_rect(fz_transform_rect(ctm, fz_unit_rect));
	bbox = fz_intersect_bbox(bbox, state->scissor);
	if (rect)
		bbox = fz_intersect_bbox(bbox, fz_bbox_covering_rect(*rect));

	dx = sqrtf(ctm.a * ctm.a + ctm.b * ctm.b);
	dy = sqrtf(ctm.c * ctm.c + ctm.d * ctm.d);
	pixmap = fz_image_to_pixmap(ctx, image, dx, dy);
	orig_pixmap = pixmap;

	fz_try(ctx)
	{
		mask = fz_new_pixmap_with_bbox(dev->ctx, NULL, bbox);
		fz_clear_pixmap(dev->ctx, mask);

		dest = fz_new_pixmap_with_bbox(dev->ctx, model, bbox);
		fz_clear_pixmap(dev->ctx, dest);
		if (state->shape)
		{
			shape = fz_new_pixmap_with_bbox(dev->ctx, NULL, bbox);
			fz_clear_pixmap(dev->ctx, shape);
		}

		if (dx < pixmap->w && dy < pixmap->h)
		{
			int gridfit = !(dev->flags & FZ_DRAWDEV_FLAGS_TYPE3);
			scaled = fz_transform_pixmap(dev->ctx, pixmap, &ctm, state->dest->x, state->dest->y, dx, dy, gridfit, &clip);
			if (!scaled)
			{
				if (dx < 1)
					dx = 1;
				if (dy < 1)
					dy = 1;
				scaled = fz_scale_pixmap(dev->ctx, pixmap, pixmap->x, pixmap->y, dx, dy, NULL);
			}
			if (scaled)
				pixmap = scaled;
		}
		fz_paint_image(mask, bbox, state->shape, pixmap, ctm, 255);

	}
	fz_always(ctx)
	{
		fz_drop_pixmap(ctx, scaled);
		fz_drop_pixmap(ctx, orig_pixmap);
	}
	fz_catch(ctx)
	{
		fz_drop_pixmap(ctx, shape);
		fz_drop_pixmap(ctx, dest);
		fz_drop_pixmap(ctx, mask);
		fz_rethrow(ctx);
	}

	state[1].blendmode |= FZ_BLEND_ISOLATED;
	state[1].scissor = bbox;
	state[1].dest = dest;
	state[1].shape = shape;
	state[1].mask = mask;
}

static void
fz_draw_pop_clip(fz_device *devp)
{
	fz_draw_device *dev = devp->user;
	fz_context *ctx = dev->ctx;
	fz_draw_state *state;

	if (dev->top == 0)
	{
		fz_warn(ctx, "Unexpected pop clip");
		return;
	}
	state = &dev->stack[--dev->top];

	/* We can get here with state[1].mask == NULL if the clipping actually
	 * resolved to a rectangle earlier.
	 */
	if (state[1].mask)
	{
#ifdef DUMP_GROUP_BLENDS
		dump_spaces(dev->top, "");
		fz_dump_blend(dev->ctx, state[1].dest, "Clipping ");
		if (state[1].shape)
			fz_dump_blend(dev->ctx, state[1].shape, "/");
		fz_dump_blend(dev->ctx, state[0].dest, " onto ");
		if (state[0].shape)
			fz_dump_blend(dev->ctx, state[0].shape, "/");
		fz_dump_blend(dev->ctx, state[1].mask, " with ");
#endif
		fz_paint_pixmap_with_mask(state[0].dest, state[1].dest, state[1].mask);
		if (state[0].shape != state[1].shape)
		{
			fz_paint_pixmap_with_mask(state[0].shape, state[1].shape, state[1].mask);
			fz_drop_pixmap(dev->ctx, state[1].shape);
		}
		fz_drop_pixmap(dev->ctx, state[1].mask);
		fz_drop_pixmap(dev->ctx, state[1].dest);
#ifdef DUMP_GROUP_BLENDS
		fz_dump_blend(dev->ctx, state[0].dest, " to get ");
		if (state[0].shape)
			fz_dump_blend(dev->ctx, state[0].shape, "/");
		printf("\n");
#endif
	}
	else
	{
#ifdef DUMP_GROUP_BLENDS
		dump_spaces(dev->top, "Clip end\n");
#endif
	}
}

static void
fz_draw_begin_mask(fz_device *devp, fz_rect rect, int luminosity, fz_colorspace *colorspace, float *colorfv)
{
	fz_draw_device *dev = devp->user;
	fz_pixmap *dest;
	fz_bbox bbox;
	fz_draw_state *state = push_stack(dev);
	fz_pixmap *shape = state->shape;

	bbox = fz_bbox_covering_rect(rect);
	bbox = fz_intersect_bbox(bbox, state->scissor);
	dest = fz_new_pixmap_with_bbox(dev->ctx, fz_device_gray, bbox);
	if (state->shape)
	{
		/* FIXME: If we ever want to support AIS true, then we
		 * probably want to create a shape pixmap here, using:
		 * shape = fz_new_pixmap_with_bbox(NULL, bbox);
		 * then, in the end_mask code, we create the mask from this
		 * rather than dest.
		 */
		shape = NULL;
	}

	if (luminosity)
	{
		float bc;
		if (!colorspace)
			colorspace = fz_device_gray;
		fz_convert_color(dev->ctx, fz_device_gray, &bc, colorspace, colorfv);
		fz_clear_pixmap_with_value(dev->ctx, dest, bc * 255);
		if (shape)
			fz_clear_pixmap_with_value(dev->ctx, shape, 255);
	}
	else
	{
		fz_clear_pixmap(dev->ctx, dest);
		if (shape)
			fz_clear_pixmap(dev->ctx, shape);
	}

#ifdef DUMP_GROUP_BLENDS
	dump_spaces(dev->top-1, "Mask begin\n");
#endif
	state[1].scissor = bbox;
	state[1].dest = dest;
	state[1].shape = shape;
	state[1].luminosity = luminosity;
}

static void
fz_draw_end_mask(fz_device *devp)
{
	fz_draw_device *dev = devp->user;
	fz_pixmap *temp, *dest;
	fz_bbox bbox;
	int luminosity;
	fz_context *ctx = dev->ctx;
	fz_draw_state *state;

	if (dev->top == 0)
	{
		fz_warn(ctx, "Unexpected draw_end_mask");
		return;
	}
	state = &dev->stack[dev->top-1];
	/* pop soft mask buffer */
	luminosity = state[1].luminosity;

#ifdef DUMP_GROUP_BLENDS
	dump_spaces(dev->top-1, "Mask -> Clip\n");
#endif
	/* convert to alpha mask */
	temp = fz_alpha_from_gray(dev->ctx, state[1].dest, luminosity);
	if (state[1].dest != state[0].dest)
		fz_drop_pixmap(dev->ctx, state[1].dest);
	state[1].dest = NULL;
	if (state[1].shape != state[0].shape)
		fz_drop_pixmap(dev->ctx, state[1].shape);
	state[1].shape = NULL;
	if (state[1].mask != state[0].mask)
		fz_drop_pixmap(dev->ctx, state[1].mask);
	state[1].mask = NULL;

	/* create new dest scratch buffer */
	bbox = fz_pixmap_bbox(ctx, temp);
	dest = fz_new_pixmap_with_bbox(dev->ctx, state->dest->colorspace, bbox);
	fz_clear_pixmap(dev->ctx, dest);

	/* push soft mask as clip mask */
	state[1].mask = temp;
	state[1].dest = dest;
	state[1].blendmode |= FZ_BLEND_ISOLATED;
	/* If we have a shape, then it'll need to be masked with the
	 * clip mask when we pop. So create a new shape now. */
	if (state[0].shape)
	{
		state[1].shape = fz_new_pixmap_with_bbox(dev->ctx, NULL, bbox);
		fz_clear_pixmap(dev->ctx, state[1].shape);
	}
	state[1].scissor = bbox;
}

static void
fz_draw_begin_group(fz_device *devp, fz_rect rect, int isolated, int knockout, int blendmode, float alpha)
{
	fz_draw_device *dev = devp->user;
	fz_bbox bbox;
	fz_pixmap *dest, *shape;
	fz_context *ctx = dev->ctx;
	fz_draw_state *state = &dev->stack[dev->top];
	fz_colorspace *model = state->dest->colorspace;

	if (state->blendmode & FZ_BLEND_KNOCKOUT)
		fz_knockout_begin(dev);

	state = push_stack(dev);
	bbox = fz_bbox_covering_rect(rect);
	bbox = fz_intersect_bbox(bbox, state->scissor);
	dest = fz_new_pixmap_with_bbox(ctx, model, bbox);

#ifndef ATTEMPT_KNOCKOUT_AND_ISOLATED
	knockout = 0;
	isolated = 1;
#endif

	if (isolated)
	{
		fz_clear_pixmap(dev->ctx, dest);
	}
	else
	{
		fz_copy_pixmap_rect(dev->ctx, dest, state[0].dest, bbox);
	}

	if (blendmode == 0 && alpha == 1.0 && isolated)
	{
		/* We can render direct to any existing shape plane. If there
		 * isn't one, we don't need to make one. */
		shape = state[0].shape;
	}
	else
	{
		fz_try(ctx)
		{
			shape = fz_new_pixmap_with_bbox(ctx, NULL, bbox);
			fz_clear_pixmap(dev->ctx, shape);
		}
		fz_catch(ctx)
		{
			fz_drop_pixmap(ctx, dest);
			fz_rethrow(ctx);
		}
	}

	state[1].alpha = alpha;
#ifdef DUMP_GROUP_BLENDS
	dump_spaces(dev->top-1, "Group begin\n");
#endif

	state[1].scissor = bbox;
	state[1].dest = dest;
	state[1].shape = shape;
	state[1].blendmode = blendmode | (isolated ? FZ_BLEND_ISOLATED : 0) | (knockout ? FZ_BLEND_KNOCKOUT : 0);
}

static void
fz_draw_end_group(fz_device *devp)
{
	fz_draw_device *dev = devp->user;
	int blendmode;
	int isolated;
	float alpha;
	fz_context *ctx = dev->ctx;
	fz_draw_state *state;

	if (dev->top == 0)
	{
		fz_warn(ctx, "Unexpected end_group");
		return;
	}

	state = &dev->stack[--dev->top];
	alpha = state[1].alpha;
	blendmode = state[1].blendmode & FZ_BLEND_MODEMASK;
	isolated = state[1].blendmode & FZ_BLEND_ISOLATED;
#ifdef DUMP_GROUP_BLENDS
	dump_spaces(dev->top, "");
	fz_dump_blend(dev->ctx, state[1].dest, "Group end: blending ");
	if (state[1].shape)
		fz_dump_blend(dev->ctx, state[1].shape, "/");
	fz_dump_blend(dev->ctx, state[0].dest, " onto ");
	if (state[0].shape)
		fz_dump_blend(dev->ctx, state[0].shape, "/");
	if (alpha != 1.0f)
		printf(" (alpha %g)", alpha);
	if (blendmode != 0)
		printf(" (blend %d)", blendmode);
	if (isolated != 0)
		printf(" (isolated)");
	if (state[1].blendmode & FZ_BLEND_KNOCKOUT)
		printf(" (knockout)");
#endif
	if ((blendmode == 0) && (state[0].shape == state[1].shape))
		fz_paint_pixmap(state[0].dest, state[1].dest, alpha * 255);
	else
		fz_blend_pixmap(state[0].dest, state[1].dest, alpha * 255, blendmode, isolated, state[1].shape);

	fz_drop_pixmap(dev->ctx, state[1].dest);
	if (state[0].shape != state[1].shape)
	{
		if (state[0].shape)
			fz_paint_pixmap(state[0].shape, state[1].shape, alpha * 255);
		fz_drop_pixmap(dev->ctx, state[1].shape);
	}
#ifdef DUMP_GROUP_BLENDS
	fz_dump_blend(dev->ctx, state[0].dest, " to get ");
	if (state[0].shape)
		fz_dump_blend(dev->ctx, state[0].shape, "/");
	printf("\n");
#endif

	if (state[0].blendmode & FZ_BLEND_KNOCKOUT)
		fz_knockout_end(dev);
}

static void
fz_draw_begin_tile(fz_device *devp, fz_rect area, fz_rect view, float xstep, float ystep, fz_matrix ctm)
{
	fz_draw_device *dev = devp->user;
	fz_pixmap *dest = NULL;
	fz_pixmap *shape;
	fz_bbox bbox;
	fz_context *ctx = dev->ctx;
	fz_draw_state *state = &dev->stack[dev->top];
	fz_colorspace *model = state->dest->colorspace;

	/* area, view, xstep, ystep are in pattern space */
	/* ctm maps from pattern space to device space */

	if (state->blendmode & FZ_BLEND_KNOCKOUT)
		fz_knockout_begin(dev);

	state = push_stack(dev);
	bbox = fz_bbox_covering_rect(fz_transform_rect(ctm, view));
	/* We should never have a bbox that entirely covers our destination.
	 * If we do, then the check for only 1 tile being visible above has
	 * failed. Actually, this *can* fail due to the round_rect, at extreme
	 * resolutions, so disable this assert.
	 * assert(bbox.x0 > state->dest->x || bbox.x1 < state->dest->x + state->dest->w ||
	 *	bbox.y0 > state->dest->y || bbox.y1 < state->dest->y + state->dest->h);
	 */
	dest = fz_new_pixmap_with_bbox(dev->ctx, model, bbox);
	fz_clear_pixmap(ctx, dest);
	shape = state[0].shape;
	if (shape)
	{
		fz_var(shape);
		fz_try(ctx)
		{
			shape = fz_new_pixmap_with_bbox(dev->ctx, NULL, bbox);
			fz_clear_pixmap(ctx, shape);
		}
		fz_catch(ctx)
		{
			fz_drop_pixmap(ctx, dest);
			fz_rethrow(ctx);
		}
	}
	state[1].blendmode |= FZ_BLEND_ISOLATED;
	state[1].xstep = xstep;
	state[1].ystep = ystep;
	state[1].area = area;
	state[1].ctm = ctm;
#ifdef DUMP_GROUP_BLENDS
	dump_spaces(dev->top-1, "Tile begin\n");
#endif

	state[1].scissor = bbox;
	state[1].dest = dest;
	state[1].shape = shape;
}

static void
fz_draw_end_tile(fz_device *devp)
{
	fz_draw_device *dev = devp->user;
	float xstep, ystep;
	fz_matrix ctm, ttm, shapectm;
	fz_rect area;
	int x0, y0, x1, y1, x, y;
	fz_context *ctx = dev->ctx;
	fz_draw_state *state;
	/* SumatraPDF: make sure that the whole area is covered */
	fz_point tl;

	if (dev->top == 0)
	{
		fz_warn(ctx, "Unexpected end_tile");
		return;
	}

	state = &dev->stack[--dev->top];
	xstep = state[1].xstep;
	ystep = state[1].ystep;
	area = state[1].area;
	ctm = state[1].ctm;

	/* SumatraPDF: make sure that the whole area is covered */
	tl.x = state[1].dest->x; tl.y = state[1].dest->y;
	tl = fz_transform_point(fz_invert_matrix(ctm), tl);
	x0 = floorf((area.x0 - MAX(tl.x, 0)) / xstep);
	y0 = floorf((area.y0 - MAX(tl.y, 0)) / ystep);
	x1 = ceilf((area.x1 - MAX(tl.x, 0)) / xstep);
	y1 = ceilf((area.y1 - MAX(tl.y, 0)) / ystep);

	ctm.e = state[1].dest->x;
	ctm.f = state[1].dest->y;
	if (state[1].shape)
	{
		shapectm = ctm;
		shapectm.e = state[1].shape->x;
		shapectm.f = state[1].shape->y;
		/* SumatraPDF: dest and shape have the same coordinates during tiling */
		assert(ctm.e == shapectm.e && ctm.f == shapectm.f);
	}

#ifdef DUMP_GROUP_BLENDS
	dump_spaces(dev->top, "");
	fz_dump_blend(dev->ctx, state[1].dest, "Tiling ");
	if (state[1].shape)
		fz_dump_blend(dev->ctx, state[1].shape, "/");
	fz_dump_blend(dev->ctx, state[0].dest, " onto ");
	if (state[0].shape)
		fz_dump_blend(dev->ctx, state[0].shape, "/");
#endif

	/* SumatraPDF: some documents need one additional step */
	for (y = y0; y <= y1; y++)
	{
		for (x = x0; x <= x1; x++)
		{
			ttm = fz_concat(fz_translate(x * xstep, y * ystep), ctm);
			state[1].dest->x = ttm.e;
			state[1].dest->y = ttm.f;
			fz_paint_pixmap_with_rect(state[0].dest, state[1].dest, 255, state[0].scissor);
			if (state[1].shape)
			{
				ttm = fz_concat(fz_translate(x * xstep, y * ystep), shapectm);
				state[1].shape->x = ttm.e;
				state[1].shape->y = ttm.f;
				fz_paint_pixmap_with_rect(state[0].shape, state[1].shape, 255, state[0].scissor);
			}
		}
	}

	fz_drop_pixmap(dev->ctx, state[1].dest);
	fz_drop_pixmap(dev->ctx, state[1].shape);
#ifdef DUMP_GROUP_BLENDS
	fz_dump_blend(dev->ctx, state[0].dest, " to get ");
	if (state[0].shape)
		fz_dump_blend(dev->ctx, state[0].shape, "/");
	printf("\n");
#endif

	if (state->blendmode & FZ_BLEND_KNOCKOUT)
		fz_knockout_end(dev);
}

static void
fz_draw_free_user(fz_device *devp)
{
	fz_draw_device *dev = devp->user;
	fz_context *ctx = dev->ctx;
	/* pop and free the stacks */
	if (dev->top > 0)
	{
		fz_draw_state *state = &dev->stack[--dev->top];
		fz_warn(ctx, "items left on stack in draw device: %d", dev->top+1);

		do
		{
			if (state[1].mask != state[0].mask)
				fz_drop_pixmap(ctx, state[1].mask);
			if (state[1].dest != state[0].dest)
				fz_drop_pixmap(ctx, state[1].dest);
			if (state[1].shape != state[0].shape)
				fz_drop_pixmap(ctx, state[1].shape);
			state--;
		}
		while(--dev->top > 0);
	}
	if (dev->stack != &dev->init_stack[0])
		fz_free(ctx, dev->stack);
	fz_free_gel(dev->gel);
	fz_free(ctx, dev);
}

fz_device *
fz_new_draw_device(fz_context *ctx, fz_pixmap *dest)
{
	fz_device *dev = NULL;
	fz_draw_device *ddev = fz_malloc_struct(ctx, fz_draw_device);

	fz_var(dev);
	fz_try(ctx)
	{
		ddev->gel = fz_new_gel(ctx);
		ddev->flags = 0;
		ddev->ctx = ctx;
		ddev->top = 0;
		ddev->stack = &ddev->init_stack[0];
		ddev->stack_max = STACK_SIZE;
		ddev->stack[0].dest = dest;
		ddev->stack[0].shape = NULL;
		ddev->stack[0].mask = NULL;
		ddev->stack[0].blendmode = 0;
		ddev->stack[0].scissor.x0 = dest->x;
		ddev->stack[0].scissor.y0 = dest->y;
		ddev->stack[0].scissor.x1 = dest->x + dest->w;
		ddev->stack[0].scissor.y1 = dest->y + dest->h;

		dev = fz_new_device(ctx, ddev);
	}
	fz_catch(ctx)
	{
		fz_free_gel(ddev->gel);
		fz_free(ctx, ddev);
		fz_rethrow(ctx);
	}
	dev->free_user = fz_draw_free_user;

	dev->fill_path = fz_draw_fill_path;
	dev->stroke_path = fz_draw_stroke_path;
	dev->clip_path = fz_draw_clip_path;
	dev->clip_stroke_path = fz_draw_clip_stroke_path;

	dev->fill_text = fz_draw_fill_text;
	dev->stroke_text = fz_draw_stroke_text;
	dev->clip_text = fz_draw_clip_text;
	dev->clip_stroke_text = fz_draw_clip_stroke_text;
	dev->ignore_text = fz_draw_ignore_text;

	dev->fill_image_mask = fz_draw_fill_image_mask;
	dev->clip_image_mask = fz_draw_clip_image_mask;
	dev->fill_image = fz_draw_fill_image;
	dev->fill_shade = fz_draw_fill_shade;

	dev->pop_clip = fz_draw_pop_clip;

	dev->begin_mask = fz_draw_begin_mask;
	dev->end_mask = fz_draw_end_mask;
	dev->begin_group = fz_draw_begin_group;
	dev->end_group = fz_draw_end_group;

	dev->begin_tile = fz_draw_begin_tile;
	dev->end_tile = fz_draw_end_tile;

	return dev;
}

fz_device *
fz_new_draw_device_type3(fz_context *ctx, fz_pixmap *dest)
{
	fz_device *dev = fz_new_draw_device(ctx, dest);
	fz_draw_device *ddev = dev->user;
	ddev->flags |= FZ_DRAWDEV_FLAGS_TYPE3;
	return dev;
}
