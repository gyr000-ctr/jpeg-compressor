// jpegref.c - Reference JPEG encoder/decoder using system libjpeg(-turbo).
// Used to generate arithmetic-coded test JPEGs and to produce reference
// decodes for comparison against jpgd.
//
// Usage:
//   jpegref enc <in.ppm|in.pgm> <out.jpg> <quality> <arith 0|1> <hsamp> <vsamp> <restart_mcus>
//   jpegref dec <in.jpg> <out.rgba>     (writes raw RGBA, prints "WxH" to stdout)
//
// Run dec mode with JSIMD_FORCENONE=1 to force the C IDCT (deterministic).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>

static unsigned char* read_pnm(const char* fn, int* w, int* h, int* comps)
{
    FILE* f = fopen(fn, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", fn); exit(1); }
    int c, maxval;
    // parse magic
    char magic[3] = { 0, 0, 0 };
    magic[0] = (char)fgetc(f); magic[1] = (char)fgetc(f);
    if (magic[0] != 'P' || (magic[1] != '6' && magic[1] != '5')) { fprintf(stderr, "not a P6/P5 file\n"); exit(1); }
    *comps = (magic[1] == '6') ? 3 : 1;
    // read ints, skipping whitespace/comments
    int vals[3], nv = 0;
    while (nv < 3)
    {
        c = fgetc(f);
        while (c == ' ' || c == '\t' || c == '\r' || c == '\n') c = fgetc(f);
        if (c == '#') { while (c != '\n' && c != EOF) c = fgetc(f); continue; }
        ungetc(c, f);
        if (fscanf(f, "%d", &vals[nv]) != 1) { fprintf(stderr, "bad pnm header\n"); exit(1); }
        nv++;
    }
    fgetc(f); // single whitespace after maxval
    *w = vals[0]; *h = vals[1]; maxval = vals[2];
    if (maxval != 255) { fprintf(stderr, "maxval must be 255\n"); exit(1); }
    size_t sz = (size_t)(*w) * (*h) * (*comps);
    unsigned char* buf = (unsigned char*)malloc(sz);
    if (fread(buf, 1, sz, f) != sz) { fprintf(stderr, "short pnm data\n"); exit(1); }
    fclose(f);
    return buf;
}

static int do_enc(const char* in_fn, const char* out_fn, int quality, int arith, int hsamp, int vsamp, int restart)
{
    int w, h, comps;
    unsigned char* img = read_pnm(in_fn, &w, &h, &comps);

    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    FILE* f = fopen(out_fn, "wb");
    if (!f) { fprintf(stderr, "cannot create %s\n", out_fn); return 1; }
    jpeg_stdio_dest(&cinfo, f);

    cinfo.image_width = w;
    cinfo.image_height = h;
    cinfo.input_components = comps;
    cinfo.in_color_space = (comps == 3) ? JCS_RGB : JCS_GRAYSCALE;

    jpeg_set_defaults(&cinfo);
    if (arith)
        cinfo.arith_code = TRUE;
    jpeg_set_quality(&cinfo, quality, TRUE);

    if (comps == 3)
    {
        cinfo.comp_info[0].h_samp_factor = hsamp;
        cinfo.comp_info[0].v_samp_factor = vsamp;
        cinfo.comp_info[1].h_samp_factor = 1;
        cinfo.comp_info[1].v_samp_factor = 1;
        cinfo.comp_info[2].h_samp_factor = 1;
        cinfo.comp_info[2].v_samp_factor = 1;
    }

    if (restart > 0)
        cinfo.restart_interval = restart;

    if (getenv("JPEGREF_PROGRESSIVE") && atoi(getenv("JPEGREF_PROGRESSIVE")))
        jpeg_simple_progression(&cinfo);
    if (getenv("JPEGREF_OPTIMIZE") && atoi(getenv("JPEGREF_OPTIMIZE")))
        cinfo.optimize_coding = TRUE;

    jpeg_start_compress(&cinfo, TRUE);
    while (cinfo.next_scanline < cinfo.image_height)
    {
        JSAMPROW row = img + (size_t)cinfo.next_scanline * w * comps;
        jpeg_write_scanlines(&cinfo, &row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    fclose(f);
    free(img);
    return 0;
}

static int do_dec(const char* in_fn, const char* out_fn)
{
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    FILE* f = fopen(in_fn, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", in_fn); return 1; }
    jpeg_stdio_src(&cinfo, f);

    jpeg_read_header(&cinfo, TRUE);

    cinfo.out_color_space = JCS_EXT_RGBA;
    cinfo.dct_method = JDCT_ISLOW;
    cinfo.do_fancy_upsampling = FALSE;

    jpeg_start_decompress(&cinfo);

    int w = cinfo.output_width, h = cinfo.output_height;
    unsigned char* img = (unsigned char*)malloc((size_t)w * h * 4);
    while (cinfo.output_scanline < cinfo.output_height)
    {
        JSAMPROW row = img + (size_t)cinfo.output_scanline * w * 4;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(f);

    FILE* o = fopen(out_fn, "wb");
    if (!o) { fprintf(stderr, "cannot create %s\n", out_fn); return 1; }
    fwrite(img, 1, (size_t)w * h * 4, o);
    fclose(o);

    printf("%dx%d\n", w, h);
    free(img);
    return 0;
}

int main(int argc, char** argv)
{
    if (argc >= 2 && !strcmp(argv[1], "enc"))
    {
        if (argc != 9)
        {
            fprintf(stderr, "usage: jpegref enc in.ppm out.jpg quality arith hsamp vsamp restart\n");
            return 1;
        }
        return do_enc(argv[2], argv[3], atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), atoi(argv[7]), atoi(argv[8]));
    }
    if (argc >= 2 && !strcmp(argv[1], "dec"))
    {
        if (argc != 4)
        {
            fprintf(stderr, "usage: jpegref dec in.jpg out.rgba\n");
            return 1;
        }
        return do_dec(argv[2], argv[3]);
    }
    fprintf(stderr, "usage: jpegref {enc|dec} ...\n");
    return 1;
}
