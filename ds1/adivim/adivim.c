// Disksim SSD ADIVIM support
// ©2012 Seoul National University lecture project1 ADIVIM PROJECT team. All right reserved.

#include "disksim_global.h"

#ifdef ADIVIM

#include "adivim.h"
#include "ssd_timing.h"
#include "../ssdmodule/modules/ssdmodel_ssd_param.h"
#include "../ssdmodel/ssd_utils.h" // For listnode.

void adivim_init ();
void adivim_assign_judgement (ssd_timing_t *t, ioreq_event *req);
ADIVIM_JUDGEMENT adivim_get_judgement_by_blkno (ssd_timing_t *t, int blkno);

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

void ll_do_in_order (*start, bool (*cmp) (void * data), void (*job) (void *data));

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
void adivim_mark (ioreq_event *req, ADIVIM_TYPE type);

void adivim_assign_judgement (ssd_timing_t *t, ioreq_event *req)
{
    struct my_timing_t *tt = (struct my_timing_t *) t;
    ADIVIM_SECTION *section = (ADIVIM_SECTION *) malloc (sizeof (ADIVIM_SECTION));
    
    section->starting = (req->blkno)/(tt->params->page_size);
    section->length = (req->bcount)/(tt->params->page_size);
    
    if (curr->flags & READ)
    {
        section->adivim_section_log.read_count = 1;
        section->adivim_section_log.write_count = 0;
    }
    else
    {
        section->adivim_section_log.read_count = 0;
        section->adivim_section_log.write_count = 1;
    }
    
    adivim_mark (req, adivim_judge (section_job (section)));
}

ADIVIM_JUDGEMENT adivim_get_judgement_by_blkno (ssd_timing_t *t, int blkno)
{
    
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

/*
 * Insert the data at the tail.
 */
listnode *ll_insert_at_tail(listnode *start, void *data)
{
    /* allocate a new node */
    listnode *newnode = malloc(sizeof(listnode));
    newnode->data = data;
    
    /* increment the number of entries */
    ((header_data *)(start->data))->size ++;
    
    return _ll_insert_at_tail(start, newnode);
}

void ll_apply (*start, bool (*job) (listnode *start, listnode *target, void *arg), void *arg)
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
    ADIVIM_APN_ALLOC *arg = (ADIVIM_APN_ALLOC *) arg;
    
    if (data->starting + data->length < arg->starting)
    {
        return true;
    }
    
    if (arg->starting < data->starting)
    {
        if (arg->starting + arg->length < data->starting)
        {
            // concatenation doesn't occur
            // allocate a new node
            listnode *newnode = malloc(sizeof(listnode));
            ADIVIM_APN_ALLOC *newdata = malloc (sizeof (ADIVIM_APN_ALLOC));
            newdata->starting = arg->starting;
            newdata->length = arg->length;
            newnode->data = (void *) newdata
            
            // increment the number of entries
            ((header_data *)(start->data))->size ++;
            
            // insert before target
            _ll_insert_at_head(target->prev, newnode);
            
            return false;
        }
        else
        {
            // concatenation occur
            // modify target
            ADIVIM_APN diff = data->starting - arg->starting;
            data->starting = arg->starting;
            data->length += diff;
        }

    }
    
    if (arg->starting < data->starting + data->length && data->starting + data->length < arg->starting + arg->length)
    {
        if (target->next->data->starting < arg->starting + arg->length)
        {
            // concatenation occur
            // modify target
            void *nextdata = target->next->data;
            ADIVIM_APN diff = nextdata->starting + nextdata->length - (data->starting + data->length);
            data->length += diff;
            
            // release next node and check again on target
            ll_release_node (start, target->next);
            return free_apn (start, target, arg);
        }
        else
        {
            // concatenation doesn't occur
            //modify target
            ADIVIM_APN diff = arg->starting + arg->length - (data->starting + data->length);
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
    ADIVIM_SECTION *arg = (ADIVIM_SECTION *) arg;
    
    if (data->starting + data->length < arg->starting)
    {
        return true;
    }
    
    if (arg->starting < data->starting)
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
        newdata->starting = arg->starting;
        newdata->adivim_access_log.read_count = arg->adivim_access_log.read_count;
        newdata->adivim_access_log.write_count = arg->adivim_access_log.write_count;
        
        if (arg->starting + arg->length <= data->starting)
        {
            // Really newbe
            newdata->length = arg->length;
            newdata->adivim_judgement = adivim_judge (newdata);
            return false;
        }
        else
        {
            // divide section occur. that will be devided in follwing code.
            newdata->length = data->starting - arg->starting;
            newdata->adivim_judgement = adivim_judge (newdata);
            arg->length = arg->starting + arg->length - data->starting;
            arg->starting = data->starting;
            
            return _section_job (start, target, arg);
        }
    }
    
    if (arg->starting = data->starting)
    {
        if (data->length <= arg->length)
        {
            data->adivim_access_log.read_count += arg->adivim_access_log.read_count;
            data->adivim_access_log.write_count += arg->adivim_access_log.write_count;
            data->adivim_judgement = adivim_judge (data);
            
            arg->starting += data->length;
            arg->length = arg->length - data->length;
            
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
            newdata->starting = arg->starting;
            newdata->length = arg->length
            newdata->adivim_access_log.read_count = data->adivim_access_log.read_count + arg->adivim_access_log.read_count;
            newdata->adivim_access_log.write_count = data->adivim_access_log.write_count + arg->adivim_access_log.write_count;
            newdata->adivim_judgement = adivim_judge (newdata);
            
            // modify target
            data->starting += arg->length;
            data->length -= arg->length;
            
            return false;
        }
    }
    
    if (data->starting < arg->starting && arg->starting < data->starting + data->length)
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
        newdata->starting = arg->starting;
        newdata->length = data->starting + data->length - arg->starting;
        newdata->adivim_access_log.read_count = data->adivim_access_log.read_count;
        newdata->adivim_access_log.write_count = data->adivim_access_log.write_count;
        newdata->adivim_judgement = adivim_judge (newdata);
        
        // modify target
        data->length = arg->starting - data->starting;
        
        return true;
    }
    
    return false;
}

void section_job (listnode *start, ADIVIM_SECTION *arg)
{
    ll_apply (start, _section_job, (void *) arg);
}

ADIVIM_JUDGEMENT adivim_judge (ADIVIM_SECTION *section)
{
    if (section->adivim_section_log.read_count > 1 & section->adivim_section_log.write_count > 1) // Section will be hot
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
        alloc_apn (adivim_free_hapn_list, section->length);
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
        alloc_apn (adivim_free_capn_list, section->length);
    }
    
    return section->adivim_judgement;
}

adivim_section adivim_section_list_update (ioreq_event *req)
{
    // Lookup
    
    if (the_same_section_found)
    {
		if (IO_request.type = read)
        {
			the_same_section.access_count.read_count++;
        }
		else
        {
			the_same_section.access_count.write_count++;
        }
        
        return (the_same_section.access_count);
    }
    else if (several_overlapping_sections_found)
    {
        divide_sections_properly ();
        divided_section = the_same_section_with_the_IO_request_among_devided_sections;
        if (IO_request.type = read)
        {
            divided_section.read_count++;
        }
        else
        {
            divided_section.write_count++;
        }
        
        return (divided_section.access_count);
    }
	else // Not found
    {
        add_to_section_array (IO_request);
        if (IO_request.type = read)
        {
            new_section.access_count.read_count++;
        }
        else
        {
            new_section.access_count.write_count++;
        }
        return (new_section.access_count);
    }
}

void adivim_mark (ioreq_event *req, ADIVIM_JUDGEMENT adivim_judgement)
{
    req->adivim_judgement = adivim_judgement;
}

#endif