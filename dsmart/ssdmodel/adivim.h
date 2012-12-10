
// Disksim SSD ADIVIM support
// ©2012 Seoul National University lecture project1 ADIVIM PROJECT team. All right reserved.

//#include "disksim_global.h"

#ifdef ADIVIM
#include "disksim_global.h"
#include "ssd_timing.h"

void adivim_init ();
/*
 * For given reqest,
 * allocate apn.
 * To do so, this function updates section list, judges section and records the judgement in the request
 */
void adivim_assign_judgement (void *t, ioreq_event *req);
ADIVIM_JUDGEMENT adivim_get_judgement_by_blkno (void *t, int blkno);

#endif
