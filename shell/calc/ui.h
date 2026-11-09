/*
 * ui.h - w16 Calculator: the user interface's shared declarations.
 */
#ifndef CALC_UI_H
#define CALC_UI_H

/* client sizes (VGA, observed) */
#define SCI_CX  480
#define SCI_CY  298
#define STD_CX  259
#define STD_CY  230

extern HINSTANCE g_hinst;
extern HWND g_wnd, g_statbox;

/* main.c */
void ui_update(void);
void do_command(int id);
void paste_command(int id, int fromkey);

/* draw.c */
void draw_paint(HWND w);
void mouse_down(HWND w, int x, int y);
void mouse_move(HWND w, int x, int y);
void mouse_up(HWND w, int x, int y);

/* stat.c */
void stat_open(void);
void stat_close(void);

/* clip.c */
void clip_copy(void);
void clip_paste(void);

/* help.c */
void help_command(int id);
void help_menuselect(WPARAM wp, LPARAM lp);
void help_quit(HWND w);
void about(void);

#endif
