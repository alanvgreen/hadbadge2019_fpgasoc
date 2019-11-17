#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "mach_defines.h"
#include "sdk.h"
#include "gfx_load.h"
#include "cache.h"
#include "badgetime.h"

//Pointer to the framebuffer memory.
uint8_t *fbmem;


uint32_t * const SPIS = (uint32_t *)(void *)(SPIS_OFFSET);

extern uint32_t GFXPAL[];

#define FB_WIDTH 480
#define FB_HEIGHT 320

void moveTo(FILE * f, uint16_t x, uint16_t y) {
	fprintf(f, "\033%uX", x);
	fprintf(f, "\033%uY", y); 
}

void main(int argc, char **argv) {
	//We're running in app context. We have full control over the badge and can do with the hardware what we want. As
	//soon as main() returns, however, we will go back to the IPL.
	printf("Hello World app: main running\n");
	
	//First, allocate some memory for the background framebuffer. We're gonna dump a fancy image into it. The image is
	//going to be 8-bit, so we allocate 1 byte per pixel.
	uint32_t buf=(uint32_t) malloc(FB_WIDTH*FB_HEIGHT + 0x400);
	//fbmem = (buf + 0x400) & 0x3ff;
	uint8_t *fbmem = (uint8_t *) ((buf + 0x3ff) & ~(0x3ff));
	memset(fbmem, 0, FB_WIDTH * FB_HEIGHT);

	memset(fbmem + FB_WIDTH * 100, '0', FB_WIDTH*10);
	printf("Hello World: framebuffer at %p\n", fbmem);
	
	//Tell the GFX hardware to use this, and its pitch. We also tell the GFX hardware to use palette entries starting
	//from 128 for the frame buffer; the tiles left by the IPL will use palette entries 0-16 already.
	GFX_REG(GFX_FBPITCH_REG)=(128<<GFX_FBPITCH_PAL_OFF)|(FB_WIDTH<<GFX_FBPITCH_PITCH_OFF);
	//Set up the framebuffer address
	GFX_REG(GFX_FBADDR_REG)=((uint32_t)fbmem);

	//Flush the memory region to psram so the GFX hw can stream it from there.
	cache_flush(fbmem, fbmem+FB_WIDTH*FB_HEIGHT);

	//The IPL leaves us with a tileset that has tile 0 to 127 map to ASCII characters, so we do not need to
	//load anything specific for this. In order to get some text out, we can use the /dev/console device
	//that will use these tiles to put text in a tilemap. It uses escape codes to do so, see 
	//ipl/gloss/console_out.c for more info.
	FILE *f;
	f=fopen("/dev/console", "w");
	setvbuf(f, NULL, _IONBF, 0); //make console line unbuffered
	//Note that without the setvbuf command, no characters would be printed until 1024 characters are
	//buffered. You normally don't want this.
	fprintf(f, "\033C"); //clear the console. Note '\033' is the escape character.
	fprintf(f, "\0335X"); //set Xpos to 5
	fprintf(f, "\0338Y"); //set Ypos to 8
	fprintf(f, "%x", (uint32_t)fbmem); // Print a nice greeting.
	
	//The user can still see nothing of this graphics goodness, so let's re-enable the framebuffer and
	//tile layer A (the default layer for the console). Also indicate the framebuffer we have is
	//8-bit.
	GFX_REG(GFX_LAYEREN_REG)=GFX_LAYEREN_FB_8BIT|GFX_LAYEREN_FB|GFX_LAYEREN_TILEA;

	// Need to set up palette properly
	for (int i = 128+1; i < 128+255; i++) {
		GFXPAL[i] = 0xffffffff;
	}

	fprintf(f, "\0335X"); //set Xpos to 5
	fprintf(f, "\0339Y"); //set Ypos to 8
	fprintf(f, "A"); // Print a nice greeting.

	// Set the SPI slave to point to somewhere in fbmem
	SPIS[1] = ((uint32_t) fbmem) + FB_WIDTH * 24; // 24 lines down
	SPIS[0] = 0; // run
	SPIS[0] = 1; // run

	fprintf(f, "\0335X"); //set Xpos to 5
	fprintf(f, "\03310Y"); //set Ypos to 8
	fprintf(f, "C"); // Print a nice greeting.

	//Wait until button A is pressed
	wait_for_button_press(BUTTON_A);
	fprintf(f, "\0335X"); //set Xpos to 5
	fprintf(f, "\03311Y"); //set Ypos to 8
	fprintf(f, "D"); // Print a nice greeting.
	
	uint8_t *addr = fbmem + (FB_WIDTH * 24) + 2;
	if (*addr) {
		fprintf(f, "\0335X"); //set Xpos to 5
		fprintf(f, "\03312Y"); //set Ypos to 8
		fprintf(f, "got"); // Print a nice greeting.

	}
	fprintf(f, "\0337X"); //set Xpos to 5
	fprintf(f, "\03313Y"); //set Ypos to 8
	fprintf(f, "%08x", SPIS[0]); // Print a nice greeting.
	fprintf(f, "\0337X"); //set Xpos to 5
	fprintf(f, "\03314Y"); //set Ypos to 8
	fprintf(f, "%08x", SPIS[2]); // Print a nice greeting.
	delay(100);

	printf("Hello World ready. Press a button to exit.\n");
	//Wait until all buttons are released
	wait_for_button_release();

	//Wait until button A is pressed
	wait_for_button_press(BUTTON_A);
	printf("Hello World done. Bye!\n");
	SPIS[0] = 0; // stop
}
