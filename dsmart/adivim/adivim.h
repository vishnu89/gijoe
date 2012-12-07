
// Disksim SSD ADIVIM support
// ©2012 Seoul National University lecture project1 ADIVIM PROJECT team. All right reserved.

//#include "disksim_global.h"

#ifdef ADIVIM
#include "disksim_global.h"
#include "ssdmodel/ssd_timing.h"

void adivim_init ();
/*
 * For given reqest,
 * allocate apn.
 * To do so, this function updates section list, judges section and records the judgement in the request
 */
void adivim_assign_judgement (ssd_timing_t *t, ioreq_event *req);
ADIVIM_JUDGEMENT adivim_get_judgement_by_blkno (ssd_timing_t *t, int blkno);

#endif
