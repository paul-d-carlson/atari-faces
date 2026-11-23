/*
 * dbgcov.c -- see dbgcov.h.
 *
 * cc65's ld65 --dbgfile format is a flat text file, one record per line,
 * of the form:
 *
 *   type    key=value,key=value,...
 *
 * where type is one of "version", "info", "csym", "file", "lib", "line",
 * "mod", "scope", "seg", "span", "sym", "type". String values are
 * double-quoted (e.g. name="../src/buffer.c"); everything else is a
 * plain decimal or 0x-prefixed hex integer.
 *
 * We only care about four record types:
 *
 *   file id=F,name="...",...       -- source file table
 *   seg  id=S,start=0xNNNN,...     -- segment base addresses
 *   span id=P,seg=S,start=O,size=N -- a contiguous range of generated
 *                                      code/data; O is an OFFSET within
 *                                      segment S, so the absolute address
 *                                      range is [seg[S].start + O, +N)
 *   line id=L,file=F,line=N,span=P -- a source line, plus the span of
 *                                      code generated for it (absent
 *                                      "span=" means no code was
 *                                      generated for that line)
 *
 * Line records reference spans/segs by id, but (empirically) appear
 * *before* the seg/span records in the file, so we make two passes over
 * the buffered file contents: first to populate the file/seg/span
 * tables, then to resolve each line record's address range and check it
 * against the caller-supplied execution-hit bitmap.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dbgcov.h"

#define MAX_FILES 4096
#define MAX_SEGS    256
#define MAX_SPANS 131072

typedef struct {
    int  valid;
    char name[512];
} file_rec_t;

typedef struct {
    int           valid;
    unsigned long start;
} seg_rec_t;

typedef struct {
    int           valid;
    int           seg;
    unsigned long start;   /* offset within segment */
    unsigned long size;
} span_rec_t;

static file_rec_t *g_files;
static seg_rec_t  *g_segs;
static span_rec_t *g_spans;

/* True if `key=` appears in rec at a proper field boundary (start of
 * record, or right after a ',' / space / tab). */
static int field_present(const char *rec, const char *key)
{
    const char *p = rec;
    size_t keylen = strlen(key);

    for (;;) {
        p = strstr(p, key);
        if (!p)
            return 0;
        if ((p == rec || p[-1] == ',' || p[-1] == ' ' || p[-1] == '\t') &&
            p[keylen] == '=')
            return 1;
        p += keylen;
    }
}

static unsigned long field_num(const char *rec, const char *key)
{
    const char *p = rec;
    size_t keylen = strlen(key);

    for (;;) {
        p = strstr(p, key);
        if (!p)
            return 0;
        if ((p == rec || p[-1] == ',' || p[-1] == ' ' || p[-1] == '\t') &&
            p[keylen] == '=')
            return strtoul(p + keylen + 1, NULL, 0);
        p += keylen;
    }
}

static void field_str(const char *rec, const char *key, char *out, size_t outsz)
{
    const char *p = rec;
    size_t keylen = strlen(key);

    out[0] = '\0';
    for (;;) {
        p = strstr(p, key);
        if (!p)
            return;
        if ((p == rec || p[-1] == ',' || p[-1] == ' ' || p[-1] == '\t') &&
            p[keylen] == '=' && p[keylen + 1] == '"') {
            const char *start = p + keylen + 2;
            const char *end = strchr(start, '"');
            size_t len;

            if (!end)
                return;
            len = (size_t)(end - start);
            if (len >= outsz)
                len = outsz - 1;
            memcpy(out, start, len);
            out[len] = '\0';
            return;
        }
        p += keylen;
    }
}

static int record_is(const char *rec, const char *type)
{
    size_t len = strlen(type);

    return strncmp(rec, type, len) == 0 &&
           (rec[len] == ' ' || rec[len] == '\t');
}

typedef void (*record_cb_t)(const char *rec, void *ctx);

/* Walks buf (size bytes, not necessarily NUL-terminated at [size]) one
 * line at a time, copying each line into a bounded stack buffer (so the
 * original buffer is never modified -- we need to scan it twice). */
static void for_each_record(const char *buf, long size, record_cb_t cb, void *ctx)
{
    const char *p = buf;
    const char *end = buf + size;
    char linebuf[4096];

    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);

        if (len > 0) {
            if (len >= sizeof(linebuf))
                len = sizeof(linebuf) - 1;
            memcpy(linebuf, p, len);
            linebuf[len] = '\0';
            cb(linebuf, ctx);
        }
        if (!nl)
            break;
        p = nl + 1;
    }
}

static void pass1_cb(const char *rec, void *ctx)
{
    (void)ctx;

    if (record_is(rec, "file")) {
        unsigned long id = field_num(rec, "id");
        if (id < MAX_FILES) {
            field_str(rec, "name", g_files[id].name, sizeof(g_files[id].name));
            g_files[id].valid = 1;
        }
    } else if (record_is(rec, "seg")) {
        unsigned long id = field_num(rec, "id");
        if (id < MAX_SEGS) {
            g_segs[id].start = field_num(rec, "start");
            g_segs[id].valid = 1;
        }
    } else if (record_is(rec, "span")) {
        unsigned long id = field_num(rec, "id");
        if (id < MAX_SPANS) {
            g_spans[id].seg   = (int)field_num(rec, "seg");
            g_spans[id].start = field_num(rec, "start");
            g_spans[id].size  = field_num(rec, "size");
            g_spans[id].valid = 1;
        }
    }
}

typedef struct {
    int           file_id;   /* resolved dbg file id, -1 if unresolved */
    unsigned char coverable[DBGCOV_MAX_LINE];
    unsigned char covered[DBGCOV_MAX_LINE];
} target_work_t;

typedef struct {
    target_work_t       *work;
    int                   count;
    const unsigned char  *hit;
} pass2_ctx_t;

static void pass2_cb(const char *rec, void *ctx_v)
{
    pass2_ctx_t *ctx = (pass2_ctx_t *)ctx_v;
    unsigned long file_id, line_no, span_id;
    int i;

    if (!record_is(rec, "line"))
        return;
    if (!field_present(rec, "span"))
        return; /* no code generated for this source line */

    file_id = field_num(rec, "file");
    line_no = field_num(rec, "line");
    span_id = field_num(rec, "span");

    if (line_no >= DBGCOV_MAX_LINE || span_id >= MAX_SPANS || !g_spans[span_id].valid)
        return;

    for (i = 0; i < ctx->count; i++) {
        target_work_t *w = &ctx->work[i];
        span_rec_t *sp = &g_spans[span_id];
        seg_rec_t *sg;
        unsigned long addr, a;

        if (w->file_id < 0 || (unsigned long)w->file_id != file_id)
            continue;
        if (sp->size == 0)
            continue;
        if (sp->seg < 0 || sp->seg >= MAX_SEGS || !g_segs[sp->seg].valid)
            continue;
        sg = &g_segs[sp->seg];

        w->coverable[line_no] = 1;
        addr = sg->start + sp->start;
        for (a = 0; a < sp->size; a++) {
            unsigned long byte_addr = addr + a;
            if (byte_addr < 65536 && ctx->hit[byte_addr]) {
                w->covered[line_no] = 1;
                break;
            }
        }
    }
}

int dbgcov_analyze(const char *dbg_path, dbgcov_target_t *targets,
                    int num_targets, const unsigned char hit[65536])
{
    FILE *f;
    long size;
    char *buf;
    target_work_t *work;
    pass2_ctx_t ctx;
    int i, ret = 0;

    g_files = calloc(MAX_FILES, sizeof(*g_files));
    g_segs  = calloc(MAX_SEGS, sizeof(*g_segs));
    g_spans = calloc(MAX_SPANS, sizeof(*g_spans));
    work    = calloc((size_t)num_targets, sizeof(*work));
    if (!g_files || !g_segs || !g_spans || !work) {
        ret = -1;
        goto out;
    }

    f = fopen(dbg_path, "rb");
    if (!f) {
        ret = -1;
        goto out;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);
    buf = malloc((size_t)size + 1);
    if (!buf || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        ret = -1;
        goto out;
    }
    buf[size] = '\0';
    fclose(f);

    for_each_record(buf, size, pass1_cb, NULL);

    for (i = 0; i < num_targets; i++) {
        int fid = -1, j;
        size_t suflen = strlen(targets[i].suffix);

        for (j = 0; j < MAX_FILES; j++) {
            size_t namelen;

            if (!g_files[j].valid)
                continue;
            namelen = strlen(g_files[j].name);
            if (namelen >= suflen &&
                strcmp(g_files[j].name + namelen - suflen, targets[i].suffix) == 0) {
                fid = j;
                break;
            }
        }
        work[i].file_id = fid;
    }

    ctx.work = work;
    ctx.count = num_targets;
    ctx.hit = hit;
    for_each_record(buf, size, pass2_cb, &ctx);

    for (i = 0; i < num_targets; i++) {
        unsigned int coverable = 0, covered = 0;
        int ln;

        for (ln = 0; ln < DBGCOV_MAX_LINE; ln++) {
            if (work[i].coverable[ln]) {
                coverable++;
                if (work[i].covered[ln])
                    covered++;
                else if (targets[i].verbose)
                    printf("  %s:%d NOT COVERED\n", targets[i].label, ln);
            }
        }
        targets[i].coverable_lines = coverable;
        targets[i].covered_lines = covered;
    }

    free(buf);

out:
    free(g_files);
    free(g_segs);
    free(g_spans);
    free(work);
    return ret;
}
