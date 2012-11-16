// Disksim SSD ADIVIM support
// ©2012 Seoul National University lecture project1 ADIVIM PROJECT team. All right reserved.

#include "disksim_global.h"

/*
 * Section types.
 */
typedef enum {
    ADIVIM_SECTION_HOT,
    ADIVIM_SECTION_COLD
    /*
     * Yet divided section is not implemented. Will divided section be hot or cold?
     ,
    ADIVIM_SECTION_DIVIDED
     */
} ADIVIM_SECTION_TYPE;

/*
 * Access log is part of section information. 
 */
typedef struct _section_access_log {
    int read_count;
    int write_count;
    /* TODO 
     * add access log, frequency etc (later).
     */
} section_access_log;

/*
 * Section infomation for ADIVIM.
 */
typedef struct _adivim_section {
    int blkno; // Starting sector no. from host
    int bcount; // Length of request in sector from host
    section_access_log log;
    ADIVIM_SECTION_TYPE type;
    int apn; // String apn no
} adivim_section;

/*
 * For given reqest,
 * update section list, judge section and record the judgement in the request
 */
void adivim_separate (ioreq_event *req);
