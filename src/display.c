#include <stdlib.h>
#include <raylib.h>
#include "display.h"

struct display* display_create(int w, int h) {
	struct display* display = malloc(sizeof(struct display));
	display->width = w;
	display->height = h;
	display->color = DISPLAY_COLOR_DEFAULT;
	display->data = calloc(w * h, sizeof(bool));
	return display;
}

void display_destroy(struct display* display) {
	if (display) {
		free(display->data);
		free(display);
	}
}

void display_set_pixel(struct display* display, int x, int y, bool value) {
	if (x < 0 || x >= display->width) return;
	if (y < 0 || y >= display->height) return;
	display->data[y * display->width + x] = value;
}

bool display_get_pixel(struct display* display, int x, int y) {
	if (x < 0 || x >= display->width) return false;
	if (y < 0 || y >= display->height) return false;
	return display->data[y * display->width + x];
}

void display_clear(struct display* display) {
	for (int ii = 0; ii < display->width * display->height; ++ii)
		display->data[ii] = false;
}

void display_set_color(struct display* display, Color color) {
	display->color = color;
}

void display_show(struct display* display, int xoffs, int yoffs, int pixelw, int pixelh) {
	for (int y = 0; y < display->height; ++y) {
		for (int x = 0; x < display->width; ++x) {
			bool d = display_get_pixel(display, x, y);
			DrawRectangle(xoffs + x * pixelw, yoffs + y * pixelh,
					pixelw - 1, pixelh - 1,
					d ? display->color : DARKGRAY);
		}
	}
}

