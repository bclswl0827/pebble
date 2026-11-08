#include <stdint.h>
#include "co_utils.h"
#include "prf_types.h"
#include "attm_db_128.h"
#include "user_custs1_def.h"

static const att_svc_desc128_t dult_svc = DEF_DULT_SVC_UUID_128;
static const uint8_t DULT_CTRL_POINT_UUID_128[ATT_UUID_128_LEN] = DEF_DULT_CTRL_POINT_UUID_128;

static const uint16_t att_decl_svc  = ATT_DECL_PRIMARY_SERVICE;
static const uint16_t att_decl_char = ATT_DECL_CHARACTERISTIC;

/* Read by custs1.c and the ROM custom profile code. */
const uint8_t custs1_services[] = {DULT_IDX_SVC, CUSTS1_IDX_NB};
const uint8_t custs1_services_size = ARRAY_LEN(custs1_services) - 1;
const uint16_t custs1_att_max_nb = CUSTS1_IDX_NB;

const struct attm_desc_128 custs1_att_db[CUSTS1_IDX_NB] =
{
    [DULT_IDX_SVC]             = {(uint8_t *)&att_decl_svc, ATT_UUID_128_LEN, PERM(RD, ENABLE),
                                  sizeof(dult_svc), sizeof(dult_svc), (uint8_t *)&dult_svc},

    [DULT_IDX_CTRL_POINT_CHAR] = {(uint8_t *)&att_decl_char, ATT_UUID_16_LEN, PERM(RD, ENABLE),
                                  0, 0, NULL},

    /* No CCCD: OpenTagViewer then writes the opcode directly. */
    [DULT_IDX_CTRL_POINT_VAL]  = {DULT_CTRL_POINT_UUID_128, ATT_UUID_128_LEN,
                                  PERM(WR, ENABLE) | PERM(WRITE_REQ, ENABLE),
                                  DEF_DULT_CTRL_POINT_CHAR_LEN, 0, NULL},
};
