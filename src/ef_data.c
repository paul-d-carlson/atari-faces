/*
 * ef_data.c -- see ef_data.h.
 */
#include "ef_data.h"

u8 efd_parse_header(const u8 *buf, efd_info *info)
{
    if (buf[0] != 'E' || buf[1] != 'F') {
        return EFD_BAD_MAGIC;
    }
    if (buf[2] != EFD_VERSION) {
        return EFD_BAD_VERSION;
    }
    if (buf[5] != EF_W || buf[6] != EF_H || buf[7] != EF_LEVELS) {
        return EFD_BAD_SIZE;
    }
    if (buf[3] < 2u || buf[3] > EF_MAX_FACES || buf[4] > EFD_MAX_QUERY) {
        return EFD_BAD_COUNT;
    }
    info->n_train = buf[3];
    info->n_query = buf[4];
    return EFD_OK;
}

u16 efd_meta_len(const efd_info *info)
{
    return (u16)(2u * (info->n_train + info->n_query));
}

u8 efd_parse_meta(const u8 *buf, efd_info *info)
{
    u8 i;
    u8 n = (u8)(info->n_train + info->n_query);

    for (i = 0; i < n; ++i) {
        if (buf[2u * i + 1u] > 9u) {
            return EFD_BAD_META;
        }
        info->label[i] = buf[2u * i];
        info->shot[i] = buf[2u * i + 1u];
    }
    return EFD_OK;
}

u8 efd_check_face(const u8 *pixels)
{
    u16 p;

    for (p = 0; p < EF_PIXELS; ++p) {
        if (pixels[p] >= EF_LEVELS) {
            return EFD_BAD_PIXEL;
        }
    }
    return EFD_OK;
}
