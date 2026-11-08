#include "app_prf_types.h"
#include "app_customs.h"
#include "user_custs1_def.h"
#include "user_custs_config.h"

extern const struct attm_desc_128 custs1_att_db[CUSTS1_IDX_NB];

/* app.c creates and enables custom profiles from this table, not from user_prf_funcs. */
const struct cust_prf_func_callbacks cust_prf_funcs[] =
{
    {TASK_ID_CUSTS1, custs1_att_db, CUSTS1_IDX_NB, app_custs1_create_db, NULL, NULL, NULL},
    {TASK_ID_INVALID, NULL, 0, NULL, NULL, NULL, NULL},   // DO NOT MOVE. Must always be last
};
