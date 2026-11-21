/* pmimgtest.c - pmimg in colour (opts->colour, EPOC's 256 colours): every
 * index valid, the palette EPOC's, the picture's mean colour kept, a
 * progressive JPEG refused unless prog_colour asks, and then decoded in
 * colour (fuzzed too); and the 16 greys still as they were for the
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
	/* pmimg_opts_room: half of what is free above 1.5 MB, at least 64 KB */
	{
		PmImgOpts r;
		pmimg_opts_fill(&r, 0);
		pmimg_opts_room(&r, 0);
		CHECK(r.max_full_bytes == 640L * 1024L);
		pmimg_opts_room(&r, 1536L * 1024L + 400L * 1024L);
		CHECK(r.max_full_bytes == 200L * 1024L);
		pmimg_opts_room(&r, 512L * 1024L);
		CHECK(r.max_full_bytes == 64L * 1024L);
		pmimg_opts_fill(&r, 0);
		pmimg_opts_room(&r, 20L * 1024L * 1024L);
		CHECK(r.max_full_bytes == 640L * 1024L);
	}
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
	/* (prog_colour) progressive JPEGs in colour: the mean colour kept as
	   the baseline's is; a grey one in greys of the palette; CMYK refused;
	   cut short or damaged, never a crash (the sanitizers watch) */
	{
		const char *pn[] = { "cp.jpg", "cp444.jpg", "cprst.jpg", "cpg.jpg", 0 };
		static unsigned char data[1 << 20];
		PmImgOpts o;
		PmImage im;
		char err[64];
		int m[3], k, j;
		long len;
		for (i = 0; pn[i]; i++) {
			memset(&o, 0, sizeof(o));
			o.max_w = 300; o.max_h = 900; o.exact = 1; o.colour = 1; o.prog_colour = 1;
			snprintf(path, sizeof(path), "%s/%s", dir, pn[i]);
			CHECK(pmimg_decode_file(path, &o, &im, err, sizeof(err)) == PMIMG_OK);
			CHECK(im.colour == 1 && im.w == 300 && im.h == 200 && im.stride == 300);
			mean_rgb(&im, m);
			printf("%-9s progressive colour %dx%d, mean %d %d %d\n", pn[i], im.w, im.h, m[0], m[1], m[2]);
			if (strcmp(pn[i], "cpg.jpg")) { for (k = 0; k < 3; k++) CHECK(abs(m[k] - m0[k]) <= 10); }
			else CHECK(abs(m[0] - m[1]) <= 6 && abs(m[1] - m[2]) <= 6);
			pmimg_free(&im);
			/* at 1/2, 1/4 and DC alone (the memory limit forcing it) */
			for (k = 0; k < 3; k++) {
				o.exact = 0; o.max_w = k == 0 ? 300 : 75; o.max_full_bytes = k == 2 ? 6000 : 0;
				if (pmimg_decode_file(path, &o, &im, err, sizeof(err)) == PMIMG_OK) {
					mean_rgb(&im, m);
					if (strcmp(pn[i], "cpg.jpg")) for (j = 0; j < 3; j++) CHECK(abs(m[j] - m0[j]) <= 14);
					pmimg_free(&im);
				} else CHECK(k == 2);
			}
		}
		memset(&o, 0, sizeof(o));
		o.max_w = 300; o.colour = 1; o.prog_colour = 1;
		snprintf(path, sizeof(path), "%s/cpk.jpg", dir);
		CHECK(pmimg_decode_file(path, &o, &im, err, sizeof(err)) == PMIMG_E_UNSUPPORTED);
		/* damaged: cut at many places, bytes changed */
		snprintf(path, sizeof(path), "%s/cp.jpg", dir);
		{
			FILE *pf = fopen(path, "rb");
			len = pf ? (long)fread(data, 1, sizeof(data), pf) : 0;
			if (pf) fclose(pf);
		}
		CHECK(len > 1000);
		srand(5);
		for (k = 0; k < 200 && len > 1000; k++) {
			static unsigned char bad[1 << 20];
			long n = len, p;
			memcpy(bad, data, len);
			if (k % 2) n = 200 + rand() % (len - 200);
			for (j = 0; j < 1 + k % 8; j++) { p = 2 + rand() % (len - 2); bad[p] = (unsigned char)rand(); }
			memset(&o, 0, sizeof(o));
			o.max_w = 120 + k; o.colour = 1; o.prog_colour = 1;
			if (pmimg_decode_mem(bad, n, &o, &im, err, sizeof(err)) == PMIMG_OK) {
				CHECK(im.w > 0 && im.w <= o.max_w);
				pmimg_free(&im);
			}
		}
	}
	printf("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
