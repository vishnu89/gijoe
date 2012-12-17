
// Disksim SSD ADIVIM support
// ©2012 Seoul National University lecture project1 ADIVIM PROJECT team. All right reserved.

#include "disksim_global.h"

#ifdef ADIVIM
//#include "disksim_global.h"

void adivim_init ();
/*
 * For given reqest,
 * allocate apn.
 * To do so, this function updates section list, judges section and records the judgement in the request
 */
void adivim_assign_judgement (void *t, ioreq_event *req);
void adivim_assign_flag_by_blkno (void *t, int blkno, int *flag);
ADIVIM_JUDGEMENT adivim_get_judgement_by_blkno (void *t, int blkno);

/* IMPORTANT
 * becuase adivim can not know that hot to cold and cold to hot transfer is done or not
 * ssd must tell adivim that transfer is done. After transfer is done, adivim_get_judgement_by_blkno will return flag
 * either hot or cold neither something like hot_to_cold nor cold_to_hot.
 */
void adivim_do_not_need_to_keep_both_apn (void *t, int blkno, ADIVIM_APN length);

void adivim_print_threshold (FILE *output);
#endif
