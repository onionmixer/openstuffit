#include "openstuffit_fr_bridge_json.h"

void ost_fr_json_print_string(FILE *out, const char *text) {
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    fputc('"', out);
    while (*p) {
        unsigned char c = *p++;
        switch (c) {
            case '"':
                fputs("\\\"", out);
                break;
            case '\\':
                fputs("\\\\", out);
                break;
            case '\b':
                fputs("\\b", out);
                break;
            case '\f':
                fputs("\\f", out);
                break;
            case '\n':
                fputs("\\n", out);
                break;
            case '\r':
                fputs("\\r", out);
                break;
            case '\t':
                fputs("\\t", out);
                break;
            default:
                if (c < 0x20u) fprintf(out, "\\u%04x", (unsigned)c);
                else fputc((int)c, out);
                break;
        }
    }
    fputc('"', out);
}
