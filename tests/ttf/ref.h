/* ref.h - the float stb_truetype reference (ref.c) */
typedef struct { int type, x, y, cx, cy; } ref_vertex;
int ref_init(const unsigned char *data);
int ref_glyph(int cp);
int ref_shape(int g, ref_vertex **out);
void ref_hmetrics(int g, int *adv, int *lsb);
int ref_box(int g, int *x0, int *y0, int *x1, int *y1);
int ref_kern(int g1, int g2);
int ref_kern_len(void);
void ref_vmetrics(int *a, int *d, int *l);
float ref_scale_ph(float px);
float ref_scale_em(float px);
void ref_bitmap_box(int g, float s, int *x0, int *y0, int *x1, int *y1);
unsigned char *ref_aa(int g, float s, int *w, int *h, int *xo, int *yo);
void ref_free_aa(unsigned char *p);
int ref_flatten(int g, float s, int xo, int yo, float **px, float **py, int **counts, int *nc);
int ref_name(char *buf, int size, int plat, int enc, int lang, int id);
