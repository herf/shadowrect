// ================================================================================================
// Shadowrect.cpp
//
//   Copyright (C) 2001 Herf Consulting LLC.  All rights reserved.
//
//   Please do not redistribute this source code.
//
//	 This source is provided "as-is," and Herf Consulting LLC will not be liable
//   for any damages resulting from its use or misuse.
//
// ================================================================================================

#include <stdio.h>
#include <stdlib.h>

#pragma warning(disable: 4244)


typedef unsigned long uint32;
typedef long int32;
typedef float real32;
typedef unsigned char uint8;

// Globals (ack)
uint32 bg = 0xFFFFFF;
real32 srad = 5.1f;
uint32 opac = 153;
uint8 *sprof = NULL;
uint8 *rgbbuf = NULL;
uint32 sprofsize = 0;

// shadow offsets in bitmap space
int32 xoff = 2;
int32 yoff = 3;

// offsets from bitmap to output space
int32 xbitmap = 0, ybitmap = 0;

// size of bitmap
int32 width, height;

// size of output
int32 outwidth, outheight;

// output state
int32 outputY = 0;

int32 topql, topqt, topqr, topqb;

FILE *in = stdin;

// ================================================================================================
// approximation to the gaussian integral [x, infty]
// ================================================================================================
static inline real32 gi(real32 x)
{
	const real32 i6 = 1.f / 6.0f;
	const real32 i4 = 1.f / 4.0f;
	const real32 i3 = 1.f / 3.0f;

	if (x > 1.5f) return 0.0f;
	if (x < -1.5f) return 1.0f;

	real32 x2 = x * x;
	real32 x3 = x2 * x;

	if (x >  0.5) return .5625  - ( x3 * i6 - 3 * x2 * i4 + 1.125 * x);
	if (x > -0.5) return 0.5    - (0.75 * x - x3 * i3);
				  return 0.4375 + (-x3 * i6 - 3 * x2 * i4 - 1.125 * x);
}

static int32 UpdateProfile()
{
	int32 size = srad * 3 + 1;
	int32 c    = size >> 1;

	sprof = new uint8[size];
	sprofsize = size;
	if (sprof == NULL) return -1;

	real32 invr = 1.f / srad;
	for (int32 x = 1; x < size; x ++) {
		real32 xp   = (c - x) * invr;
		real32 gint = gi(xp);
		sprof[x] = 255 - int32(255.f * gint);
	}
	// unstable here!
	sprof[0] = 255;

	return 0;
}

char line[2048];
static char GetLine() {
	if (fgets(line, 2048, in)) {
		return line[0];
	} else {
		return 0;
	}
}

static int32 Initialize() 
{
	uint32 ignoreme;

	// try to parse PPM
	GetLine();	if (line[0] != 'P' || line[1] != '6') return -1;
	while (GetLine() == '#') {}
	if (sscanf(line, "%d %d\n", &width, &height) != 2) return -1;
	while (GetLine() == '#') {}
	if (sscanf(line, "%d\n", &ignoreme) != 1) return -1;
	if (ignoreme != 255) return -1;

	rgbbuf = new uint8[width * 3];
	if (!rgbbuf) return -1;

	// setup shadow based on width, height
	int32 sl = int(-srad * 1.5) + xoff;
	int32 sr = int( srad * 1.5 + width + 0.9999) + xoff;
	int32 st = int(-srad * 1.5) + yoff;
	int32 sb = int( srad * 1.5 + height + 0.9999) + yoff;

	if (sl < 0) {
		xbitmap = -sl;
		sr += xbitmap;
		sl = 0;
	}
	if (st < 0) {
		ybitmap = -st;
		sb += ybitmap;
		st = 0;
	}

	int32 maxx = sr - sl;
	int32 maxy = sb - st;
	int32 bitx = width + xbitmap;
	int32 bity = height + ybitmap;

	if (bitx > maxx) maxx = bitx;
	if (bity > maxy) maxy = bity;

	outwidth = maxx;
	outheight = maxy;

	printf("P6\n# Not created by the GIMP\n%d %d\n255\n", outwidth, outheight);

	// find top quadrant of shadow * 2 (.1 fixed point)
	topql = xoff + xbitmap;
	topqr = width + xoff + xbitmap;
	topqt = yoff + ybitmap;
	topqb = height + yoff + ybitmap;

	topqb  = topqt + topqb;
	topqt += topqt;

	topqr  = topql + topqr;
	topql += topql;

	// we've computed xbitmap, ybitmap, outwidth, outheight
	return UpdateProfile();
}

// ================================================================================================
// multiply with black
// ================================================================================================
static inline void PixelDarken(uint32 &d, const uint32 amt)
{
	uint32 rb = d & 0xFF00FF;
	uint32 g  = d & 0x00FF00;
	d &= 0xFF000000;

	rb = rb * amt & 0xFF00FF00;
	g  = g  * amt & 0x00FF0000;

	rb |= g;
	d  |= (rb >> 8);
}

// scan from x0 to x1 using outputY as the scanline
int32 ScanAbs(int32 x0, int32 x1)
{
	if (x1 <= x0) return 0;

	int32 maxi = sprofsize - 1;

	int32 filll2 = x0 * 2;
	int32 fillr2 = x1 * 2;

	int32 center = (sprofsize & ~1) - 1;//(sprof.Used() & ~1) - 1;

	int32 w = topqr - topql - center;
	int32 h = topqb - topqt - center;

	// scan
	int32 y = outputY;

	int32 dy = abs(y * 2 - topqb) - h;
	int32 oy = dy >> 1;
	if (oy < 0) oy = 0;
	
	uint32 sh0 = sprof[oy];

	sh0 = sh0 * opac >> 8;

	//uint32 *dp0 = dst.Pixel(x0, y);
	for (int32 x = filll2; x < fillr2; x += 2) {

		int32 dx = abs(x - topqr) - w;
		int32 ox = dx >> 1;
		if (ox < 0) ox = 0;

		uint32 sh = 256 - ((sprof[ox]) * sh0 >> 8);
		//uint32 sh = (sprof[ox]) * sh0 >> 8;
		
		uint32 dst = bg;
		PixelDarken(dst, sh);
		putchar(dst >> 16 & 0xFF);
		putchar(dst >>  8 & 0xFF);
		putchar(dst       & 0xFF);
	}

	return 0;
}

static int32 ProcessLine()
{
	// write a line for "outputY"
	if (outputY < ybitmap || outputY >= (ybitmap + height)) {
		// "write"
		// write a whole line of shadow
		ScanAbs(0, outwidth);
	} else {
		// "read-write"
		// clip and write a line of the input bitmap
		ScanAbs(0, xbitmap);

		// in case read fails, fill buffer with bg color
		uint8 *rgb = rgbbuf;
		for (int32 i = 0; i < width; i++) {
			rgb[0] = bg >> 16 & 0xFF;
			rgb[1] = bg >>  8 & 0xFF;
			rgb[2] = bg       & 0xFF;
			rgb += 3;
		}
		fread(rgbbuf, 3, width, in);
		fwrite(rgbbuf, 3, width, stdout);
		ScanAbs(xbitmap + width, outwidth);
	}

	outputY ++;

	return 0;
}


// shadowrect main:
//   give a background color ("#FFFFFF")
//   give a shadow radius (5.1)
//   give a shadow X offset (2)
//   give a shadow Y offset (3)
//   give an opacity (0.6)

int main(int argc, char **argv)
{
	in = fopen("test.ppm", "rb");

	if (argc < 1) { 
		printf("Usage: %s [background] [radius] [xoffset] [yoffset] [%opacity]\x13", argv[0]);
		return -1;
	}

	switch (argc) {
	case 6:
		opac = uint32(2.56 * atof(argv[5]));
	case 5:
		yoff = atoi(argv[4]);
	case 4:
		xoff = atoi(argv[3]);
	case 3:
		srad = atof(argv[2]);
	case 2:
	  	if (argv[1][0] == '#') argv[1]++;
		bg = strtol(argv[1], NULL, 16);
	}

	if (Initialize() != 0) {
		fprintf(stderr, "Input is not a valid PPM\n");
		return -1;	// format problem
	}
	
	while (outputY < outheight) {
		ProcessLine();
	}

	if (rgbbuf) delete rgbbuf; rgbbuf = NULL;
	if (sprof) delete sprof; sprof = NULL;

	return 0;
}

