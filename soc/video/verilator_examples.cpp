#include "verilator_examples.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <gd.h>
#include <functional>



// Allocate a frame buffer and set the FBADDR_REG to point to it
// Width should be at least 480
// Height should be at least 320
// 
uint8_t *fb_alloc(size_t width, size_t height, bool eight_bit) {
	size_t len = width * height  / (eight_bit ? 1 : 2);
	// In your code, you would use malloc() or new to allocate as much memory as you need
	//
	// uint8_t *result = (uint8_t *) calloc(len, 1);
	// memset(result, 0, len); // clear memory
	// GFX_REG(GFX_FBADDR_REG) = result;
	// return result;

	// However, since this example runs in the simulator, we just use an address in
	// simulated RAM.
	GFX_REG(GFX_FBADDR_REG) = 0x30000;
	memset(qpi_mem + 0x30000, 0, len);
	return qpi_mem + 0x30000;
}

// Read the fb_addr back from video memory
uint8_t *fb_get() {
	// In real code, fbaddr is the exact memory address
	// return GFX_REG(GFX_FBADDR_REG);
	// Simulated code must use a different memory range
	return qpi_mem + (0xffffff & GFX_REG(GFX_FBADDR_REG));	
}

// Draw a pixel, assuming 8 bit color
// Absolutely no bounds checking - beware!
void draw_pixel(int x, int y, uint8_t color) {
	uint32_t width = GFX_REG(GFX_FBPITCH_REG) & 0xffff;
	uint8_t *fb = fb_get();
	fb[y*width+x] = color;
}

// Draw a circle
void draw_circle(int xc, int yc, int r, uint8_t color) {
	int r2 = r * r;
	int yv = r;
	int yv2 = yv * yv;
	// Calculate (xv, yv) for 1/8 of circle, and mirror it eight ways
	for (int xv = 0; xv < yv; xv++) {
		// For each step of xv, yv, might reduce by 1
		int nextyv2 = (yv - 1) * (yv -1);
		int target = r2 - (xv * xv);
		if (yv2 - target > yv2 - nextyv2) {
			yv -= 1;
			yv2 = nextyv2;
		}
		draw_pixel(xc + xv, yc + yv, color);
		draw_pixel(xc - xv, yc + yv, color);
		draw_pixel(xc + xv, yc - yv, color);
		draw_pixel(xc - xv, yc - yv, color);
		draw_pixel(xc + yv, yc + xv, color);
		draw_pixel(xc - yv, yc + xv, color);
		draw_pixel(xc + yv, yc - xv, color);
		draw_pixel(xc - yv, yc - xv, color);
	}
}

// See https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm#Algorithm_for_integer_arithmetic
void draw_line_low(int x0, int y0, int x1, int y1, uint8_t color) {
	int dx = x1 - x0;
	int dy = (y1 > y0) ? y1 - y0 : y0 - y1;
	int yi = (y1 > y0) ? 1 : -1;

	int D = 2*dy - dx;
	int y = y0;

	for (int x = x0; x <= x1; x++) {
		draw_pixel(x, y, color);
		if (D > 0) {
			y = y + yi;
			D = D - 2*dx;
		}
		D = D + 2*dy;
	}
}

void draw_line_high(int x0, int y0, int x1, int y1, uint8_t color) {
	int dy = y1 - y0;
	int dx = (x1 > x0) ? x1 - x0 : x0 - x1;
	int xi = (x1 > x0) ? 1 : -1;

	int D = 2*dx - dy;
	int x = x0;
	for (int y = y0; y <= y1; y++) {
		draw_pixel(x, y, color);
		if (D > 0) {
			x = x + xi;
			D = D - 2*dy;
		}
		D = D + 2*dx;
	}
}

void draw_line(int x0, int y0, int x1, int y1, int8_t color) {
	if (abs(y1 - y0) < abs(x1 - x0)) {
		if (x0 > x1) {
			draw_line_low(x1, y1, x0, y0, color);
		} else {
			draw_line_low(x0, y0, x1, y1, color);
		}
	} else {
		if (y0 > y1) {
			draw_line_high(x1, y1, x0, y0, color);
		} else {
			draw_line_high(x0, y0, x1, y1, color);
		}
	}
}

// Simplest example of using the frame buffer
// Minimum necessary to do basic drawing
void frame_buffer_example1() {

	// Step 1 & 2: Allocate a buffer and set GFX_FBADDR_REG
	// This is handled by fb_alloc
	uint8_t *fb = fb_alloc(480, 320, true);

	// Step 3: Set width of buffer in pixels
	// only first 480 pixels of each line will be shown
	GFX_REG(GFX_FBPITCH_REG) = 480 << GFX_FBPITCH_PITCH_OFF;

	// Step 4: Enable frame buffer
	// Also set it to 8 bit depth.
	GFX_REG(GFX_LAYEREN_REG) = GFX_LAYEREN_FB_8BIT | GFX_LAYEREN_FB;

	// Step 5: set palette colors
	// 32-bits in form 0xAARRGGBB 
	GFXPAL[0] = 0x00000000; // color 0 is transparent
	GFXPAL[1] = 0xffffffff; // White
	GFXPAL[2] = 0xff00ff00; // Green
	GFXPAL[3] = 0xff0000ff; // Red

	// For fun, also set background, which will shine through color 0
	GFX_REG(GFX_BGNDCOL_REG) = 0xffff0000; // Blue

	// Draw a line, circle and pixel.
	draw_line(100, 250, 380, 250, 1); 
	draw_circle(240, 160, 100, 2); 
	draw_pixel(240, 160, 3); 
}

static void fb2_cb(int field) {
	int shift = (field / 3) & 0xf;
	GFX_REG(GFX_FBPITCH_REG) = 
		(shift << GFX_FBPITCH_PAL_OFF) + 
		(480 << GFX_FBPITCH_PITCH_OFF);	
}

// Demonstrates palette shifting
void frame_buffer_example2() {

	// Step 1 & 2: Allocate a buffer and set GFX_FBADDR_REG
	// This is handled by fb_alloc
	uint8_t *fb = fb_alloc(480, 320, true);

	// Step 3: Set width of buffer in pixels
	// only first 480 pixels of each line will be shown
	GFX_REG(GFX_FBPITCH_REG) = 480 << GFX_FBPITCH_PITCH_OFF;

	// Step 4: Enable frame buffer
	// Also set it to 8 bit depth.
	GFX_REG(GFX_LAYEREN_REG) = GFX_LAYEREN_FB_8BIT | GFX_LAYEREN_FB;
	
	// Step 5 set palette
	load_default_palette();

	// Draw every element in a new color

	// Head	
	draw_circle(240, 160, 100, 1);

	// Eyes
	draw_circle(200, 130, 20, 2);
	draw_circle(195, 125, 7, 3);
	draw_circle(280, 130, 20, 4);
	draw_circle(275, 125, 7, 5);

	// Mouth
	draw_line(220, 220, 290, 220, 6);
	draw_line(215, 225, 290, 220, 7);
	draw_line(220, 220, 215, 225, 8);

	// Nose
	draw_line(240, 140, 255, 170, 9);
	draw_line(240, 180, 255, 170, 10);

	// Set end-of-frame callback to change palettte
	end_of_frame_callback = fb2_cb;
}

struct Example3Data {
	uint8_t * fb_base;
	int buffer_width;
	int sx, sy;
	int dx = 16; 
	int dy = 16;
	int x = 0;
	int y = 0;
};
static Example3Data e3data;

// This call back bounces the viewport around the large image
static void e3_callback(int field) {
	// Move top left corner and allow to bounce off the "sides"
	e3data.x += e3data.dx;
	if (e3data.x <= 0) {
		e3data.dx = 16;
	} else if (e3data.x >= e3data.sx - 480) {
		e3data.dx = -16;
	}
	e3data.y += e3data.dy;
	if (e3data.y <= 0) {
		e3data.dy = 16;
	} else if (e3data.y >= e3data.sy - 320) {
		e3data.dy = -16;
	}

	// Set frame buffer x pixels across and y pixels down
	// Normally, you'd use e3data.fb_base:
	// GFX_REG(GFX_FBADDR_REG) = e3data.fb_base + e3data.x + e3data.y * e3data.buffer_width;

	// But for the sim we use the address we know the fb_init() gives
	 GFX_REG(GFX_FBADDR_REG) = 0x30000 + e3data.x + e3data.y * e3data.buffer_width;
}

// Demonstrates scrolling
void frame_buffer_example3() {
	// Open a PNG image file, get its dimensions
	FILE *f=fopen("elventower.png", "r");
	if (f==NULL) {
		perror("elventower.png");
		exit(1);
	}
	gdImagePtr im=gdImageCreateFromPng(f);
	uint32_t sx = gdImageSX(im);
	// Buffer width must be a multiple of 16
	uint32_t buffer_width = (sx + 15) & ~15;
	uint32_t sy = gdImageSY(im);

	// Step 1 & 2: Allocate a buffer and set GFX_FBADDR_REG
	// This is handled by fb_alloc
	uint8_t *fb = fb_alloc(buffer_width, sy, true);

	// Step 3: Set width of buffer in pixels
	// only first 480 pixels of each line will be shown
	GFX_REG(GFX_FBPITCH_REG) = buffer_width << GFX_FBPITCH_PITCH_OFF;

	// Step 4: Enable frame buffer
	// Also set it to 8 bit depth.
	GFX_REG(GFX_LAYEREN_REG) = GFX_LAYEREN_FB_8BIT | GFX_LAYEREN_FB;
	
	// Step 5 set palette from image
	unsigned pal_size = gdImageColorsTotal(im);
	for (unsigned i = 0; i < pal_size; i++) {
		uint32_t t = (0xff << 24) +
			(gdImageBlue(im, i) << 16) +
			(gdImageGreen(im, i) << 8) +
			(gdImageRed(im, i));
		GFXPAL[i] = t;
	}

	// Load image pixels in to framebuffer
	unsigned p = 0;
	for (unsigned y = 0; y < sy; y++) {
		for (unsigned x = 0; x < buffer_width; x++) {
			unsigned t = (uint8_t) gdImageGetPixel(im, x, y);
			fb[p++] = t;
		}
	}

	// Set up data for callback to use
	e3data.fb_base = fb;
	e3data.sx = sx;
	e3data.sy = sy;
	e3data.buffer_width = buffer_width;
	e3data.sx = sx;
	
	// Set end-of-frame callback to move view
	end_of_frame_callback = e3_callback;
}
