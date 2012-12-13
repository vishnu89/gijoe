// Disksim SSD ADIVIM support
// ©2012 Seoul National University lecture project1 ADIVIM PROJECT team. All right reserved.


#include "disksim_global.h"

#ifdef ADIVIM
#define ADIVIM_INIT_TYPE ADIVIM_COLD
#define ADIVIM_ALLOC_INIT_HAPN(length) (ADIVIM_APN) -1
#define ADIVIM_ALLOC_INIT_CAPN(length) adivim_alloc_apn(*adivim_free_capn_list, length)
//#include "disksim_global.h"
#include <stdbool.h>
#include "adivim.h"
#include "ssd_timing.h"
#include "modules/ssdmodel_ssd_param.h"
#include "ssd_utils.h" // For listnode.

void adivim_init ();
void adivim_assign_judgement (void *t, ioreq_event *req);
void adivim_assign_flag_by_blkno (void *t, int blkno, int *flag);
ADIVIM_JUDGEMENT adivim_get_judgement_by_blkno (void *t, int blkno);

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
    int blkno; // Starting sector no. from host
    int bcount; // Length of request in sector from host
    ADIVIM_APN starting;
    ADIVIM_APN length; // Length of section in apn
    ADIVIM_ACCESS_LOG adivim_access_log;
    ADIVIM_JUDGEMENT adivim_judgement;
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

listnode *_adivim_ll_insert_at_head(listnode *start, listnode *toinsert);
void adivim_ll_apply (listnode *start, bool (*job) (listnode *start, listnode *target, void *arg), void *arg);
void adivim_free_apn (listnode *start, ADIVIM_APN starting, ADIVIM_APN length);
bool _adivim_free_apn (listnode *start, listnode *target, void *arg);
bool _adivim_section_job (listnode *start, listnode *target, void *arg);
void adivim_section_job (listnode *start, ADIVIM_SECTION *arg);
bool _adivim_alloc_apn (listnode *start, listnode *target, void *arg);
ADIVIM_APN adivim_alloc_apn (listnode *start, ADIVIM_APN size);
bool _adivim_section_lookup (listnode *start,listnode *target, void *arg);
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
    struct my_timing_t *tt = (struct my_timing_t *) t;
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

void adivim_init ()
{
    ADIVIM_SECTION *adivim_empty_section = (ADIVIM_SECTION *) malloc (sizeof (ADIVIM_SECTION));
    ADIVIM_APN_ALLOC *adivim_hapn_alloc = (ADIVIM_APN_ALLOC *) malloc (sizeof (ADIVIM_APN_ALLOC));
    ADIVIM_APN_ALLOC *adivim_capn_alloc = (ADIVIM_APN_ALLOC *) malloc (sizeof (ADIVIM_APN_ALLOC));
    
    adivim_section_list = (listnode **) malloc (sizeof (listnode *));
    adivim_free_hapn_list = (listnode **) malloc (sizeof (listnode *));
    adivim_free_capn_list = (listnode **) malloc (sizeof (listnode *));
    
    ll_create (adivim_section_list);
    adivim_empty_section->starting = ADIVIM_APN_INFINITY;
    adivim_empty_section->length = 0;
    ll_insert_at_head (*adivim_section_list, adivim_empty_section);
    
    ll_create (adivim_free_hapn_list);
    adivim_hapn_alloc->starting = 0;
    adivim_hapn_alloc->length = ADIVIM_APN_INFINITY;
    ll_insert_at_head (*adivim_free_hapn_list, adivim_hapn_alloc);
                       
    ll_create (adivim_free_capn_list);
    adivim_capn_alloc->starting = 0;
    adivim_capn_alloc->length = ADIVIM_APN_INFINITY;
    ll_insert_at_head (*adivim_free_capn_list, adivim_capn_alloc);
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
    
    if (data->starting + data->length < toinsert->starting)
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
            newdata->adivim_judgement.adivim_type = ADIVIM_INIT_TYPE;
            newdata->adivim_judgement.adivim_hapn = ADIVIM_ALLOC_INIT_HAPN(newdata->length);
            newdata->adivim_judgement.adivim_capn = ADIVIM_ALLOC_INIT_CAPN(newdata->length);
            
            return false;
        }
        else
        {
            // divide section occur. that will be devided in follwing code.
            newdata->length = data->starting - toinsert->starting;
            newdata->adivim_judgement.adivim_type = ADIVIM_INIT_TYPE;
            newdata->adivim_judgement.adivim_hapn = ADIVIM_ALLOC_INIT_HAPN(newdata->length);
            newdata->adivim_judgement.adivim_capn = ADIVIM_ALLOC_INIT_CAPN(newdata->length);
            
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
            data->adivim_judgement.adivim_hapn += toinsert->length;
            data->adivim_judgement.adivim_capn += toinsert->length;
            
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
        newdata->adivim_judgement.adivim_hapn = data->adivim_judgement.adivim_hapn + (toinsert->starting - data->starting);
        newdata->adivim_judgement.adivim_capn = data->adivim_judgement.adivim_capn + (toinsert->starting - data->starting);
        
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

    if (data->starting <= pg && pg <= data->starting + data->length)
    {
        ADIVIM_JUDGEMENT *datajudge = &(data->adivim_judgement);
        ADIVIM_JUDGEMENT *copy = (ADIVIM_JUDGEMENT *) malloc (sizeof (ADIVIM_JUDGEMENT));
        
        copy->adivim_type = datajudge->adivim_type;
        copy->adivim_hapn = datajudge->adivim_hapn;
        copy->adivim_capn = datajudge->adivim_capn;
        
        free (*((ADIVIM_APN **) arg));
        *((ADIVIM_SECTION **) arg) = (void *) copy;
        
        return false;
    }
    
    return true;
}

ADIVIM_JUDGEMENT adivim_section_lookup (listnode *start, ADIVIM_APN pg)
{
    ADIVIM_APN **arg = (ADIVIM_APN *) malloc (sizeof (ADIVIM_APN));
    ADIVIM_JUDGEMENT ret;
    *arg = pg;
    
    adivim_ll_apply (start, _adivim_section_lookup, (void *) (&arg));
    
    ret.adivim_type = ((ADIVIM_JUDGEMENT *) arg)->adivim_type;
    ret.adivim_hapn = ((ADIVIM_JUDGEMENT *) arg)->adivim_hapn;
    ret.adivim_capn = ((ADIVIM_JUDGEMENT *) arg)->adivim_capn;
    
    free (arg);
    return ret;
}

bool _adivim_print_section (listnode *start, listnode *target, void *arg)
{
    ADIVIM_SECTION *data = (ADIVIM_SECTION *) target->data;
    printf ("->(%d, %d, %d, %d, %d, %d, %d)", data->starting, data->length, data->adivim_access_log.read_count, data->adivim_access_log.write_count, data->adivim_judgement.adivim_type, data->adivim_judgement.adivim_hapn, data->adivim_judgement.adivim_capn);
    
    return true;
}

void adivim_print_section ()
{
    printf ("section list: (starting, length, rcount, wcount, type, hapn, capn)");
    adivim_ll_apply (*adivim_section_list, _adivim_print_section, NULL);
}

ADIVIM_SECTION *adivim_judge (ADIVIM_SECTION *section)
{
    if (section->adivim_access_log.read_count > 1 && section->adivim_access_log.write_count > 1) // Section will be hot
    {
        // Assign ADIVIM_TYPE and allocation ADIVIM_APNs
        switch (section->adivim_judgement.adivim_type)
        {
            case ADIVIM_HOT : break;
            case ADIVIM_COLD :
                section->adivim_judgement.adivim_type = ADIVIM_HOT;
                section->adivim_judgement.adivim_hapn = adivim_alloc_apn (*adivim_free_hapn_list, section->length);
                adivim_free_apn (*adivim_free_capn_list, section->starting, section->length);
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
                adivim_free_apn (*adivim_free_hapn_list, section->starting, section->length);
                break;
            case ADIVIM_COLD :break;
        };
    }
    
    return section;
}
#endif