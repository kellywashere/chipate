#ifndef __DISPLAY_H__
#define __DISPLAY_H__

#include <stdbool.h>
#include <raylib.h>

#define DISPLAY_COLOR_DEFAULT GREEN

struct display {
	int   width;
	int   height;
	Color color;
	bool* data;
};

struct display* display_create(int w, int h);
void display_destroy(struct display* display);

void display_set_pixel(struct display* display, int x, int y, bool value);
bool display_get_pixel(struct display* display, int x, int y);
void display_clear(struct display* display);
void display_set_color(struct display* display, Color color);

void display_show(struct display* display, int xoffs, int yoffs, int pixelw, int pixelh);

#endif
