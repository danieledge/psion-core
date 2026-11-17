/* pmimgtest.c - pmimg in colour (opts->colour, EPOC's 256 colours): every
 * index valid, the palette EPOC's, the picture's mean colour kept, a
 * progressive JPEG refused; and the 16 greys still as they were for the
 * same pictures (sizes, two pixels a byte).
 *
 *   make -C mail/engine/img/test        (needs python3 with Pillow, zlib)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pmimg.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static void mean_rgb(const PmImage *im, int m[3])
{
	const unsigned char *pal = pmimg_epoc256();
	long s[3] = { 0, 0, 0 };
	int x, y, c;
	for (y = 0; y < im->h; y++)
		for (x = 0; x < im->w; x++)
			for (c = 0; c < 3; c++) s[c] += pal[3 * im->bits[y * im->stride + x] + c];
	for (c = 0; c < 3; c++) m[c] = (int)(s[c] / ((long)im->w * im->h));
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "pics";
	const char *names[] = { "c.jpg", "c.png", "ci.png", "c.gif", "ca.png", 0 };
	const unsigned char *pal = pmimg_epoc256();
	char path[256];
	int i, m0[3] = { 0, 0, 0 };
	FILE *f;
	snprintf(path, sizeof(path), "%s/mean.txt", dir);
	f = fopen(path, "r");
	if (!f || fscanf(f, "%d %d %d", &m0[0], &m0[1], &m0[2]) != 3) { printf("no %s: run mkpics.py\n", path); return 1; }
	fclose(f);
	/* the palette: EPOC's (spot checks: TRgb::Color256's 0, 1, 108, 128, 255) */
	CHECK(pal[0] == 0 && pal[1] == 0 && pal[2] == 0);
	CHECK(pal[3] == 0x33 && pal[4] == 0 && pal[5] == 0);
	CHECK(pal[3 * 108] == 0x11 && pal[3 * 108 + 1] == 0x11 && pal[3 * 108 + 2] == 0x11);
	CHECK(pal[3 * 128] == 0 && pal[3 * 128 + 1] == 0 && pal[3 * 128 + 2] == 0x88);
	CHECK(pal[3 * 255] == 255 && pal[3 * 255 + 1] == 255 && pal[3 * 255 + 2] == 255);
	for (i = 0; names[i]; i++) {
		PmImgOpts o;
		PmImage im, g;
		char err[64];
		int r, m[3], n = 0, used[256], k;
		memset(&o, 0, sizeof(o));
		o.max_w = 300; o.max_h = 900; o.exact = 1; o.colour = 1;
		o.max_full_bytes = 2L * 1024 * 1024;   /* (Pillow writes GIFs interlaced: kept whole, 3 bytes a pixel) */
		snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
		r = pmimg_decode_file(path, &o, &im, err, sizeof(err));
		CHECK(r == PMIMG_OK);
		if (r) { printf("%s: %s\n", names[i], err); continue; }
		CHECK(im.colour == 1 && im.w == 300 && im.h == 200 && im.stride == 300);
		mean_rgb(&im, m);
		memset(used, 0, sizeof(used));
		for (k = 0; k < im.stride * im.h; k++) used[im.bits[k]] = 1;
		for (k = 0; k < 256; k++) n += used[k];
		printf("%-7s colour %dx%d, mean %d %d %d (source %d %d %d), %d colours\n", names[i], im.w, im.h,
			m[0], m[1], m[2], m0[0], m0[1], m0[2], n);
		if (strcmp(names[i], "ca.png")) {
			for (k = 0; k < 3; k++) CHECK(abs(m[k] - m0[k]) <= 10);
		} else
			for (k = 0; k < 3; k++) CHECK(m[k] > m0[k]);   /* half transparent: over white */
		CHECK(n > 16);
		pmimg_free(&im);
		/* the same in 16 greys: as always */
		o.colour = 0;
		r = pmimg_decode_file(path, &o, &g, err, sizeof(err));
		CHECK(r == PMIMG_OK && g.colour == 0 && g.w == 300 && g.stride == 152);
		pmimg_free(&g);
	}
	/* a progressive JPEG: refused in colour, decoded in greys */
	{
		PmImgOpts o;
		PmImage im;
		char err[64];
		memset(&o, 0, sizeof(o));
		o.max_w = 300; o.colour = 1;
		snprintf(path, sizeof(path), "%s/cp.jpg", dir);
		CHECK(pmimg_decode_file(path, &o, &im, err, sizeof(err)) == PMIMG_E_UNSUPPORTED);
		o.colour = 0;
		CHECK(pmimg_decode_file(path, &o, &im, err, sizeof(err)) == PMIMG_OK);
		pmimg_free(&im);
	}
	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
