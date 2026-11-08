#ifndef USER_CUSTS1_DEF_H
#define USER_CUSTS1_DEF_H

#include "attm_db_128.h"

/*
 * DULT (Detecting Unwanted Location Trackers) non-owner service; OpenTagViewer
 * writes Sound_Start / Sound_Stop to its control point. UUIDs are little-endian.
 */
/* 15190001-12F4-C226-88ED-2AC5579F2A85 */
#define DEF_DULT_SVC_UUID_128           {0x85, 0x2A, 0x9F, 0x57, 0xC5, 0x2A, 0xED, 0x88, \
                                         0x26, 0xC2, 0xF4, 0x12, 0x01, 0x00, 0x19, 0x15}
/* 8E0C0001-1D68-FB92-BF61-48377421680E */
#define DEF_DULT_CTRL_POINT_UUID_128    {0x0E, 0x68, 0x21, 0x74, 0x37, 0x48, 0x61, 0xBF, \
                                         0x92, 0xFB, 0x68, 0x1D, 0x01, 0x00, 0x0C, 0x8E}

/* Longest write that fits the 23-byte ATT MTU; longer writes are rejected without an event. */
#define DEF_DULT_CTRL_POINT_CHAR_LEN    (20)

enum
{
    DULT_IDX_SVC,
    DULT_IDX_CTRL_POINT_CHAR,
    DULT_IDX_CTRL_POINT_VAL,
    CUSTS1_IDX_NB
};

#endif
