
#if !defined(_GKSCORE_H_)
#define _GKSCORE_H_

#ifdef _WIN32

#include <windows.h>    /* required for all Windows applications */
#define DLLEXPORT __declspec(dllexport)

#ifdef __cplusplus
extern "C" {
#endif

#ifdef TEXT
#undef TEXT
#endif

#else

#ifndef DLLEXPORT
#define DLLEXPORT
#endif

#endif

#define GRALGKS 3
#define GLIGKS 4
#define GKS5 5

#define MAX_WS 16	/* maximum number of workstations */
#define MAX_TNR 9	/* maximum number of normalization transformations */

#define OPEN_GKS 0
#define CLOSE_GKS 1
#define OPEN_WS 2
#define CLOSE_WS 3
#define ACTIVATE_WS 4
#define DEACTIVATE_WS 5
#define CLEAR_WS 6
#define REDRAW_SEG_ON_WS 7
#define UPDATE_WS 8
#define SET_DEFERRAL_STATE 9
#define MESSAGE 10
#define ESCAPE 11
#define POLYLINE 12
#define POLYMARKER 13
#define TEXT 14
#define FILLAREA 15
#define CELLARRAY 16
#define SET_PLINE_INDEX 18
#define SET_PLINE_LINETYPE 19
#define SET_PLINE_LINEWIDTH 20
#define SET_PLINE_COLOR_INDEX 21
#define SET_PMARK_INDEX 22
#define SET_PMARK_TYPE 23
#define SET_PMARK_SIZE 24
#define SET_PMARK_COLOR_INDEX 25
#define SET_TEXT_INDEX 26
#define SET_TEXT_FONTPREC 27
#define SET_TEXT_EXPFAC 28
#define SET_TEXT_SPACING 29
#define SET_TEXT_COLOR_INDEX 30
#define SET_TEXT_HEIGHT 31
#define SET_TEXT_UPVEC 32
#define SET_TEXT_PATH 33
#define SET_TEXT_ALIGN 34
#define SET_FILL_INDEX 35
#define SET_FILL_INT_STYLE 36
#define SET_FILL_STYLE_INDEX 37
#define SET_FILL_COLOR_INDEX 38
#define SET_ASF 41
#define SET_COLOR_REP 48
#define SET_WINDOW 49
#define SET_VIEWPORT 50
#define SELECT_XFORM 52
#define SET_CLIPPING 53
#define SET_WS_WINDOW 54
#define SET_WS_VIEWPORT 55
#define CREATE_SEG 56
#define CLOSE_SEG 57
#define DELETE_SEG 58
#define ASSOC_SEG_WITH_WS 61
#define COPY_SEG_TO_WS 62
#define SET_SEG_XFORM 64
#define INITIALIZE_LOCATOR 69
#define REQUEST_LOCATOR 81
#define REQUEST_STROKE 82
#define REQUEST_CHOICE 84
#define REQUEST_STRING 86
#define GET_ITEM 102
#define READ_ITEM 103
#define INTERPRET_ITEM 104
#define EVAL_XFORM_MATRIX 105

#define SET_TEXT_SLANT 200
#define DRAW_IMAGE 201
#define SET_SHADOW 202
#define SET_TRANSPARENCY 203
#define SET_COORD_XFORM 204

#define BEGIN_SELECTION 250
#define END_SELECTION 251
#define MOVE_SELECTION 252
#define RESIZE_SELECTION 253
#define INQ_BBOX 254

typedef struct
  {
    int lindex;
    int ltype;
    float lwidth;
    int plcoli;
    int mindex;
    int mtype;
    float mszsc;
    int pmcoli;
    int tindex;
    int txfont, txprec;
    float chxp;
    float chsp;
    int txcoli;
    float chh;
    float chup[2];
    int txp;
    int txal[2];
    int findex;
    int ints;
    int styli;
    int facoli;
    float window[MAX_TNR][4], viewport[MAX_TNR][4];
    int cntnr, clip, opsg;
    float mat[3][2];
    int asf[13];
    int wiss, version;
    int fontfile;
    float txslant;
    float shoff[2];
    float blur;
    float alpha;
    float a[MAX_TNR], b[MAX_TNR], c[MAX_TNR], d[MAX_TNR];
  }
gks_state_list_t;

typedef struct gks_list
  {
    int item;
    struct gks_list *next;
    void *ptr;
  }
gks_list_t;

typedef struct
  {
    int wkid;
    char *path;
    int wtype;
    int conid;
    void *ptr;
  }
ws_list_t;

typedef struct
  {
    int wtype;
    int dcunit;
    float sizex, sizey;
    int unitsx, unitsy;
    int wscat;
    char *path;
    char *env;
  }
ws_descr_t;

typedef struct
  {
    int state;
    char *buffer;
    int size, nbytes, position;
  }
gks_display_list_t;

typedef struct
  {
    int left, right;
    int size;
    int bottom, base, cap, top;
    int length;
    int coord[124][2];
  }
stroke_data_t;

int gks_open_font(void);
void gks_lookup_font(
  int fd, int version, int font, int chr, stroke_data_t *buffer);
void gks_close_font(int fd);

void gks_lookup_afm(int font, int chr, stroke_data_t *buffer);

char *gks_malloc(int size);
char *gks_realloc(void *ptr, int size);
void gks_free(void *ptr);

void gks_perror(const char *, ...);
void gks_fatal_error(const char *, ...);
const char *gks_function_name(int routine);
void gks_report_error(int routine, int errnum);

void gks_init_core(gks_state_list_t *list);
gks_list_t *gks_list_find(gks_list_t *list, int element);
gks_list_t *gks_list_add(gks_list_t *list, int element, void *ptr);
gks_list_t *gks_list_del(gks_list_t *list, int element);
void gks_list_free(gks_list_t *list);
void gks_inq_pattern_array(int index, int *pa);
void gks_set_pattern_array(int index, int *pa);
void gks_inq_rgb(int index, float *red, float *green, float *blue);
void gks_set_rgb(int index, float red, float green, float blue);
void gks_inq_pixel(int index, int *pixel);
void gks_set_pixel(int index, int pixel);
void gks_fit_ws_viewport(float *viewport, float xmax, float ymax, float margin);
void gks_set_norm_xform(int tnr, float *window, float *viewport);
void gks_set_xform_matrix(float tran[3][2]);
void gks_seg_xform(float *x, float *y);
void gks_WC_to_NDC(int tnr, float *x, float *y);
void gks_NDC_to_WC(int tnr, float *x, float *y);
void gks_set_dev_xform(gks_state_list_t *s, float *window, float *viewport);
void gks_inq_dev_xform(float *window, float *viewport);
void gks_set_chr_xform(void);
void gks_chr_height(float *height);
void gks_get_dash(int ltype, float scale, char *dash);
void gks_get_dash_list(int ltype, float scale, int list[10]);
void gks_move(float x, float y, void (*move)(float x, float y));
void gks_dash(float x, float y,
  void (*move)(float x, float y), void (*draw)(float x, float y));
void gks_emul_polyline(int n, float *px, float *py, int ltype, int tnr,
  void (*move)(float x, float y), void (*draw)(float x, float y));
void gks_emul_polymarker(
  int n, float *px, float *py, void (*marker)(float x, float y, int mtype));
void gks_emul_text(float px, float py, int nchars, char *chars,
  void (*polyline)(int n, float *px, float *py, int ltype, int tnr),
  void (*fillarea)(int n, float *px, float *py, int tnr));
void gks_emul_fillarea(int n, float *px, float *py, int tnr,
  void (*polyline)(int n, float *px, float *py, int ltype, int tnr),
  float yres);
void gks_util_inq_text_extent(float px, float py, char *chars, int nchars,
  float *cpx, float *cpy, float tx[4], float ty[4]);
int gks_get_ws_type(void);
int gks_base64(
  unsigned char *src, size_t srclength, char *target, size_t targsize);
const char *gks_getenv(const char *env);

DLLEXPORT void gks_dl_write_item(gks_display_list_t *d,
  int fctid, int dx, int dy, int dimx, int *ia,
  int lr1, float *r1, int lr2, float *r2, int lc, char *c,
  gks_state_list_t *gkss);

void gks_drv_mo(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

void gks_drv_mi(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

void gks_drv_wiss(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

void gks_wiss_dispatch(int fctid, int wkid, int segn);

void gks_drv_cgm(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

void gks_drv_win(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

void gks_drv_mac(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

void gks_drv_ps(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

void gks_drv_pdf(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

void gks_drv_qt(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

void gks_drv_x11(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

void gks_drv_socket(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

void gks_drv_plugin(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

DLLEXPORT void gks_gs_plugin(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

DLLEXPORT void gks_fig_plugin(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

DLLEXPORT void gks_qt_plugin(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

DLLEXPORT void gks_svg_plugin(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

DLLEXPORT void gks_wmf_plugin(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

DLLEXPORT void gks_quartz_plugin(
  int fctid,
  int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, float *f_arr_1, int len_f_arr_2, float *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

void gks_compress(
  int bits, unsigned char *in, int in_len, unsigned char *out, int *out_len);

int gks_open_file(const char *path, const char *mode);
int gks_read_file(int fd, void *buf, int count);
int gks_write_file(int fd, void *buf, int count);
int gks_close_file(int fd);

#ifdef _WIN32
#ifdef __cplusplus
}
#endif
#endif

#endif