#include "fitz-internal.h"
#include "mupdf-internal.h"

#define HUGENUM 32000 /* how far to extend axial/radial shadings */
#define FUNSEGS 32 /* size of sampled mesh for function-based shadings */
#define RADSEGS 32 /* how many segments to generate for radial meshes */
#define SUBDIV 3 /* how many levels to subdivide patches */
//extern zblstack* currentstackpoint;
int zcounter;
extern struct zblrouteset *getline;
extern struct zblstack* currentstackpoint;
struct vertex
{
	float x, y;
	float c[FZ_MAX_COLORS];
};

static void
pdf_grow_mesh(fz_context *ctx, fz_shade *shade, int amount)
{
	if (shade->mesh_len + amount < shade->mesh_cap)
		return;

	if (shade->mesh_cap == 0)
		shade->mesh_cap = 1024;

	while (shade->mesh_len + amount > shade->mesh_cap)
		shade->mesh_cap = (shade->mesh_cap * 3) / 2;

	shade->mesh = fz_resize_array(ctx, shade->mesh, shade->mesh_cap, sizeof(float));
}

static void
pdf_add_vertex(fz_context *ctx, fz_shade *shade, struct vertex *v)
{
	int ncomp = shade->use_function ? 1 : shade->colorspace->n;
	int i;
	pdf_grow_mesh(ctx, shade, 2 + ncomp);
	shade->mesh[shade->mesh_len++] = v->x;
	shade->mesh[shade->mesh_len++] = v->y;
	for (i = 0; i < ncomp; i++)
		shade->mesh[shade->mesh_len++] = v->c[i];
}

static void
pdf_add_triangle(fz_context *ctx, fz_shade *shade,
	struct vertex *v0,
	struct vertex *v1,
	struct vertex *v2)
{
	pdf_add_vertex(ctx, shade, v0);
	pdf_add_vertex(ctx, shade, v1);
	pdf_add_vertex(ctx, shade, v2);
}

static void
pdf_add_quad(fz_context *ctx, fz_shade *shade,
	struct vertex *v0,
	struct vertex *v1,
	struct vertex *v2,
	struct vertex *v3)
{
	pdf_add_triangle(ctx, shade, v0, v1, v3);
	pdf_add_triangle(ctx, shade, v1, v3, v2);
}

/* Subdivide and tessellate tensor-patches */

typedef struct pdf_tensor_patch_s pdf_tensor_patch;

struct pdf_tensor_patch_s
{
	fz_point pole[4][4];
	float color[4][FZ_MAX_COLORS];
};

static void
triangulate_patch(fz_context *ctx, pdf_tensor_patch p, fz_shade *shade)
{
	struct vertex v0, v1, v2, v3;

	v0.x = p.pole[0][0].x;
	v0.y = p.pole[0][0].y;
	memcpy(v0.c, p.color[0], sizeof(v0.c));

	v1.x = p.pole[0][3].x;
	v1.y = p.pole[0][3].y;
	memcpy(v1.c, p.color[1], sizeof(v1.c));

	v2.x = p.pole[3][3].x;
	v2.y = p.pole[3][3].y;
	memcpy(v2.c, p.color[2], sizeof(v2.c));

	v3.x = p.pole[3][0].x;
	v3.y = p.pole[3][0].y;
	memcpy(v3.c, p.color[3], sizeof(v3.c));

	pdf_add_quad(ctx, shade, &v0, &v1, &v2, &v3);
}

static inline void midcolor(float *c, float *c1, float *c2)
{
	int i;
	for (i = 0; i < FZ_MAX_COLORS; i++)
		c[i] = (c1[i] + c2[i]) * 0.5f;
}

static void
split_curve(fz_point *pole, fz_point *q0, fz_point *q1, int polestep)
{
	/*
	split bezier curve given by control points pole[0]..pole[3]
	using de casteljau algo at midpoint and build two new
	bezier curves q0[0]..q0[3] and q1[0]..q1[3]. all indices
	should be multiplies by polestep == 1 for vertical bezier
	curves in patch and == 4 for horizontal bezier curves due
	to C's multi-dimensional matrix memory layout.
	*/

	float x12 = (pole[1 * polestep].x + pole[2 * polestep].x) * 0.5f;
	float y12 = (pole[1 * polestep].y + pole[2 * polestep].y) * 0.5f;

	q0[1 * polestep].x = (pole[0 * polestep].x + pole[1 * polestep].x) * 0.5f;
	q0[1 * polestep].y = (pole[0 * polestep].y + pole[1 * polestep].y) * 0.5f;
	q1[2 * polestep].x = (pole[2 * polestep].x + pole[3 * polestep].x) * 0.5f;
	q1[2 * polestep].y = (pole[2 * polestep].y + pole[3 * polestep].y) * 0.5f;

	q0[2 * polestep].x = (q0[1 * polestep].x + x12) * 0.5f;
	q0[2 * polestep].y = (q0[1 * polestep].y + y12) * 0.5f;
	q1[1 * polestep].x = (x12 + q1[2 * polestep].x) * 0.5f;
	q1[1 * polestep].y = (y12 + q1[2 * polestep].y) * 0.5f;

	q0[3 * polestep].x = (q0[2 * polestep].x + q1[1 * polestep].x) * 0.5f;
	q0[3 * polestep].y = (q0[2 * polestep].y + q1[1 * polestep].y) * 0.5f;
	q1[0 * polestep].x = (q0[2 * polestep].x + q1[1 * polestep].x) * 0.5f;
	q1[0 * polestep].y = (q0[2 * polestep].y + q1[1 * polestep].y) * 0.5f;

	q0[0 * polestep].x = pole[0 * polestep].x;
	q0[0 * polestep].y = pole[0 * polestep].y;
	q1[3 * polestep].x = pole[3 * polestep].x;
	q1[3 * polestep].y = pole[3 * polestep].y;
}

static void
split_stripe(pdf_tensor_patch *p, pdf_tensor_patch *s0, pdf_tensor_patch *s1)
{
	/*
	split all horizontal bezier curves in patch,
	creating two new patches with half the width.
	*/
	split_curve(&p->pole[0][0], &s0->pole[0][0], &s1->pole[0][0], 4);
	split_curve(&p->pole[0][1], &s0->pole[0][1], &s1->pole[0][1], 4);
	split_curve(&p->pole[0][2], &s0->pole[0][2], &s1->pole[0][2], 4);
	split_curve(&p->pole[0][3], &s0->pole[0][3], &s1->pole[0][3], 4);

	/* interpolate the colors for the two new patches. */
	memcpy(s0->color[0], p->color[0], sizeof(s0->color[0]));
	memcpy(s0->color[1], p->color[1], sizeof(s0->color[1]));
	midcolor(s0->color[2], p->color[1], p->color[2]);
	midcolor(s0->color[3], p->color[0], p->color[3]);

	memcpy(s1->color[0], s0->color[3], sizeof(s1->color[0]));
	memcpy(s1->color[1], s0->color[2], sizeof(s1->color[1]));
	memcpy(s1->color[2], p->color[2], sizeof(s1->color[2]));
	memcpy(s1->color[3], p->color[3], sizeof(s1->color[3]));
}

static void
draw_stripe(fz_context *ctx, pdf_tensor_patch *p, fz_shade *shade, int depth)
{
	pdf_tensor_patch s0, s1;

	/* split patch into two half-height patches */
	split_stripe(p, &s0, &s1);

	depth--;
	if (depth == 0)
	{
		/* if no more subdividing, draw two new patches... */
		triangulate_patch(ctx, s0, shade);
		triangulate_patch(ctx, s1, shade);
	}
	else
	{
		/* ...otherwise, continue subdividing. */
		draw_stripe(ctx, &s0, shade, depth);
		draw_stripe(ctx, &s1, shade, depth);
	}
}

static void
split_patch(pdf_tensor_patch *p, pdf_tensor_patch *s0, pdf_tensor_patch *s1)
{
	/*
	split all vertical bezier curves in patch,
	creating two new patches with half the height.
	*/
	split_curve(p->pole[0], s0->pole[0], s1->pole[0], 1);
	split_curve(p->pole[1], s0->pole[1], s1->pole[1], 1);
	split_curve(p->pole[2], s0->pole[2], s1->pole[2], 1);
	split_curve(p->pole[3], s0->pole[3], s1->pole[3], 1);

	/* interpolate the colors for the two new patches. */
	memcpy(s0->color[0], p->color[0], sizeof(s0->color[0]));
	midcolor(s0->color[1], p->color[0], p->color[1]);
	midcolor(s0->color[2], p->color[2], p->color[3]);
	memcpy(s0->color[3], p->color[3], sizeof(s0->color[3]));

	memcpy(s1->color[0], s0->color[1], sizeof(s1->color[0]));
	memcpy(s1->color[1], p->color[1], sizeof(s1->color[1]));
	memcpy(s1->color[2], p->color[2], sizeof(s1->color[2]));
	memcpy(s1->color[3], s0->color[2], sizeof(s1->color[3]));
}

static void
draw_patch(fz_context *ctx, fz_shade *shade, pdf_tensor_patch *p, int depth, int origdepth)
{
	pdf_tensor_patch s0, s1;

	/* split patch into two half-width patches */
	split_patch(p, &s0, &s1);

	depth--;
	if (depth == 0)
	{
		/* if no more subdividing, draw two new patches... */
		draw_stripe(ctx, &s0, shade, origdepth);
		draw_stripe(ctx, &s1, shade, origdepth);
	}
	else
	{
		/* ...otherwise, continue subdividing. */
		draw_patch(ctx, shade, &s0, depth, origdepth);
		draw_patch(ctx, shade, &s1, depth, origdepth);
	}
}

static fz_point
pdf_compute_tensor_interior(
	fz_point a, fz_point b, fz_point c, fz_point d,
	fz_point e, fz_point f, fz_point g, fz_point h)
{
	fz_point pt;

	/* see equations at page 330 in pdf 1.7 */

	pt.x = -4 * a.x;
	pt.x += 6 * (b.x + c.x);
	pt.x += -2 * (d.x + e.x);
	pt.x += 3 * (f.x + g.x);
	pt.x += -1 * h.x;
	pt.x /= 9;

	pt.y = -4 * a.y;
	pt.y += 6 * (b.y + c.y);
	pt.y += -2 * (d.y + e.y);
	pt.y += 3 * (f.y + g.y);
	pt.y += -1 * h.y;
	pt.y /= 9;

	return pt;
}

static void
pdf_make_tensor_patch(pdf_tensor_patch *p, int type, fz_point *pt)
{
	if (type == 6)
	{
		/* see control point stream order at page 325 in pdf 1.7 */

		p->pole[0][0] = pt[0];
		p->pole[0][1] = pt[1];
		p->pole[0][2] = pt[2];
		p->pole[0][3] = pt[3];
		p->pole[1][3] = pt[4];
		p->pole[2][3] = pt[5];
		p->pole[3][3] = pt[6];
		p->pole[3][2] = pt[7];
		p->pole[3][1] = pt[8];
		p->pole[3][0] = pt[9];
		p->pole[2][0] = pt[10];
		p->pole[1][0] = pt[11];

		/* see equations at page 330 in pdf 1.7 */

		p->pole[1][1] = pdf_compute_tensor_interior(
			p->pole[0][0], p->pole[0][1], p->pole[1][0], p->pole[0][3],
			p->pole[3][0], p->pole[3][1], p->pole[1][3], p->pole[3][3]);

		p->pole[1][2] = pdf_compute_tensor_interior(
			p->pole[0][3], p->pole[0][2], p->pole[1][3], p->pole[0][0],
			p->pole[3][3], p->pole[3][2], p->pole[1][0], p->pole[3][0]);

		p->pole[2][1] = pdf_compute_tensor_interior(
			p->pole[3][0], p->pole[3][1], p->pole[2][0], p->pole[3][3],
			p->pole[0][0], p->pole[0][1], p->pole[2][3], p->pole[0][3]);

		p->pole[2][2] = pdf_compute_tensor_interior(
			p->pole[3][3], p->pole[3][2], p->pole[2][3], p->pole[3][0],
			p->pole[0][3], p->pole[0][2], p->pole[2][0], p->pole[0][0]);
	}
	else if (type == 7)
	{
		/* see control point stream order at page 330 in pdf 1.7 */

		p->pole[0][0] = pt[0];
		p->pole[0][1] = pt[1];
		p->pole[0][2] = pt[2];
		p->pole[0][3] = pt[3];
		p->pole[1][3] = pt[4];
		p->pole[2][3] = pt[5];
		p->pole[3][3] = pt[6];
		p->pole[3][2] = pt[7];
		p->pole[3][1] = pt[8];
		p->pole[3][0] = pt[9];
		p->pole[2][0] = pt[10];
		p->pole[1][0] = pt[11];
		p->pole[1][1] = pt[12];
		p->pole[1][2] = pt[13];
		p->pole[2][2] = pt[14];
		p->pole[2][1] = pt[15];
	}
}

/* Sample various functions into lookup tables 将各种函数采样到查找表中*/

static void
pdf_sample_composite_shade_function(fz_context *ctx, fz_shade *shade, pdf_function *func, float t0, float t1)//一个函数,t1,t0默认为0,1
{
	int i;
	float t;

	for (i = 0; i < 256; i++)//为什么要遍历256次?
	{
		zcounter=i;
		t = t0 + (i / 255.0f) * (t1 - t0);//t从0到1,每次增加1/255,第4个参数为1
		pdf_eval_function(ctx, func, &t, 1, shade->function[i], shade->colorspace->n);
		shade->function[i][shade->colorspace->n] = 1;
	}
}

static void
pdf_sample_component_shade_function(fz_context *ctx, fz_shade *shade, int funcs, pdf_function **func, float t0, float t1)
{
	int i, k;
	float t;

	for (i = 0; i < 256; i++)
	{
		t = t0 + (i / 255.0f) * (t1 - t0);
		for (k = 0; k < funcs; k++)
			pdf_eval_function(ctx, func[k], &t, 1, &shade->function[i][k], 1);
		shade->function[i][k] = 1;
	}
}

static void
pdf_sample_shade_function(fz_context *ctx, fz_shade *shade, int funcs, pdf_function **func, float t0, float t1)//处理函数,d0,d1默认为 0,1
{
	shade->use_function = 1;
	if (funcs == 1)//只有一个函数
		pdf_sample_composite_shade_function(ctx, shade, func[0], t0, t1);//混合函数
	else//有多个函数
		pdf_sample_component_shade_function(ctx, shade, funcs, func, t0, t1);//合成函数
}

/* Type 1-3 -- Function-based, axial and radial shadings */

static void
pdf_load_function_based_shading(fz_shade *shade, pdf_document *xref, pdf_obj *dict, pdf_function *func)
{
	pdf_obj *obj;
	float x0, y0, x1, y1;
	fz_matrix matrix;
	struct vertex v[4];
	int xx, yy;
	float x, y;
	float xn, yn;
	int i;
	fz_context *ctx = xref->ctx;

	x0 = y0 = 0;
	x1 = y1 = 1;
	obj = pdf_dict_gets(dict, "Domain");
	if (pdf_array_len(obj) == 4)
	{
		x0 = pdf_to_real(pdf_array_get(obj, 0));
		x1 = pdf_to_real(pdf_array_get(obj, 1));
		y0 = pdf_to_real(pdf_array_get(obj, 2));
		y1 = pdf_to_real(pdf_array_get(obj, 3));
	}

	matrix = fz_identity;
	obj = pdf_dict_gets(dict, "Matrix");
	if (pdf_array_len(obj) == 6)
		matrix = pdf_to_matrix(ctx, obj);

	for (yy = 0; yy < FUNSEGS; yy++)
	{
		y = y0 + (y1 - y0) * yy / FUNSEGS;
		yn = y0 + (y1 - y0) * (yy + 1) / FUNSEGS;

		for (xx = 0; xx < FUNSEGS; xx++)
		{
			x = x0 + (x1 - x0) * xx / FUNSEGS;
			xn = x0 + (x1 - x0) * (xx + 1) / FUNSEGS;

			v[0].x = x; v[0].y = y;
			v[1].x = xn; v[1].y = y;
			v[2].x = xn; v[2].y = yn;
			v[3].x = x; v[3].y = yn;

			for (i = 0; i < 4; i++)
			{
				fz_point pt;
				float fv[2];

				fv[0] = v[i].x;
				fv[1] = v[i].y;
				pdf_eval_function(ctx, func, fv, 2, v[i].c, shade->colorspace->n);

				pt.x = v[i].x;
				pt.y = v[i].y;
				pt = fz_transform_point(matrix, pt);
				v[i].x = pt.x;
				v[i].y = pt.y;
			}

			pdf_add_quad(ctx, shade, &v[0], &v[1], &v[2], &v[3]);
		}
	}
}
//类型二阴影
static void
pdf_load_axial_shading(fz_shade *shade, pdf_document *xref, pdf_obj *dict, int funcs, pdf_function **func)
{
	pdf_obj *obj;
	float d0, d1;
	int e0, e1;
	float x0, y0, x1, y1;
	struct vertex p1, p2;
	fz_context *ctx = xref->ctx;

	obj = pdf_dict_gets(dict, "Coords");//坐标,起点与结束点坐标轴,表示其坐标空间
	x0 = pdf_to_real(pdf_array_get(obj, 0));
	y0 = pdf_to_real(pdf_array_get(obj, 1));
	x1 = pdf_to_real(pdf_array_get(obj, 2));
	y1 = pdf_to_real(pdf_array_get(obj, 3));

	d0 = 0;
	d1 = 1;
	obj = pdf_dict_gets(dict, "Domain");//颜色功能的输入参数,颜色变化起始点与结束点轴
	if (pdf_array_len(obj) == 2)
	{
		d0 = pdf_to_real(pdf_array_get(obj, 0));
		d1 = pdf_to_real(pdf_array_get(obj, 1));
	}

	e0 = e1 = 0;
	obj = pdf_dict_gets(dict, "Extend");//是否扩展底纹功能到起点与结束点
	if (pdf_array_len(obj) == 2)
	{
		e0 = pdf_to_bool(pdf_array_get(obj, 0));
		e1 = pdf_to_bool(pdf_array_get(obj, 1));
	}

	pdf_sample_shade_function(ctx, shade, funcs, func, d0, d1);//处理函数,d0,d1默认为 0,1

	shade->type = FZ_LINEAR;

	shade->extend[0] = e0;
	shade->extend[1] = e1;

	p1.x = x0;
	p1.y = y0;
	p1.c[0] = 0;
	pdf_add_vertex(ctx, shade, &p1);

	p2.x = x1;
	p2.y = y1;
	p2.c[0] = 0;
	pdf_add_vertex(ctx, shade, &p2);
}

static void
pdf_load_radial_shading(fz_shade *shade, pdf_document *xref, pdf_obj *dict, int funcs, pdf_function **func)//处理径向阴影
{
	pdf_obj *obj;
	float d0, d1;
	int e0, e1;
	float x0, y0, r0, x1, y1, r1;
	struct vertex p1, p2;
	fz_context *ctx = xref->ctx;

	obj = pdf_dict_gets(dict, "Coords");//园的一些参数
	x0 = pdf_to_real(pdf_array_get(obj, 0));
	y0 = pdf_to_real(pdf_array_get(obj, 1));
	r0 = pdf_to_real(pdf_array_get(obj, 2));
	x1 = pdf_to_real(pdf_array_get(obj, 3));
	y1 = pdf_to_real(pdf_array_get(obj, 4));
	r1 = pdf_to_real(pdf_array_get(obj, 5));

	d0 = 0;
	d1 = 1;
	obj = pdf_dict_gets(dict, "Domain");
	if (pdf_array_len(obj) == 2)
	{
		d0 = pdf_to_real(pdf_array_get(obj, 0));
		d1 = pdf_to_real(pdf_array_get(obj, 1));
	}

	e0 = e1 = 0;
	obj = pdf_dict_gets(dict, "Extend");
	if (pdf_array_len(obj) == 2)
	{
		e0 = pdf_to_bool(pdf_array_get(obj, 0));
		e1 = pdf_to_bool(pdf_array_get(obj, 1));
	}

	pdf_sample_shade_function(ctx, shade, funcs, func, d0, d1);

	shade->type = FZ_RADIAL;

	shade->extend[0] = e0;
	shade->extend[1] = e1;

	p1.x = x0;
	p1.y = y0;
	p1.c[0] = r0;
	pdf_add_vertex(ctx, shade, &p1);

	p2.x = x1;
	p2.y = y1;
	p2.c[0] = r1;
	pdf_add_vertex(ctx, shade, &p2);
}

/* Type 4-7 -- Triangle and patch mesh shadings */

static inline float read_sample(fz_stream *stream, int bits, float min, float max)
{
	/* we use pow(2,x) because (1<<x) would overflow the math on 32-bit samples */
	float bitscale = 1 / (powf(2, bits) - 1);
	return min + fz_read_bits(stream, bits) * (max - min) * bitscale;
}

struct mesh_params
{
	int vprow;
	int bpflag;
	int bpcoord;
	int bpcomp;
	float x0, x1;
	float y0, y1;
	float c0[FZ_MAX_COLORS];
	float c1[FZ_MAX_COLORS];
};

static void
pdf_load_mesh_params(pdf_document *xref, pdf_obj *dict, struct mesh_params *p)
{
	pdf_obj *obj;
	int i, n;

	p->x0 = p->y0 = 0;
	p->x1 = p->y1 = 1;
	for (i = 0; i < FZ_MAX_COLORS; i++)
	{
		p->c0[i] = 0;
		p->c1[i] = 1;
	}

	p->vprow = pdf_to_int(pdf_dict_gets(dict, "VerticesPerRow"));
	p->bpflag = pdf_to_int(pdf_dict_gets(dict, "BitsPerFlag"));
	p->bpcoord = pdf_to_int(pdf_dict_gets(dict, "BitsPerCoordinate"));
	p->bpcomp = pdf_to_int(pdf_dict_gets(dict, "BitsPerComponent"));

	obj = pdf_dict_gets(dict, "Decode");
	if (pdf_array_len(obj) >= 6)
	{
		n = (pdf_array_len(obj) - 4) / 2;
		p->x0 = pdf_to_real(pdf_array_get(obj, 0));
		p->x1 = pdf_to_real(pdf_array_get(obj, 1));
		p->y0 = pdf_to_real(pdf_array_get(obj, 2));
		p->y1 = pdf_to_real(pdf_array_get(obj, 3));
		for (i = 0; i < n; i++)
		{
			p->c0[i] = pdf_to_real(pdf_array_get(obj, 4 + i * 2));
			p->c1[i] = pdf_to_real(pdf_array_get(obj, 5 + i * 2));
		}
	}

	if (p->vprow < 2)
		p->vprow = 2;

	if (p->bpflag != 2 && p->bpflag != 4 && p->bpflag != 8)
		p->bpflag = 8;

	if (p->bpcoord != 1 && p->bpcoord != 2 && p->bpcoord != 4 &&
		p->bpcoord != 8 && p->bpcoord != 12 && p->bpcoord != 16 &&
		p->bpcoord != 24 && p->bpcoord != 32)
		p->bpcoord = 8;

	if (p->bpcomp != 1 && p->bpcomp != 2 && p->bpcomp != 4 &&
		p->bpcomp != 8 && p->bpcomp != 12 && p->bpcomp != 16)
		p->bpcomp = 8;
}

static void
pdf_load_type4_shade(fz_shade *shade, pdf_document *xref, pdf_obj *dict,
	int funcs, pdf_function **func)
{
	fz_context *ctx = xref->ctx;
	struct mesh_params p;
	struct vertex va, vb, vc, vd;
	int ncomp;
	int flag;
	int i;
	fz_stream *stream;

	pdf_load_mesh_params(xref, dict, &p);

	if (funcs > 0)
	{
		ncomp = 1;
		pdf_sample_shade_function(ctx, shade, funcs, func, p.c0[0], p.c1[0]);
	}
	else
		ncomp = shade->colorspace->n;

	stream = pdf_open_stream(xref, pdf_to_num(dict), pdf_to_gen(dict));

	while (!fz_is_eof_bits(stream))
	{
		flag = fz_read_bits(stream, p.bpflag);
		vd.x = read_sample(stream, p.bpcoord, p.x0, p.x1);
		vd.y = read_sample(stream, p.bpcoord, p.y0, p.y1);
		for (i = 0; i < ncomp; i++)
			vd.c[i] = read_sample(stream, p.bpcomp, p.c0[i], p.c1[i]);

		switch (flag)
		{
		case 0: /* start new triangle */
			va = vd;

			fz_read_bits(stream, p.bpflag);
			vb.x = read_sample(stream, p.bpcoord, p.x0, p.x1);
			vb.y = read_sample(stream, p.bpcoord, p.y0, p.y1);
			for (i = 0; i < ncomp; i++)
				vb.c[i] = read_sample(stream, p.bpcomp, p.c0[i], p.c1[i]);

			fz_read_bits(stream, p.bpflag);
			vc.x = read_sample(stream, p.bpcoord, p.x0, p.x1);
			vc.y = read_sample(stream, p.bpcoord, p.y0, p.y1);
			for (i = 0; i < ncomp; i++)
				vc.c[i] = read_sample(stream, p.bpcomp, p.c0[i], p.c1[i]);

			pdf_add_triangle(ctx, shade, &va, &vb, &vc);
			break;

		case 1: /* Vb, Vc, Vd */
			va = vb;
			vb = vc;
			vc = vd;
			pdf_add_triangle(ctx, shade, &va, &vb, &vc);
			break;

		case 2: /* Va, Vc, Vd */
			vb = vc;
			vc = vd;
			pdf_add_triangle(ctx, shade, &va, &vb, &vc);
			break;
		}
	}
	fz_close(stream);
}

static void
pdf_load_type5_shade(fz_shade *shade, pdf_document *xref, pdf_obj *dict,
	int funcs, pdf_function **func)
{
	fz_context *ctx = xref->ctx;
	struct mesh_params p;
	struct vertex *buf, *ref;
	int first;
	int ncomp;
	int i, k;
	fz_stream *stream;

	pdf_load_mesh_params(xref, dict, &p);

	if (funcs > 0)
	{
		ncomp = 1;
		pdf_sample_shade_function(ctx, shade, funcs, func, p.c0[0], p.c1[0]);
	}
	else
		ncomp = shade->colorspace->n;

	ref = fz_malloc_array(ctx, p.vprow, sizeof(struct vertex));
	buf = fz_malloc_array(ctx, p.vprow, sizeof(struct vertex));
	first = 1;

	stream = pdf_open_stream(xref, pdf_to_num(dict), pdf_to_gen(dict));

	while (!fz_is_eof_bits(stream))
	{
		for (i = 0; i < p.vprow; i++)
		{
			buf[i].x = read_sample(stream, p.bpcoord, p.x0, p.x1);
			buf[i].y = read_sample(stream, p.bpcoord, p.y0, p.y1);
			for (k = 0; k < ncomp; k++)
				buf[i].c[k] = read_sample(stream, p.bpcomp, p.c0[k], p.c1[k]);
		}

		if (!first)
			for (i = 0; i < p.vprow - 1; i++)
				pdf_add_quad(ctx, shade,
					&ref[i], &ref[i+1], &buf[i+1], &buf[i]);

		memcpy(ref, buf, p.vprow * sizeof(struct vertex));
		first = 0;
	}

	fz_free(ctx, ref);
	fz_free(ctx, buf);
	fz_close(stream);
}

/* Type 6 & 7 -- Patch mesh shadings */

static void
pdf_load_type6_shade(fz_shade *shade, pdf_document *xref, pdf_obj *dict,
	int funcs, pdf_function **func)
{
	fz_context *ctx = xref->ctx;
	struct mesh_params p;
	int haspatch, hasprevpatch;
	float prevc[4][FZ_MAX_COLORS];
	fz_point prevp[12];
	int ncomp;
	int i, k;
	fz_stream *stream;

	pdf_load_mesh_params(xref, dict, &p);

	if (funcs > 0)
	{
		ncomp = 1;
		pdf_sample_shade_function(ctx, shade, funcs, func, p.c0[0], p.c1[0]);
	}
	else
		ncomp = shade->colorspace->n;

	hasprevpatch = 0;

	stream = pdf_open_stream(xref, pdf_to_num(dict), pdf_to_gen(dict));

	while (!fz_is_eof_bits(stream))
	{
		float c[4][FZ_MAX_COLORS];
		fz_point v[12];
		int startcolor;
		int startpt;
		int flag;

		flag = fz_read_bits(stream, p.bpflag);

		if (flag == 0)
		{
			startpt = 0;
			startcolor = 0;
		}
		else
		{
			startpt = 4;
			startcolor = 2;
		}

		for (i = startpt; i < 12; i++)
		{
			v[i].x = read_sample(stream, p.bpcoord, p.x0, p.x1);
			v[i].y = read_sample(stream, p.bpcoord, p.y0, p.y1);
		}

		for (i = startcolor; i < 4; i++)
		{
			for (k = 0; k < ncomp; k++)
				c[i][k] = read_sample(stream, p.bpcomp, p.c0[k], p.c1[k]);
		}

		haspatch = 0;

		if (flag == 0)
		{
			haspatch = 1;
		}
		else if (flag == 1 && hasprevpatch)
		{
			v[0] = prevp[3];
			v[1] = prevp[4];
			v[2] = prevp[5];
			v[3] = prevp[6];
			memcpy(c[0], prevc[1], ncomp * sizeof(float));
			memcpy(c[1], prevc[2], ncomp * sizeof(float));

			haspatch = 1;
		}
		else if (flag == 2 && hasprevpatch)
		{
			v[0] = prevp[6];
			v[1] = prevp[7];
			v[2] = prevp[8];
			v[3] = prevp[9];
			memcpy(c[0], prevc[2], ncomp * sizeof(float));
			memcpy(c[1], prevc[3], ncomp * sizeof(float));

			haspatch = 1;
		}
		else if (flag == 3 && hasprevpatch)
		{
			v[0] = prevp[ 9];
			v[1] = prevp[10];
			v[2] = prevp[11];
			v[3] = prevp[ 0];
			memcpy(c[0], prevc[3], ncomp * sizeof(float));
			memcpy(c[1], prevc[0], ncomp * sizeof(float));

			haspatch = 1;
		}

		if (haspatch)
		{
			pdf_tensor_patch patch;

			pdf_make_tensor_patch(&patch, 6, v);

			for (i = 0; i < 4; i++)
				memcpy(patch.color[i], c[i], ncomp * sizeof(float));

			draw_patch(ctx, shade, &patch, SUBDIV, SUBDIV);

			for (i = 0; i < 12; i++)
				prevp[i] = v[i];

			for (i = 0; i < 4; i++)
				memcpy(prevc[i], c[i], ncomp * sizeof(float));

			hasprevpatch = 1;
		}
	}
	fz_close(stream);
}

static void
pdf_load_type7_shade(fz_shade *shade, pdf_document *xref, pdf_obj *dict,
	int funcs, pdf_function **func)
{
	fz_context *ctx = xref->ctx;
	struct mesh_params p;
	int haspatch, hasprevpatch;
	float prevc[4][FZ_MAX_COLORS];
	fz_point prevp[16];
	int ncomp;
	int i, k;
	fz_stream *stream;

	pdf_load_mesh_params(xref, dict, &p);

	if (funcs > 0)
	{
		ncomp = 1;
		pdf_sample_shade_function(ctx, shade, funcs, func, p.c0[0], p.c1[0]);
	}
	else
		ncomp = shade->colorspace->n;

	hasprevpatch = 0;

	stream = pdf_open_stream(xref, pdf_to_num(dict), pdf_to_gen(dict));

	while (!fz_is_eof_bits(stream))
	{
		float c[4][FZ_MAX_COLORS];
		fz_point v[16];
		int startcolor;
		int startpt;
		int flag;

		flag = fz_read_bits(stream, p.bpflag);

		if (flag == 0)
		{
			startpt = 0;
			startcolor = 0;
		}
		else
		{
			startpt = 4;
			startcolor = 2;
		}

		for (i = startpt; i < 16; i++)
		{
			v[i].x = read_sample(stream, p.bpcoord, p.x0, p.x1);
			v[i].y = read_sample(stream, p.bpcoord, p.y0, p.y1);
		}

		for (i = startcolor; i < 4; i++)
		{
			for (k = 0; k < ncomp; k++)
				c[i][k] = read_sample(stream, p.bpcomp, p.c0[k], p.c1[k]);
		}

		haspatch = 0;

		if (flag == 0)
		{
			haspatch = 1;
		}
		else if (flag == 1 && hasprevpatch)
		{
			v[0] = prevp[3];
			v[1] = prevp[4];
			v[2] = prevp[5];
			v[3] = prevp[6];
			memcpy(c[0], prevc[1], ncomp * sizeof(float));
			memcpy(c[1], prevc[2], ncomp * sizeof(float));

			haspatch = 1;
		}
		else if (flag == 2 && hasprevpatch)
		{
			v[0] = prevp[6];
			v[1] = prevp[7];
			v[2] = prevp[8];
			v[3] = prevp[9];
			memcpy(c[0], prevc[2], ncomp * sizeof(float));
			memcpy(c[1], prevc[3], ncomp * sizeof(float));

			haspatch = 1;
		}
		else if (flag == 3 && hasprevpatch)
		{
			v[0] = prevp[ 9];
			v[1] = prevp[10];
			v[2] = prevp[11];
			v[3] = prevp[ 0];
			memcpy(c[0], prevc[3], ncomp * sizeof(float));
			memcpy(c[1], prevc[0], ncomp * sizeof(float));

			haspatch = 1;
		}

		if (haspatch)
		{
			pdf_tensor_patch patch;

			pdf_make_tensor_patch(&patch, 7, v);

			for (i = 0; i < 4; i++)
				memcpy(patch.color[i], c[i], ncomp * sizeof(float));

			draw_patch(ctx, shade, &patch, SUBDIV, SUBDIV);

			for (i = 0; i < 16; i++)
				prevp[i] = v[i];

			for (i = 0; i < 4; i++)
				memcpy(prevc[i], c[i], FZ_MAX_COLORS * sizeof(float));

			hasprevpatch = 1;
		}
	}
	fz_close(stream);
}

/* Load all of the shading dictionary parameters, then switch on the shading type. */

static fz_shade *
pdf_load_shading_dict(pdf_document *xref, pdf_obj *dict, fz_matrix transform)
{
	fz_shade *shade = NULL;
	pdf_function *func[FZ_MAX_COLORS] = { NULL };
	pdf_obj *obj;
	int funcs = 0;
	int type = 0;
	int i;
	fz_context *ctx = xref->ctx;

	fz_var(shade);
	fz_var(func);
	fz_var(funcs);
	fz_var(type);

	fz_try(ctx)
	{
		shade = fz_malloc_struct(ctx, fz_shade);
		FZ_INIT_STORABLE(shade, 1, fz_free_shade_imp);
		shade->type = FZ_MESH;
		shade->use_background = 0;
		shade->use_function = 0;
		shade->matrix = transform;
		shade->bbox = fz_infinite_rect;
		shade->extend[0] = 0;
		shade->extend[1] = 0;

		shade->mesh_len = 0;
		shade->mesh_cap = 0;
		shade->mesh = NULL;

		shade->colorspace = NULL;

		funcs = 0;

		obj = pdf_dict_gets(dict, "ShadingType");
		type = pdf_to_int(obj);

		obj = pdf_dict_gets(dict, "ColorSpace");
		if (!obj)
			fz_throw(ctx, "shading colorspace is missing");
		shade->colorspace = pdf_load_colorspace(xref, obj);
		/* RJW: "cannot load colorspace (%d %d R)", pdf_to_num(obj), pdf_to_gen(obj) */

		obj = pdf_dict_gets(dict, "Background");
		if (obj)
		{
			shade->use_background = 1;
			for (i = 0; i < shade->colorspace->n; i++)
				shade->background[i] = pdf_to_real(pdf_array_get(obj, i));
		}

		obj = pdf_dict_gets(dict, "BBox");
		if (pdf_is_array(obj))
		{
			shade->bbox = pdf_to_rect(ctx, obj);
		}

		obj = pdf_dict_gets(dict, "Function");
		if (pdf_is_dict(obj))
		{
			funcs = 1;

			func[0] = pdf_load_function(xref, obj);
			if (!func[0])
				fz_throw(ctx, "cannot load shading function (%d %d R)", pdf_to_num(obj), pdf_to_gen(obj));
		}
		else if (pdf_is_array(obj))
		{
			funcs = pdf_array_len(obj);
			if (funcs != 1 && funcs != shade->colorspace->n)
				fz_throw(ctx, "incorrect number of shading functions");

			for (i = 0; i < funcs; i++)
			{
				func[i] = pdf_load_function(xref, pdf_array_get(obj, i));
				if (!func[i])
					fz_throw(ctx, "cannot load shading function (%d %d R)", pdf_to_num(obj), pdf_to_gen(obj));
			}
		}
		printf("阴影类型%d 函数数量%d\n",type,funcs);
		currentstackpoint->shadowtype=type;
		currentstackpoint->bcolor=(float*)malloc(4*sizeof(float));
		currentstackpoint->ecolor=(float*)malloc(4*sizeof(float));//为渐变颜色分配空间
		switch (type)//处理不同类型的阴影
		{
		case 1: pdf_load_function_based_shading(shade, xref, dict, func[0]); break;
		case 2: pdf_load_axial_shading(shade, xref, dict, funcs, func); break;//测试用的是阴影类型二
		case 3: pdf_load_radial_shading(shade, xref, dict, funcs, func); break;
		case 4: pdf_load_type4_shade(shade, xref, dict, funcs, func); break;
		case 5: pdf_load_type5_shade(shade, xref, dict, funcs, func); break;
		case 6: pdf_load_type6_shade(shade, xref, dict, funcs, func); break;
		case 7: pdf_load_type7_shade(shade, xref, dict, funcs, func); break;
		default:
			fz_throw(ctx, "unknown shading type: %d", type);
		}

		for (i = 0; i < funcs; i++)
			if (func[i])
				pdf_drop_function(ctx, func[i]);
	}
	fz_catch(ctx)
	{
		for (i = 0; i < funcs; i++)
			if (func[i])
				pdf_drop_function(ctx, func[i]);
		fz_drop_shade(ctx, shade);

		fz_throw(ctx, "cannot load shading type %d (%d %d R)", type, pdf_to_num(dict), pdf_to_gen(dict));
	}
	return shade;
}

static unsigned int
fz_shade_size(fz_shade *s)
{
	if (s == NULL)
		return 0;
	return sizeof(*s) + s->mesh_cap * sizeof(*s->mesh) + s->colorspace->size;
}

fz_shade *
pdf_load_shading(pdf_document *xref, pdf_obj *dict)
{
	fz_matrix mat;
	pdf_obj *obj;
	fz_context *ctx = xref->ctx;
	fz_shade *shade;

	if ((shade = pdf_find_item(ctx, fz_free_shade_imp, dict)))
	{
		return shade;
	}

	/* Type 2 pattern dictionary */
	if (pdf_dict_gets(dict, "PatternType"))
	{
		obj = pdf_dict_gets(dict, "Matrix");
		if (obj)
			mat = pdf_to_matrix(ctx, obj);
		else
			mat = fz_identity;

		obj = pdf_dict_gets(dict, "ExtGState");
		if (obj)
		{
			if (pdf_dict_gets(obj, "CA") || pdf_dict_gets(obj, "ca"))
			{
				fz_warn(ctx, "shading with alpha not supported");
			}
		}

		obj = pdf_dict_gets(dict, "Shading");
		if (!obj)
			fz_throw(ctx, "syntaxerror: missing shading dictionary");

		shade = pdf_load_shading_dict(xref, obj, mat);
		/* RJW: "cannot load shading dictionary (%d %d R)", pdf_to_num(obj), pdf_to_gen(obj) */
	}

	/* Naked shading dictionary */
	else
	{
		shade = pdf_load_shading_dict(xref, dict, fz_identity);
		/* RJW: "cannot load shading dictionary (%d %d R)", pdf_to_num(dict), pdf_to_gen(dict) */
	}

	pdf_store_item(ctx, dict, shade, fz_shade_size(shade));

	return shade;
}
