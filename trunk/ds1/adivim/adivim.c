// Disksim SSD ADIVIM support
// ©2012 Seoul National University lecture project1 ADIVIM PROJECT team. All right reserved.
/*
#include "disksim_global.h"
#include "adivim.h"
#include "../ssdmodel/ssd_utils.h" // For listnode.

static listnode *adivim_section_list; // Sectino list.
static listnode *adivim_free_hapn_list; // Free hot apn list.
static listnode *adivim_free_capn_list; // Free cold apn list.

/*
 * For given section information,
 * judge which is hot and cold.
 */
ADIVIM_SECTION_TYPE adivim_judge (adivim_section *sec);

/*
 * For given request,
 * update section list and return updated section infomation.
 */
adivim_section adivim_update_section_list (ioreq_event *req);

/*
 * For given request and type,
 * mark type to request.
 */
void adivim_mark (ioreq_event *req, ADIVIM_SECTION_TYPE type);



void adivim_separate (ioreq_event *req)
{
    adivim_mark (req, adivim_judge (adivim_update_section_list (req)));
}


ADIVIM_SECTION_TYPE adivim_judge (adivim_section *sec)
{
    if (sec->log->read_count > 1 & sec->log->write_count > 1)
    {
        sec->type = ADIVIM_SECTION_HOT;
    }
    else
    {
        sec->type = ADIVIM_SECTION_COLD;
    }
    
    return sec->type;
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

void adivim_mark (ioreq_event *req, ADIVIM_SECTION_TYPE type)
{
    req->type = type;
}*/