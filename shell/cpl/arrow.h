/* arrow.h - the cpArrow spin control class (arrow.c) */
#ifndef W16_ARROW_H
#define W16_ARROW_H
extern char cls_arrow[];
LRESULT FAR PASCAL __loadds ArrowProc(HWND w, UINT msg, WPARAM wp, LPARAM lp);
void arrow_register(HINSTANCE h);
void arrow_unregister(HINSTANCE h);
#endif
