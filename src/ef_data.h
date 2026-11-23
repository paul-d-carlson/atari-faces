/*
 * ef_data.h -- parse the FACES.DAT format written by tools/export_faces.py.
 *
 *   0  'E' 'F'   magic
 *   2  1         version
 *   3  n_train   1..EF_MAX_FACES
 *   4  n_query   0..EFD_MAX_QUERY
 *   5  24 24 64  width, height, gray levels
 *   8  {label, photo} per training face, then per query face
 *      (label = Olivetti subject number 0..39, i.e. who is in the photo;
 *       photo = which of that subject's ten photos, 0..9)
 *   .. n_train * 576 pixel bytes, then n_query * 576, values 0..63
 *
 * The caller reads the file in pieces (so faces can be drawn as they
 * arrive) and hands each piece to the matching function here.
 */
#ifndef EF_DATA_H
#define EF_DATA_H

#include "types.h"
#include "ef_model.h"

#define EFD_HEADER_LEN  8u
#define EFD_MAX_QUERY   5u
#define EFD_MAX_TOTAL   (EF_MAX_FACES + EFD_MAX_QUERY)
#define EFD_VERSION     1u

#define EFD_OK          0u
#define EFD_BAD_MAGIC   1u
#define EFD_BAD_VERSION 2u
#define EFD_BAD_SIZE    3u
#define EFD_BAD_COUNT   4u
#define EFD_BAD_META    5u
#define EFD_BAD_PIXEL   6u

typedef struct {
    u8 n_train;
    u8 n_query;
    /* Training faces first, then queries. */
    u8 label[EFD_MAX_TOTAL];
    u8 photo[EFD_MAX_TOTAL];
} efd_info;

/* Check the 8-byte header and fill n_train / n_query. */
u8 efd_parse_header(const u8 *buf, efd_info *info);
/* Bytes of {label, photo} pairs that follow the header. */
u16 efd_meta_len(const efd_info *info);
/* Fill label[] / photo[] from those bytes. Photo numbers must be 0..9. */
u8 efd_parse_meta(const u8 *buf, efd_info *info);
/* Every pixel of one face must be 0..63. */
u8 efd_check_face(const u8 *pixels);

#endif /* EF_DATA_H */
