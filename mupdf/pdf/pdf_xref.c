#include "fitz-internal.h"
#include "mupdf-internal.h"
#include <Windows.h>
static inline int iswhite(int ch)
{
	return
		ch == '\000' || ch == '\011' || ch == '\012' ||
		ch == '\014' || ch == '\015' || ch == '\040';
}

/*
 * magic version tag and startxref
 */

static void
pdf_load_version(pdf_document *xref)
{
	char buf[20];

	fz_seek(xref->file, 0, 0);
	fz_read_line(xref->file, buf, sizeof buf);
	if (memcmp(buf, "%PDF-", 5) != 0)
		fz_throw(xref->ctx, "cannot recognize version marker");

	/* SumatraPDF: use fz_atof once the major or minor PDF version reaches 10 */
	xref->version = atoi(buf + 5) * 10 + atoi(buf + 7);
	printf("Current PDF Version: %d",xref->version);
}

static void
pdf_read_start_xref(pdf_document *xref)
{
	unsigned char buf[1024];
	int t, n;
	int i;

	fz_seek(xref->file, 0, 2);

	xref->file_size = fz_tell(xref->file);

	t = MAX(0, xref->file_size - (int)sizeof buf);
	fz_seek(xref->file, t, 0);

	n = fz_read(xref->file, buf, sizeof buf);
	if (n < 0)
		fz_throw(xref->ctx, "cannot read from file");

	for (i = n - 9; i >= 0; i--)
	{
		if (memcmp(buf + i, "startxref", 9) == 0)
		{
			i += 9;
			while (iswhite(buf[i]) && i < n)
				i ++;
			xref->startxref = atoi((char*)(buf + i));

			return;
		}
	}

	fz_throw(xref->ctx, "cannot find startxref");
}

/*
 * trailer dictionary
 */

static void
pdf_read_old_trailer(pdf_document *xref, pdf_lexbuf *buf)
{
	int len;
	char *s;
	int t;
	int tok;
	int c;

	fz_read_line(xref->file, buf->scratch, buf->size);
	if (strncmp(buf->scratch, "xref", 4) != 0)
		fz_throw(xref->ctx, "cannot find xref marker");

	while (1)
	{
		c = fz_peek_byte(xref->file);
		if (!(c >= '0' && c <= '9'))
			break;

		fz_read_line(xref->file, buf->scratch, buf->size);
		s = buf->scratch;
		fz_strsep(&s, " "); /* ignore ofs */
		if (!s)
			fz_throw(xref->ctx, "invalid range marker in xref");
		len = atoi(fz_strsep(&s, " "));

		/* broken pdfs where the section is not on a separate line */
		if (s && *s != '\0')
			fz_seek(xref->file, -(2 + (int)strlen(s)), 1);

		t = fz_tell(xref->file);
		if (t < 0)
			fz_throw(xref->ctx, "cannot tell in file");

		fz_seek(xref->file, t + 20 * len, 0);
	}

	fz_try(xref->ctx)
	{
		tok = pdf_lex(xref->file, buf);
		if (tok != PDF_TOK_TRAILER)
			fz_throw(xref->ctx, "expected trailer marker");

		tok = pdf_lex(xref->file, buf);
		if (tok != PDF_TOK_OPEN_DICT)
			fz_throw(xref->ctx, "expected trailer dictionary");

		xref->trailer = pdf_parse_dict(xref, xref->file, buf);
	}
	fz_catch(xref->ctx)
	{
		fz_throw(xref->ctx, "cannot parse trailer");
	}
}

static void
pdf_read_new_trailer(pdf_document *xref, pdf_lexbuf *buf)
{
	fz_try(xref->ctx)
	{
		xref->trailer = pdf_parse_ind_obj(xref, xref->file, buf, NULL, NULL, NULL);
	}
	fz_catch(xref->ctx)
	{
		fz_throw(xref->ctx, "cannot parse trailer (compressed)");
	}
}

static void
pdf_read_trailer(pdf_document *xref, pdf_lexbuf *buf)
{
	int c;

	fz_seek(xref->file, xref->startxref, 0);

	while (iswhite(fz_peek_byte(xref->file)))
		fz_read_byte(xref->file);

	fz_try(xref->ctx)
	{
		c = fz_peek_byte(xref->file);
		if (c == 'x')
			pdf_read_old_trailer(xref, buf);
		else if (c >= '0' && c <= '9')
			pdf_read_new_trailer(xref, buf);
		else
			fz_throw(xref->ctx, "cannot recognize xref format: '%c'", c);
	}
	fz_catch(xref->ctx)
	{
		fz_throw(xref->ctx, "cannot read trailer");
	}
}

/*
 * xref tables
 */

void
pdf_resize_xref(pdf_document *xref, int newlen)
{
	int i;

	xref->table = fz_resize_array(xref->ctx, xref->table, newlen, sizeof(pdf_xref_entry));
	for (i = xref->len; i < newlen; i++)
	{
		xref->table[i].type = 0;
		xref->table[i].ofs = 0;
		xref->table[i].gen = 0;
		xref->table[i].stm_ofs = 0;
		xref->table[i].obj = NULL;
	}
	xref->len = newlen;
}

static pdf_obj *
pdf_read_old_xref(pdf_document *xref, pdf_lexbuf *buf)
{
	int ofs, len;
	char *s;
	int n;
	int tok;
	int i;
	int c;
	pdf_obj *trailer;

	fz_read_line(xref->file, buf->scratch, buf->size);
	if (strncmp(buf->scratch, "xref", 4) != 0)
		fz_throw(xref->ctx, "cannot find xref marker");

	while (1)
	{
		c = fz_peek_byte(xref->file);
		if (!(c >= '0' && c <= '9'))
			break;

		fz_read_line(xref->file, buf->scratch, buf->size);
		s = buf->scratch;
		ofs = atoi(fz_strsep(&s, " "));
		len = atoi(fz_strsep(&s, " "));

		/* broken pdfs where the section is not on a separate line */
		if (s && *s != '\0')
		{
			fz_warn(xref->ctx, "broken xref section. proceeding anyway.");
			fz_seek(xref->file, -(2 + (int)strlen(s)), 1);
		}

		/* broken pdfs where size in trailer undershoots entries in xref sections */
		if (ofs + len > xref->len)
		{
			fz_warn(xref->ctx, "broken xref section, proceeding anyway.");
			pdf_resize_xref(xref, ofs + len);
		}

		for (i = ofs; i < ofs + len; i++)
		{
			n = fz_read(xref->file, (unsigned char *) buf->scratch, 20);
			if (n < 0)
				fz_throw(xref->ctx, "cannot read xref table");
			if (!xref->table[i].type)
			{
				s = buf->scratch;

				/* broken pdfs where line start with white space */
				while (*s != '\0' && iswhite(*s))
					s++;

				xref->table[i].ofs = atoi(s);
				xref->table[i].gen = atoi(s + 11);
				xref->table[i].type = s[17];
				if (s[17] != 'f' && s[17] != 'n' && s[17] != 'o')
					fz_throw(xref->ctx, "unexpected xref type: %#x (%d %d R)", s[17], i, xref->table[i].gen);
			}
		}
	}

	fz_try(xref->ctx)
	{
		tok = pdf_lex(xref->file, buf);
		if (tok != PDF_TOK_TRAILER)
			fz_throw(xref->ctx, "expected trailer marker");

		tok = pdf_lex(xref->file, buf);
		if (tok != PDF_TOK_OPEN_DICT)
			fz_throw(xref->ctx, "expected trailer dictionary");

		trailer = pdf_parse_dict(xref, xref->file, buf);
	}
	fz_catch(xref->ctx)
	{
		fz_throw(xref->ctx, "cannot parse trailer");
	}
	return trailer;
}

static void
pdf_read_new_xref_section(pdf_document *xref, fz_stream *stm, int i0, int i1, int w0, int w1, int w2)
{
	int i, n;

	if (i0 < 0 || i0 + i1 > xref->len)
		fz_throw(xref->ctx, "xref stream has too many entries");

	for (i = i0; i < i0 + i1; i++)
	{
		int a = 0;
		int b = 0;
		int c = 0;

		if (fz_is_eof(stm))
			fz_throw(xref->ctx, "truncated xref stream");

		for (n = 0; n < w0; n++)
			a = (a << 8) + fz_read_byte(stm);
		for (n = 0; n < w1; n++)
			b = (b << 8) + fz_read_byte(stm);
		for (n = 0; n < w2; n++)
			c = (c << 8) + fz_read_byte(stm);

		if (!xref->table[i].type)
		{
			int t = w0 ? a : 1;
			xref->table[i].type = t == 0 ? 'f' : t == 1 ? 'n' : t == 2 ? 'o' : 0;
			xref->table[i].ofs = w1 ? b : 0;
			xref->table[i].gen = w2 ? c : 0;
		}
	}
}

/* Entered with file locked. Drops the lock in the middle, but then picks
 * it up again before exiting. */
static pdf_obj *
pdf_read_new_xref(pdf_document *xref, pdf_lexbuf *buf)
{
	fz_stream *stm = NULL;
	pdf_obj *trailer = NULL;
	pdf_obj *index = NULL;
	pdf_obj *obj = NULL;
	int num, gen, stm_ofs;
	int size, w0, w1, w2;
	int t;
	fz_context *ctx = xref->ctx;

	fz_var(trailer);
	fz_var(stm);

	fz_try(ctx)
	{
		trailer = pdf_parse_ind_obj(xref, xref->file, buf, &num, &gen, &stm_ofs);
	}
	fz_catch(ctx)
	{
		fz_throw(ctx, "cannot parse compressed xref stream object");
	}

	fz_try(ctx)
	{
		fz_unlock(ctx, FZ_LOCK_FILE);
		obj = pdf_dict_gets(trailer, "Size");
		if (!obj)
			fz_throw(ctx, "xref stream missing Size entry (%d %d R)", num, gen);

		size = pdf_to_int(obj);
		if (size > xref->len)
			pdf_resize_xref(xref, size);

		if (num < 0 || num >= xref->len)
			fz_throw(ctx, "object id (%d %d R) out of range (0..%d)", num, gen, xref->len - 1);

		obj = pdf_dict_gets(trailer, "W");
		if (!obj)
			fz_throw(ctx, "xref stream missing W entry (%d %d R)", num, gen);
		w0 = pdf_to_int(pdf_array_get(obj, 0));
		w1 = pdf_to_int(pdf_array_get(obj, 1));
		w2 = pdf_to_int(pdf_array_get(obj, 2));

		index = pdf_dict_gets(trailer, "Index");

		stm = pdf_open_stream_with_offset(xref, num, gen, trailer, stm_ofs);
		/* RJW: Ensure pdf_open_stream does fz_throw(ctx, "cannot open compressed xref stream (%d %d R)", num, gen); */

		if (!index)
		{
			pdf_read_new_xref_section(xref, stm, 0, size, w0, w1, w2);
			/* RJW: Ensure above does fz_throw(ctx, "cannot read xref stream (%d %d R)", num, gen); */
		}
		else
		{
			int n = pdf_array_len(index);
			for (t = 0; t < n; t += 2)
			{
				int i0 = pdf_to_int(pdf_array_get(index, t + 0));
				int i1 = pdf_to_int(pdf_array_get(index, t + 1));
				pdf_read_new_xref_section(xref, stm, i0, i1, w0, w1, w2);
			}
		}
	}
	fz_always(ctx)
	{
		fz_close(stm);
	}
	fz_catch(ctx)
	{
		pdf_drop_obj(trailer);
		pdf_drop_obj(index);
		fz_rethrow(ctx);
	}
	fz_lock(ctx, FZ_LOCK_FILE);

	return trailer;
}

/* File is locked on entry, and exit (but may be dropped in the middle) */
static pdf_obj *
pdf_read_xref(pdf_document *xref, int ofs, pdf_lexbuf *buf)
{
	int c;
	fz_context *ctx = xref->ctx;
	pdf_obj *trailer;

	fz_seek(xref->file, ofs, 0);

	while (iswhite(fz_peek_byte(xref->file)))
		fz_read_byte(xref->file);

	fz_try(ctx)
	{
		c = fz_peek_byte(xref->file);
		if (c == 'x')
			trailer = pdf_read_old_xref(xref, buf);
		else if (c >= '0' && c <= '9')
			trailer = pdf_read_new_xref(xref, buf);
		else
			fz_throw(ctx, "cannot recognize xref format");
	}
	fz_catch(ctx)
	{
		fz_throw(ctx, "cannot read xref (ofs=%d)", ofs);
	}
	return trailer;
}

static void
pdf_read_xref_sections(pdf_document *xref, int ofs, pdf_lexbuf *buf)
{
	pdf_obj *trailer = NULL;
	pdf_obj *xrefstm = NULL;
	pdf_obj *prev = NULL;
	fz_context *ctx = xref->ctx;

	fz_try(ctx)
	{
		trailer = pdf_read_xref(xref, ofs, buf);

		/* FIXME: do we overwrite free entries properly? */
		xrefstm = pdf_dict_gets(trailer, "XRefStm");
		if (xrefstm)
			pdf_read_xref_sections(xref, pdf_to_int(xrefstm), buf);

		prev = pdf_dict_gets(trailer, "Prev");
		if (prev)
			pdf_read_xref_sections(xref, pdf_to_int(prev), buf);
	}
	fz_catch(ctx)
	{
		pdf_drop_obj(trailer);
		fz_throw(ctx, "cannot read xref at offset %d", ofs);
	}

	pdf_drop_obj(trailer);
}

/*
 * load xref tables from pdf 加载xref表
 */

static void
pdf_load_xref(pdf_document *xref, pdf_lexbuf *buf)
{
	pdf_obj *size;
	int i;
	fz_context *ctx = xref->ctx;

	pdf_load_version(xref);

	pdf_read_start_xref(xref);

	pdf_read_trailer(xref, buf);

	size = pdf_dict_gets(xref->trailer, "Size");
	if (!size)
		fz_throw(ctx, "trailer missing Size entry");

	pdf_resize_xref(xref, pdf_to_int(size));

	pdf_read_xref_sections(xref, xref->startxref, buf);

	/* broken pdfs where first object is not free */
	if (xref->table[0].type != 'f')
		fz_throw(ctx, "first object in xref is not free");

	/* broken pdfs where object offsets are out of range */
	for (i = 0; i < xref->len; i++)
	{
		if (xref->table[i].type == 'n')
		{
			/* Special case code: "0000000000 * n" means free,
			 * according to some producers (inc Quartz) */
			if (xref->table[i].ofs == 0)
				xref->table[i].type = 'f';
			else if (xref->table[i].ofs <= 0 || xref->table[i].ofs >= xref->file_size)
				fz_throw(ctx, "object offset out of range: %d (%d 0 R)", xref->table[i].ofs, i);
		}
		if (xref->table[i].type == 'o')
			if (xref->table[i].ofs <= 0 || xref->table[i].ofs >= xref->len || xref->table[xref->table[i].ofs].type != 'n')
				fz_throw(ctx, "invalid reference to an objstm that does not exist: %d (%d 0 R)", xref->table[i].ofs, i);
	}
}

void
pdf_ocg_set_config(pdf_document *xref, int config)
{
	int i, j, len, len2;
	pdf_ocg_descriptor *desc = xref->ocg;
	pdf_obj *obj, *cobj;
	char *name;

	obj = pdf_dict_gets(pdf_dict_gets(xref->trailer, "Root"), "OCProperties");
	if (!obj)
	{
		if (config == 0)
			return;
		else
			fz_throw(xref->ctx, "Unknown OCG config (None known!)");
	}
	if (config == 0)
	{
		cobj = pdf_dict_gets(obj, "D");
		if (!cobj)
			fz_throw(xref->ctx, "No default OCG config");
	}
	else
	{
		cobj = pdf_array_get(pdf_dict_gets(obj, "Configs"), config);
		if (!cobj)
			fz_throw(xref->ctx, "Illegal OCG config");
	}

	if (desc->intent)
		pdf_drop_obj(desc->intent);
	desc->intent = pdf_dict_gets(cobj, "Intent");
	if (desc->intent)
		pdf_keep_obj(desc->intent);

	len = desc->len;
	name = pdf_to_name(pdf_dict_gets(cobj, "BaseState"));
	if (strcmp(name, "Unchanged") == 0)
	{
		/* Do nothing */
	}
	else if (strcmp(name, "OFF") == 0)
	{
		for (i = 0; i < len; i++)
		{
			desc->ocgs[i].state = 0;
		}
	}
	else /* Default to ON */
	{
		for (i = 0; i < len; i++)
		{
			desc->ocgs[i].state = 1;
		}
	}

	obj = pdf_dict_gets(cobj, "ON");
	len2 = pdf_array_len(obj);
	for (i = 0; i < len2; i++)
	{
		pdf_obj *o = pdf_array_get(obj, i);
		int n = pdf_to_num(o);
		int g = pdf_to_gen(o);
		for (j=0; j < len; j++)
		{
			if (desc->ocgs[j].num == n && desc->ocgs[j].gen == g)
			{
				desc->ocgs[j].state = 1;
				break;
			}
		}
	}

	obj = pdf_dict_gets(cobj, "OFF");
	len2 = pdf_array_len(obj);
	for (i = 0; i < len2; i++)
	{
		pdf_obj *o = pdf_array_get(obj, i);
		int n = pdf_to_num(o);
		int g = pdf_to_gen(o);
		for (j=0; j < len; j++)
		{
			if (desc->ocgs[j].num == n && desc->ocgs[j].gen == g)
			{
				desc->ocgs[j].state = 0;
				break;
			}
		}
	}

	/* FIXME: Should make 'num configs' available in the descriptor. */
	/* FIXME: Should copy out 'Intent' here into the descriptor, and remove
	 * csi->intent in favour of that. */
	/* FIXME: Should copy 'AS' into the descriptor, and visibility
	 * decisions should respect it. */
	/* FIXME: Make 'Order' available via the descriptor (when we have an
	 * app that needs it) */
	/* FIXME: Make 'ListMode' available via the descriptor (when we have
	 * an app that needs it) */
	/* FIXME: Make 'RBGroups' available via the descriptor (when we have
	 * an app that needs it) */
	/* FIXME: Make 'Locked' available via the descriptor (when we have
	 * an app that needs it) */
}

static void
pdf_read_ocg(pdf_document *xref)
{
	pdf_obj *obj, *ocg;
	int len, i;
	pdf_ocg_descriptor *desc;
	fz_context *ctx = xref->ctx;

	fz_var(desc);

	obj = pdf_dict_gets(pdf_dict_gets(xref->trailer, "Root"), "OCProperties");
	if (!obj)
		return;
	ocg = pdf_dict_gets(obj, "OCGs");
	if (!ocg || !pdf_is_array(ocg))
		/* Not ever supposed to happen, but live with it. */
		return;
	len = pdf_array_len(ocg);
	fz_try(ctx)
	{
		desc = fz_calloc(ctx, 1, sizeof(*desc));
		desc->len = len;
		desc->ocgs = fz_calloc(ctx, len, sizeof(*desc->ocgs));
		desc->intent = NULL;
		for (i=0; i < len; i++)
		{
			pdf_obj *o = pdf_array_get(ocg, i);
			desc->ocgs[i].num = pdf_to_num(o);
			desc->ocgs[i].gen = pdf_to_gen(o);
			desc->ocgs[i].state = 0;
		}
		xref->ocg = desc;
	}
	fz_catch(ctx)
	{
		if (desc)
			fz_free(ctx, desc->ocgs);
		fz_free(ctx, desc);
		fz_rethrow(ctx);
	}

	pdf_ocg_set_config(xref, 0);
}

static void
pdf_free_ocg(fz_context *ctx, pdf_ocg_descriptor *desc)
{
	if (!desc)
		return;

	if (desc->intent)
		pdf_drop_obj(desc->intent);
	fz_free(ctx, desc->ocgs);
	fz_free(ctx, desc);
}

/*
 * Initialize and load xref tables.
 * If password is not null, try to decrypt.
 初始化和加载xref表。如果密码不是NULL，请尝试解密
 */

static void pdf_init_document(pdf_document *xref);

pdf_document *
pdf_open_document_with_stream(fz_stream *file)//实际从文件流中读取记录内容
{
	pdf_document *xref;//document结构体
	pdf_obj *encrypt, *id;//加密
	pdf_obj *dict = NULL;//字典
	pdf_obj *obj;
	pdf_obj *nobj = NULL;
	int i, repaired = 0;
	int locked;
	fz_context *ctx = file->ctx;

	fz_var(dict);//异常宏定义 黑盒 不需要搞清楚内容
	fz_var(nobj);//同上
	fz_var(locked);//同上
	//OutputDebugString(L"================================================进入pdf_open_document_with_stream函数");
	xref = fz_malloc_struct(ctx, pdf_document);//给结构体分配空间
	//OutputDebugString(L"================================================分配pdf_document空间成功");
	pdf_init_document(xref);
	//OutputDebugString(L"================================================初始化pdf_document成功");
	xref->lexbuf.base.size = PDF_LEXBUF_LARGE;

	xref->file = fz_keep_stream(file);
	xref->ctx = ctx;

	fz_lock(ctx, FZ_LOCK_FILE);
	locked = 1;

	fz_try(ctx)//将文件名和行号添加到错误和警告中
	{
		pdf_load_xref(xref, &xref->lexbuf.base);//加载xref表
		//OutputDebugString(L"================================================加载xref表成功");
	}
	fz_catch(ctx)
	{
		if (xref->table)
		{
			fz_free(xref->ctx, xref->table);
			xref->table = NULL;
			xref->len = 0;
		}
		if (xref->trailer)
		{
			pdf_drop_obj(xref->trailer);
			xref->trailer = NULL;
		}
		fz_warn(xref->ctx, "trying to repair broken xref");
		repaired = 1;
	}

	fz_try(ctx)
	{
		int hasroot, hasinfo;

		if (repaired)
			pdf_repair_xref(xref, &xref->lexbuf.base);

		fz_unlock(ctx, FZ_LOCK_FILE);
		locked = 0;

		encrypt = pdf_dict_gets(xref->trailer, "Encrypt");
		id = pdf_dict_gets(xref->trailer, "ID");
		if (pdf_is_dict(encrypt))
			xref->crypt = pdf_new_crypt(ctx, encrypt, id);

		/* Allow lazy clients to read encrypted files with a blank password 允许懒惰客户端读取带有空白密码的加密文件。*/
		pdf_authenticate_password(xref, "");

		if (repaired)
		{
			pdf_repair_obj_stms(xref);

			hasroot = (pdf_dict_gets(xref->trailer, "Root") != NULL);
			hasinfo = (pdf_dict_gets(xref->trailer, "Info") != NULL);

			for (i = 1; i < xref->len; i++)//加载对象
			{
				if (xref->table[i].type == 0 || xref->table[i].type == 'f')
					continue;

				fz_try(ctx)
				{
					dict = pdf_load_object(xref, i, 0);
				}
				fz_catch(ctx)
				{
					fz_warn(ctx, "ignoring broken object (%d 0 R)", i);
					continue;
				}

				if (!hasroot)
				{
					obj = pdf_dict_gets(dict, "Type");
					if (pdf_is_name(obj) && !strcmp(pdf_to_name(obj), "Catalog"))
					{
						nobj = pdf_new_indirect(ctx, i, 0, xref);
						pdf_dict_puts(xref->trailer, "Root", nobj);
						pdf_drop_obj(nobj);
						nobj = NULL;
					}
				}

				if (!hasinfo)
				{
					if (pdf_dict_gets(dict, "Creator") || pdf_dict_gets(dict, "Producer"))
					{
						nobj = pdf_new_indirect(ctx, i, 0, xref);
						pdf_dict_puts(xref->trailer, "Info", nobj);
						pdf_drop_obj(nobj);
						nobj = NULL;
					}
				}

				pdf_drop_obj(dict);
				dict = NULL;
			}
		}
	}
	fz_always(ctx)
	{
		if (locked)
			fz_unlock(ctx, FZ_LOCK_FILE);
	}
	fz_catch(ctx)
	{
		pdf_drop_obj(dict);
		pdf_drop_obj(nobj);
		pdf_close_document(xref);
		fz_throw(ctx, "cannot open document");
	}

	fz_try(ctx)
	{
		pdf_read_ocg(xref);
	}
	fz_catch(ctx)
	{
		fz_warn(ctx, "Ignoring Broken Optional Content");
	}

	return xref;
}

void
pdf_close_document(pdf_document *xref)
{
	int i;
	fz_context *ctx;

	if (!xref)
		return;
	ctx = xref->ctx;

	if (xref->table)
	{
		for (i = 0; i < xref->len; i++)
		{
			if (xref->table[i].obj)
			{
				pdf_drop_obj(xref->table[i].obj);
				xref->table[i].obj = NULL;
			}
		}
		fz_free(xref->ctx, xref->table);
	}

	if (xref->page_objs)
	{
		for (i = 0; i < xref->page_len; i++)
			pdf_drop_obj(xref->page_objs[i]);
		fz_free(ctx, xref->page_objs);
	}

	if (xref->page_refs)
	{
		for (i = 0; i < xref->page_len; i++)
			pdf_drop_obj(xref->page_refs[i]);
		fz_free(ctx, xref->page_refs);
	}

	if (xref->file)
		fz_close(xref->file);
	if (xref->trailer)
		pdf_drop_obj(xref->trailer);
	if (xref->crypt)
		pdf_free_crypt(ctx, xref->crypt);

	pdf_free_ocg(ctx, xref->ocg);

	fz_empty_store(ctx);

	fz_free(ctx, xref);
}

void
pdf_print_xref(pdf_document *xref)
{
	int i;
	printf("xref\n0 %d\n", xref->len);
	for (i = 0; i < xref->len; i++)
	{
		printf("%05d: %010d %05d %c (stm_ofs=%d)\n", i,
			xref->table[i].ofs,
			xref->table[i].gen,
			xref->table[i].type ? xref->table[i].type : '-',
			xref->table[i].stm_ofs);
	}
}

/*
 * compressed object streams
 */

static void
pdf_load_obj_stm(pdf_document *xref, int num, int gen, pdf_lexbuf *buf)
{
	fz_stream *stm = NULL;
	pdf_obj *objstm = NULL;
	int *numbuf = NULL;
	int *ofsbuf = NULL;

	pdf_obj *obj;
	int first;
	int count;
	int i;
	int tok;
	fz_context *ctx = xref->ctx;

	fz_var(numbuf);
	fz_var(ofsbuf);
	fz_var(objstm);
	fz_var(stm);

	fz_try(ctx)
	{
		objstm = pdf_load_object(xref, num, gen);

		count = pdf_to_int(pdf_dict_gets(objstm, "N"));
		first = pdf_to_int(pdf_dict_gets(objstm, "First"));

		numbuf = fz_calloc(ctx, count, sizeof(int));
		ofsbuf = fz_calloc(ctx, count, sizeof(int));

		stm = pdf_open_stream(xref, num, gen);
		for (i = 0; i < count; i++)
		{
			tok = pdf_lex(stm, buf);
			if (tok != PDF_TOK_INT)
				fz_throw(ctx, "corrupt object stream (%d %d R)", num, gen);
			numbuf[i] = buf->i;

			tok = pdf_lex(stm, buf);
			if (tok != PDF_TOK_INT)
				fz_throw(ctx, "corrupt object stream (%d %d R)", num, gen);
			ofsbuf[i] = buf->i;
		}

		fz_seek(stm, first, 0);

		for (i = 0; i < count; i++)
		{
			fz_seek(stm, first + ofsbuf[i], 0);

			obj = pdf_parse_stm_obj(xref, stm, buf);
			/* RJW: Ensure above does fz_throw(ctx, "cannot parse object %d in stream (%d %d R)", i, num, gen); */

			if (numbuf[i] < 1 || numbuf[i] >= xref->len)
			{
				pdf_drop_obj(obj);
				fz_throw(ctx, "object id (%d 0 R) out of range (0..%d)", numbuf[i], xref->len - 1);
			}

			if (xref->table[numbuf[i]].type == 'o' && xref->table[numbuf[i]].ofs == num)
			{
				if (xref->table[numbuf[i]].obj)
					pdf_drop_obj(xref->table[numbuf[i]].obj);
				xref->table[numbuf[i]].obj = obj;
			}
			else
			{
				pdf_drop_obj(obj);
			}
		}
	}
	fz_always(ctx)
	{
		fz_close(stm);
		fz_free(xref->ctx, ofsbuf);
		fz_free(xref->ctx, numbuf);
		pdf_drop_obj(objstm);
	}
	fz_catch(ctx)
	{
		fz_throw(ctx, "cannot open object stream (%d %d R)", num, gen);
	}
}

/*
 * object loading
 */

void
pdf_cache_object(pdf_document *xref, int num, int gen)
{
	pdf_xref_entry *x;
	int rnum, rgen;
	fz_context *ctx = xref->ctx;

	if (num < 0 || num >= xref->len)
		fz_throw(ctx, "object out of range (%d %d R); xref size %d", num, gen, xref->len);

	x = &xref->table[num];

	if (x->obj)
		return;

	if (x->type == 'f')
	{
		x->obj = pdf_new_null(ctx);
		return;
	}
	else if (x->type == 'n')
	{
		fz_lock(ctx, FZ_LOCK_FILE);
		fz_seek(xref->file, x->ofs, 0);

		fz_try(ctx)
		{
			x->obj = pdf_parse_ind_obj(xref, xref->file, &xref->lexbuf.base,
					&rnum, &rgen, &x->stm_ofs);
		}
		fz_catch(ctx)
		{
			fz_unlock(ctx, FZ_LOCK_FILE);
			fz_throw(ctx, "cannot parse object (%d %d R)", num, gen);
		}

		if (rnum != num)
		{
			pdf_drop_obj(x->obj);
			x->obj = NULL;
			fz_unlock(ctx, FZ_LOCK_FILE);
			fz_throw(ctx, "found object (%d %d R) instead of (%d %d R)", rnum, rgen, num, gen);
		}

		if (xref->crypt)
			pdf_crypt_obj(ctx, xref->crypt, x->obj, num, gen);
		fz_unlock(ctx, FZ_LOCK_FILE);
	}
	else if (x->type == 'o')
	{
		if (!x->obj)
		{
			fz_try(ctx)
			{
				pdf_load_obj_stm(xref, x->ofs, 0, &xref->lexbuf.base);
			}
			fz_catch(ctx)
			{
				fz_throw(ctx, "cannot load object stream containing object (%d %d R)", num, gen);
			}
			if (!x->obj)
				fz_throw(ctx, "object (%d %d R) was not found in its object stream", num, gen);
		}
	}
	else
	{
		fz_throw(ctx, "assert: corrupt xref struct");
	}
}

pdf_obj *
pdf_load_object(pdf_document *xref, int num, int gen)
{
	fz_context *ctx = xref->ctx;

	fz_try(ctx)
	{
		pdf_cache_object(xref, num, gen);
	}
	fz_catch(ctx)
	{
		fz_throw(ctx, "cannot load object (%d %d R) into cache", num, gen);
	}

	assert(xref->table[num].obj);

	return pdf_keep_obj(xref->table[num].obj);
}

pdf_obj *
pdf_resolve_indirect(pdf_obj *ref)
{
	int sanity = 10;
	int num;
	int gen;
	fz_context *ctx = NULL; /* Avoid warning for stupid compilers */
	pdf_document *xref;

	while (pdf_is_indirect(ref))
	{
		if (--sanity == 0)
		{
			fz_warn(ctx, "Too many indirections (possible indirection cycle involving %d %d R)", num, gen);
			return NULL;
		}
		xref = pdf_get_indirect_document(ref);
		if (!xref)
			return NULL;
		ctx = xref->ctx;
		num = pdf_to_num(ref);
		gen = pdf_to_gen(ref);
		fz_try(ctx)
		{
			pdf_cache_object(xref, num, gen);
		}
		fz_catch(ctx)
		{
			fz_warn(ctx, "cannot load object (%d %d R) into cache", num, gen);
			return NULL;
		}
		if (!xref->table[num].obj)
			return NULL;
		ref = xref->table[num].obj;
	}

	return ref;
}

int pdf_count_objects(pdf_document *doc)
{
	return doc->len;
}

/* Replace numbered object -- for use by pdfclean and similar tools */
void
pdf_update_object(pdf_document *xref, int num, int gen, pdf_obj *newobj)
{
	pdf_xref_entry *x;

	if (num < 0 || num >= xref->len)
	{
		fz_warn(xref->ctx, "object out of range (%d %d R); xref size %d", num, gen, xref->len);
		return;
	}

	x = &xref->table[num];

	if (x->obj)
		pdf_drop_obj(x->obj);

	x->obj = pdf_keep_obj(newobj);
	x->type = 'n';
	x->ofs = 0;
}

/*
 * Convenience function to open a file then call pdf_open_document_with_stream.
 */

pdf_document *
pdf_open_document(fz_context *ctx, const char *filename)
{
	fz_stream *file = NULL;
	pdf_document *xref;
	//OutputDebugString(L"================================================调用pdf_opendocument函数");
	//system("pause");
	fz_var(file);
	fz_try(ctx)
	{
		file = fz_open_file(ctx, filename);//打开文件
		//OutputDebugString(L"================================================打开文件成功");
		xref = pdf_open_document_with_stream(file);//读取流式PDF文件并提取出其中信息放入pdf_document类的结构体中
	}
	fz_catch(ctx)
	{
		fz_close(file);
		fz_throw(ctx, "cannot load document '%s'", filename);
	}
	//OutputDebugString(L"================================================执行成功");
	fz_close(file);
	//OutputDebugString(L"================================================关闭文件流成功");
	return xref;
}

/* Document interface wrappers */

static void pdf_close_document_shim(fz_document *doc)
{
	pdf_close_document((pdf_document*)doc);
}

static int pdf_needs_password_shim(fz_document *doc)
{
	return pdf_needs_password((pdf_document*)doc);
}

static int pdf_authenticate_password_shim(fz_document *doc, char *password)
{
	return pdf_authenticate_password((pdf_document*)doc, password);
}

static fz_outline *pdf_load_outline_shim(fz_document *doc)
{
	return pdf_load_outline((pdf_document*)doc);
}

static int pdf_count_pages_shim(fz_document *doc)
{
	return pdf_count_pages((pdf_document*)doc);
}

static fz_page *pdf_load_page_shim(fz_document *doc, int number)
{
	return (fz_page*) pdf_load_page((pdf_document*)doc, number);
}

static fz_link *pdf_load_links_shim(fz_document *doc, fz_page *page)
{
	return pdf_load_links((pdf_document*)doc, (pdf_page*)page);
}

static fz_rect pdf_bound_page_shim(fz_document *doc, fz_page *page)
{
	return pdf_bound_page((pdf_document*)doc, (pdf_page*)page);
}

static void pdf_run_page_shim(fz_document *doc, fz_page *page, fz_device *dev, fz_matrix transform, fz_cookie *cookie)
{
	pdf_run_page((pdf_document*)doc, (pdf_page*)page, dev, transform, cookie);
}

static void pdf_free_page_shim(fz_document *doc, fz_page *page)
{
	pdf_free_page((pdf_document*)doc, (pdf_page*)page);
}

static void
pdf_init_document(pdf_document *doc)
{
	doc->super.close = pdf_close_document_shim;
	doc->super.needs_password = pdf_needs_password_shim;
	doc->super.authenticate_password = pdf_authenticate_password_shim;
	doc->super.load_outline = pdf_load_outline_shim;
	doc->super.count_pages = pdf_count_pages_shim;
	doc->super.load_page = pdf_load_page_shim;
	doc->super.load_links = pdf_load_links_shim;
	doc->super.bound_page = pdf_bound_page_shim;
	doc->super.run_page = pdf_run_page_shim;
	doc->super.free_page = pdf_free_page_shim;
}
