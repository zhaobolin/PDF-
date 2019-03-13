#ifndef FITZ_H
#define FITZ_H

/*
	Include the standard libc headers.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>	/* INT_MAX & co */
#include <float.h> /* FLT_EPSILON, FLT_MAX & co */
#include <fcntl.h> /* O_RDONLY & co */

#include <setjmp.h>

/* SumatraPDF: memento's license header can be read as being non-GPLv3 */
#ifndef MEMENTO
#define Memento_label(ptr, label) (ptr)
#else
#include "memento.h"
#endif

/*
	Some versions of setjmp/longjmp (notably MacOSX and ios) store/restore
	signal handlers too. We don't alter signal handlers within mupdf, so
	there is no need for us to store/restore - hence we use the
	non-restoring variants. This makes a large speed difference.
*/
#ifdef __APPLE__
#define fz_setjmp _setjmp
#define fz_longjmp _longjmp
#else
#define fz_setjmp setjmp
#define fz_longjmp longjmp
#endif

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "libmupdf"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)
#else
#define LOGI(...) do {} while(0)
#define LOGE(...) do {} while(0)
#endif

#define nelem(x) (sizeof(x)/sizeof((x)[0]))

#define ABS(x) ( (x) < 0 ? -(x) : (x) )
#define MIN(a,b) ( (a) < (b) ? (a) : (b) )
#define MAX(a,b) ( (a) > (b) ? (a) : (b) )
#define CLAMP(x,a,b) ( (x) > (b) ? (b) : ( (x) < (a) ? (a) : (x) ) )
#define DIV_BY_ZERO(a, b, min, max) (((a) < 0) ^ ((b) < 0) ? (min) : (max))

/*
	Some differences in libc can be smoothed over
*/

#ifdef _MSC_VER /* Microsoft Visual C */

#pragma warning( disable: 4244 ) /* conversion from X to Y, possible loss of data */
#pragma warning( disable: 4996 ) /* The POSIX name for this item is deprecated */
#pragma warning( disable: 4996 ) /* This function or variable may be unsafe */

#include <io.h>

int gettimeofday(struct timeval *tv, struct timezone *tz);

#define snprintf _snprintf
#define isnan _isnan
#define hypotf _hypotf

#else /* Unix or close enough */

#include <unistd.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef M_SQRT2
#define M_SQRT2 1.41421356237309504880
#endif

/*
	Variadic macros, inline and restrict keywords
*/

#if __STDC_VERSION__ == 199901L /* C99 */
#elif _MSC_VER >= 1500 /* MSVC 9 or newer */
#define inline __inline
#define restrict __restrict
#elif __GNUC__ >= 3 /* GCC 3 or newer */
#define inline __inline
#define restrict __restrict
#else /* Unknown or ancient */
#define inline
#define restrict
#endif

/*
	GCC can do type checking of printf strings
*/

#ifndef __printflike
#if __GNUC__ > 2 || __GNUC__ == 2 && __GNUC_MINOR__ >= 7
#define __printflike(fmtarg, firstvararg) \
	__attribute__((__format__ (__printf__, fmtarg, firstvararg)))
#else
#define __printflike(fmtarg, firstvararg)
#endif
#endif

/*
	Contexts
*/
typedef struct fz_alloc_context_s fz_alloc_context;
typedef struct fz_error_context_s fz_error_context;
typedef struct fz_warn_context_s fz_warn_context;
typedef struct fz_font_context_s fz_font_context;
typedef struct fz_aa_context_s fz_aa_context;
typedef struct fz_locks_context_s fz_locks_context;
typedef struct fz_store_s fz_store;
typedef struct fz_glyph_cache_s fz_glyph_cache;
typedef struct fz_context_s fz_context;


struct zblroute//ͼ��״̬ջ�е�һ��·��������������·�����ü���·������ת�þ����
{
	int type;//·������ ֱ��-1 ����������-2 ����-3 ����·������-4

	float linewidth;//��¼�߿�
	int colorspace;//��¼����һ����ɫ�ռ�(����չ) 1-G 2-g 3-RG 4-rg 5-cmyk(k) 6-cmyk(K)
	float color[4];//��¼��ɫ
	int scolorspace;
	float *scolor;//�������,��������ɫ�������ɫ

	int drawingmethord;//���Ʒ��� 1-S 2-f* 3-f/F 4-s 5-B 6-B* 7-b 8-b*
	
	int countpoint;//��¼·�������
	struct routepoint* pointheadler;//p0 p1�ֱ���ֱ��·�����x,y����,����������p0-p6�����,state����·����m,l,h,c״̬(�ֱ��Ӧ0��1��2��3��
	struct routepoint* currentpoint;//�����ĵ�ǰָ��

	struct zblroute* nextroute;//�¸�·��
};

struct zblstack//һ��ͼ��״̬ջ
{
	int existstack;//�Ƿ���ͼ��״̬ջ
	int countroute;//��¼һ��ͼ��״̬ջ���ж��ٸ�·��
	int existnest;//��־λ �Ƿ����Ƕ��ջ ��������ȡʱ�����ı�־λ

	int countclip;//��¼�ü�·������	
	int existclip;//�жϱ�־������ṹ�Ƿ���ڲü�·��,�Լ�������,���ڵ������� 0-������	1-W 2-W*
	struct  routepoint* clipheadler;//��Ųü�·����ָ��,��pointsһ���Ľṹ	
	struct  routepoint* currentclip;//��ǰ�ü�·����ָ��	

	int shadowtype;//�Ƿ������Ӱ(Ϊ����ȡ����ɫ)0-������ ֮��ÿ�����ֶ��и��Զ�Ӧ������
	float *bcolor;//��¼��ʼ��ɫ
	float *ecolor;//��¼������ɫ
	float ca;//͸����,Ĭ��Ϊ1

	int existcm;//�Ƿ����ת�þ���,Ĭ��Ϊ0-������ 1-����
	float* matrix;//���ת�þ���,Ĭ��ΪNULL

	struct zblroute* routeheadler;//·��ͷ
	struct zblroute* currentroute;//��ǰ·��

	struct zblstack* stackheadler;//ջ��Ƕ��ջ����ͷ  �������ʼ��NULL
	struct zblstack* currentstack;//��ǰǶ��ջ

	struct zblstack* nextstack;//�¸�ջ
};

struct zstacknode
{
	struct zblstack* pointer;
	struct zstacknode* next;
};
struct spointstack
{
	struct zstacknode* head;
	struct zstacknode* tail;
};


struct zblrouteset//��ȡ·�����Ͻṹ�� һҳ�е�����ͼ����Ϣ
{
	int count;
	struct zblstack* stackheadler;
	struct zblstack* currentstack;
};

struct routepoint
{
	struct routepoint* nextpoint;
	float p0;
	float p1;
	float p2;
	float p3;
	float p4;
	float p5;
	float state;//state����·����m,l,h,c״̬(�ֱ��Ӧ0��1��2��3��//�˴�������v y������
};


/*
extern
void copyclippoint(struct zblroute* route)
{
	route->currentclippoint->p0=route->currentpoint->p0;
	route->currentclippoint->p1=route->currentpoint->p1;
	route->currentclippoint->p2=route->currentpoint->p2;
	route->currentclippoint->p3=route->currentpoint->p3;
	route->currentclippoint->p4=route->currentpoint->p4;
	route->currentclippoint->p5=route->currentpoint->p5;
	route->currentclippoint->state=route->currentpoint->state;
}
extern
void addclippoints(struct zblroute* route)//Ϊ�ü�·�����ӿյ㼯
{
	struct routepoint* points=(struct routepoint*)malloc(sizeof(struct routepoint));
	points->nextpoint=NULL;
	points->p0=-1;
	points->p1=-1;
	points->p2=-1;
	points->p3=-1;
	points->p4=-1;
	points->p5=-1;
	points->state=-1;
	route->currentclippoint->nextpoint=points;
	route->currentclippoint=route->currentclippoint->nextpoint;//currentclippointָ���µ�
}
void addpoints(struct zblroute* route)//Ϊ·������һ���յ� ��Ϊcurrentָ�븳ֵ
{
	struct routepoint* points=(struct routepoint*)malloc(sizeof(struct routepoint));
	points->nextpoint=NULL;
	points->p0=-1;
	points->p1=-1;
	points->p2=-1;
	points->p3=-1;
	points->p4=-1;
	points->p5=-1;
	points->state=-1;
	route->currentpoint->nextpoint=points;//���ӵ�
	route->currentpoint=route->currentpoint->nextpoint;//currentָ���µ�
}

extern
void addroute(struct zblrouteset* getline)//Ϊrouteset����һ��route
{
	//��ʼ��һ��route
	struct zblroute* route=(struct zblroute*)malloc(sizeof(struct zblroute));
	//route=(zblroute*)malloc(sizeof(struct zblroute));
	route->countpoint=0;
	route->colorspace=-1;
	route->color[0]=-1;
	route->color[1]=-1;
	route->color[2]=-1;
	route->color[3]=-1;
	route->linewidth=-1;
	route->drawingmethord=-1;
	route->points=(struct routepoint*)malloc(sizeof(struct routepoint));//�㼯������ͷ���
	route->currentpoint=route->points;
	route->currentpoint->nextpoint=NULL;

	route->countclip=0;//�ü�·������Ϊ��
	route->existclip=0;//Ĭ�ϲ����ڲü�·��
	route->clipregion=NULL;//��ʼ���ü�·��Ϊ��
	route->currentclippoint=NULL;//��ʼ����ǰ�ü�·����ָ��Ϊ��
	
	addpoints(route);//����һ���յĵ㼯
	route->currentpoint=route->points;
	route->existcm=0;
	route->matrix=NULL;
	route->type=0;
	route->next=NULL;//��һ��routeΪ��
	getline->current->next=route;
	getline->current=getline->current->next;
}

*/

struct fz_alloc_context_s
{
	void *user;
	void *(*malloc)(void *, unsigned int);//void* ���Ϳ���ǿ��ת��Ϊ�κ��������͵�ָ��
	void *(*realloc)(void *, void *, unsigned int);
	void (*free)(void *, void *);
};

struct fz_error_context_s
{
	int top;
	struct {
		int code;
		jmp_buf buffer;
	/* SumatraPDF: stack overflows happen in pdf_load_page_tree_node and pdf_read_xref_sections ջ�����pdf_load_page_tree_node �� pdf_read_xref_sections�������ط�*/
	} stack[1024];
	char message[256];
};


void fz_var_imp(void *);
#define fz_var(var) fz_var_imp((void *)&(var))

/*
	Exception macro definitions. Just treat these as a black box - pay no
	attention to the man behind the curtain.
	�쳣�궨�塣����Щ����һ���ں��ӣ���ע�ⴰ����������ˡ� 
*/

#define fz_try(ctx) \
	if (fz_push_try(ctx->error), \
		(ctx->error->stack[ctx->error->top].code = fz_setjmp(ctx->error->stack[ctx->error->top].buffer)) == 0) \
	{ do {

#define fz_always(ctx) \
		} while (0); \
	} \
	{ do { \

#define fz_catch(ctx) \
		} while(0); \
	} \
	if (ctx->error->stack[ctx->error->top--].code)

void fz_push_try(fz_error_context *ex);
/* SumatraPDF: add filename and line number to errors and warnings ���ļ������к����ӵ�����;����� */
#define fz_throw(CTX, MSG, ...) fz_throw_imp(CTX, __FILE__, __LINE__, MSG, __VA_ARGS__)
void fz_throw_imp(fz_context *ctx, char *file, int line, char *fmt, ...) __printflike(4, 5);
void fz_rethrow(fz_context *);
/* SumatraPDF: add filename and line number to errors and warnings ���ļ������к����ӵ�����;����� */
#define fz_warn(CTX, MSG, ...) fz_warn_imp(CTX, __FILE__, __LINE__, MSG, __VA_ARGS__)
void fz_warn_imp(fz_context *ctx, char *file, int line, char *fmt, ...) __printflike(4, 5);

/*
	fz_flush_warnings: Flush any repeated warnings.��ϴ�κ��ظ�����

	Repeated warnings are buffered, counted and eventually printed
	along with the number of repetitions. Call fz_flush_warnings
	to force printing of the latest buffered warning and the
	number of repetitions, for example to make sure that all
	warnings are printed before exiting an application.
	�ظ��ľ��汻���桢���㲢�����ظ��������������մ�ӡ����������fz_flush_warnings��ǿ�ƴ�ӡ�������ľ������ظ�������ȷ�����˳�Ӧ�ó���֮ǰ��ӡ���о��档
	Does not throw exceptions.
*/
void fz_flush_warnings(fz_context *ctx);

struct fz_context_s
{
	fz_alloc_context *alloc;
	fz_locks_context *locks;
	fz_error_context *error;
	fz_warn_context *warn;
	fz_font_context *font;
	fz_aa_context *aa;
	fz_store *store;
	fz_glyph_cache *glyph_cache;
};

/*
	Specifies the maximum size in bytes of the resource store in
	fz_context. Given as argument to fz_new_context.
	ָ����Դ�洢���������������Ϊfz_new_context�Ĳο�

	FZ_STORE_UNLIMITED: Let resource store grow unbounded.
	ʹ��Դ�洢�����޴�

	FZ_STORE_DEFAULT: A reasonable upper bound on the size, for
	devices that are not memory constrained.
	Ϊ�ڴ治�����Ƶ��豸�ṩһ����������Դ��������
*/
enum {
	FZ_STORE_UNLIMITED = 0,
	FZ_STORE_DEFAULT = 256 << 20,
};

/*
	fz_new_context: Allocate context containing global state.
	�������ȫ��״̬��������

	The global state contains an exception stack, resource store,
	etc. Most functions in MuPDF take a context argument to be
	able to reference the global state. See fz_free_context for
	freeing an allocated context.
	ȫ��״̬������ջ�쳣����Դ�洢�����ݣ������MUPDF�еĺ���ʹ�������Ĳ���������ȫ��״̬

	alloc: Supply a custom memory allocator through a set of
	function pointers. Set to NULL for the standard library
	allocator. The context will keep the allocator pointer, so the
	data it points to must not be modified or freed during the
	lifetime of the context.
	ͨ��һ�麯��ָ���ṩһ���Զ�����ڴ��������Ϊ��׼�����������Ϊnull��
	�����Ľ�����������ָ�룬�����ָ������ݲ����������ĵ����������ڱ��޸Ļ��ͷš�
	�����ڴ棬����ָ�룬��ָ�뱣������ݽ����ᶪʧ
	locks: Supply a set of locks and functions to lock/unlock
	them, intended for multi-threaded applications. Set to NULL
	when using MuPDF in a single-threaded applications. The
	context will keep the locks pointer, so the data it points to
	must not be modified or freed during the lifetime of the
	context.
	���̣߳�����ȥ������
	max_store: Maximum size in bytes of the resource store, before
	it will start evicting cached resources such as fonts and
	images. FZ_STORE_UNLIMITED can be used if a hard limit is not
	desired. Use FZ_STORE_DEFAULT to get a reasonable size.
	��Դ�洢����������
	Does not throw exceptions, but may return NULL.
*/
fz_context *fz_new_context(fz_alloc_context *alloc, fz_locks_context *locks, unsigned int max_store);

/*
	fz_clone_context: Make a clone of an existing context.
	��¡���ڵ�������
	This function is meant to be used in multi-threaded
	applications where each thread requires its own context, yet
	parts of the global state, for example caching, is shared.
	Ϊ���߳�׼��
	ctx: Context obtained from fz_new_context to make a copy of.
	ctx must have had locks and lock/functions setup when created.
	The two contexts will share the memory allocator, resource
	store, locks and lock/unlock functions. They will each have
	their own exception stacks though.

	Does not throw exception, but may return NULL.
*/
fz_context *fz_clone_context(fz_context *ctx);//�������������ݣ�Ӧ�Զ��߳�

/*
	fz_free_context: Free a context and its global state.
	�ͷ�һ�������ĺ���ȫ��״̬
	The context and all of its global state is freed, and any
	buffered warnings are flushed (see fz_flush_warnings). If NULL
	is passed in nothing will happen.

	Does not throw exceptions.
*/
void fz_free_context(fz_context *ctx);

/*
	fz_aa_level: Get the number of bits of antialiasing we are
	using. Between 0 and 8.
	��ȡ�����ʹ�õı�������ȡֵ��Χ0~8
*/
int fz_aa_level(fz_context *ctx);

/*
	fz_set_aa_level: Set the number of bits of antialiasing we should use.

	bits: The number of bits of antialiasing to use (values are clamped
	to within the 0 to 8 range).
*/
void fz_set_aa_level(fz_context *ctx, int bits);

/*
	Locking functions

	MuPDF is kept deliberately free of any knowledge of particular
	threading systems. As such, in order for safe multi-threaded
	operation, we rely on callbacks to client provided functions.
	mupdf�ǿ��Ᵽ�����κ�֪ʶ���ض��̵߳�ϵͳ��Ϊ�˰�ȫ�Ķ��̲߳����������ص��ͻ����ṩ�Ĺ���

	A client is expected to provide FZ_LOCK_MAX number of mutexes,
	and a function to lock/unlock each of them. These may be
	recursive mutexes, but do not have to be.

	If a client does not intend to use multiple threads, then it
	may pass NULL instead of a lock structure.

	In order to avoid deadlocks, we have one simple rule
	internally as to how we use locks: We can never take lock n
	when we already hold any lock i, where 0 <= i <= n. In order
	to verify this, we have some debugging code, that can be
	enabled by defining FITZ_DEBUG_LOCKING.
	Ԥ������
*/

struct fz_locks_context_s
{
	void *user;
	void (*lock)(void *user, int lock);
	void (*unlock)(void *user, int lock);
};

enum {
	FZ_LOCK_ALLOC = 0,
	FZ_LOCK_FILE,
	FZ_LOCK_FREETYPE,
	FZ_LOCK_GLYPHCACHE,
	FZ_LOCK_MAX
};

/*
	Memory Allocation and Scavenging:
	�ڴ���������
	All calls to MuPDFs allocator functions pass through to the
	underlying allocators passed in when the initial context is
	created, after locks are taken (using the supplied locking function)
	to ensure that only one thread at a time calls through.

	If the underlying allocator fails, MuPDF attempts to make room for
	the allocation by evicting elements from the store, then retrying.

	Any call to allocate may then result in several calls to the underlying
	allocator, and result in elements that are only referred to by the
	store being freed.
*/

/*
	fz_malloc: Allocate a block of memory (with scavenging)
	����һ���ڴ�
	size: The number of bytes to allocate.

	Returns a pointer to the allocated block. May return NULL if size is
	0. Throws exception on failure to allocate.
*/
void *fz_malloc(fz_context *ctx, unsigned int size);

/*
	fz_calloc: Allocate a zeroed block of memory (with scavenging)
	����һ���ڴ�
	count: The number of objects to allocate space for.
	count��Ҫ��ŵĶ��������
	size: The size (in bytes) of each object.
	size��ÿ������Ĵ�С
	Returns a pointer to the allocated block. May return NULL if size
	and/or count are 0. Throws exception on failure to allocate.
*/
void *fz_calloc(fz_context *ctx, unsigned int count, unsigned int size);

/*
	fz_malloc_array: Allocate a block of (non zeroedδ�������) memory (with
	scavenging). Equivalent to fz_calloc without the memory clearing.
	�൱��fz_callocû���ڴ�����
	count: The number of objects to allocate space for.

	size: The size (in bytes) of each object.

	Returns a pointer to the allocated block. May return NULL if size
	and/or count are 0. Throws exception on failure to allocate.
*/
void *fz_malloc_array(fz_context *ctx, unsigned int count, unsigned int size);

/*
	fz_resize_array: Resize a block of memory (with scavenging).
	���������ڴ�Ĵ�С
	p: The existing block to resize
	��Ҫ������С�����п�
	count: The number of objects to resize to.
	Ҫ������С�Ķ��������
	size: The size (in bytes) of each object.

	Returns a pointer to the resized block. May return NULL if size
	and/or count are 0. Throws exception on failure to resize (original
	block is left unchanged).
*/
void *fz_resize_array(fz_context *ctx, void *p, unsigned int count, unsigned int size);

/*
	fz_strdup: Duplicate a C string (with scavenging)
	����һ��C�ַ���
	s: The string to duplicate.

	Returns a pointer to a duplicated string. Throws exception on failure
	to allocate.
*/
char *fz_strdup(fz_context *ctx, char *s);

/*
	fz_free: Frees an allocation.
	//�ͷ�һ���ڴ�
	Does not throw exceptions.
*/
void fz_free(fz_context *ctx, void *p);

/*
	fz_malloc_no_throw: Allocate a block of memory (with scavenging)

	size: The number of bytes to allocate.
	�ͷ�һ�����ڴ�
	Returns a pointer to the allocated block. May return NULL if size is
	0. Returns NULL on failure to allocate.
*/
void *fz_malloc_no_throw(fz_context *ctx, unsigned int size);

/*
	fz_calloc_no_throw: Allocate a zeroed block of memory (with scavenging)
	�ͷ�һ����������ڴ�
	count: The number of objects to allocate space for.

	size: The size (in bytes) of each object.

	Returns a pointer to the allocated block. May return NULL if size
	and/or count are 0. Returns NULL on failure to allocate.
*/
void *fz_calloc_no_throw(fz_context *ctx, unsigned int count, unsigned int size);

/*
	fz_malloc_array_no_throw: Allocate a block of (non zeroed) memory
	(with scavenging). Equivalent to fz_calloc_no_throw without the
	memory clearing.
	�ͷ�һ�����ڴ棬���������ⲿ������
	count: The number of objects to allocate space for.

	size: The size (in bytes) of each object.

	Returns a pointer to the allocated block. May return NULL if size
	and/or count are 0. Returns NULL on failure to allocate.
*/
void *fz_malloc_array_no_throw(fz_context *ctx, unsigned int count, unsigned int size);

/*
	fz_resize_array_no_throw: Resize a block of memory (with scavenging).
	���·����ڴ��С
	p: The existing block to resize

	count: The number of objects to resize to.

	size: The size (in bytes) of each object.

	Returns a pointer to the resized block. May return NULL if size
	and/or count are 0. Returns NULL on failure to resize (original
	block is left unchanged).
*/
void *fz_resize_array_no_throw(fz_context *ctx, void *p, unsigned int count, unsigned int size);

/*
	fz_strdup_no_throw: Duplicate a C string (with scavenging)

	s: The string to duplicate.

	Returns a pointer to a duplicated string. Returns NULL on failure
	to allocate.
*/
char *fz_strdup_no_throw(fz_context *ctx, char *s);

/*
	Safe string functions ��ȫ���ַ�������
*/
/*
	fz_strsep: Given a pointer to a C string (or a pointer to NULL) break
	it at the first occurence of a delimiter char (from a given set).
	��Stringһ��ָ���ڶ�����ַ��ĵ�һ�γ���ʱ�ж���
	stringp: Pointer to a C string pointer (or NULL). Updated on exit to
	point to the first char of the string after the delimiter that was
	found. The string pointed to by stringp will be corrupted by this
	call (as the found delimiter will be overwritten by 0).
	c�ַ���ָ���ָ�룬���˳�ʱ���£������ҵ��ָ���֮��ָ���ַ����ĵ�һ���ַ����ַ�����ָ���stringp����������𻵣��緢�ַָ�������0���ǣ�
	delim: A C string of acceptable delimiter characters.

	Returns a pointer to a C string containing the chars of stringp up
	to the first delimiter char (or the end of the string), or NULL.
	���ҷָ���
*/
char *fz_strsep(char **stringp, const char *delim);

/*
	fz_strlcpy: Copy at most n-1 chars of a string into a destination
	buffer with null termination, returning the real length of the
	initial string (excluding terminator).
	��ȫ���ַ������ƺ���
	dst: Destination buffer, at least n bytes long.

	src: C string (non-NULL).

	n: Size of dst buffer in bytes.

	Returns the length (excluding terminator) of src.
*/
int fz_strlcpy(char *dst, const char *src, int n);

/*
	fz_strlcat: Concatenate 2 strings, with a maximum length.
	��ȫ���ַ������Ӻ���
	dst: pointer to first string in a buffer of n bytes.

	src: pointer to string to concatenate.

	n: Size (in bytes) of buffer that dst is in.

	Returns the real length that a concatenated dst + src would have been
	(not including terminator).
*/
int fz_strlcat(char *dst, const char *src, int n);

/*
	fz_chartorune: UTF8 decode a string of chars to a rune.
	ʹ��UTF8�����ַ�������Ϊһ������
	rune: Pointer to an int to assign the decoded 'rune' to.
	ָ��ָ������ġ����ġ���intָ��
	str: Pointer to a UTF8 encoded string

	Returns the number of bytes consumed. Does not throw exceptions.
*/
int fz_chartorune(int *rune, char *str);

/*
	runetochar: UTF8 encode a run to a string of chars.
	UTF8�����ı���Ϊ�ַ���
	str: Pointer to a place to put the UTF8 encoded string.

	rune: Pointer to a 'rune'.

	Returns the number of bytes the rune took to output. Does not throw
	exceptions.
*/
int fz_runetochar(char *str, int rune);

/*
	fz_runelen: Count many chars are required to represent a rune.
	ͳ����Ҫ����charת��Ϊ����
	rune: The rune to encode.

	Returns the number of bytes required to represent this run in UTF8.
*/
int fz_runelen(int rune);

/* getopt */
extern int fz_getopt(int nargc, char * const *nargv, const char *ostr);
extern int fz_optind;
extern char *fz_optarg;

/*
	fz_point is a point in a two-dimensional space .fz_point�Ƕ�ά�ռ��е�һ����
*/
typedef struct fz_point_s fz_point;
struct fz_point_s//��ά�ռ��е�һ����
{
	float x, y;
};

/*
	fz_rect is a rectangle represented by two diagonally opposite
	corners at arbitrary coordinates.
	�������������������Խ���ԵĽǱ�ʾ�ľ��Ρ�

	Rectangles are always axis-aligned with the X- and Y- axes.
	The relationship between the coordinates are that x0 <= x1 and
	y0 <= y1 in all cases except for infinte rectangles. The area
	of a rectangle is defined as (x1 - x0) * (y1 - y0). If either
	x0 > x1 or y0 > y1 is true for a given rectangle then it is
	defined to be infinite.

	To check for empty or infinite rectangles use fz_is_empty_rect
	and fz_is_infinite_rect. Compare to fz_bbox which has corners
	at integer coordinates.
	ʹ��fz_is_empty_rect �� fz_is_infinite_rect������Ƿ��ǿյĻ������޵ľ���
	x0, y0: The top left corner.

	x1, y1: The botton right corner.
*/
typedef struct fz_rect_s fz_rect;
struct fz_rect_s//���Ͻ������½Ƕ���ľ���
{
	float x0, y0;//���Ͻ�
	float x1, y1;//���½�
};

/*
	fz_bbox is a bounding box similar to a fz_rect, except that
	all corner coordinates are rounded to integer coordinates.
	To check for empty or infinite bounding boxes use
	fz_is_empty_bbox and fz_is_infinite_bbox.
	fz_bbox��fz_rect���ƣ�ֻ����������ֵ��Ϊ����
	x0, y0: The top left corner.

	x1, y1: The bottom right corner.
*/
typedef struct fz_bbox_s fz_bbox;
struct fz_bbox_s
{
	int x0, y0;
	int x1, y1;
};

/*
	A rectangle with sides of length one.
	�߳�Ϊһ��������
	The bottom left corner is at (0, 0) and the top right corner
	is at (1, 1).
*/
extern const fz_rect fz_unit_rect;

/*
	A bounding box with sides of length one. See fz_unit_rect.
	�߳�Ϊһ�İ�Χ��
*/
extern const fz_bbox fz_unit_bbox;

/*
	An empty rectangle with an area equal to zero.
	�վ���
	Both the top left and bottom right corner are at (0, 0).
*/
extern const fz_rect fz_empty_rect;

/*
	An empty bounding box. See fz_empty_rect.
	�հ�Χ��
*/
extern const fz_bbox fz_empty_bbox;

/*
	An infinite rectangle with negative area.
	���޵ľ���
	The corner (x0, y0) is at (1, 1) while the corner (x1, y1) is
	at (-1, -1).
*/
extern const fz_rect fz_infinite_rect;

/*
	An infinite bounding box. See fz_infinite_rect.
	���޵İ�Χ��
*/
extern const fz_bbox fz_infinite_bbox;

/*
	fz_is_empty_rect: Check if rectangle is empty.
	����Ƿ�Ϊ��
	An empty rectangle is defined as one whose area is zero.
*/
#define fz_is_empty_rect(r) ((r).x0 == (r).x1 || (r).y0 == (r).y1)

/*
	fz_is_empty_bbox: Check if bounding box is empty.
	����Ƿ�Ϊ��
	Same definition of empty bounding boxes as for empty
	rectangles. See fz_is_empty_rect.
*/
#define fz_is_empty_bbox(b) ((b).x0 == (b).x1 || (b).y0 == (b).y1)

/*
	fz_is_infinite: Check if rectangle is infinite.
	����Ƿ�Ϊ����
	An infinite rectangle is defined as one where either of the
	two relationships between corner coordinates are not true.
*/
#define fz_is_infinite_rect(r) ((r).x0 > (r).x1 || (r).y0 > (r).y1)

/*
	fz_is_infinite_bbox: Check if bounding box is infinite.
	����Ƿ�Ϊ����
	Same definition of infinite bounding boxes as for infinite
	rectangles. See fz_is_infinite_rect.
*/
#define fz_is_infinite_bbox(b) ((b).x0 > (b).x1 || (b).y0 > (b).y1)

/*
	fz_matrix is a a row-major 3x3 matrix used for representing
	transformations of coordinates throughout MuPDF.
	fz_matrix��һ��3*3�ľ�������ʾ����任

	Since all points reside in a two-dimensional space, one vector
	is always a constant unit vector; hence only some elements may
	vary in a matrix. Below is how the elements map between
	different representations.

	/ a b 0 \
	| c d 0 |   normally represented as    [ a b c d e f ].
	\ e f 1 /
*/
typedef struct fz_matrix_s fz_matrix;
struct fz_matrix_s//ת������
{
	float a, b, c, d, e, f;
};


/*
	fz_identity: Identity transform matrix.
*/
extern const fz_matrix fz_identity;//ʹ�õ�ת������

/*
	fz_concat: Multiply two matrices.
	�����������
	The order of the two matrices are important since matrix
	multiplication is not commutative.
	ע��˳�򣬾�����˲�֧�ֽ�����
	Does not throw exceptions.
*/
fz_matrix fz_concat(fz_matrix left, fz_matrix right);

/*
	fz_scale: Create a scaling matrix.

	The returned matrix is of the form [ sx 0 0 sy 0 0 ].
	���ž���
	sx, sy: Scaling factors along the X- and Y-axes. A scaling
	factor of 1.0 will not cause any scaling along the relevant
	axis.

	Does not throw exceptions.
*/
fz_matrix fz_scale(float sx, float sy);

/*
	fz_shear: Create a shearing matrix.
	���о���
	The returned matrix is of the form [ 1 sy sx 1 0 0 ].

	sx, sy: Shearing factors. A shearing factor of 0.0 will not
	cause any shearing along the relevant axis.

	Does not throw exceptions.
*/
fz_matrix fz_shear(float sx, float sy);

/*
	fz_rotate: Create a rotation matrix.
	��ת����
	The returned matrix is of the form
	[ cos(deg) sin(deg) -sin(deg) cos(deg) 0 0 ].

	degrees: Degrees of counter clockwise rotation. Values less
	than zero and greater than 360 are handled as expected.

	Does not throw exceptions.
*/
fz_matrix fz_rotate(float degrees);

/*
	fz_translate: Create a translation matrix.
	ת������
	The returned matrix is of the form [ 1 0 0 1 tx ty ].

	tx, ty: Translation distances along the X- and Y-axes. A
	translation of 0 will not cause any translation along the
	relevant axis.

	Does not throw exceptions.
*/
fz_matrix fz_translate(float tx, float ty);

/*
	fz_invert_matrix: Create an inverse matrix.
	����һ������󣬾�����������
	matrix: Matrix to invert. A degenerate matrix, where the
	determinant is equal to zero, can not be inverted and the
	original matrix is returned instead.

	Does not throw exceptions.
*/
fz_matrix fz_invert_matrix(fz_matrix matrix);

/*
	fz_is_rectilinear: Check if a transformation is rectilinear.
	���ת���Ƿ������Ե�
	Rectilinear means that no shearing is present and that any
	rotations present are a multiple of 90 degrees. Usually this
	is used to make sure that axis-aligned rectangles before the
	transformation are still axis-aligned rectangles afterwards.

	Does not throw exceptions.
*/
int fz_is_rectilinear(fz_matrix m);

/*
	fz_matrix_expansion: Calculate average scaling factor of matrix.
	��������ƽ��߶�����
*/
float fz_matrix_expansion(fz_matrix m); /* sumatrapdf */

/*
	fz_bbox_covering_rect: Convert a rect into the minimal bounding box
	that covers the rectangle.
	������ת��Ϊ���Ǿ��ε���С��Χ��
	Coordinates in a bounding box are integers, so rounding of the
	rects coordinates takes place. The top left corner is rounded
	upwards and left while the bottom right corner is rounded
	downwards and to the right. Overflows or underflowing
	coordinates are clamped to INT_MIN/INT_MAX.

	Does not throw exceptions.
*/
fz_bbox fz_bbox_covering_rect(fz_rect rect);

/*
	fz_round_rect: Convert a rect into a bounding box.
	ת��һ������Ϊһ����װ��
	Coordinates in a bounding box are integers, so rounding of the
	rects coordinates takes place. The top left corner is rounded
	upwards and left while the bottom right corner is rounded
	downwards and to the right. Overflows or underflowing
	coordinates are clamped to INT_MIN/INT_MAX.

	This differs from fz_bbox_covering_rect, in that fz_bbox_covering_rect
	slavishly follows the numbers (i.e any slight over/under calculations
	can cause whole extra pixels to be added). fz_round_rect
	allows for a small amount of rounding error when calculating
	the bbox.

	Does not throw exceptions.
*/
fz_bbox fz_round_rect(fz_rect rect);

/*
	fz_intersect_rect: Compute intersection of two rectangles.
	�����������εĽ���
	Compute the largest axis-aligned rectangle that covers the
	area covered by both given rectangles. If either rectangle is
	empty then the intersection is also empty. If either rectangle
	is infinite then the intersection is simply the non-infinite
	rectangle. Should both rectangles be infinite, then the
	intersection is also infinite.

	Does not throw exceptions.
*/
fz_rect fz_intersect_rect(fz_rect a, fz_rect b);

/*
	fz_intersect_bbox: Compute intersection of two bounding boxes.

	Similar to fz_intersect_rect but operates on two bounding
	boxes instead of two rectangles.

	Does not throw exceptions.
*/
fz_bbox fz_intersect_bbox(fz_bbox a, fz_bbox b);

/*
	fz_union_rect: Compute union of two rectangles.
	�����������εĽ���
	Compute the smallest axis-aligned rectangle that encompasses
	both given rectangles. If either rectangle is infinite then
	the union is also infinite. If either rectangle is empty then
	the union is simply the non-empty rectangle. Should both
	rectangles be empty, then the union is also empty.

	Does not throw exceptions.
*/
fz_rect fz_union_rect(fz_rect a, fz_rect b);

/*
	fz_union_bbox: Compute union of two bounding boxes.
	����������װ�еĽ���
	Similar to fz_union_rect but operates on two bounding boxes
	instead of two rectangles.

	Does not throw exceptions.
*/
fz_bbox fz_union_bbox(fz_bbox a, fz_bbox b);

/*
	fz_transform_point: Apply a transformation to a point.
	��ת��Ӧ����ĳһ��
	transform: Transformation matrix to apply. See fz_concat,
	fz_scale, fz_rotate and fz_translate for how to create a
	matrix.

	Does not throw exceptions.
*/
fz_point fz_transform_point(fz_matrix transform, fz_point point);

/*
	fz_transform_vector: Apply a transformation to a vector.
	��ת��Ӧ����ĳ������
	transform: Transformation matrix to apply. See fz_concat,
	fz_scale and fz_rotate for how to create a matrix. Any
	translation will be ignored.

	Does not throw exceptions.
*/
fz_point fz_transform_vector(fz_matrix transform, fz_point vector);

/*
	fz_transform_rect: Apply a transform to a rectangle.
	��ת��Ӧ����һ������
	After the four corner points of the axis-aligned rectangle
	have been transformed it may not longer be axis-aligned. So a
	new axis-aligned rectangle is created covering at least the
	area of the transformed rectangle.

	transform: Transformation matrix to apply. See fz_concat,
	fz_scale and fz_rotate for how to create a matrix.

	rect: Rectangle to be transformed. The two special cases
	fz_empty_rect and fz_infinite_rect, may be used but are
	returned unchanged as expected.

	Does not throw exceptions.
*/
fz_rect fz_transform_rect(fz_matrix transform, fz_rect rect);

/*
	fz_transform_bbox: Transform a given bounding box.
	��ת��Ӧ����һ����װ��
	Similar to fz_transform_rect, but operates on a bounding box
	instead of a rectangle.

	Does not throw exceptions.
*/
fz_bbox fz_transform_bbox(fz_matrix matrix, fz_bbox bbox);

/*
	fz_buffer is a wrapper around a dynamically allocated array of bytes.
	fz_buffer��һ����̬�����������İ�װ
	Buffers have a capacity (the number of bytes storage immediately
	available) and a current size.
	������ṹ��֪��ǰ���ñ������Լ���ǰbuffer��С
*/
typedef struct fz_buffer_s fz_buffer;

/*
	fz_keep_buffer: Increment the reference count for a buffer.
	���ӻ����������ü���
	buf: The buffer to increment the reference count for.

	Returns a pointer to the buffer. Does not throw exceptions.
*/
fz_buffer *fz_keep_buffer(fz_context *ctx, fz_buffer *buf);

/*
	fz_drop_buffer: Decrement the reference count for a buffer.
	���ٻ����������ü���
	buf: The buffer to decrement the reference count for.
*/
void fz_drop_buffer(fz_context *ctx, fz_buffer *buf);

/*
	fz_buffer_storage: Retrieve information on the storage currently used
	by a buffer.
	������������ǰʹ�õĴ洢��Ϣ
	data: Pointer to place to retrieve data pointer.

	Returns length of stream.
*/
int fz_buffer_storage(fz_context *ctx, fz_buffer *buf, unsigned char **data);

/*
	fz_stream is a buffered reader capable of seeking in both
	directions.
	fz_stream��һ����ȡ���壬���Դ����������ȡ

	Streams are reference counted, so references must be dropped
	by a call to fz_close.
	���ǻᱻ��¼���ô����ģ��������ñ������fz_close����ȥ

	Only the data between rp and wp is valid.
	��rp��wp֮������ݲſ���
*/
typedef struct fz_stream_s fz_stream;

/*
	fz_open_file: Open the named file and wrap it in a stream.
	���ļ�
	filename: Path to a file as it would be given to open(2).
*/
fz_stream *fz_open_file(fz_context *ctx, const char *filename);

/*
	fz_open_file_w: Open the named file and wrap it in a stream.

	This function is only available when compiling for Win32.

	filename: Wide character path to the file as it would be given
	to _wopen().
*/
fz_stream *fz_open_file_w(fz_context *ctx, const wchar_t *filename);

/*
	fz_open_fd: Wrap an open file descriptor in a stream.

	file: An open file descriptor supporting bidirectional
	seeking. The stream will take ownership of the file
	descriptor, so it may not be modified or closed after the call
	to fz_open_fd. When the stream is closed it will also close
	the file descriptor.
*/
fz_stream *fz_open_fd(fz_context *ctx, int file);

/*
	fz_open_memory: Open a block of memory as a stream.

	data: Pointer to start of data block. Ownership of the data block is
	NOT passed in.

	len: Number of bytes in data block.

	Returns pointer to newly created stream. May throw exceptions on
	failure to allocate.
*/
fz_stream *fz_open_memory(fz_context *ctx, unsigned char *data, int len);

/*
	fz_open_buffer: Open a buffer as a stream.

	buf: The buffer to open. Ownership of the buffer is NOT passed in
	(this function takes it's own reference).

	Returns pointer to newly created stream. May throw exceptions on
	failure to allocate.
*/
fz_stream *fz_open_buffer(fz_context *ctx, fz_buffer *buf);

/*
	fz_close: Close an open stream.

	Drops a reference for the stream. Once no references remain
	the stream will be closed, as will any file descriptor the
	stream is using.

	Does not throw exceptions.
*/
void fz_close(fz_stream *stm);

/*
	fz_tell: return the current reading position within a stream
*/
int fz_tell(fz_stream *stm);

/*
	fz_seek: Seek within a stream.

	stm: The stream to seek within.

	offset: The offset to seek to.
	ƫ��
	whence: From where the offset is measured (see fseek).
*/
void fz_seek(fz_stream *stm, int offset, int whence);

/*
	fz_read: Read from a stream into a given data block.
	������ȡ�����������ݿ�
	stm: The stream to read from.

	data: The data block to read into.

	len: The length of the data block (in bytes).

	Returns the number of bytes read. May throw exceptions.
*/
int fz_read(fz_stream *stm, unsigned char *data, int len);

/*
	fz_read_all: Read all of a stream into a buffer.

	stm: The stream to read from

	initial: Suggested initial size for the buffer.

	Returns a buffer created from reading from the stream. May throw
	exceptions on failure to allocate.
*/
fz_buffer *fz_read_all(fz_stream *stm, int initial);
/* cf. http://code.google.com/p/sumatrapdf/issues/detail?id=1587 */
fz_buffer *fz_read_all2(fz_stream *stm, int initial, int fail_on_error);

/* SumatraPDF: allow to clone a stream */
fz_stream *fz_clone_stream(fz_context *ctx, fz_stream *stm);

/*
	Bitmaps have 1 bit per component. Only used for creating halftoned
	versions of contone buffers, and saving out. Samples are stored msb
	first, akin to pbms.
	bitmapÿ�������1bit��С�������ڴ�������ӳ�񻺳����İ�ɫ���汾����Ʒ�洢MSB��һ��������pbms
*/
typedef struct fz_bitmap_s fz_bitmap;

/*
	fz_keep_bitmap: Take a reference to a bitmap.
	����λͼ
	bit: The bitmap to increment the reference for.
	Ҫ�������õ�λͼ
	Returns bit. Does not throw exceptions.
*/
fz_bitmap *fz_keep_bitmap(fz_context *ctx, fz_bitmap *bit);

/*
	fz_drop_bitmap: Drop a reference and free a bitmap.
	ȡ�����ò��ͷ�λͼ
	Decrement the reference count for the bitmap. When no
	references remain the pixmap will be freed.

	Does not throw exceptions.
*/
void fz_drop_bitmap(fz_context *ctx, fz_bitmap *bit);

/*
	An fz_colorspace object represents an abstract colorspace. While
	this should be treated as a black box by callers of the library at
	this stage, know that it encapsulates knowledge of how to convert
	colors to and from the colorspace, any lookup tables generated, the
	number of components in the colorspace etc.
	һ���������ɫ�ռ䣬Ӧ�����ֽ׶ο�ĵ����߿�����һ�����䣬��װ����ν���ɫת��Ϊ��ɫ�ռ��֪ʶ
*/
typedef struct fz_colorspace_s fz_colorspace;

/*
	fz_find_device_colorspace: Find a standard colorspace based upon
	it's name.
	Ѱ�һ������ֵı�׼ɫ�ʿռ�
*/
fz_colorspace *fz_find_device_colorspace(fz_context *ctx, char *name);

/*
	fz_device_gray: Abstract colorspace representing device specific
	gray.
	�����ɫ�ʿռ�����豸�ر�Ļ�ɫ
*/
extern fz_colorspace *fz_device_gray;

/*
	fz_device_rgb: Abstract colorspace representing device specific
	rgb.
	������ԭɫ
*/
extern fz_colorspace *fz_device_rgb;

/*
	fz_device_bgr: Abstract colorspace representing device specific
	bgr.
	�����ƴ�����ԭɫ��BGR�����෴
*/
extern fz_colorspace *fz_device_bgr;

/*
	fz_device_cmyk: Abstract colorspace representing device specific
	CMYK.
	CMYKҲ����ӡˢɫ��ģʽ
*/
extern fz_colorspace *fz_device_cmyk;

/*
	Pixmaps represent a set of pixels for a 2 dimensional region of a
	plane. Each pixel has n components per pixel, the last of which is
	always alpha. The data is in premultiplied alpha when rendering, but
	non-premultiplied for colorspace conversions and rescaling.
*/
typedef struct fz_pixmap_s fz_pixmap;

/*
	fz_pixmap_bbox: Return a bounding box for a pixmap.
	����һ����Χ��Ϊ����ͼ
	Returns an exact bounding box for the supplied pixmap.
*/
fz_bbox fz_pixmap_bbox(fz_context *ctx, fz_pixmap *pix);

/*
	fz_pixmap_width: Return the width of the pixmap in pixels.
*/
int fz_pixmap_width(fz_context *ctx, fz_pixmap *pix);

/*
	fz_pixmap_height: Return the height of the pixmap in pixels.
*/
int fz_pixmap_height(fz_context *ctx, fz_pixmap *pix);

/*
	fz_new_pixmap: Create a new pixmap, with it's origin at (0,0)

	cs: The colorspace to use for the pixmap, or NULL for an alpha
	plane/mask.

	w: The width of the pixmap (in pixels)

	h: The height of the pixmap (in pixels)

	Returns a pointer to the new pixmap. Throws exception on failure to
	allocate.
*/
fz_pixmap *fz_new_pixmap(fz_context *ctx, fz_colorspace *cs, int w, int h);//��ʼ��һ��pixmap

/*
	fz_new_pixmap_with_bbox: Create a pixmap of a given size,
	location and pixel format.

	The bounding box specifies the size of the created pixmap and
	where it will be located. The colorspace determines the number
	of components per pixel. Alpha is always present. Pixmaps are
	reference counted, so drop references using fz_drop_pixmap.

	colorspace: Colorspace format used for the created pixmap. The
	pixmap will keep a reference to the colorspace.

	bbox: Bounding box specifying location/size of created pixmap.

	Returns a pointer to the new pixmap. Throws exception on failure to
	allocate.
*/
fz_pixmap *fz_new_pixmap_with_bbox(fz_context *ctx, fz_colorspace *colorspace, fz_bbox bbox);

/*
	fz_new_pixmap_with_data: Create a new pixmap, with it's origin at
	(0,0) using the supplied data block.
	ʹ���ṩ�����ݿ鴴������ͼ
	cs: The colorspace to use for the pixmap, or NULL for an alpha
	plane/mask.

	w: The width of the pixmap (in pixels)

	h: The height of the pixmap (in pixels)

	samples: The data block to keep the samples in.
	������Ʒ�����ݿ�
	Returns a pointer to the new pixmap. Throws exception on failure to
	allocate.
*/
fz_pixmap *fz_new_pixmap_with_data(fz_context *ctx, fz_colorspace *colorspace, int w, int h, unsigned char *samples);

/*
	fz_new_pixmap_with_bbox_and_data: Create a pixmap of a given size,
	location and pixel format, using the supplied data block.

	The bounding box specifies the size of the created pixmap and
	where it will be located. The colorspace determines the number
	of components per pixel. Alpha is always present. Pixmaps are
	reference counted, so drop references using fz_drop_pixmap.
	ָ������ͼ�Ĵ�Сλ��
	colorspace: Colorspace format used for the created pixmap. The
	pixmap will keep a reference to the colorspace.

	bbox: Bounding box specifying location/size of created pixmap.

	samples: The data block to keep the samples in.

	Returns a pointer to the new pixmap. Throws exception on failure to
	allocate.
*/
fz_pixmap *fz_new_pixmap_with_bbox_and_data(fz_context *ctx, fz_colorspace *colorspace, fz_bbox bbox, unsigned char *samples);

/*
	fz_keep_pixmap: Take a reference to a pixmap.

	pix: The pixmap to increment the reference for.

	Returns pix. Does not throw exceptions.
*/
fz_pixmap *fz_keep_pixmap(fz_context *ctx, fz_pixmap *pix);

/*
	fz_drop_pixmap: Drop a reference and free a pixmap.
	ȡ�����ã��ͷ�pixmap
	Decrement the reference count for the pixmap. When no
	references remain the pixmap will be freed.

	Does not throw exceptions.
*/
void fz_drop_pixmap(fz_context *ctx, fz_pixmap *pix);

/*
	fz_pixmap_colorspace: Return the colorspace of a pixmap

	Returns colorspace. Does not throw exceptions.
*/
fz_colorspace *fz_pixmap_colorspace(fz_context *ctx, fz_pixmap *pix);

/*
	fz_pixmap_components: Return the number of components in a pixmap.
	�������
	Returns the number of components. Does not throw exceptions.
*/
int fz_pixmap_components(fz_context *ctx, fz_pixmap *pix);

/*
	fz_pixmap_samples: Returns a pointer to the pixel data of a pixmap.

	Returns the pointer. Does not throw exceptions.
*/
unsigned char *fz_pixmap_samples(fz_context *ctx, fz_pixmap *pix);

/*
	fz_clear_pixmap_with_value: Clears a pixmap with the given value.

	pix: The pixmap to clear.

	value: Values in the range 0 to 255 are valid. Each component
	sample for each pixel in the pixmap will be set to this value,
	while alpha will always be set to 255 (non-transparent).

	Does not throw exceptions.
*/
void fz_clear_pixmap_with_value(fz_context *ctx, fz_pixmap *pix, int value);

/*
	fz_clear_pixmap_with_value: Sets all components (including alpha) of
	all pixels in a pixmap to 0.
	����ͼ����
	pix: The pixmap to clear.

	Does not throw exceptions.
*/
void fz_clear_pixmap(fz_context *ctx, fz_pixmap *pix);

/*
	fz_invert_pixmap: Invert all the pixels in a pixmap. All components
	of all pixels are inverted (except alpha, which is unchanged).

	Does not throw exceptions.
*/
void fz_invert_pixmap(fz_context *ctx, fz_pixmap *pix);

/*
	fz_invert_pixmap: Invert all the pixels in a given rectangle of a
	pixmap. All components of all pixels in the rectangle are inverted
	(except alpha, which is unchanged).

	Does not throw exceptions.
*/
void fz_invert_pixmap_rect(fz_pixmap *image, fz_bbox rect);

/*
	fz_gamma_pixmap: Apply gamma correction to a pixmap. All components
	of all pixels are modified (except alpha, which is unchanged).

	gamma: The gamma value to apply; 1.0 for no change.

	Does not throw exceptions.
*/
void fz_gamma_pixmap(fz_context *ctx, fz_pixmap *pix, float gamma);

/*
	fz_unmultiply_pixmap: Convert a pixmap from premultiplied to
	non-premultiplied format.

	Does not throw exceptions.
*/
void fz_unmultiply_pixmap(fz_context *ctx, fz_pixmap *pix);

/*
	fz_convert_pixmap: Convert from one pixmap to another (assumed to be
	the same size, but possibly with a different colorspace).

	dst: the source pixmap.

	src: the destination pixmap.
*/
void fz_convert_pixmap(fz_context *ctx, fz_pixmap *dst, fz_pixmap *src);

/*
	fz_write_pixmap: Save a pixmap out.
	����λͼ
	name: The prefix for the name of the pixmap. The pixmap will be saved
	as "name.png" if the pixmap is RGB or Greyscale, "name.pam" otherwise.

	rgb: If non zero, the pixmap is converted to rgb (if possible) before
	saving.
*/
void fz_write_pixmap(fz_context *ctx, fz_pixmap *img, char *name, int rgb);

/*
	fz_write_pnm: Save a pixmap as a pnm

	filename: The filename to save as (including extension).
*/
void fz_write_pnm(fz_context *ctx, fz_pixmap *pixmap, char *filename);

/*
	fz_write_pam: Save a pixmap as a pam

	filename: The filename to save as (including extension).
*/
void fz_write_pam(fz_context *ctx, fz_pixmap *pixmap, char *filename, int savealpha);

/*
	fz_write_png: Save a pixmap as a png

	filename: The filename to save as (including extension).
*/
void fz_write_png(fz_context *ctx, fz_pixmap *pixmap, char *filename, int savealpha);

/*
	fz_write_pbm: Save a bitmap as a pbm

	filename: The filename to save as (including extension).
*/
void fz_write_pbm(fz_context *ctx, fz_bitmap *bitmap, char *filename);

/*
	fz_md5_pixmap: Return the md5 digest for a pixmap

	filename: The filename to save as (including extension).
*/
void fz_md5_pixmap(fz_pixmap *pixmap, unsigned char digest[16]);

/* SumatraPDF: support TGA as output format */
void fz_write_tga(fz_context *ctx, fz_pixmap *pixmap, char *filename, int savealpha);

/*
	Images are storable objects from which we can obtain fz_pixmaps.
	These may be implemented as simple wrappers around a pixmap, or as
	more complex things that decode at different subsample settings on
	demand.
 */
typedef struct fz_image_s fz_image;

/*
	fz_image_to_pixmap: Called to get a handle to a pixmap from an image.
	���ô˺�����ʵ��image��pixmap��ת��
	image: The image to retrieve a pixmap from.

	w: The desired width (in pixels). This may be completely ignored, but
	may serve as an indication of a suitable subsample factor to use for
	image types that support this.

	h: The desired height (in pixels). This may be completely ignored, but
	may serve as an indication of a suitable subsample factor to use for
	image types that support this.

	Returns a non NULL pixmap pointer. May throw exceptions.
*/
fz_pixmap *fz_image_to_pixmap(fz_context *ctx, fz_image *image, int w, int h);

/*
	fz_drop_image: Drop a reference to an image.

	image: The image to drop a reference to.
*/
void fz_drop_image(fz_context *ctx, fz_image *image);

/*
	fz_keep_image: Increment the reference count of an image.

	image: The image to take a reference to.

	Returns a pointer to the image.
*/
fz_image *fz_keep_image(fz_context *ctx, fz_image *image);

/*
	A halftone is a set of threshold tiles, one per component. Each
	threshold tile is a pixmap, possibly of varying sizes and phases.
	Currently, we only provide one 'default' halftone tile for operating
	on 1 component plus alpha pixmaps (where the alpha is ignored). This
	is signified by an fz_halftone pointer to NULL.
	��ɫ����һ����ֵ��Ƭ��ÿ��component����һ����ÿ����ֵ��Ƭ��һ��pixmap
*/
typedef struct fz_halftone_s fz_halftone;

/*
	fz_halftone_pixmap: Make a bitmap from a pixmap and a halftone.
	ת��
	pix: The pixmap to generate from. Currently must be a single color
	component + alpha (where the alpha is assumed to be solid).

	ht: The halftone to use. NULL implies the default halftone.

	Returns the resultant bitmap. Throws exceptions in the case of
	failure to allocate.
*/
fz_bitmap *fz_halftone_pixmap(fz_context *ctx, fz_pixmap *pix, fz_halftone *ht);

/*
	An abstract font handle. Currently there are no public API functions
	for handling these.
	һ����������崦��
*/
typedef struct fz_font_s fz_font;

/*
	The different format handlers (pdf, xps etc) interpret pages to a
	device. These devices can then process the stream of calls they
	recieve in various ways:
		The trace device outputs debugging information for the calls.
		The draw device will render them.
		The list device stores them in a list to play back later.
		The text device performs text extraction and searching.
		The bbox device calculates the bounding box for the page.
	Other devices can (and will) be written in future.
	��ͬ�ĸ�ʽ��������pdf��xps�ȣ���ҳ�����Ϊ�豸�� ��Щ�豸Ȼ������Ը��ַ�ʽ�������ǽ��յĺ�������
	�����豸������õĵ�����Ϣ��
	��ͼ�豸���������ǡ�
	�б��豸�����Ǵ洢���б����Թ��Ժ�طš�
	�ı��豸ִ���ı���ȡ��������
	bbox�豸����ҳ��ı߽��
	�����豸���ԣ����ҽ�����д�롣

*/
typedef struct fz_device_s fz_device;

/*
	fz_free_device: Free a devices of any type and its resources.
*/
void fz_free_device(fz_device *dev);

/*
	fz_new_trace_device: Create a device to print a debug trace of
	all device calls.
*/
fz_device *fz_new_trace_device(fz_context *ctx);

/*
	fz_new_bbox_device: Create a device to compute the bounding
	box of all marks on a page.

	The returned bounding box will be the union of all bounding
	boxes of all objects on a page.
*/
fz_device *fz_new_bbox_device(fz_context *ctx, fz_bbox *bboxp);

/*
	fz_new_draw_device: Create a device to draw on a pixmap.

	dest: Target pixmap for the draw device. See fz_new_pixmap*
	for how to obtain a pixmap. The pixmap is not cleared by the
	draw device, see fz_clear_pixmap* for how to clear it prior to
	calling fz_new_draw_device. Free the device by calling
	fz_free_device.
*/
fz_device *fz_new_draw_device(fz_context *ctx, fz_pixmap *dest);

/* SumatraPDF: GDI+ draw device */
#ifdef _WIN32
fz_device *fz_new_gdiplus_device(fz_context *ctx, void *dc, fz_bbox base_clip);
#endif

/*
	Text extraction device: Used for searching, format conversion etc.�ı���ȡ�豸��������������ʽת���� 
 */

typedef struct fz_text_style_s fz_text_style;
typedef struct fz_text_char_s fz_text_char;
typedef struct fz_text_span_s fz_text_span;
typedef struct fz_text_line_s fz_text_line;
typedef struct fz_text_block_s fz_text_block;

typedef struct fz_text_sheet_s fz_text_sheet;
typedef struct fz_text_page_s fz_text_page;

struct fz_text_style_s
{
	int id;
	fz_font *font;
	float size;
	int wmode;
	int script;
	/* etc... */
	fz_text_style *next;
};

struct fz_text_sheet_s
{
	int maxid;
	fz_text_style *style;
};

struct fz_text_char_s
{
	fz_rect bbox;
	int c;
};

struct fz_text_span_s
{
	fz_rect bbox;
	int len, cap;
	fz_text_char *text;
	fz_text_style *style;
};

struct fz_text_line_s
{
	fz_rect bbox;
	int len, cap;
	fz_text_span *spans;
};

struct fz_text_block_s
{
	fz_rect bbox;
	int len, cap;
	fz_text_line *lines;
};

struct fz_text_page_s
{
	fz_rect mediabox;
	int len, cap;
	fz_text_block *blocks;
};

/*
	fz_new_text_device: Create a device to extract the text on a page.

	Gather and sort the text on a page into spans of uniform style,
	arranged into lines and blocks by reading order. The reading order
	is determined by various heuristics, so may not be accurate.
*/
fz_device *fz_new_text_device(fz_context *ctx, fz_text_sheet *sheet, fz_text_page *page);

/*
	fz_new_text_sheet: Create an empty style sheet.

	The style sheet is filled out by the text device, creating
	one style for each unique font, color, size combination that
	is used.
*/
fz_text_sheet *fz_new_text_sheet(fz_context *ctx);
void fz_free_text_sheet(fz_context *ctx, fz_text_sheet *sheet);

/*
	fz_new_text_page: Create an empty text page.

	The text page is filled out by the text device to contain the blocks,
	lines and spans of text on the page.
*/
fz_text_page *fz_new_text_page(fz_context *ctx, fz_rect mediabox);
void fz_free_text_page(fz_context *ctx, fz_text_page *page);

void fz_print_text_sheet(fz_context *ctx, FILE *out, fz_text_sheet *sheet);
void fz_print_text_page_html(fz_context *ctx, FILE *out, fz_text_page *page);
void fz_print_text_page_xml(fz_context *ctx, FILE *out, fz_text_page *page);
void fz_print_text_page(fz_context *ctx, FILE *out, fz_text_page *page);

/*
 * Cookie support - simple communication channel between app/library.
 */

typedef struct fz_cookie_s fz_cookie;

/*
	Provide two-way communication between application and library.
	Intended for multi-threaded applications where one thread is
	rendering pages and another thread wants read progress
	feedback or abort a job that takes a long time to finish. The
	communication is unsynchronized without locking.

	abort: The appliation should set this field to 0 before
	calling fz_run_page to render a page. At any point when the
	page is being rendered the application my set this field to 1
	which will cause the rendering to finish soon. This field is
	checked periodically when the page is rendered, but exactly
	when is not known, therefore there is no upper bound on
	exactly when the the rendering will abort. If the application
	did not provide a set of locks to fz_new_context, it must also
	await the completion of fz_run_page before issuing another
	call to fz_run_page. Note that once the application has set
	this field to 1 after it called fz_run_page it may not change
	the value again.

	progress: Communicates rendering progress back to the
	application and is read only. Increments as a page is being
	rendered. The value starts out at 0 and is limited to less
	than or equal to progress_max, unless progress_max is -1.

	progress_max: Communicates the known upper bound of rendering
	back to the application and is read only. The maximum value
	that the progress field may take. If there is no known upper
	bound on how long the rendering may take this value is -1 and
	progress is not limited. Note that the value of progress_max
	may change from -1 to a positive value once an upper bound is
	known, so take this into consideration when comparing the
	value of progress to that of progress_max.
*/
struct fz_cookie_s
{
	int abort;
	int progress;
	int progress_max; /* -1 for unknown */
};

/*
	Display list device -- record and play back device commands.
*/

/*
	fz_display_list is a list containing drawing commands (text,
	images, etc.). The intent is two-fold: as a caching-mechanism
	to reduce parsing of a page, and to be used as a data
	structure in multi-threading where one thread parses the page
	and another renders pages.

	Create a displaylist with fz_new_display_list, hand it over to
	fz_new_list_device to have it populated, and later replay the
	list (once or many times) by calling fz_run_display_list. When
	the list is no longer needed free it with fz_free_display_list.
*/
typedef struct fz_display_list_s fz_display_list;

/*
	fz_new_display_list: Create an empty display list.

	A display list contains drawing commands (text, images, etc.).
	Use fz_new_list_device for populating the list.
*/
fz_display_list *fz_new_display_list(fz_context *ctx);

/*
	fz_new_list_device: Create a rendering device for a display list.

	When the device is rendering a page it will populate the
	display list with drawing commsnds (text, images, etc.). The
	display list can later be reused to render a page many times
	without having to re-interpret the page from the document file
	for each rendering. Once the device is no longer needed, free
	it with fz_free_device.

	list: A display list that the list device takes ownership of.
*/
fz_device *fz_new_list_device(fz_context *ctx, fz_display_list *list);

/*
	fz_run_display_list: (Re)-run a display list through a device.

	list: A display list, created by fz_new_display_list and
	populated with objects from a page by running fz_run_page on a
	device obtained from fz_new_list_device.

	dev: Device obtained from fz_new_*_device.

	ctm: Transform to apply to display list contents. May include
	for example scaling and rotation, see fz_scale, fz_rotate and
	fz_concat. Set to fz_identity if no transformation is desired.

	area: Only the part of the contents of the display list
	visible within this area will be considered when the list is
	run through the device. This does not imply for tile objects
	contained in the display list.

	cookie: Communication mechanism between caller and library
	running the page. Intended for multi-threaded applications,
	while single-threaded applications set cookie to NULL. The
	caller may abort an ongoing page run. Cookie also communicates
	progress information back to the caller. The fields inside
	cookie are continually updated while the page is being run.
*/
void fz_run_display_list(fz_display_list *list, fz_device *dev, fz_matrix ctm, fz_bbox area, fz_cookie *cookie);

/*
	fz_free_display_list: Frees a display list.

	list: Display list to be freed. Any objects put into the
	display list by a list device will also be freed.

	Does not throw exceptions.
*/
void fz_free_display_list(fz_context *ctx, fz_display_list *list);

/* Links */

typedef struct fz_link_s fz_link;

typedef struct fz_link_dest_s fz_link_dest;

typedef enum fz_link_kind_e
{
	FZ_LINK_NONE = 0,
	FZ_LINK_GOTO,
	FZ_LINK_URI,
	FZ_LINK_LAUNCH,
	FZ_LINK_NAMED,
	FZ_LINK_GOTOR
} fz_link_kind;

enum {
	fz_link_flag_l_valid = 1, /* lt.x is valid */
	fz_link_flag_t_valid = 2, /* lt.y is valid */
	fz_link_flag_r_valid = 4, /* rb.x is valid */
	fz_link_flag_b_valid = 8, /* rb.y is valid */
	fz_link_flag_fit_h = 16, /* Fit horizontally */
	fz_link_flag_fit_v = 32, /* Fit vertically */
	fz_link_flag_r_is_zoom = 64 /* rb.x is actually a zoom figure */
};

/*
	fz_link_dest: XXX

	kind: Set to one of FZ_LINK_* to tell what what type of link
	destination this is, and where in the union to look for
	information. XXX

	gotor.page: Page number, 0 is the first page of the document. XXX

	gotor.flags: A bitfield consisting of fz_link_flag_* telling
	what parts of gotor.lt and gotor.rb are valid, whether
	fitting to width/height should be used, or if an arbitrary
	zoom factor is used. XXX

	gotor.lt: The top left corner of the destination bounding box. XXX
	gotor.rb: The bottom right corner of the destination bounding box. XXX

	gotor.file_spec: XXX

	gotor.new_window: XXX

	uri.uri: XXX
	uri.is_map: XXX

	launch.file_spec: XXX
	launch.new_window: XXX

	named.named: XXX
*/
struct fz_link_dest_s
{
	fz_link_kind kind;
	union
	{
		struct
		{
			int page;
			int flags;
			fz_point lt;
			fz_point rb;
			char *file_spec;
			int new_window;
			char *rname; /* SumatraPDF: allow to resolve against remote documents */
		}
		gotor;
		struct
		{
			char *uri;
			int is_map;
		}
		uri;
		struct
		{
			char *file_spec;
			int new_window;
			/* SumatraPDF: support launching embedded files */
			int embedded_num, embedded_gen;
		}
		launch;
		struct
		{
			char *named;
		}
		named;
	}
	ld;
};

/*
	fz_link is a list of interactive links on a page.

	There is no relation between the order of the links in the
	list and the order they appear on the page. The list of links
	for a given page can be obtained from fz_load_links.

	A link is reference counted. Dropping a reference to a link is
	done by calling fz_drop_link.

	rect: The hot zone. The area that can be clicked in
	untransformed coordinates.

	dest: Link destinations come in two forms: Page and area that
	an application should display when this link is activated. Or
	as an URI that can be given to a browser.

	next: A pointer to the next link on the same page.
*/
struct fz_link_s
{
	int refs;
	fz_rect rect;
	fz_link_dest dest;
	fz_link *next;
};

fz_link *fz_new_link(fz_context *ctx, fz_rect bbox, fz_link_dest dest);
fz_link *fz_keep_link(fz_context *ctx, fz_link *link);

/*
	fz_drop_link: Drop and free a list of links.

	Does not throw exceptions.
*/
void fz_drop_link(fz_context *ctx, fz_link *link);

void fz_free_link_dest(fz_context *ctx, fz_link_dest *dest);

/* Outline */

typedef struct fz_outline_s fz_outline;

/*
	fz_outline is a tree of the outline of a document (also known
	as table of contents).

	title: Title of outline item using UTF-8 encoding. May be NULL
	if the outline item has no text string.

	dest: Destination in the document to be displayed when this
	outline item is activated. May be FZ_LINK_NONE if the outline
	item does not have a destination.

	next: The next outline item at the same level as this outline
	item. May be NULL if no more outline items exist at this level.

	down: The outline items immediate children in the hierarchy.
	May be NULL if no children exist.
*/
struct fz_outline_s
{
	char *title;
	fz_link_dest dest;
	fz_outline *next;
	fz_outline *down;
	int is_open; /* SumatraPDF: support expansion states */
};

/*
	fz_print_outline_xml: Dump the given outlines as (pseudo) XML.

	out: The file handle to output to.

	outline: The outlines to output.
*/
void fz_print_outline_xml(fz_context *ctx, FILE *out, fz_outline *outline);

/*
	fz_print_outline: Dump the given outlines to as text.

	out: The file handle to output to.

	outline: The outlines to output.
*/
void fz_print_outline(fz_context *ctx, FILE *out, fz_outline *outline);

/*
	fz_free_outline: Free hierarchical outline.

	Free an outline obtained from fz_load_outline.

	Does not throw exceptions.
*/
void fz_free_outline(fz_context *ctx, fz_outline *outline);

/*
	Document interface
*/
typedef struct fz_document_s fz_document;
typedef struct fz_page_s fz_page;

/*
	fz_open_document: Open a PDF, XPS or CBZ document.

	Open a document file and read its basic structure so pages and
	objects can be located. MuPDF will try to repair broken
	documents (without actually changing the file contents).

	The returned fz_document is used when calling most other
	document related functions. Note that it wraps the context, so
	those functions implicitly can access the global state in
	context.

	filename: a path to a file as it would be given to open(2).
*/
fz_document *fz_open_document(fz_context *ctx, char *filename);

/*
	fz_close_document: Close and free an open document.

	The resource store in the context associated with fz_document
	is emptied, and any allocations for the document are freed.

	Does not throw exceptions.
*/
void fz_close_document(fz_document *doc);

/*
	fz_needs_password: Check if a document is encrypted with a
	non-blank password.

	Does not throw exceptions.
*/
int fz_needs_password(fz_document *doc);

/*
	fz_authenticate_password: Test if the given password can
	decrypt the document.

	password: The password string to be checked. Some document
	specifications do not specify any particular text encoding, so
	neither do we.

	Does not throw exceptions.
*/
int fz_authenticate_password(fz_document *doc, char *password);

/*
	fz_load_outline: Load the hierarchical document outline.

	Should be freed by fz_free_outline.
*/
fz_outline *fz_load_outline(fz_document *doc);

/*
	fz_count_pages: Return the number of pages in document

	May return 0 for documents with no pages.
*/
int fz_count_pages(fz_document *doc);

/*
	fz_load_page: Load a page.

	After fz_load_page is it possible to retrieve the size of the
	page using fz_bound_page, or to render the page using
	fz_run_page_*. Free the page by calling fz_free_page.

	number: page number, 0 is the first page of the document.
*/
/* SumatraPDF: caution: fz_load_page uses cached page objects for XPS documents! */
fz_page *fz_load_page(fz_document *doc, int number);

/*
	fz_load_links: Load the list of links for a page.

	Returns a linked list of all the links on the page, each with
	its clickable region and link destination. Each link is
	reference counted so drop and free the list of links by
	calling fz_drop_link on the pointer return from fz_load_links.

	page: Page obtained from fz_load_page.
*/
fz_link *fz_load_links(fz_document *doc, fz_page *page);

/*
	fz_bound_page: Determine the size of a page at 72 dpi.

	Does not throw exceptions.
*/
fz_rect fz_bound_page(fz_document *doc, fz_page *page);

/*
	fz_run_page: Run a page through a device.

	page: Page obtained from fz_load_page.

	dev: Device obtained from fz_new_*_device.

	transform: Transform to apply to page. May include for example
	scaling and rotation, see fz_scale, fz_rotate and fz_concat.
	Set to fz_identity if no transformation is desired.

	cookie: Communication mechanism between caller and library
	rendering the page. Intended for multi-threaded applications,
	while single-threaded applications set cookie to NULL. The
	caller may abort an ongoing rendering of a page. Cookie also
	communicates progress information back to the caller. The
	fields inside cookie are continually updated while the page is
	rendering.
*/
void fz_run_page(fz_document *doc, fz_page *page, fz_device *dev, fz_matrix transform, fz_cookie *cookie);

/*
	fz_free_page: Free a loaded page.

	Does not throw exceptions.
*/
void fz_free_page(fz_document *doc, fz_page *page);

#endif