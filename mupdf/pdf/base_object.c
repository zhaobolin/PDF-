#include "fitz-internal.h"
#include "mupdf-internal.h"

typedef enum pdf_objkind_e
{
	PDF_NULL,
	PDF_BOOL,
	PDF_INT,
	PDF_REAL,
	PDF_STRING,
	PDF_NAME,
	PDF_ARRAY,
	PDF_DICT,
	PDF_INDIRECT
} pdf_objkind;

struct keyval
{
	pdf_obj *k;
	pdf_obj *v;
};

struct pdf_obj_s//object结构体
{
	int refs;
	pdf_objkind kind;//object类别
	fz_context *ctx;
	union
	{
		int b;
		int i;
		float f;
		struct {
			unsigned short len;
			char buf[1];
		} s;
		char n[1];
		struct {
			int len;
			int cap;
			pdf_obj **items;
		} a;
		struct {
			char sorted;
			char marked;
			int len;
			int cap;
			struct keyval *items;
		} d;
		struct {
			int num;
			int gen;
			struct pdf_xref_s *xref;
		} r;
	} u;
};

pdf_obj *
pdf_new_null(fz_context *ctx)
{
	pdf_obj *obj;
	obj = Memento_label(fz_malloc(ctx, sizeof(pdf_obj)), "pdf_obj(null)");
	obj->ctx = ctx;
	obj->refs = 1;
	obj->kind = PDF_NULL;
	return obj;
}

pdf_obj *
pdf_new_bool(fz_context *ctx, int b)
{
	pdf_obj *obj;
	obj = Memento_label(fz_malloc(ctx, sizeof(pdf_obj)), "pdf_obj(bool)");
	obj->ctx = ctx;
	obj->refs = 1;
	obj->kind = PDF_BOOL;
	obj->u.b = b;
	return obj;
}

pdf_obj *
pdf_new_int(fz_context *ctx, int i)
{
	pdf_obj *obj;
	obj = Memento_label(fz_malloc(ctx, sizeof(pdf_obj)), "pdf_obj(int)");
	obj->ctx = ctx;
	obj->refs = 1;
	obj->kind = PDF_INT;
	obj->u.i = i;
	return obj;
}

pdf_obj *
pdf_new_real(fz_context *ctx, float f)
{
	pdf_obj *obj;
	obj = Memento_label(fz_malloc(ctx, sizeof(pdf_obj)), "pdf_obj(real)");
	obj->ctx = ctx;
	obj->refs = 1;
	obj->kind = PDF_REAL;
	obj->u.f = f;
	return obj;
}

pdf_obj *
pdf_new_string(fz_context *ctx, char *str, int len)
{
	pdf_obj *obj;
	obj = Memento_label(fz_malloc(ctx, offsetof(pdf_obj, u.s.buf) + len + 1), "pdf_obj(string)");
	obj->ctx = ctx;
	obj->refs = 1;
	obj->kind = PDF_STRING;
	obj->u.s.len = len;
	memcpy(obj->u.s.buf, str, len);
	obj->u.s.buf[len] = '\0';
	return obj;
}

pdf_obj *
fz_new_name(fz_context *ctx, char *str)
{
	pdf_obj *obj;
	obj = Memento_label(fz_malloc(ctx, offsetof(pdf_obj, u.n) + strlen(str) + 1), "pdf_obj(name)");
	obj->ctx = ctx;
	obj->refs = 1;
	obj->kind = PDF_NAME;
	strcpy(obj->u.n, str);
	return obj;
}

pdf_obj *
pdf_new_indirect(fz_context *ctx, int num, int gen, void *xref)
{
	pdf_obj *obj;
	obj = Memento_label(fz_malloc(ctx, sizeof(pdf_obj)), "pdf_obj(indirect)");
	obj->ctx = ctx;
	obj->refs = 1;
	obj->kind = PDF_INDIRECT;
	obj->u.r.num = num;
	obj->u.r.gen = gen;
	obj->u.r.xref = xref;
	return obj;
}

pdf_obj *
pdf_keep_obj(pdf_obj *obj)
{
	assert(obj);
	obj->refs ++;
	return obj;
}

int pdf_is_indirect(pdf_obj *obj)
{
	return obj ? obj->kind == PDF_INDIRECT : 0;
}

#define RESOLVE(obj) \
	do { \
		if (obj && obj->kind == PDF_INDIRECT) \
		{\
			fz_assert_lock_not_held(obj->ctx, FZ_LOCK_FILE); \
			obj = pdf_resolve_indirect(obj); \
		} \
	} while (0)

int pdf_is_null(pdf_obj *obj)
{
	RESOLVE(obj);
	return obj ? obj->kind == PDF_NULL : 0;
}

int pdf_is_bool(pdf_obj *obj)
{
	RESOLVE(obj);
	return obj ? obj->kind == PDF_BOOL : 0;
}

int pdf_is_int(pdf_obj *obj)
{
	RESOLVE(obj);
	return obj ? obj->kind == PDF_INT : 0;
}

int pdf_is_real(pdf_obj *obj)
{
	RESOLVE(obj);
	return obj ? obj->kind == PDF_REAL : 0;
}

int pdf_is_string(pdf_obj *obj)
{
	RESOLVE(obj);
	return obj ? obj->kind == PDF_STRING : 0;
}

int pdf_is_name(pdf_obj *obj)
{
	RESOLVE(obj);
	return obj ? obj->kind == PDF_NAME : 0;
}

int pdf_is_array(pdf_obj *obj)
{
	RESOLVE(obj);
	return obj ? obj->kind == PDF_ARRAY : 0;
}

int pdf_is_dict(pdf_obj *obj)
{
	RESOLVE(obj);
	return obj ? obj->kind == PDF_DICT : 0;
}

int pdf_to_bool(pdf_obj *obj)
{
	RESOLVE(obj);
	if (!obj)
		return 0;
	return obj->kind == PDF_BOOL ? obj->u.b : 0;
}

int pdf_to_int(pdf_obj *obj)
{
	RESOLVE(obj);
	if (!obj)
		return 0;
	if (obj->kind == PDF_INT)
		return obj->u.i;
	if (obj->kind == PDF_REAL)
		return (int)(obj->u.f + 0.5f); /* No roundf in MSVC */
	return 0;
}

float pdf_to_real(pdf_obj *obj)
{
	RESOLVE(obj);
	if (!obj)
		return 0;
	if (obj->kind == PDF_REAL)
		return obj->u.f;
	if (obj->kind == PDF_INT)
		return obj->u.i;
	return 0;
}

char *pdf_to_name(pdf_obj *obj)
{
	RESOLVE(obj);
	if (!obj || obj->kind != PDF_NAME)
		return "";
	return obj->u.n;
}

char *pdf_to_str_buf(pdf_obj *obj)
{
	RESOLVE(obj);
	if (!obj || obj->kind != PDF_STRING)
		return "";
	return obj->u.s.buf;
}

int pdf_to_str_len(pdf_obj *obj)
{
	RESOLVE(obj);
	if (!obj || obj->kind != PDF_STRING)
		return 0;
	return obj->u.s.len;
}

/* for use by pdf_crypt_obj_imp to decrypt AES string in place */
void pdf_set_str_len(pdf_obj *obj, int newlen)
{
	RESOLVE(obj);
	if (!obj || obj->kind != PDF_STRING)
		return; /* This should never happen */
	if (newlen > obj->u.s.len)
		return; /* This should never happen */
	obj->u.s.len = newlen;
}

pdf_obj *pdf_to_dict(pdf_obj *obj)
{
	RESOLVE(obj);
	return (obj && obj->kind == PDF_DICT ? obj : NULL);
}

int pdf_to_num(pdf_obj *obj)
{
	if (!obj || obj->kind != PDF_INDIRECT)
		return 0;
	return obj->u.r.num;
}

int pdf_to_gen(pdf_obj *obj)
{
	if (!obj || obj->kind != PDF_INDIRECT)
		return 0;
	return obj->u.r.gen;
}

void *pdf_get_indirect_document(pdf_obj *obj)
{
	if (!obj || obj->kind != PDF_INDIRECT)
		return NULL;
	return obj->u.r.xref;
}

int
pdf_objcmp(pdf_obj *a, pdf_obj *b)
{
	int i;

	if (a == b)
		return 0;

	if (!a || !b)
		return 1;

	if (a->kind != b->kind)
		return 1;

	switch (a->kind)
	{
	case PDF_NULL:
		return 0;

	case PDF_BOOL:
		return a->u.b - b->u.b;

	case PDF_INT:
		return a->u.i - b->u.i;

	case PDF_REAL:
		if (a->u.f < b->u.f)
			return -1;
		if (a->u.f > b->u.f)
			return 1;
		return 0;

	case PDF_STRING:
		if (a->u.s.len < b->u.s.len)
		{
			if (memcmp(a->u.s.buf, b->u.s.buf, a->u.s.len) <= 0)
				return -1;
			return 1;
		}
		if (a->u.s.len > b->u.s.len)
		{
			if (memcmp(a->u.s.buf, b->u.s.buf, b->u.s.len) >= 0)
				return 1;
			return -1;
		}
		return memcmp(a->u.s.buf, b->u.s.buf, a->u.s.len);

	case PDF_NAME:
		return strcmp(a->u.n, b->u.n);

	case PDF_INDIRECT:
		if (a->u.r.num == b->u.r.num)
			return a->u.r.gen - b->u.r.gen;
		return a->u.r.num - b->u.r.num;

	case PDF_ARRAY:
		if (a->u.a.len != b->u.a.len)
			return a->u.a.len - b->u.a.len;
		for (i = 0; i < a->u.a.len; i++)
			if (pdf_objcmp(a->u.a.items[i], b->u.a.items[i]))
				return 1;
		return 0;

	case PDF_DICT:
		if (a->u.d.len != b->u.d.len)
			return a->u.d.len - b->u.d.len;
		for (i = 0; i < a->u.d.len; i++)
		{
			if (pdf_objcmp(a->u.d.items[i].k, b->u.d.items[i].k))
				return 1;
			if (pdf_objcmp(a->u.d.items[i].v, b->u.d.items[i].v))
				return 1;
		}
		return 0;

	}
	return 1;
}

static char *
pdf_objkindstr(pdf_obj *obj)
{
	if (!obj)
		return "<NULL>";
	switch (obj->kind)
	{
	case PDF_NULL: return "null";
	case PDF_BOOL: return "boolean";
	case PDF_INT: return "integer";
	case PDF_REAL: return "real";
	case PDF_STRING: return "string";
	case PDF_NAME: return "name";
	case PDF_ARRAY: return "array";
	case PDF_DICT: return "dictionary";
	case PDF_INDIRECT: return "reference";
	}
	return "<unknown>";
}

pdf_obj *
pdf_new_array(fz_context *ctx, int initialcap)
{
	pdf_obj *obj;
	int i;

	obj = Memento_label(fz_malloc(ctx, sizeof(pdf_obj)), "pdf_obj(array)");
	obj->ctx = ctx;
	obj->refs = 1;
	obj->kind = PDF_ARRAY;

	obj->u.a.len = 0;
	obj->u.a.cap = initialcap > 1 ? initialcap : 6;

	fz_try(ctx)
	{
		obj->u.a.items = Memento_label(fz_malloc_array(ctx, obj->u.a.cap, sizeof(pdf_obj*)), "pdf_obj(array items)");
	}
	fz_catch(ctx)
	{
		fz_free(ctx, obj);
		fz_rethrow(ctx);
	}
	for (i = 0; i < obj->u.a.cap; i++)
		obj->u.a.items[i] = NULL;

	return obj;
}

static void
pdf_array_grow(pdf_obj *obj)
{
	int i;

	obj->u.a.cap = (obj->u.a.cap * 3) / 2;
	obj->u.a.items = fz_resize_array(obj->ctx, obj->u.a.items, obj->u.a.cap, sizeof(pdf_obj*));

	for (i = obj->u.a.len ; i < obj->u.a.cap; i++)
		obj->u.a.items[i] = NULL;
}

pdf_obj *
pdf_copy_array(fz_context *ctx, pdf_obj *obj)
{
	pdf_obj *arr;
	int i;
	int n;

	RESOLVE(obj);
	if (!obj)
		return NULL; /* Can't warn :( */
	if (obj->kind != PDF_ARRAY)
		fz_warn(ctx, "assert: not an array (%s)", pdf_objkindstr(obj));

	arr = pdf_new_array(ctx, pdf_array_len(obj));
	n = pdf_array_len(obj);
	for (i = 0; i < n; i++)
		pdf_array_push(arr, pdf_array_get(obj, i));

	return arr;
}

int
pdf_array_len(pdf_obj *obj)
{
	RESOLVE(obj);
	if (!obj || obj->kind != PDF_ARRAY)
		return 0;
	return obj->u.a.len;
}

pdf_obj *
pdf_array_get(pdf_obj *obj, int i)
{
	RESOLVE(obj);

	if (!obj || obj->kind != PDF_ARRAY)
		return NULL;

	if (i < 0 || i >= obj->u.a.len)
		return NULL;

	return obj->u.a.items[i];
}

void
pdf_array_put(pdf_obj *obj, int i, pdf_obj *item)
{
	RESOLVE(obj);

	if (!obj)
		return; /* Can't warn :( */
	if (obj->kind != PDF_ARRAY)
		fz_warn(obj->ctx, "assert: not an array (%s)", pdf_objkindstr(obj));
	else if (i < 0)
		fz_warn(obj->ctx, "assert: index %d < 0", i);
	else if (i >= obj->u.a.len)
		fz_warn(obj->ctx, "assert: index %d > length %d", i, obj->u.a.len);
	else
	{
		if (obj->u.a.items[i])
			pdf_drop_obj(obj->u.a.items[i]);
		obj->u.a.items[i] = pdf_keep_obj(item);
	}
}

void
pdf_array_push(pdf_obj *obj, pdf_obj *item)
{
	RESOLVE(obj);

	if (!obj)
		return; /* Can't warn :( */
	if (obj->kind != PDF_ARRAY)
		fz_warn(obj->ctx, "assert: not an array (%s)", pdf_objkindstr(obj));
	else
	{
		if (obj->u.a.len + 1 > obj->u.a.cap)
			pdf_array_grow(obj);
		obj->u.a.items[obj->u.a.len] = pdf_keep_obj(item);
		obj->u.a.len++;
	}
}

void
pdf_array_insert(pdf_obj *obj, pdf_obj *item)
{
	RESOLVE(obj);

	if (!obj)
		return; /* Can't warn :( */
	if (obj->kind != PDF_ARRAY)
		fz_warn(obj->ctx, "assert: not an array (%s)", pdf_objkindstr(obj));
	else
	{
		if (obj->u.a.len + 1 > obj->u.a.cap)
			pdf_array_grow(obj);
		memmove(obj->u.a.items + 1, obj->u.a.items, obj->u.a.len * sizeof(pdf_obj*));
		obj->u.a.items[0] = pdf_keep_obj(item);
		obj->u.a.len++;
	}
}

int
pdf_array_contains(pdf_obj *arr, pdf_obj *obj)
{
	int i;

	for (i = 0; i < pdf_array_len(arr); i++)
		if (!pdf_objcmp(pdf_array_get(arr, i), obj))
			return 1;

	return 0;
}

/* dicts may only have names as keys! */

static int keyvalcmp(const void *ap, const void *bp)
{
	const struct keyval *a = ap;
	const struct keyval *b = bp;
	return strcmp(pdf_to_name(a->k), pdf_to_name(b->k));
}

pdf_obj *
pdf_new_dict(fz_context *ctx, int initialcap)
{
	pdf_obj *obj;
	int i;

	obj = Memento_label(fz_malloc(ctx, sizeof(pdf_obj)), "pdf_obj(dict)");
	obj->ctx = ctx;
	obj->refs = 1;
	obj->kind = PDF_DICT;

	obj->u.d.sorted = 0;
	obj->u.d.marked = 0;
	obj->u.d.len = 0;
	obj->u.d.cap = initialcap > 1 ? initialcap : 10;

	fz_try(ctx)
	{
		obj->u.d.items = Memento_label(fz_malloc_array(ctx, obj->u.d.cap, sizeof(struct keyval)), "pdf_obj(dict items)");
	}
	fz_catch(ctx)
	{
		fz_free(ctx, obj);
		fz_rethrow(ctx);
	}
	for (i = 0; i < obj->u.d.cap; i++)
	{
		obj->u.d.items[i].k = NULL;
		obj->u.d.items[i].v = NULL;
	}

	return obj;
}

static void
pdf_dict_grow(pdf_obj *obj)
{
	int i;

	obj->u.d.cap = (obj->u.d.cap * 3) / 2;
	obj->u.d.items = fz_resize_array(obj->ctx, obj->u.d.items, obj->u.d.cap, sizeof(struct keyval));

	for (i = obj->u.d.len; i < obj->u.d.cap; i++)
	{
		obj->u.d.items[i].k = NULL;
		obj->u.d.items[i].v = NULL;
	}
}

pdf_obj *
pdf_copy_dict(fz_context *ctx, pdf_obj *obj)
{
	pdf_obj *dict;
	int i, n;

	RESOLVE(obj);
	if (!obj)
		return NULL; /* Can't warn :( */
	if (obj->kind != PDF_DICT)
		fz_warn(ctx, "assert: not a dict (%s)", pdf_objkindstr(obj));

	n = pdf_dict_len(obj);
	dict = pdf_new_dict(ctx, n);
	for (i = 0; i < n; i++)
		fz_dict_put(dict, pdf_dict_get_key(obj, i), pdf_dict_get_val(obj, i));

	return dict;
}

int
pdf_dict_len(pdf_obj *obj)
{
	RESOLVE(obj);
	if (!obj || obj->kind != PDF_DICT)
		return 0;
	return obj->u.d.len;
}

pdf_obj *
pdf_dict_get_key(pdf_obj *obj, int i)
{
	RESOLVE(obj);
	if (!obj || obj->kind != PDF_DICT)
		return NULL;

	if (i < 0 || i >= obj->u.d.len)
		return NULL;

	return obj->u.d.items[i].k;
}

pdf_obj *
pdf_dict_get_val(pdf_obj *obj, int i)
{
	RESOLVE(obj);
	if (!obj || obj->kind != PDF_DICT)
		return NULL;

	if (i < 0 || i >= obj->u.d.len)
		return NULL;

	return obj->u.d.items[i].v;
}

static int
pdf_dict_finds(pdf_obj *obj, char *key, int *location)
{
	if (obj->u.d.sorted && obj->u.d.len > 0)
	{
		int l = 0;
		int r = obj->u.d.len - 1;

		if (strcmp(pdf_to_name(obj->u.d.items[r].k), key) < 0)
		{
			if (location)
				*location = r + 1;
			return -1;
		}

		while (l <= r)
		{
			int m = (l + r) >> 1;
			int c = -strcmp(pdf_to_name(obj->u.d.items[m].k), key);
			if (c < 0)
				r = m - 1;
			else if (c > 0)
				l = m + 1;
			else
				return m;

			if (location)
				*location = l;
		}
	}

	else
	{
		int i;
		for (i = 0; i < obj->u.d.len; i++)
			if (strcmp(pdf_to_name(obj->u.d.items[i].k), key) == 0)
				return i;

		if (location)
			*location = obj->u.d.len;
	}

	return -1;
}

pdf_obj *
pdf_dict_gets(pdf_obj *obj, char *key)
{
	int i;

	RESOLVE(obj);
	if (!obj || obj->kind != PDF_DICT)
		return NULL;

	i = pdf_dict_finds(obj, key, NULL);
	if (i >= 0)
		return obj->u.d.items[i].v;

	return NULL;
}

pdf_obj *
pdf_dict_get(pdf_obj *obj, pdf_obj *key)
{
	if (!key || key->kind != PDF_NAME)
		return NULL;
	return pdf_dict_gets(obj, pdf_to_name(key));
}

pdf_obj *
pdf_dict_getsa(pdf_obj *obj, char *key, char *abbrev)
{
	pdf_obj *v;
	v = pdf_dict_gets(obj, key);
	if (v)
		return v;
	return pdf_dict_gets(obj, abbrev);
}

void
fz_dict_put(pdf_obj *obj, pdf_obj *key, pdf_obj *val)
{
	int location;
	char *s;
	int i;

	RESOLVE(obj);
	if (!obj)
		return; /* Can't warn :( */
	if (obj->kind != PDF_DICT)
	{
		fz_warn(obj->ctx, "assert: not a dict (%s)", pdf_objkindstr(obj));
		return;
	}

	RESOLVE(key);
	if (!key || key->kind != PDF_NAME)
	{
		fz_warn(obj->ctx, "assert: key is not a name (%s)", pdf_objkindstr(obj));
		return;
	}
	else
		s = pdf_to_name(key);

	if (!val)
	{
		fz_warn(obj->ctx, "assert: val does not exist for key (%s)", s);
		return;
	}

	if (obj->u.d.len > 100 && !obj->u.d.sorted)
		pdf_sort_dict(obj);

	i = pdf_dict_finds(obj, s, &location);
	if (i >= 0 && i < obj->u.d.len)
	{
		pdf_drop_obj(obj->u.d.items[i].v);
		obj->u.d.items[i].v = pdf_keep_obj(val);
	}
	else
	{
		if (obj->u.d.len + 1 > obj->u.d.cap)
			pdf_dict_grow(obj);

		i = location;
		if (obj->u.d.sorted && obj->u.d.len > 0)
			memmove(&obj->u.d.items[i + 1],
				&obj->u.d.items[i],
				(obj->u.d.len - i) * sizeof(struct keyval));

		obj->u.d.items[i].k = pdf_keep_obj(key);
		obj->u.d.items[i].v = pdf_keep_obj(val);
		obj->u.d.len ++;
	}
}

void
pdf_dict_puts(pdf_obj *obj, char *key, pdf_obj *val)
{
	pdf_obj *keyobj = fz_new_name(obj->ctx, key);
	fz_dict_put(obj, keyobj, val);
	pdf_drop_obj(keyobj);
}

void
pdf_dict_dels(pdf_obj *obj, char *key)
{
	RESOLVE(obj);

	if (!obj)
		return; /* Can't warn :( */
	if (obj->kind != PDF_DICT)
		fz_warn(obj->ctx, "assert: not a dict (%s)", pdf_objkindstr(obj));
	else
	{
		int i = pdf_dict_finds(obj, key, NULL);
		if (i >= 0)
		{
			pdf_drop_obj(obj->u.d.items[i].k);
			pdf_drop_obj(obj->u.d.items[i].v);
			obj->u.d.sorted = 0;
			obj->u.d.items[i] = obj->u.d.items[obj->u.d.len-1];
			obj->u.d.len --;
		}
	}
}

void
pdf_dict_del(pdf_obj *obj, pdf_obj *key)
{
	RESOLVE(key);
	if (!key || key->kind != PDF_NAME)
		fz_warn(obj->ctx, "assert: key is not a name (%s)", pdf_objkindstr(obj));
	else
		pdf_dict_dels(obj, key->u.n);
}

void
pdf_sort_dict(pdf_obj *obj)
{
	RESOLVE(obj);
	if (!obj || obj->kind != PDF_DICT)
		return;
	if (!obj->u.d.sorted)
	{
		qsort(obj->u.d.items, obj->u.d.len, sizeof(struct keyval), keyvalcmp);
		obj->u.d.sorted = 1;
	}
}

int
pdf_dict_marked(pdf_obj *obj)
{
	RESOLVE(obj);
	if (!obj || obj->kind != PDF_DICT)
		return 0;
	return obj->u.d.marked;
}

int
pdf_dict_mark(pdf_obj *obj)
{
	int marked;
	RESOLVE(obj);
	if (!obj || obj->kind != PDF_DICT)
		return 0;
	marked = obj->u.d.marked;
	obj->u.d.marked = 1;
	return marked;
}

void
pdf_dict_unmark(pdf_obj *obj)
{
	RESOLVE(obj);
	if (!obj || obj->kind != PDF_DICT)
		return;
	obj->u.d.marked = 0;
}

static void
pdf_free_array(pdf_obj *obj)
{
	int i;

	for (i = 0; i < obj->u.a.len; i++)
		if (obj->u.a.items[i])
			pdf_drop_obj(obj->u.a.items[i]);

	fz_free(obj->ctx, obj->u.a.items);
	fz_free(obj->ctx, obj);
}

static void
pdf_free_dict(pdf_obj *obj)
{
	int i;

	for (i = 0; i < obj->u.d.len; i++) {
		if (obj->u.d.items[i].k)
			pdf_drop_obj(obj->u.d.items[i].k);
		if (obj->u.d.items[i].v)
			pdf_drop_obj(obj->u.d.items[i].v);
	}

	fz_free(obj->ctx, obj->u.d.items);
	fz_free(obj->ctx, obj);
}

void
pdf_drop_obj(pdf_obj *obj)
{
	if (!obj)
		return;
	if (--obj->refs)
		return;
	if (obj->kind == PDF_ARRAY)
		pdf_free_array(obj);
	else if (obj->kind == PDF_DICT)
		pdf_free_dict(obj);
	else
		fz_free(obj->ctx, obj);
}

/* Pretty printing objects */

struct fmt
{
	char *buf;
	int cap;
	int len;
	int indent;
	int tight;
	int col;
	int sep;
	int last;
};

static void fmt_obj(struct fmt *fmt, pdf_obj *obj);

static inline int iswhite(int ch)
{
	return
		ch == '\000' ||
		ch == '\011' ||
		ch == '\012' ||
		ch == '\014' ||
		ch == '\015' ||
		ch == '\040';
}

static inline int isdelim(int ch)
{
	return	ch == '(' || ch == ')' ||
		ch == '<' || ch == '>' ||
		ch == '[' || ch == ']' ||
		ch == '{' || ch == '}' ||
		ch == '/' ||
		ch == '%';
}

static inline void fmt_putc(struct fmt *fmt, int c)
{
	if (fmt->sep && !isdelim(fmt->last) && !isdelim(c)) {
		fmt->sep = 0;
		fmt_putc(fmt, ' ');
	}
	fmt->sep = 0;

	if (fmt->buf && fmt->len < fmt->cap)
		fmt->buf[fmt->len] = c;

	if (c == '\n')
		fmt->col = 0;
	else
		fmt->col ++;

	fmt->len ++;

	fmt->last = c;
}

static inline void fmt_indent(struct fmt *fmt)
{
	int i = fmt->indent;
	while (i--) {
		fmt_putc(fmt, ' ');
		fmt_putc(fmt, ' ');
	}
}

static inline void fmt_puts(struct fmt *fmt, char *s)
{
	while (*s)
		fmt_putc(fmt, *s++);
}

static inline void fmt_sep(struct fmt *fmt)
{
	fmt->sep = 1;
}

static void fmt_str(struct fmt *fmt, pdf_obj *obj)
{
	char *s = pdf_to_str_buf(obj);
	int n = pdf_to_str_len(obj);
	int i, c;

	fmt_putc(fmt, '(');
	for (i = 0; i < n; i++)
	{
		c = (unsigned char)s[i];
		if (c == '\n')
			fmt_puts(fmt, "\\n");
		else if (c == '\r')
			fmt_puts(fmt, "\\r");
		else if (c == '\t')
			fmt_puts(fmt, "\\t");
		else if (c == '\b')
			fmt_puts(fmt, "\\b");
		else if (c == '\f')
			fmt_puts(fmt, "\\f");
		else if (c == '(')
			fmt_puts(fmt, "\\(");
		else if (c == ')')
			fmt_puts(fmt, "\\)");
		else if (c < 32 || c >= 127) {
			char buf[16];
			fmt_putc(fmt, '\\');
			sprintf(buf, "%03o", c);
			fmt_puts(fmt, buf);
		}
		else
			fmt_putc(fmt, c);
	}
	fmt_putc(fmt, ')');
}

static void fmt_hex(struct fmt *fmt, pdf_obj *obj)
{
	char *s = pdf_to_str_buf(obj);
	int n = pdf_to_str_len(obj);
	int i, b, c;

	fmt_putc(fmt, '<');
	for (i = 0; i < n; i++) {
		b = (unsigned char) s[i];
		c = (b >> 4) & 0x0f;
		fmt_putc(fmt, c < 0xA ? c + '0' : c + 'A' - 0xA);
		c = (b) & 0x0f;
		fmt_putc(fmt, c < 0xA ? c + '0' : c + 'A' - 0xA);
	}
	fmt_putc(fmt, '>');
}

static void fmt_name(struct fmt *fmt, pdf_obj *obj)
{
	unsigned char *s = (unsigned char *) pdf_to_name(obj);
	int i, c;

	fmt_putc(fmt, '/');

	for (i = 0; s[i]; i++)
	{
		if (isdelim(s[i]) || iswhite(s[i]) ||
			s[i] == '#' || s[i] < 32 || s[i] >= 127)
		{
			fmt_putc(fmt, '#');
			c = (s[i] >> 4) & 0xf;
			fmt_putc(fmt, c < 0xA ? c + '0' : c + 'A' - 0xA);
			c = s[i] & 0xf;
			fmt_putc(fmt, c < 0xA ? c + '0' : c + 'A' - 0xA);
		}
		else
		{
			fmt_putc(fmt, s[i]);
		}
	}
}

static void fmt_array(struct fmt *fmt, pdf_obj *obj)
{
	int i, n;

	n = pdf_array_len(obj);
	if (fmt->tight) {
		fmt_putc(fmt, '[');
		for (i = 0; i < n; i++) {
			fmt_obj(fmt, pdf_array_get(obj, i));
			fmt_sep(fmt);
		}
		fmt_putc(fmt, ']');
	}
	else {
		fmt_puts(fmt, "[ ");
		for (i = 0; i < n; i++) {
			if (fmt->col > 60) {
				fmt_putc(fmt, '\n');
				fmt_indent(fmt);
			}
			fmt_obj(fmt, pdf_array_get(obj, i));
			fmt_putc(fmt, ' ');
		}
		fmt_putc(fmt, ']');
		fmt_sep(fmt);
	}
}

static void fmt_dict(struct fmt *fmt, pdf_obj *obj)
{
	int i, n;
	pdf_obj *key, *val;

	n = pdf_dict_len(obj);
	if (fmt->tight) {
		fmt_puts(fmt, "<<");
		for (i = 0; i < n; i++) {
			fmt_obj(fmt, pdf_dict_get_key(obj, i));
			fmt_sep(fmt);
			fmt_obj(fmt, pdf_dict_get_val(obj, i));
			fmt_sep(fmt);
		}
		fmt_puts(fmt, ">>");
	}
	else {
		fmt_puts(fmt, "<<\n");
		fmt->indent ++;
		for (i = 0; i < n; i++) {
			key = pdf_dict_get_key(obj, i);
			val = pdf_dict_get_val(obj, i);
			fmt_indent(fmt);
			fmt_obj(fmt, key);
			fmt_putc(fmt, ' ');
			if (!pdf_is_indirect(val) && pdf_is_array(val))
				fmt->indent ++;
			fmt_obj(fmt, val);
			fmt_putc(fmt, '\n');
			if (!pdf_is_indirect(val) && pdf_is_array(val))
				fmt->indent --;
		}
		fmt->indent --;
		fmt_indent(fmt);
		fmt_puts(fmt, ">>");
	}
}

static void fmt_obj(struct fmt *fmt, pdf_obj *obj)
{
	char buf[256];

	if (!obj)
		fmt_puts(fmt, "<NULL>");
	else if (pdf_is_indirect(obj))
	{
		sprintf(buf, "%d %d R", pdf_to_num(obj), pdf_to_gen(obj));
		fmt_puts(fmt, buf);
	}
	else if (pdf_is_null(obj))
		fmt_puts(fmt, "null");
	else if (pdf_is_bool(obj))
		fmt_puts(fmt, pdf_to_bool(obj) ? "true" : "false");
	else if (pdf_is_int(obj))
	{
		sprintf(buf, "%d", pdf_to_int(obj));
		fmt_puts(fmt, buf);
	}
	else if (pdf_is_real(obj))
	{
		sprintf(buf, "%g", pdf_to_real(obj));
		if (strchr(buf, 'e')) /* bad news! */
			sprintf(buf, fabsf(pdf_to_real(obj)) > 1 ? "%1.1f" : "%1.8f", pdf_to_real(obj));
		fmt_puts(fmt, buf);
	}
	else if (pdf_is_string(obj))
	{
		char *str = pdf_to_str_buf(obj);
		int len = pdf_to_str_len(obj);
		int added = 0;
		int i, c;
		for (i = 0; i < len; i++) {
			c = (unsigned char)str[i];
			if (strchr("()\\\n\r\t\b\f", c))
				added ++;
			else if (c < 32 || c >= 127)
				added += 3;
		}
		if (added < len)
			fmt_str(fmt, obj);
		else
			fmt_hex(fmt, obj);
	}
	else if (pdf_is_name(obj))
		fmt_name(fmt, obj);
	else if (pdf_is_array(obj))
		fmt_array(fmt, obj);
	else if (pdf_is_dict(obj))
		fmt_dict(fmt, obj);
	else
		fmt_puts(fmt, "<unknown object>");
}

static int
pdf_sprint_obj(char *s, int n, pdf_obj *obj, int tight)
{
	struct fmt fmt;

	fmt.indent = 0;
	fmt.col = 0;
	fmt.sep = 0;
	fmt.last = 0;

	fmt.tight = tight;
	fmt.buf = s;
	fmt.cap = n;
	fmt.len = 0;
	fmt_obj(&fmt, obj);

	if (fmt.buf && fmt.len < fmt.cap)
		fmt.buf[fmt.len] = '\0';

	return fmt.len;
}

int
pdf_fprint_obj(FILE *fp, pdf_obj *obj, int tight)
{
	char buf[1024];
	char *ptr;
	int n;

	n = pdf_sprint_obj(NULL, 0, obj, tight);
	if ((n + 1) < sizeof buf)
	{
		pdf_sprint_obj(buf, sizeof buf, obj, tight);
		fputs(buf, fp);
		fputc('\n', fp);
	}
	else
	{
		ptr = fz_malloc(obj->ctx, n + 1);
		pdf_sprint_obj(ptr, n + 1, obj, tight);
		fputs(ptr, fp);
		fputc('\n', fp);
		fz_free(obj->ctx, ptr);
	}
	return n;
}

void
pdf_print_obj(pdf_obj *obj)
{
	pdf_fprint_obj(stdout, obj, 0);
}

void
pdf_print_ref(pdf_obj *ref)
{
	pdf_print_obj(pdf_resolve_indirect(ref));
}
