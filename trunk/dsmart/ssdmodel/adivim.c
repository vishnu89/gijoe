// Disksim SSD ADIVIM support
// ©2012 Seoul National University lecture project1 ADIVIM PROJECT team. All right reserved.


//#include "disksim_global.h"

#ifdef ADIVIM
#include "disksim_global.h"
#include <stdbool.h>
#include "adivim.h"
#include "ssd_timing.h"
#include "ssdmodel_ssd_param.h"
#include "ssd_utils.h" // For listnode.

void adivim_init ();
void adivim_assign_judgement (void *t, ioreq_event *req);
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
ADIVIM_JUDGEMENT adivim_judge (ADIVIM_SECTION *section);

/*
 * For given request,
 * update section list and return updated section infomation.
 */
ADIVIM_SECTION adivim_update_section_list (ioreq_event *req);

/*
 * For given request and type,
 * mark type to request.
 */
void adivim_mark (ioreq_event *req, ADIVIM_JUDGEMENT adivim_judgement);

listnode *ll_insert_at_tail(listnode *start, void *data);
void ll_apply (listnode *start, bool (*job) (listnode *start, listnode *target, void *arg), void *arg);
void free_apn (listnode *start, ADIVIM_APN starting, ADIVIM_APN length);
bool _free_apn (listnode *start, listnode *target, void *arg);
bool _section_job (listnode *start, listnode *target, void *arg);
void section_job (listnode *start, ADIVIM_SECTION *arg);
bool _alloc_apn (listnode *start, listnode *target, void *arg);
ADIVIM_APN alloc_apn (listnode *start, ADIVIM_APN size);
bool _section_lookup (listnode *start,listnode *target, void *arg);
ADIVIM_JUDGEMENT section_lookup (listnode *start, ADIVIM_APN pg);


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
    
    //adivim_mark (req, adivim_judge (section_job (*adivim_section_list, section)));
}

ADIVIM_JUDGEMENT adivim_get_judgement_by_blkno (void *t, int blkno)
{
    struct my_timing_t *tt = (struct my_timing_t *) t;
    ADIVIM_APN pg = blkno/(tt->params->page_size);
    
    return section_lookup (*adivim_section_list, pg);
}

void adivim_init ()
{
    ADIVIM_APN_ALLOC *adivim_hapn_alloc = (ADIVIM_APN_ALLOC *) malloc (sizeof (ADIVIM_APN_ALLOC));
    ADIVIM_APN_ALLOC *adivim_capn_alloc = (ADIVIM_APN_ALLOC *) malloc (sizeof (ADIVIM_APN_ALLOC));
    
    ll_create (adivim_section_list);
    
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
listnode *_ll_insert_at_head(listnode *start, listnode *toinsert)
{
    if ((!start) || (!toinsert)) {
        fprintf(stderr, "Error: invalid value to _ll_insert_at_head\n");
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

void ll_apply (listnode *start, bool (*job) (listnode *start, listnode *target, void *arg), void *arg)
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

bool _free_apn (listnode *start, listnode *target, void *arg)
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
            _ll_insert_at_head(target->prev, newnode);
            
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
            return _free_apn (start, target, toinsert);
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

void free_apn (listnode *start, ADIVIM_APN starting, ADIVIM_APN length)
{
    ADIVIM_APN_ALLOC *arg = malloc (sizeof (ADIVIM_APN_ALLOC));
    arg->starting = starting;
    arg->length = length;
    
    ll_apply (start, _free_apn, (void *) arg);
    free (arg);
}

bool _alloc_apn (listnode *start, listnode *target, void *arg)
{
    ADIVIM_APN_ALLOC *data = (ADIVIM_APN_ALLOC *) target->data;
    ADIVIM_APN size = *((ADIVIM_APN *) arg);
    
    if (size < data->length)
    {
        *((ADIVIM_APN *) arg) = data->starting;
        data->starting += size;
        data->length -= size;
        
        return false;
    }
    
    return true;
}

ADIVIM_APN alloc_apn (listnode *start, ADIVIM_APN size)
{
    ADIVIM_APN *arg = (ADIVIM_APN *) malloc (sizeof (ADIVIM_APN));
    ADIVIM_APN ret;
    *arg = size;
    
    ll_apply (start, _alloc_apn, (void *) arg);
    
    ret = *((ADIVIM_APN *) arg);
    free (arg);
    
    return ret;
}

bool _section_job (listnode *start, listnode *target, void *arg)
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
        _ll_insert_at_head(target->prev, newnode);
        
        // init new section
        newdata->starting = toinsert->starting;
        newdata->adivim_access_log.read_count = toinsert->adivim_access_log.read_count;
        newdata->adivim_access_log.write_count = toinsert->adivim_access_log.write_count;
        
        if (toinsert->starting + toinsert->length <= data->starting)
        {
            // Really newbe
            newdata->length = toinsert->length;
            newdata->adivim_judgement = adivim_judge (newdata);
            return false;
        }
        else
        {
            // divide section occur. that will be devided in follwing code.
            newdata->length = data->starting - toinsert->starting;
            newdata->adivim_judgement = adivim_judge (newdata);
            toinsert->length = toinsert->starting + toinsert->length - data->starting;
            toinsert->starting = data->starting;
            
            return _section_job (start, target, toinsert);
        }
    }
    
    if ((toinsert->starting) == (data->starting))
    {
        if (data->length <= toinsert->length)
        {
            data->adivim_access_log.read_count += toinsert->adivim_access_log.read_count;
            data->adivim_access_log.write_count += toinsert->adivim_access_log.write_count;
            data->adivim_judgement = adivim_judge (data);
            
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
            _ll_insert_at_head(target->prev, newnode);
            
            // init new section
            newdata->starting = toinsert->starting;
            newdata->length = toinsert->length;
            newdata->adivim_access_log.read_count = data->adivim_access_log.read_count + toinsert->adivim_access_log.read_count;
            newdata->adivim_access_log.write_count = data->adivim_access_log.write_count + toinsert->adivim_access_log.write_count;
            newdata->adivim_judgement = adivim_judge (newdata);
            
            // modify target
            data->starting += toinsert->length;
            data->length -= toinsert->length;
            
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
        _ll_insert_at_head(target, newnode);
        
        // init new section
        newdata->starting = toinsert->starting;
        newdata->length = data->starting + data->length - toinsert->starting;
        newdata->adivim_access_log.read_count = data->adivim_access_log.read_count;
        newdata->adivim_access_log.write_count = data->adivim_access_log.write_count;
        newdata->adivim_judgement = adivim_judge (newdata);
        
        // modify target
        data->length = toinsert->starting - data->starting;
        
        return true;
    }
    
    return false;
}

void section_job (listnode *start, ADIVIM_SECTION *arg)
{
    ll_apply (start, _section_job, (void *) arg);
}

bool _section_lookup (listnode *start,listnode *target, void *arg)
{
    ADIVIM_SECTION *data = (ADIVIM_SECTION *) target->data;
    ADIVIM_APN pg = *((ADIVIM_APN *) arg);

    if (data->starting < pg && pg < data->starting + data->length)
    {
        ADIVIM_JUDGEMENT *datajudge = &(data->adivim_judgement);
        arg = (ADIVIM_JUDGEMENT *) malloc (sizeof (ADIVIM_JUDGEMENT));
        
        ((ADIVIM_JUDGEMENT *) arg)->adivim_type = datajudge->adivim_type;
        ((ADIVIM_JUDGEMENT *) arg)->adivim_hapn = datajudge->adivim_hapn;
        ((ADIVIM_JUDGEMENT *) arg)->adivim_capn = datajudge->adivim_capn;
        
        return false;
    }
    
    return true;
}

ADIVIM_JUDGEMENT section_lookup (listnode *start, ADIVIM_APN pg)
{
    ADIVIM_APN *arg = malloc (sizeof (ADIVIM_APN));
    ADIVIM_JUDGEMENT ret;
    *arg = pg;
    
    ll_apply (start, _section_lookup, (void *) arg);
    
    ret.adivim_type = ((ADIVIM_JUDGEMENT *) arg)->adivim_type;
    ret.adivim_hapn = ((ADIVIM_JUDGEMENT *) arg)->adivim_hapn;
    ret.adivim_capn = ((ADIVIM_JUDGEMENT *) arg)->adivim_capn;
    
    return ret;
}

ADIVIM_JUDGEMENT adivim_judge (ADIVIM_SECTION *section)
{
    if (section->adivim_access_log.read_count > 1 && section->adivim_access_log.write_count > 1) // Section will be hot
    {
        // Assign ADIVIM_TYPE
        switch (section->adivim_judgement.adivim_type)
        {
            case ADIVIM_HOT : break;
            case ADIVIM_COLD :
                section->adivim_judgement.adivim_type = ADIVIM_HOT; break;
            case ADIVIM_HOT_TO_COLD : // Some error occured because ADIVIM_HOT_TO_COLD and ADIVIM_COLD_TO_HOT is only used in ioreq_event. Section should be either ADIVIM_HOT or ADIVIM_COLD.
                section->adivim_judgement.adivim_type = ADIVIM_HOT; break;
            case ADIVIM_COLD_TO_HOT :
                section->adivim_judgement.adivim_type = ADIVIM_HOT; break;
        };
        
        // Allocate ADIVIM_HAPN
        alloc_apn (*adivim_free_hapn_list, section->length);
    }
    else // Section will be cold
    {
        // Assign ADIVIM_TYPE
        switch (section->adivim_judgement.adivim_type)
        {
            case ADIVIM_HOT : 
                section->adivim_judgement.adivim_type = ADIVIM_COLD; break;
            case ADIVIM_COLD : break;
            case ADIVIM_HOT_TO_COLD : // Some error occured because ADIVIM_HOT_TO_COLD and ADIVIM_COLD_TO_HOT is only used in ioreq_event. Section should be either ADIVIM_HOT or ADIVIM_COLD.
                section->adivim_judgement.adivim_type = ADIVIM_COLD; break;
            case ADIVIM_COLD_TO_HOT :
                section->adivim_judgement.adivim_type = ADIVIM_COLD; break;
        };
        
        // Allocate ADIVIM_CAPN
        alloc_apn (*adivim_free_capn_list, section->length);
    }
    
    return section->adivim_judgement;
}

void adivim_mark (ioreq_event *req, ADIVIM_JUDGEMENT adivim_judgement)
{
    req->adivim_judgement = adivim_judgement;
}

#endif
