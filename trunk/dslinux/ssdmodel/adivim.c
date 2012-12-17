// Disksim SSD ADIVIM support
// ©2012 Seoul National University lecture project1 ADIVIM PROJECT team. All right reserved.


#include "disksim_global.h"

#ifdef ADIVIM
#define SEPARATION_BY_SIZE

#ifdef SEPARATION_BY_ADIVIM
#define ADIVIM_JUDGEMENT_READ_THRESHOLD 1
#define ADIVIM_JUDGEMENT_WRITE_THRESHOLD 1
#define ADIVIM_INIT_TYPE(section) ADIVIM_COLD
#define ADIVIM_ALLOC_INIT_HAPN(section) (ADIVIM_APN) -1
#define ADIVIM_ALLOC_INIT_CAPN(section) adivim_alloc_apn(*adivim_free_capn_list, section->length)
#endif

#ifdef SEPARATION_BY_SIZE
#define ADIVIM_JUDGEMENT_LENGTH_THRESHOLD 1
#define ADIVIM_INIT_TYPE(section) ((section->length < ADIVIM_JUDGEMENT_LENGTH_THRESHOLD) ?  ADIVIM_HOT : ADIVIM_COLD)
#define ADIVIM_ALLOC_INIT_HAPN(section) ((section->length < ADIVIM_JUDGEMENT_LENGTH_THRESHOLD) ? adivim_alloc_apn(*adivim_free_hapn_list, section->length) : (ADIVIM_APN) -1)
#define ADIVIM_ALLOC_INIT_CAPN(section) ((section->length < ADIVIM_JUDGEMENT_LENGTH_THRESHOLD) ? (ADIVIM_APN) -1 : adivim_alloc_apn(*adivim_free_capn_list, section->length))
#endif

#ifdef SEPARATION_BY_SIZE_PLUS_ADIVIM
#define ADIVIM_JUDGEMENT_FIRST_LENGTH_THRESHOLD 1
#define ADIVIM_JUDGEMENT_SECOND_READ_THRESHOLD 0
#define ADIVIM_JUDGEMENT_SECOND_WRITE_THRESHOLD 4
#define ADIVIM_INIT_TYPE(section) ((section->length < ADIVIM_JUDGEMENT_FIRST_LENGTH_THRESHOLD) ?  ADIVIM_HOT : ADIVIM_COLD)
#define ADIVIM_ALLOC_INIT_HAPN(section) ((section->length < ADIVIM_JUDGEMENT_FIRST_LENGTH_THRESHOLD) ? adivim_alloc_apn(*adivim_free_hapn_list, section->length) : (ADIVIM_APN) -1)
#define ADIVIM_ALLOC_INIT_CAPN(section) ((section->length < ADIVIM_JUDGEMENT_FIRST_LENGTH_THRESHOLD) ? (ADIVIM_APN) -1 : adivim_alloc_apn(*adivim_free_capn_list, section->length))
#endif

#include <stdbool.h>
#include <stdlib.h>
#include "adivim.h"
#include "ssd_timing.h"
#include "modules/ssdmodel_ssd_param.h"
#include "ssd_utils.h" // For listnode.

void adivim_init ();
void adivim_assign_judgement (void *t, ioreq_event *req);
void adivim_assign_flag_by_blkno (void *t, int blkno, int *flag);
ADIVIM_JUDGEMENT adivim_get_judgement_by_blkno (void *t, int blkno);
void adivim_do_not_need_to_keep_both_apn (void *t, int blkno, ADIVIM_APN length);

/*
 * Access log as part of section information.
 */
typedef struct _access_log {
    int read_count;
    int write_count;
    /* TODO
     * add access log, frequency etc (later).
     */
} ADIVIM_ACCESS_LOG;

/*
 * Section infomation for ADIVIM.
 */
typedef struct _adivim_section {
    ADIVIM_APN starting; // starting point of section in apn
    ADIVIM_APN length; // Length of section in apn
    ADIVIM_ACCESS_LOG adivim_access_log;
    ADIVIM_JUDGEMENT adivim_judgement;
    ADIVIM_APN do_not_need_to_keep_both_apn_requested; // for _adivim_do_not_need_to_keep_both_apn
} ADIVIM_SECTION;

/*
 * Data struction for apn lists
 */
typedef struct _adivim_apn_alloc {
    ADIVIM_APN starting;
    ADIVIM_APN length;
} ADIVIM_APN_ALLOC;

static listnode **adivim_section_list; // Section list.
static listnode **adivim_free_hapn_list; // Free hot apn list.
static listnode **adivim_free_capn_list; // Free cold apn list.
static unsigned int hot_to_cold_count;
static unsigned int cold_to_hot_count;

/*
 * Init lists for ADIVIM
 */
void adivim_init ();

/*
 * For given section information,
 * judge which is hot and cold.
 */
ADIVIM_SECTION *adivim_judge (ADIVIM_SECTION *section);

/*
 * For given request,
 * update section list and return updated section infomation.
 */
ADIVIM_SECTION adivim_update_section_list (ioreq_event *req);

void adivim_extand_lba_table (ssd_t *s);
listnode *_adivim_ll_insert_at_head(listnode *start, listnode *toinsert);
void adivim_ll_apply (listnode *start, bool (*job) (listnode *start, listnode *target, void *arg), void *arg);
void adivim_free_apn (listnode *start, ADIVIM_APN starting, ADIVIM_APN length);
bool _adivim_free_apn (listnode *start, listnode *target, void *arg);
bool _adivim_section_job (listnode *start, listnode *target, void *arg);
void adivim_section_job (listnode *start, ADIVIM_SECTION *arg);
bool _adivim_alloc_apn (listnode *start, listnode *target, void *arg);
ADIVIM_APN adivim_alloc_apn (listnode *start, ADIVIM_APN size);
bool _adivim_section_lookup (listnode *start,listnode *target, void *arg);
bool _adivim_section_do_not_need_to_keep_both_apn (listnode *start,listnode *target, void *arg);
void adivim_section_do_not_need_to_keep_both_apn (ADIVIM_APN starting, ADIVIM_APN length);
bool _adivim_print_section (listnode *start, listnode *target, void *arg);
void adivim_print_section ();
ADIVIM_JUDGEMENT adivim_section_lookup (listnode *start, ADIVIM_APN pg);

struct my_timing_t {
    ssd_timing_t          t;
    ssd_timing_params    *params;
    int next_write_page[SSD_MAX_ELEMENTS];
};

void adivim_assign_judgement (void *t, ioreq_event *req)
{
    ssd_t *s = (ssd_t *) t;
    struct my_timing_t *tt = (struct my_timing_t *) s->timing_t;
    ADIVIM_SECTION *section = (ADIVIM_SECTION *) malloc (sizeof (ADIVIM_SECTION));

    
    section->starting = (req->blkno)/(tt->params->page_size);
    section->length = (req->bcount)/(tt->params->page_size);
    
    if (req->flags & READ)
    {
        section->adivim_access_log.read_count = 1;
        section->adivim_access_log.write_count = 0;
    }
    else
    {
        section->adivim_access_log.read_count = 0;
        section->adivim_access_log.write_count = 1;
    }
    
    adivim_section_job (*adivim_section_list, section);
    adivim_print_section ();
    adivim_extand_lba_table (s);
}

void adivim_extand_lba_table (ssd_t *s)
{
    int elem, max_hot_lba_size, max_cold_lba_size;
    ADIVIM_APN max_hapn, max_capn;
    ADIVIM_APN_ALLOC *apn_list_bound;
    
    // check largest hapn and capn is larger than lba table size or not. If so, realloc lba table large enought to afford it's apn.
    // find largest hot_lba_size and cold_lba_size among elements
    max_hot_lba_size = 0;
    max_cold_lba_size = 0;
    for (elem = 0; elem < s->params.planes_per_pkg; elem++)
    {
        if (max_hot_lba_size < s->elements[elem].metadata.hot_lba_size)
        {
            max_hot_lba_size = s->elements[elem].metadata.hot_lba_size;
        }
        
        if (max_cold_lba_size < s->elements[elem].metadata.cold_lba_size)
        {
            max_cold_lba_size = s->elements[elem].metadata.cold_lba_size;
        }
    }
    
    //find max hapn and capn
    max_hapn = 0;
    max_capn = 0;
    
    apn_list_bound = (ADIVIM_APN_ALLOC *) ((ll_get_tail (*adivim_free_hapn_list)));
    max_hapn = apn_list_bound->starting;
    
    apn_list_bound = (ADIVIM_APN_ALLOC *) ((ll_get_tail (*adivim_free_capn_list)));
    max_capn = apn_list_bound->starting;
    
    // check if need to be realloc or not
    if (max_hot_lba_size < max_hapn)
    {
        for (elem = 0; elem < s->params.planes_per_pkg; elem++)
        {
            int *new_hot_lba_table = (int *) realloc (s->elements[elem].metadata.hot_lba_table, max_hapn * sizeof(int));
            
            if (new_hot_lba_table == NULL)
            {
                printf ("adivim_extand_lba_table: realloc fail.");
                ASSERT (new_hot_lba_table != NULL);
            }
            else
            {
                s->elements[elem].metadata.hot_lba_table = new_hot_lba_table;
            }
        }
    }
    
    if (max_cold_lba_size < max_capn)
    {
        for (elem = 0; elem < s->params.planes_per_pkg; elem++)
        {
            int *new_cold_lba_table = (int *) realloc (s->elements[elem].metadata.cold_lba_table, max_capn * sizeof(int));
            
            if (new_cold_lba_table == NULL)
            {
                printf ("adivim_extand_lba_table: realloc fail.");
                ASSERT (new_cold_lba_table != NULL);
            }
            else
            {
                s->elements[elem].metadata.cold_lba_table = new_cold_lba_table;
            }
        }
    }
}

void adivim_assign_flag_by_blkno (void *t, int blkno, int *flag)
{
    ADIVIM_JUDGEMENT judgement = adivim_get_judgement_by_blkno (t, blkno);
    switch (judgement.adivim_type) {
        case ADIVIM_HOT :
            if (judgement.adivim_capn == (ADIVIM_APN) -1)
            {
                // Original page mapping
                *flag = 1;
            }
            else
            {
                // Invalid previous block mapping and do page mapping
                *flag = 2;
            }
            break;
        case ADIVIM_COLD :
            if (judgement.adivim_hapn == (ADIVIM_APN) -1)
            {
                // Block mapping
                *flag = 0;
            }
            else
            {
                // Invalid previous page mapping and do block mapping
                *flag = 3;
            }
            break;
    }
}

ADIVIM_JUDGEMENT adivim_get_judgement_by_blkno (void *t, int blkno)
{
    struct my_timing_t *tt = (struct my_timing_t *) t;
    ADIVIM_APN pg = blkno/(tt->params->page_size);
    
    return adivim_section_lookup (*adivim_section_list, pg);
}

void adivim_do_not_need_to_keep_both_apn (void *t, int blkno, ADIVIM_APN length)
{
    struct my_timing_t *tt = (struct my_timing_t *) t;
    ADIVIM_APN starting = blkno/(tt->params->page_size);
    
    adivim_section_do_not_need_to_keep_both_apn (starting, length);
}

void adivim_init ()
{
    ADIVIM_SECTION *adivim_empty_section = (ADIVIM_SECTION *) malloc (sizeof (ADIVIM_SECTION));
    ADIVIM_SECTION *adivim_init_hot_section = (ADIVIM_SECTION *) malloc (sizeof (ADIVIM_SECTION));
    ADIVIM_SECTION *adivim_init_cold_section = (ADIVIM_SECTION *) malloc (sizeof (ADIVIM_SECTION));
    ADIVIM_APN_ALLOC *adivim_hapn_alloc = (ADIVIM_APN_ALLOC *) malloc (sizeof (ADIVIM_APN_ALLOC));
    ADIVIM_APN_ALLOC *adivim_capn_alloc = (ADIVIM_APN_ALLOC *) malloc (sizeof (ADIVIM_APN_ALLOC));
    
    adivim_section_list = (listnode **) malloc (sizeof (listnode *));
    adivim_free_hapn_list = (listnode **) malloc (sizeof (listnode *));
    adivim_free_capn_list = (listnode **) malloc (sizeof (listnode *));
    
    ll_create (adivim_section_list);
    // insert infinity
    adivim_empty_section->starting = ADIVIM_APN_INFINITY;
    adivim_empty_section->length = 0;
    adivim_empty_section->adivim_access_log.read_count = -1;
    adivim_empty_section->adivim_access_log.write_count = -1;
    adivim_empty_section->adivim_judgement.adivim_type = -1;
    adivim_empty_section->adivim_judgement.adivim_hapn = -1;
    adivim_empty_section->adivim_judgement.adivim_capn = -1;
    ll_insert_at_head (*adivim_section_list, adivim_empty_section);
    /*// insert initial cold section
    adivim_init_cold_section->starting = 3509856;
    adivim_init_cold_section->length = 3509856; // actually parameter dependent. but let's go. I'm bussy
    adivim_init_cold_section->adivim_access_log.read_count = 0;
    adivim_init_cold_section->adivim_access_log.write_count = 0;
    adivim_init_cold_section->adivim_judgement.adivim_type = ADIVIM_COLD;
    adivim_init_cold_section->adivim_judgement.adivim_hapn = -1;
    adivim_init_cold_section->adivim_judgement.adivim_capn = 0;
    ll_insert_at_head (*adivim_section_list, adivim_init_cold_section);
    // insert initial hot section
    adivim_init_hot_section->starting = 0;
    adivim_init_hot_section->length = 3509856; // actually parameter dependent. but let's go. I'm bussy
    adivim_init_hot_section->adivim_access_log.read_count = ADIVIM_JUDGEMENT_READ_THRESHOLD + 1;
    adivim_init_hot_section->adivim_access_log.write_count = ADIVIM_JUDGEMENT_WRITE_THRESHOLD + 1;
    adivim_init_hot_section->adivim_judgement.adivim_type = ADIVIM_HOT;
    adivim_init_hot_section->adivim_judgement.adivim_hapn = 0;
    adivim_init_hot_section->adivim_judgement.adivim_capn = -1;
    ll_insert_at_head (*adivim_section_list, adivim_init_hot_section);*/
    
    ll_create (adivim_free_hapn_list);
    adivim_hapn_alloc->starting = 0;
    adivim_hapn_alloc->length = ADIVIM_APN_INFINITY;
    ll_insert_at_head (*adivim_free_hapn_list, adivim_hapn_alloc);
                       
    ll_create (adivim_free_capn_list);
    adivim_capn_alloc->starting = 0;
    adivim_capn_alloc->length = ADIVIM_APN_INFINITY;
    ll_insert_at_head (*adivim_free_capn_list, adivim_capn_alloc);
    
    hot_to_cold_count = 0;
    cold_to_hot_count = 0;
}

/*
 * Copied from ssd_timing.c
 */
listnode *_adivim_ll_insert_at_head(listnode *start, listnode *toinsert)
{
    if ((!start) || (!toinsert)) {
        fprintf(stderr, "Error: invalid value to _adivim_ll_insert_at_head\n");
        exit(-1);
    } else {
        if (start->next) {
            listnode *nextnode = start->next;
            
            toinsert->prev = nextnode->prev;
            nextnode->prev->next = toinsert;
            nextnode->prev = toinsert;
            toinsert->next = nextnode;
        } else {
            start->next = toinsert;
            start->prev = toinsert;
            toinsert->next = start;
            toinsert->prev = start;
        }
        
        return toinsert;
    }
}

void adivim_ll_apply (listnode *start, bool (*job) (listnode *start, listnode *target, void *arg), void *arg)
{
    listnode *node;
    bool keep_go = true;
    
    if (start && start->next)
    {
        node = start->next;
        while (node && (node != start) && keep_go)
        {
            keep_go = job (start, node, arg);
            node = node->next;
        }
    }
}

bool _adivim_free_apn (listnode *start, listnode *target, void *arg)
{
    ADIVIM_APN_ALLOC *data = (ADIVIM_APN_ALLOC *) target->data;
    ADIVIM_APN_ALLOC *toinsert = (ADIVIM_APN_ALLOC *) arg;
    
    if (data->starting + data->length < toinsert->starting)
    {
        return true;
    }
    
    if (toinsert->starting < data->starting)
    {
        if (toinsert->starting + toinsert->length < data->starting)
        {
            // concatenation doesn't occur
            // allocate a new node
            listnode *newnode = malloc(sizeof(listnode));
            ADIVIM_APN_ALLOC *newdata = malloc (sizeof (ADIVIM_APN_ALLOC));
            newdata->starting = toinsert->starting;
            newdata->length = toinsert->length;
            newnode->data = (void *) newdata;
            
            // increment the number of entries
            ((header_data *)(start->data))->size ++;
            
            // insert before ttoinsertet
            _adivim_ll_insert_at_head(target->prev, newnode);
            
            return false;
        }
        else
        {
            // concatenation occur
            // modify ttoinsertet
            ADIVIM_APN diff = data->starting - toinsert->starting;
            data->starting = toinsert->starting;
            data->length += diff;
        }
        
    }
    
    if (toinsert->starting < data->starting + data->length && data->starting + data->length < toinsert->starting + toinsert->length)
    {
        if (((ADIVIM_APN_ALLOC *) (target->next->data))->starting < toinsert->starting + toinsert->length)
        {
            // concatenation occur
            // modify ttoinsertet
            ADIVIM_APN_ALLOC *nextdata = target->next->data;
            ADIVIM_APN diff = nextdata->starting + nextdata->length - (data->starting + data->length);
            data->length += diff;
            
            // release next node and check again on ttoinsertet
            ll_release_node (start, target->next);
            return _adivim_free_apn (start, target, toinsert);
        }
        else
        {
            // concatenation doesn't occur
            //modify ttoinsertet
            ADIVIM_APN diff = toinsert->starting + toinsert->length - (data->starting + data->length);
            data->length += diff;
            
            return false;
        }
    }
    
    return false;
}

void adivim_free_apn (listnode *start, ADIVIM_APN starting, ADIVIM_APN length)
{
    ADIVIM_APN_ALLOC *arg = malloc (sizeof (ADIVIM_APN_ALLOC));
    arg->starting = starting;
    arg->length = length;
    
    adivim_ll_apply (start, _adivim_free_apn, (void *) arg);
    free (arg);
}

bool _adivim_alloc_apn (listnode *start, listnode *target, void *arg)
{
    ADIVIM_APN_ALLOC *data = (ADIVIM_APN_ALLOC *) target->data;
    ADIVIM_APN_ALLOC *new = (ADIVIM_APN_ALLOC *) arg;
    
    if (new->length < data->length)
    {
        new->starting = data->starting;
        data->starting += new->length;
        data->length -= new->length;
        
        return false;
    }
    
    return true;
}

ADIVIM_APN adivim_alloc_apn (listnode *start, ADIVIM_APN size)
{
    ADIVIM_APN_ALLOC *arg = (ADIVIM_APN_ALLOC *) malloc (sizeof (ADIVIM_APN_ALLOC));
    ADIVIM_APN ret;
    arg->length = size;
    
    adivim_ll_apply (start, _adivim_alloc_apn, (void *) arg);
    
    ret = arg->starting;
    free (arg);
    
    return ret;
}

bool _adivim_section_job (listnode *start, listnode *target, void *arg)
{
    
    ADIVIM_SECTION *data = (ADIVIM_SECTION *) target->data;
    ADIVIM_SECTION *toinsert = (ADIVIM_SECTION *) arg;
    
    if (toinsert->length == 0)
    {
        return false;
    }
    
    if (toinsert->starting < 0 || toinsert->length < 0)
    {
        printf ("_adivim_section_job: no such section (%d, %d)", toinsert->starting, toinsert->length);
        ASSERT (!(toinsert->starting < 0 || toinsert->length < 0));
    }
    
    if (data->starting + data->length <= toinsert->starting)
    {
        return true;
    }
    
    if (toinsert->starting < data->starting)
    {
        // add new section
        // allocate a new node
        listnode *newnode = malloc(sizeof(listnode));
        ADIVIM_SECTION *newdata = malloc (sizeof (ADIVIM_SECTION));
        newnode->data = (void *) newdata;
        
        // increment the number of entries
        ((header_data *)(start->data))->size ++;
        
        // insert before target
        _adivim_ll_insert_at_head(target->prev, newnode);
        
        // init new section
        newdata->starting = toinsert->starting;
        newdata->adivim_access_log.read_count = toinsert->adivim_access_log.read_count;
        newdata->adivim_access_log.write_count = toinsert->adivim_access_log.write_count;
        
        if (toinsert->starting + toinsert->length <= data->starting)
        {
            // Really newbe
            newdata->length = toinsert->length;
            newdata->adivim_judgement.adivim_type = ADIVIM_INIT_TYPE(newdata);
            newdata->adivim_judgement.adivim_hapn = ADIVIM_ALLOC_INIT_HAPN(newdata);
            newdata->adivim_judgement.adivim_capn = ADIVIM_ALLOC_INIT_CAPN(newdata);
            
            return false;
        }
        else
        {
            // divide section occur. that will be devided in follwing code.
            newdata->length = data->starting - toinsert->starting;
            newdata->adivim_judgement.adivim_type = ADIVIM_INIT_TYPE(newdata);
            newdata->adivim_judgement.adivim_hapn = ADIVIM_ALLOC_INIT_HAPN(newdata);
            newdata->adivim_judgement.adivim_capn = ADIVIM_ALLOC_INIT_CAPN(newdata);
            
            toinsert->length = toinsert->starting + toinsert->length - data->starting;
            toinsert->starting = data->starting;
            
            return _adivim_section_job (start, target, (void *) toinsert);
        }
    }
    
    if ((toinsert->starting) == (data->starting))
    {
        if (data->length <= toinsert->length)
        {
            data->adivim_access_log.read_count += toinsert->adivim_access_log.read_count;
            data->adivim_access_log.write_count += toinsert->adivim_access_log.write_count;
            adivim_judge (data);
            
            toinsert->starting += data->length;
            toinsert->length = toinsert->length - data->length;
            
            return true;
        }
        else
        {
            // divide target into two.
            // allocate a new node
            listnode *newnode = malloc(sizeof(listnode));
            ADIVIM_SECTION *newdata = malloc (sizeof (ADIVIM_SECTION));
            newnode->data = (void *) newdata;
            
            // increment the number of entries
            ((header_data *)(start->data))->size ++;
            
            // insert before target
            _adivim_ll_insert_at_head(target->prev, newnode);
            
            // init new section
            newdata->starting = toinsert->starting;
            newdata->length = toinsert->length;
            newdata->adivim_access_log.read_count = data->adivim_access_log.read_count + toinsert->adivim_access_log.read_count;
            newdata->adivim_access_log.write_count = data->adivim_access_log.write_count + toinsert->adivim_access_log.write_count;
            newdata->adivim_judgement.adivim_type = data->adivim_judgement.adivim_type;
            newdata->adivim_judgement.adivim_hapn = data->adivim_judgement.adivim_hapn;
            newdata->adivim_judgement.adivim_capn = data->adivim_judgement.adivim_capn;
            adivim_judge (newdata);
            
            // modify target
            data->starting += toinsert->length;
            data->length -= toinsert->length;
            data->adivim_judgement.adivim_hapn += (data->adivim_judgement.adivim_hapn == -1 ? 0 : toinsert->length);
            data->adivim_judgement.adivim_capn += (data->adivim_judgement.adivim_capn == -1 ? 0 : toinsert->length);
            
            return false;
        }
    }
    
    if (data->starting < toinsert->starting && toinsert->starting < data->starting + data->length)
    {
        // divide target into two.
        // allocate a new node
        listnode *newnode = malloc(sizeof(listnode));
        ADIVIM_SECTION *newdata = malloc (sizeof (ADIVIM_SECTION));
        newnode->data = (void *) newdata;
        
        // increment the number of entries
        ((header_data *)(start->data))->size ++;
        
        // insert after target
        _adivim_ll_insert_at_head(target, newnode);
        
        // init new section
        newdata->starting = toinsert->starting;
        newdata->length = data->starting + data->length - toinsert->starting;
        newdata->adivim_access_log.read_count = data->adivim_access_log.read_count;
        newdata->adivim_access_log.write_count = data->adivim_access_log.write_count;
        newdata->adivim_judgement.adivim_type = data->adivim_judgement.adivim_type;
        newdata->adivim_judgement.adivim_hapn = data->adivim_judgement.adivim_hapn + (data->adivim_judgement.adivim_hapn==-1 ? 0 : (toinsert->starting - data->starting));
        newdata->adivim_judgement.adivim_capn = data->adivim_judgement.adivim_capn + (data->adivim_judgement.adivim_capn==-1 ? 0 : (toinsert->starting - data->starting));
        
        // modify target
        data->length = toinsert->starting - data->starting;
        
        return true;
    }
    
    return false;
}

void adivim_section_job (listnode *start, ADIVIM_SECTION *arg)
{
    adivim_ll_apply (start, _adivim_section_job, (void *) arg);
}

bool _adivim_section_lookup (listnode *start,listnode *target, void *arg)
{
    ADIVIM_SECTION *data = (ADIVIM_SECTION *) target->data;
    ADIVIM_APN pg = **((ADIVIM_APN **) arg);
    
    if (data->starting == ADIVIM_APN_INFINITY)
    {
        printf ("_adivim_section_lookup: lookup fail. no such page %d.", pg);
        ASSERT (data->starting != ADIVIM_APN_INFINITY);
    }

    if (data->starting <= pg && pg < data->starting + data->length)
    {
        ADIVIM_JUDGEMENT *datajudge = &(data->adivim_judgement);
        ADIVIM_JUDGEMENT *copy = (ADIVIM_JUDGEMENT *) malloc (sizeof (ADIVIM_JUDGEMENT));
        
        copy->adivim_type = datajudge->adivim_type;
        copy->adivim_hapn = datajudge->adivim_hapn + (datajudge->adivim_hapn==-1 ? 0 : (pg - data->starting)); // if hapn == -1, must not add diff
        copy->adivim_capn = datajudge->adivim_capn + (datajudge->adivim_capn==-1 ? 0 : (pg - data->starting)); // if capn == -1, must not add diff
        
        free (*((ADIVIM_APN **) arg));
        *((ADIVIM_SECTION **) arg) = (void *) copy;
        
        return false;
    }
    
    return true;
}

ADIVIM_JUDGEMENT adivim_section_lookup (listnode *start, ADIVIM_APN pg)
{
    ADIVIM_APN *arg = (ADIVIM_APN *) malloc (sizeof (ADIVIM_APN));
    ADIVIM_JUDGEMENT ret;
    *arg = pg;
    
    adivim_ll_apply (start, _adivim_section_lookup, (void *) (&arg));
    
    ret.adivim_type = ((ADIVIM_JUDGEMENT *) arg)->adivim_type;
    ret.adivim_hapn = ((ADIVIM_JUDGEMENT *) arg)->adivim_hapn;
    ret.adivim_capn = ((ADIVIM_JUDGEMENT *) arg)->adivim_capn;
    
    free (arg);
    return ret;
}

bool _adivim_section_do_not_need_to_keep_both_apn (listnode *start,listnode *target, void *arg)
{
    ADIVIM_SECTION *data = (ADIVIM_SECTION *) target->data;
    ADIVIM_SECTION *tomodify = (ADIVIM_SECTION *) arg;
    
    if (data->starting == ADIVIM_APN_INFINITY)
    {
        printf ("_adivim_section_do_not_need_to_keep_both_apn: fail. no such section (%d, %d).", tomodify->starting, tomodify->length);
        ASSERT (data->starting != ADIVIM_APN_INFINITY);
    }
    
    if ((data->starting <= tomodify->starting) && (tomodify->starting + tomodify->length <= data->starting + data->length))
    {
        data->do_not_need_to_keep_both_apn_requested += tomodify->length;
        
        if (data->do_not_need_to_keep_both_apn_requested >= data->length)
        {
            ADIVIM_JUDGEMENT *datajudge = &(data->adivim_judgement);
            
            switch (datajudge->adivim_type)
            {
                case ADIVIM_HOT:
                    datajudge->adivim_capn = -1;
                    break;
                case ADIVIM_COLD:
                    datajudge->adivim_hapn = -1;
                    break;
            }
            data->do_not_need_to_keep_both_apn_requested = 0;
        }
        return false;
    }
    
    return true;
}

void adivim_section_do_not_need_to_keep_both_apn (ADIVIM_APN starting, ADIVIM_APN length)
{
    ADIVIM_SECTION *arg = (ADIVIM_SECTION *) malloc (sizeof (ADIVIM_SECTION));
    arg->starting = starting;
    arg->length = length;
    
    adivim_ll_apply (*adivim_section_list, _adivim_section_do_not_need_to_keep_both_apn, (void *) arg);
    
    free (arg);
}

bool _adivim_print_section (listnode *start, listnode *target, void *arg)
{
    ADIVIM_SECTION *data = (ADIVIM_SECTION *) target->data;
    printf ("\n->((%7d, %7d), (%d, %d), (%d, %7d, %7d), %7d)", data->starting, data->length, data->adivim_access_log.read_count, data->adivim_access_log.write_count, data->adivim_judgement.adivim_type, data->adivim_judgement.adivim_hapn, data->adivim_judgement.adivim_capn, data->do_not_need_to_keep_both_apn_requested);
    
    return true;
}

void adivim_print_section ()
{
    printf ("section list((starting, length), (rcount, wcount), (type, hapn, capn), DNKBA): |");
    adivim_ll_apply (*adivim_section_list, _adivim_print_section, NULL);
    printf ("\n");
    printf ("(hot_to_cold, cold_to_hot) = (%d, %d)\n", hot_to_cold_count, cold_to_hot_count);
}

ADIVIM_SECTION *adivim_judge (ADIVIM_SECTION *section)
{
#ifdef SEPARATION_BY_ADIVIM
    if (section->adivim_access_log.read_count > ADIVIM_JUDGEMENT_READ_THRESHOLD && section->adivim_access_log.write_count > ADIVIM_JUDGEMENT_WRITE_THRESHOLD) // Section will be hot
    {
        // Assign ADIVIM_TYPE and allocation ADIVIM_APNs
        switch (section->adivim_judgement.adivim_type)
        {
            case ADIVIM_HOT : break;
            case ADIVIM_COLD :
                section->adivim_judgement.adivim_type = ADIVIM_HOT;
                section->adivim_judgement.adivim_hapn = adivim_alloc_apn (*adivim_free_hapn_list, section->length);
                adivim_free_apn (*adivim_free_capn_list, section->adivim_judgement.adivim_capn, section->length);
                section->do_not_need_to_keep_both_apn_requested = 0;
                break;
        };
    }
    else // Section will be cold
    {
        switch (section->adivim_judgement.adivim_type)
        {
            case ADIVIM_HOT :
                section->adivim_judgement.adivim_type = ADIVIM_COLD;
                section->adivim_judgement.adivim_capn = adivim_alloc_apn (*adivim_free_capn_list, section->length);
                adivim_free_apn (*adivim_free_hapn_list, section->adivim_judgement.adivim_hapn, section->length);
                section->do_not_need_to_keep_both_apn_requested = 0;
                break;
            case ADIVIM_COLD :break;
        };
    }
    
    return section;
#endif
#ifdef SEPARATION_BY_SIZE
    if (section->length < ADIVIM_JUDGEMENT_LENGTH_THRESHOLD) // Section will be hot
    {
        // Assign ADIVIM_TYPE and allocation ADIVIM_APNs
        switch (section->adivim_judgement.adivim_type)
        {
            case ADIVIM_HOT : break;
            case ADIVIM_COLD :
                section->adivim_judgement.adivim_type = ADIVIM_HOT;
                section->adivim_judgement.adivim_hapn = adivim_alloc_apn (*adivim_free_hapn_list, section->length);
                adivim_free_apn (*adivim_free_capn_list, section->adivim_judgement.adivim_capn, section->length);
                section->do_not_need_to_keep_both_apn_requested = 0;
                break;
        };
    }
    else // Section will be cold
    {
        switch (section->adivim_judgement.adivim_type)
        {
            case ADIVIM_HOT :
                section->adivim_judgement.adivim_type = ADIVIM_COLD;
                section->adivim_judgement.adivim_capn = adivim_alloc_apn (*adivim_free_capn_list, section->length);
                adivim_free_apn (*adivim_free_hapn_list, section->adivim_judgement.adivim_hapn, section->length);
                section->do_not_need_to_keep_both_apn_requested = 0;
                break;
            case ADIVIM_COLD :break;
        };
    }
    
    return section;
#endif
#ifdef SEPARATION_BY_SIZE_PLUS_ADIVIM
    if ((section->length < ADIVIM_JUDGEMENT_FIRST_LENGTH_THRESHOLD) || (section->adivim_access_log.read_count > ADIVIM_JUDGEMENT_SECOND_READ_THRESHOLD && section->adivim_access_log.write_count > ADIVIM_JUDGEMENT_SECOND_WRITE_THRESHOLD)) // Section will be hot
    {
        // Assign ADIVIM_TYPE and allocation ADIVIM_APNs
        switch (section->adivim_judgement.adivim_type)
        {
            case ADIVIM_HOT : break;
            case ADIVIM_COLD :
                section->adivim_judgement.adivim_type = ADIVIM_HOT;
                section->adivim_judgement.adivim_hapn = adivim_alloc_apn (*adivim_free_hapn_list, section->length);
                adivim_free_apn (*adivim_free_capn_list, section->adivim_judgement.adivim_capn, section->length);
                section->do_not_need_to_keep_both_apn_requested = 0;
                cold_to_hot_count++;
                break;
        };
    }
    else // Section will be cold
    {
        switch (section->adivim_judgement.adivim_type)
        {
            case ADIVIM_HOT :
                section->adivim_judgement.adivim_type = ADIVIM_COLD;
                section->adivim_judgement.adivim_capn = adivim_alloc_apn (*adivim_free_capn_list, section->length);
                adivim_free_apn (*adivim_free_hapn_list, section->adivim_judgement.adivim_hapn, section->length);
                section->do_not_need_to_keep_both_apn_requested = 0;
                hot_to_cold_count++;
                break;
            case ADIVIM_COLD :break;
        };
    }
    
    return section;
#endif
}
#endif