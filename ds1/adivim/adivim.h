
// Disksim SSD ADIVIM support
// ©2012 Seoul National University lecture project1 ADIVIM PROJECT team. All right reserved.

#include "disksim_global.h"

#ifdef ADIVIM

/*
 * For given reqest,
 * allocate apn.
 * To do so, this function updates section list, judges section and records the judgement in the request
 */
void adivim_assign_judgement (ioreq_event *req);
ADIVIM_JUDGEMENT adivim_get_judgement_by_blkno (ssd_timing_t *t, int blkno);

#endif