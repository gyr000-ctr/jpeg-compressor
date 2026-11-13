// dbgdec.cpp - debug driver: decode a JPEG with jpgd and print the error code.
#include "jpgd.h"
#include <stdio.h>

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: dbgdec file.jpg\n"); return 1; }
    jpgd::jpeg_decoder_file_stream stream;
    if (!stream.open(argv[1])) { printf("cannot open\n"); return 1; }
    jpgd::jpeg_decoder dec(&stream);
    printf("after ctor: error_code=%d\n", (int)dec.get_error_code());
    if (dec.get_error_code() != jpgd::JPGD_SUCCESS) return 1;
    printf("size %dx%d comps %d\n", dec.get_width(), dec.get_height(), dec.get_num_components());
    int st = dec.begin_decoding();
    printf("begin_decoding: %d error_code=%d\n", st, (int)dec.get_error_code());
    if (st != jpgd::JPGD_SUCCESS) return 1;
    int lines = 0;
    for (;;)
    {
        const void* p; unsigned int len;
        st = dec.decode(&p, &len);
        if (st == jpgd::JPGD_DONE) break;
        if (st != jpgd::JPGD_SUCCESS) { printf("decode failed at line %d: error_code=%d\n", lines, (int)dec.get_error_code()); return 1; }
        lines++;
    }
    printf("OK, %d lines decoded\n", lines);
    return 0;
}
