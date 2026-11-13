/* mkref.c - Build arithmetic JPEGs and coefficient-identical Huffman twins.
 *
 * Uses only the system libjpeg-turbo. For every input PPM/PGM it emits:
 *   <out>.arith.jpg   : arithmetic coded (SOF9), requested subsampling/restart
 *   <out>.huff.jpg    : transcoded from the arithmetic file via
 *                       jpeg_read_coefficients/jpeg_write_coefficients, so the
 *                       DCT coefficients are bit-identical; only the entropy
 *                       coding differs.
 *
 * Usage: mkref input.ppm outprefix subsampling(0=444,1=422,2=420,3=gray) restart_rows
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <jpeglib.h>

struct err_mgr { struct jpeg_error_mgr pub; jmp_buf jb; };
static void err_exit(j_common_ptr ci) {
    struct err_mgr* e = (struct err_mgr*)ci->err;
    (*ci->err->output_message)(ci);
    longjmp(e->jb, 1);
}

static int read_pnm(const char* path, unsigned char** buf, int* w, int* h, int* comps) {
    FILE* f = fopen(path, "rb");
    if (!f) { perror(path); return 0; }
    char magic[3] = {0};
    if (fread(magic, 1, 2, f) != 2) { fclose(f); return 0; }
    int c, ww, hh, mv;
    /* skip comments/whitespace and read 3 (P6) or 2 (P5) integers then maxval */
    int ints_needed = magic[1] == '6' ? 3 : 3;
    int vals[3], n = 0;
    while (n < ints_needed) {
        do { c = fgetc(f); } while (c != EOF && (c == ' ' || c == '\t' || c == '\n' || c == '\r'));
        if (c == '#') { while (c != '\n' && c != EOF) c = fgetc(f); continue; }
        ungetc(c, f);
        if (fscanf(f, "%d", &vals[n]) != 1) { fclose(f); return 0; }
        n++;
    }
    ww = vals[0]; hh = vals[1]; mv = vals[2];
    (void)mv;
    fgetc(f); /* single whitespace after maxval */
    *comps = (magic[1] == '6') ? 3 : 1;
    size_t sz = (size_t)ww * hh * *comps;
    unsigned char* b = malloc(sz);
    if (fread(b, 1, sz, f) != sz) { free(b); fclose(f); return 0; }
    fclose(f);
    *buf = b; *w = ww; *h = hh;
    return 1;
}

static struct jpeg_error_mgr* setup_err(struct jpeg_compress_struct* c, struct err_mgr* e) {
    c->err = jpeg_std_error(&e->pub); e->pub.error_exit = err_exit;
    if (setjmp(e->jb)) return NULL;
    return &e->pub;
}

int main(int argc, char** argv) {
    if (argc != 5) { fprintf(stderr, "usage: %s in.ppm outprefix subsamp restart\n", argv[0]); return 2; }
    const char* in = argv[1]; const char* prefix = argv[2];
    int subsamp = atoi(argv[3]); int restart = atoi(argv[4]);

    unsigned char* img; int w, h, comps;
    if (!read_pnm(in, &img, &w, &h, &comps)) return 2;

    char name[512];
    /* 1. arithmetic JPEG */
    snprintf(name, sizeof name, "%s.arith.jpg", prefix);
    struct jpeg_compress_struct cc; struct err_mgr ce;
    if (!setup_err(&cc, &ce)) return 1;
    jpeg_create_compress(&cc);
    FILE* fo = fopen(name, "wb"); jpeg_stdio_dest(&cc, fo);
    cc.image_width = w; cc.image_height = h;
    cc.input_components = comps;
    cc.in_color_space = comps == 3 ? JCS_RGB : JCS_GRAYSCALE;
    jpeg_set_defaults(&cc);
    jpeg_set_quality(&cc, 90, TRUE);
    cc.arith_code = TRUE;
    if (comps == 3) {
        if (subsamp == 0) { cc.comp_info[0].h_samp_factor = 1; cc.comp_info[0].v_samp_factor = 1; }
        else if (subsamp == 1) { cc.comp_info[0].h_samp_factor = 2; cc.comp_info[0].v_samp_factor = 1; }
        else { cc.comp_info[0].h_samp_factor = 2; cc.comp_info[0].v_samp_factor = 2; }
    }
    if (restart > 0) { cc.restart_in_rows = restart; cc.write_JFIF_header = TRUE; }
    jpeg_start_compress(&cc, TRUE);
    JSAMPROW row;
    while (cc.next_scanline < cc.image_height) {
        row = img + (size_t)cc.next_scanline * w * comps;
        jpeg_write_scanlines(&cc, &row, 1);
    }
    jpeg_finish_compress(&cc);
    fclose(fo);
    jpeg_destroy_compress(&cc);
    fprintf(stderr, "wrote %s\n", name);

    /* 2. coefficient-identical Huffman twin via transcoding */
    snprintf(name, sizeof name, "%s.huff.jpg", prefix);
    struct jpeg_decompress_struct dinfo; struct err_mgr de;
    dinfo.err = jpeg_std_error(&de.pub); de.pub.error_exit = err_exit;
    if (setjmp(de.jb)) return 1;
    jpeg_create_decompress(&dinfo);
    char an[512]; snprintf(an, sizeof an, "%s.arith.jpg", prefix);
    FILE* fi = fopen(an, "rb");
    jpeg_stdio_src(&dinfo, fi);
    jpeg_read_header(&dinfo, TRUE);
    jvirt_barray_ptr* coeffs = jpeg_read_coefficients(&dinfo);

    struct jpeg_compress_struct cout; struct err_mgr ce2;
    cout.err = jpeg_std_error(&ce2.pub); ce2.pub.error_exit = err_exit;
    if (setjmp(ce2.jb)) return 1;
    jpeg_create_compress(&cout);
    FILE* fo2 = fopen(name, "wb"); jpeg_stdio_dest(&cout, fo2);
    jpeg_copy_critical_parameters(&dinfo, &cout);
    cout.arith_code = FALSE;
    jpeg_write_coefficients(&cout, coeffs);
    jpeg_finish_compress(&cout);
    fclose(fo2);
    jpeg_destroy_compress(&cout);
    jpeg_finish_decompress(&dinfo);
    fclose(fi);
    jpeg_destroy_decompress(&dinfo);
    fprintf(stderr, "wrote %s\n", name);

    free(img);
    return 0;
}
