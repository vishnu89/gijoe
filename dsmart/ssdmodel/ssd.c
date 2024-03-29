// DiskSim SSD support
// �2008 Microsoft Corporation. All Rights Reserved

#include "ssd.h"
#include "ssd_timing.h"
#include "ssd_clean.h"
#include "ssd_gang.h"
#include "ssd_init.h"
#include "modules/ssdmodel_ssd_param.h"
#include "disksim_global.h"
#ifdef ADIVIM
#include <stdbool.h>
#endif

#ifndef sprintf_s
#define sprintf_s3(x,y,z) sprintf(x,z)
#define sprintf_s4(x,y,z,w) sprintf(x,z,w)
#define sprintf_s5(x,y,z,w,s) sprintf(x,z,w,s)
#else
#define sprintf_s3(x,y,z) sprintf_s(x,z)
#define sprintf_s4(x,y,z,w) sprintf_s(x,z,w)
#define sprintf_s5(x,y,z,w,s) sprintf_s(x,z,w,s)
#endif

#ifndef _strdup
#define _strdup strdup
#endif

static void ssd_request_complete(ioreq_event *curr);
static void ssd_media_access_request(ioreq_event *curr);

#ifdef ADIVIM
void print (char c, int n);
static ssd_t *prev_ssd;
#endif

struct ssd *getssd (int devno)
{
    struct ssd *s;
    ASSERT1((devno >= 0) && (devno < MAXDEVICES), "devno", devno);
    
    s = disksim->ssdinfo->ssds[devno];
    return (disksim->ssdinfo->ssds[devno]);
}

int ssd_set_depth (int devno, int inbusno, int depth, int slotno)
{
    ssd_t *currdisk;
    int cnt;
    
    currdisk = getssd (devno);
    assert(currdisk);
    cnt = currdisk->numinbuses;
    currdisk->numinbuses++;
    if ((cnt + 1) > MAXINBUSES) {
        fprintf(stderr, "Too many inbuses specified for ssd %d - %d\n", devno, (cnt+1));
        exit(1);
    }
    currdisk->inbuses[cnt] = inbusno;
    currdisk->depth[cnt] = depth;
    currdisk->slotno[cnt] = slotno;
    return(0);
}

int ssd_get_depth (int devno)
{
    ssd_t *currdisk;
    currdisk = getssd (devno);
    return(currdisk->depth[0]);
}

int ssd_get_slotno (int devno)
{
    ssd_t *currdisk;
    currdisk = getssd (devno);
    return(currdisk->slotno[0]);
}

int ssd_get_inbus (int devno)
{
    ssd_t *currdisk;
    currdisk = getssd (devno);
    return(currdisk->inbuses[0]);
}

int ssd_get_maxoutstanding (int devno)
{
    ssd_t *currdisk;
    currdisk = getssd (devno);
    return(currdisk->maxqlen);
}

double ssd_get_blktranstime (ioreq_event *curr)
{
    ssd_t *currdisk;
    double tmptime;
    
    currdisk = getssd (curr->devno);
    tmptime = bus_get_transfer_time(ssd_get_busno(curr), 1, (curr->flags & READ));
    if (tmptime < currdisk->blktranstime) {
        tmptime = currdisk->blktranstime;
    }
    return(tmptime);
}

int ssd_get_busno (ioreq_event *curr)
{
    ssd_t *currdisk;
    intchar busno;
    int depth;
    
    currdisk = getssd (curr->devno);
    busno.value = curr->busno;
    depth = currdisk->depth[0];
    return(busno.byte[depth]);
}

static void ssd_assert_current_activity(ssd_t *currdisk, ioreq_event *curr)
{
    assert(currdisk->channel_activity != NULL &&
           currdisk->channel_activity->devno == curr->devno &&
           currdisk->channel_activity->blkno == curr->blkno &&
           currdisk->channel_activity->bcount == curr->bcount);
}

/*
 * ssd_send_event_up_path()
 *
 * Acquires the bus (if not already acquired), then uses bus_delay to
 * send the event up the path.
 *
 * If the bus is already owned by this device or can be acquired
 * immediately (interleaved bus), the event is sent immediately.
 * Otherwise, ssd_bus_ownership_grant will later send the event.
 */
static void ssd_send_event_up_path (ioreq_event *curr, double delay)
{
    ssd_t *currdisk;
    int busno;
    int slotno;
    
    // fprintf (outputfile, "ssd_send_event_up_path - devno %d, type %d, cause %d, blkno %d\n", curr->devno, curr->type, curr->cause, curr->blkno);
    
    currdisk = getssd (curr->devno);
    
    ssd_assert_current_activity(currdisk, curr);
    
    busno = ssd_get_busno(curr);
    slotno = currdisk->slotno[0];
    
    /* Put new request at head of buswait queue */
    curr->next = currdisk->buswait;
    currdisk->buswait = curr;
    
    curr->tempint1 = busno;
    curr->time = delay;
    if (currdisk->busowned == -1) {
        
        // fprintf (outputfile, "Must get ownership of the bus first\n");
        
        if (curr->next) {
            //fprintf(stderr,"Multiple bus requestors detected in ssd_send_event_up_path\n");
            /* This should be ok -- counting on the bus module to sequence 'em */
        }
        if (bus_ownership_get(busno, slotno, curr) == FALSE) {
            /* Remember when we started waiting (only place this is written) */
            currdisk->stat.requestedbus = simtime;
        } else {
            currdisk->busowned = busno;
            bus_delay(busno, DEVICE, curr->devno, delay, curr); /* Never for SCSI */
        }
    } else if (currdisk->busowned == busno) {
        
        //fprintf (outputfile, "Already own bus - so send it on up\n");
        
        bus_delay(busno, DEVICE, curr->devno, delay, curr);
    } else {
        fprintf(stderr, "Wrong bus owned for transfer desired\n");
        exit(1);
    }
}

/* The idea here is that only one request can "possess" the channel back to the
 controller at a time. All others are enqueued on queue of pending activities.
 "Completions" ... those operations that need only be signaled as done to the
 controller ... are given on this queue.  The "channel_activity" field indicates
 whether any operation currently possesses the channel.
 
 It is our hope that new requests cannot enter the system when the channel is
 possessed by another operation.  This would not model reality!!  However, this
 code (and that in ssd_request_arrive) will handle this case "properly" by enqueuing
 the incoming request.  */

static void ssd_check_channel_activity (ssd_t *currdisk)
{
    while (1) {
        ioreq_event *curr = currdisk->completion_queue;
        currdisk->channel_activity = curr;
        if (curr != NULL) {
            currdisk->completion_queue = curr->next;
            if (currdisk->neverdisconnect) {
                /* already connected */
                if (curr->flags & READ) {
                    /* transfer data up the line: curr->bcount, which is still set to */
                    /* original requested value, indicates how many blks to transfer. */
                    curr->type = DEVICE_DATA_TRANSFER_COMPLETE;
                    ssd_send_event_up_path(curr, (double) 0.0);
                } else {
                    ssd_request_complete (curr);
                }
            } else {
                /* reconnect to controller */
                curr->type = IO_INTERRUPT_ARRIVE;
                curr->cause = RECONNECT;
                ssd_send_event_up_path (curr, currdisk->bus_transaction_latency);
                currdisk->reconnect_reason = DEVICE_ACCESS_COMPLETE;
            }
        } else {
            curr = ioqueue_get_next_request(currdisk->queue);
            currdisk->channel_activity = curr;
            if (curr != NULL) {
                if (curr->flags & READ) {
                    if (!currdisk->neverdisconnect) {
                        ssd_media_access_request(ioreq_copy(curr));
                        curr->type = IO_INTERRUPT_ARRIVE;
                        curr->cause = DISCONNECT;
                        ssd_send_event_up_path (curr, currdisk->bus_transaction_latency);
                    } else {
                        ssd_media_access_request(curr);
                        continue;
                    }
                } else {
                    curr->cause = RECONNECT;
                    curr->type = IO_INTERRUPT_ARRIVE;
                    currdisk->reconnect_reason = IO_INTERRUPT_ARRIVE;
                    ssd_send_event_up_path (curr, currdisk->bus_transaction_latency);
                }
            }
        }
        break;
    }
}

/*
 * ssd_bus_ownership_grant
 * Calls bus_delay to handle the event that the disk has been granted the bus.  I believe
 * this is always initiated by a call to ssd_send_even_up_path.
 */
void ssd_bus_ownership_grant (int devno, ioreq_event *curr, int busno, double arbdelay)
{
    ssd_t *currdisk;
    ioreq_event *tmp;
    
    currdisk = getssd (devno);
    
    ssd_assert_current_activity(currdisk, curr);
    tmp = currdisk->buswait;
    while ((tmp != NULL) && (tmp != curr)) {
        tmp = tmp->next;
    }
    if (tmp == NULL) {
        fprintf(stderr, "Bus ownership granted to unknown ssd request - devno %d, busno %d\n", devno, busno);
        exit(1);
    }
    currdisk->busowned = busno;
    currdisk->stat.waitingforbus += arbdelay;
    //ASSERT (arbdelay == (simtime - currdisk->stat.requestedbus));
    currdisk->stat.numbuswaits++;
    bus_delay(busno, DEVICE, devno, tmp->time, tmp);
}

void ssd_bus_delay_complete (int devno, ioreq_event *curr, int sentbusno)
{
    ssd_t *currdisk;
    intchar slotno;
    intchar busno;
    int depth;
    
    currdisk = getssd (devno);
    ssd_assert_current_activity(currdisk, curr);
    
    // fprintf (outputfile, "Entered ssd_bus_delay_complete\n");
    
    // EPW: I think the buswait logic doesn't do anything, is confusing, and risks
    // overusing the "next" field, although an item shouldn't currently be a queue.
    if (curr == currdisk->buswait) {
        currdisk->buswait = curr->next;
    } else {
        ioreq_event *tmp = currdisk->buswait;
        while ((tmp->next != NULL) && (tmp->next != curr)) {
            tmp = tmp->next;
        }
        if (tmp->next != curr) {
            // fixed a warning here
            //fprintf(stderr, "Bus delay complete for unknown ssd request - devno %d, busno %d\n", devno, busno.value);
            fprintf(stderr, "Bus delay complete for unknown ssd request - devno %d, busno %d\n", devno, curr->busno);
            exit(1);
        }
        tmp->next = curr->next;
    }
    busno.value = curr->busno;
    slotno.value = curr->slotno;
    depth = currdisk->depth[0];
    slotno.byte[depth] = slotno.byte[depth] >> 4;
    curr->time = 0.0;
    if (depth == 0) {
        intr_request ((event *)curr);
    } else {
        bus_deliver_event(busno.byte[depth], slotno.byte[depth], curr);
    }
}


/*
 * send completion up the line
 */
static void ssd_request_complete(ioreq_event *curr)
{
    ssd_t *currdisk;
    ioreq_event *x;
    
    // fprintf (outputfile, "Entering ssd_request_complete: %12.6f\n", simtime);
    
    currdisk = getssd (curr->devno);
    ssd_assert_current_activity(currdisk, curr);
    
    if ((x = ioqueue_physical_access_done(currdisk->queue,curr)) == NULL) {
        fprintf(stderr, "ssd_request_complete:  ioreq_event not found by ioqueue_physical_access_done call\n");
        exit(1);
    }
    
    /* send completion interrupt */
    curr->type = IO_INTERRUPT_ARRIVE;
    curr->cause = COMPLETION;
    ssd_send_event_up_path(curr, currdisk->bus_transaction_latency);
}

static void ssd_bustransfer_complete (ioreq_event *curr)
{
    // fprintf (outputfile, "Entering ssd_bustransfer_complete for disk %d: %12.6f\n", curr->devno, simtime);
    
    if (curr->flags & READ) {
        ssd_request_complete (curr);
    } else {
        ssd_t *currdisk = getssd (curr->devno);
        ssd_assert_current_activity(currdisk, curr);
        if (currdisk->neverdisconnect == FALSE) {
            /* disconnect from bus */
            ioreq_event *tmp = ioreq_copy (curr);
            tmp->type = IO_INTERRUPT_ARRIVE;
            tmp->cause = DISCONNECT;
            ssd_send_event_up_path (tmp, currdisk->bus_transaction_latency);
            ssd_media_access_request (curr);
        } else {
            ssd_media_access_request (curr);
            ssd_check_channel_activity (currdisk);
        }
    }
}

/*
 * returns the logical page number within an element given a block number as
 * issued by the file system
 */
int ssd_logical_pageno(int blkno, ssd_t *s)
{
    int apn;
    int lpn;
    
    // absolute page number is the block number as written by the above layer
    apn = blkno/s->params.page_size;
    
    // find the logical page number within the ssd element. we maintain the
    // mapping between the logical page number and the actual physical page
    // number. an alternative is that we could maintain the mapping between
    // apn we calculated above and the physical page number. but the range
    // of apn is several times bigger and so we chose to go with the mapping
    // b/w lpn --> physical page number
    lpn = ((apn - (apn % (s->params.element_stride_pages * s->params.nelements)))/
           s->params.nelements) + (apn % s->params.element_stride_pages);
    
    return lpn;
}

int ssd_already_present(ssd_req **reqs, int total, ioreq_event *req)
{
    int i;
    int found = 0;
    
    for (i = 0; i < total; i ++) {
        if ((req->blkno == reqs[i]->org_req->blkno) &&
            (req->flags == reqs[i]->org_req->flags)) {
            found = 1;
            break;
        }
    }
    
    return found;
}

double _ssd_invoke_element_cleaning(int elem_num, ssd_t *s)
{
    double clean_cost = ssd_clean_element(s, elem_num);
    return clean_cost;
}

static int ssd_invoke_element_cleaning(int elem_num, ssd_t *s)
{
    double max_cost = 0;
    int cleaning_invoked = 0;
    ssd_element *elem = &s->elements[elem_num];
    
    // element must be free
    ASSERT(elem->media_busy == FALSE);
    
    max_cost = _ssd_invoke_element_cleaning(elem_num, s);
    
    // cleaning was invoked on this element. we can start
    // the next operation on this elem only after the cleaning
    // gets over.
    if (max_cost > 0) {
        ioreq_event *tmp;
        
        elem->media_busy = 1;
        cleaning_invoked = 1;
        
        // we use the 'blkno' field to store the element number
        tmp = (ioreq_event *)getfromextraq();
        tmp->devno = s->devno;
        tmp->time = simtime + max_cost;
        tmp->blkno = elem_num;
        tmp->ssd_elem_num = elem_num;
        tmp->type = SSD_CLEAN_ELEMENT;
        tmp->flags = SSD_CLEAN_ELEMENT;
        tmp->busno = -1;
        tmp->bcount = -1;
        stat_update (&s->stat.acctimestats, max_cost);
        addtointq ((event *)tmp);
        
        // stat
        elem->stat.tot_clean_time += max_cost;
    }
    
    return cleaning_invoked;
}

static void ssd_activate_elem(ssd_t *currdisk, int elem_num)
{
    ioreq_event *req;
    ssd_req **read_reqs;
    ssd_req **write_reqs;
    int i;
    int read_total = 0;
    int write_total = 0;
    double schtime = 0;
    int max_reqs;
    int tot_reqs_issued;
    double max_time_taken = 0;
    
    
    ssd_element *elem = &currdisk->elements[elem_num];
    
    // if the media is busy, we can't do anything, so return
    if (elem->media_busy == TRUE) {
        return;
    }
    
    ASSERT(ioqueue_get_reqoutstanding(elem->queue) == 0);
    
    // we can invoke cleaning in the background whether there
    // is request waiting or not
    if (currdisk->params.cleaning_in_background) {
        // if cleaning was invoked, wait until
        // it is over ...
        if (ssd_invoke_element_cleaning(elem_num, currdisk)) {
            return;
        }
    }
    
    ASSERT(elem->metadata.reqs_waiting == ioqueue_get_number_in_queue(elem->queue));
    
    if (elem->metadata.reqs_waiting > 0) {
        
        // invoke cleaning in foreground when there are requests waiting
        if (!currdisk->params.cleaning_in_background) {
            // if cleaning was invoked, wait until
            // it is over ...
            if (ssd_invoke_element_cleaning(elem_num, currdisk)) {
                return;
            }
        }
        
        // how many reqs can we issue at once
        if (currdisk->params.copy_back == SSD_COPY_BACK_DISABLE) {
            max_reqs = 1;
        } else {
            if (currdisk->params.num_parunits == 1) {
                max_reqs = 1;
            } else {
                max_reqs = MAX_REQS_ELEM_QUEUE;
            }
        }
        
        // ideally, we should issue one req per plane, overlapping them all.
        // in order to simplify the overlapping strategy, let's issue
        // requests of the same type together.
        
        read_reqs = (ssd_req **) malloc(max_reqs * sizeof(ssd_req *));
        write_reqs = (ssd_req **) malloc(max_reqs * sizeof(ssd_req *));
        
        // collect the requests
        while ((req = ioqueue_get_next_request(elem->queue)) != NULL) {
            int found = 0;
            
            elem->metadata.reqs_waiting --;
            
            // see if we already have the same request in the list.
            // this usually doesn't happen -- but on synthetic traces
            // this weird case can occur.
            if (req->flags & READ) {
                found = ssd_already_present(read_reqs, read_total, req);
            } else {
                found = ssd_already_present(write_reqs, write_total, req);
            }
            
            if (!found) {
                // this is a valid request
                ssd_req *r = malloc(sizeof(ssd_req));
                r->blk = req->blkno;
                r->count = req->bcount;
                r->is_read = req->flags & READ;
                r->org_req = req;
                r->plane_num = -1; // we don't know to which plane this req will be directed at
#ifdef ADIVIM
                r->hc_flag = req->hc_flag;
                r->range = req->range;
                r->perform = req->perform;
#endif
                if (req->flags & READ) {
                    read_reqs[read_total] = r;
                    read_total ++;
                } else {
                    write_reqs[write_total] = r;
                    write_total ++;
                }
                
                // if we have more reqs than we can handle, quit
                if ((read_total >= max_reqs) ||
                    (write_total >= max_reqs)) {
                    break;
                }
            } else {
                // throw this request -- it doesn't make sense
                stat_update (&currdisk->stat.acctimestats, 0);
                req->time = simtime;
                req->ssd_elem_num = elem_num;
                req->type = DEVICE_ACCESS_COMPLETE;
                addtointq ((event *)req);
            }
        }
        
        if (read_total > 0) {
            // first issue all the read requests (it doesn't matter what we
            // issue first). i chose read because reads are mostly synchronous.
            // find the time taken to serve these requests.
            ssd_compute_access_time(currdisk, elem_num, read_reqs, read_total);
            
            // add an event for each request completion
            for (i = 0; i < read_total; i ++) {
                elem->media_busy = TRUE;
                
                // find the maximum time taken by a request
                if (schtime < read_reqs[i]->schtime) {
                    schtime = read_reqs[i]->schtime;
                }
                
                stat_update (&currdisk->stat.acctimestats, read_reqs[i]->acctime);
                read_reqs[i]->org_req->time = simtime + read_reqs[i]->schtime;
                read_reqs[i]->org_req->ssd_elem_num = elem_num;
                read_reqs[i]->org_req->type = DEVICE_ACCESS_COMPLETE;
                
                //printf("R: blk %d elem %d acctime %f simtime %f\n", read_reqs[i]->blk,
                //  elem_num, read_reqs[i]->acctime, read_reqs[i]->org_req->time);
                
                addtointq ((event *)read_reqs[i]->org_req);
                free(read_reqs[i]);
            }
        }
        
        free(read_reqs);
        
        max_time_taken = schtime;
        
        if (write_total > 0) {
            // next issue the write requests
            ssd_compute_access_time(currdisk, elem_num, write_reqs, write_total);
            currdisk->is_updated = true;
            
            // add an event for each request completion.
            // note that we can issue the writes only after all the reads above are
            // over. so, include the maximum read time when creating the event.
            for (i = 0; i < write_total; i ++) {
                elem->media_busy = TRUE;
                
                stat_update (&currdisk->stat.acctimestats, write_reqs[i]->acctime);
                write_reqs[i]->org_req->time = simtime + schtime + write_reqs[i]->schtime;
                //printf("blk %d elem %d acc time %f\n", write_reqs[i]->blk, elem_num, write_reqs[i]->acctime);
                
                if (max_time_taken < (schtime+write_reqs[i]->schtime)) {
                    max_time_taken = (schtime+write_reqs[i]->schtime);
                }
                
                write_reqs[i]->org_req->ssd_elem_num = elem_num;
                write_reqs[i]->org_req->type = DEVICE_ACCESS_COMPLETE;
                //printf("W: blk %d elem %d acctime %f simtime %f\n", write_reqs[i]->blk,
                //  elem_num, write_reqs[i]->acctime, write_reqs[i]->org_req->time);
                
                addtointq ((event *)write_reqs[i]->org_req);
                free(write_reqs[i]);
            }
        }
        
        free(write_reqs);
        
        // statistics
        tot_reqs_issued = read_total + write_total;
#ifdef ADIVIM
        currdisk->stat.w_req_n += write_total;
#endif
	ASSERT(tot_reqs_issued > 0);
        currdisk->elements[elem_num].stat.tot_reqs_issued += tot_reqs_issued;
        currdisk->elements[elem_num].stat.tot_time_taken += max_time_taken;
    }
}


static void ssd_media_access_request_element (ioreq_event *curr)
{
    ssd_t *currdisk = getssd(curr->devno);
    int blkno = curr->blkno;
    int count = curr->bcount;
    
#ifndef ADIVIM
    /* **** CAREFUL ... HIJACKING tempint2 and tempptr2 fields here **** */
    curr->tempint2 = count;
    while (count != 0) {
        
        // find the element (package) to direct the request
        int elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, blkno);
        ssd_element *elem = &currdisk->elements[elem_num];
        
        // create a new sub-request for the element
        ioreq_event *tmp = (ioreq_event *)getfromextraq();
        tmp->devno = curr->devno;
        tmp->busno = curr->busno;
        tmp->flags = curr->flags;
        tmp->blkno = blkno;
        tmp->bcount = ssd_choose_aligned_count(currdisk->params.page_size, blkno, count);
        ASSERT(tmp->bcount == currdisk->params.page_size);
        
        tmp->tempptr2 = curr;
        blkno += tmp->bcount;
        count -= tmp->bcount;
        
        elem->metadata.reqs_waiting ++;
        
        // add the request to the corresponding element's queue
        ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
        ssd_activate_elem(currdisk, elem_num);
    }
#else
    //-------------------- Integreation ---------------------------
    //struct section*;
    int flag, elem_num;
    int range = 0;
    int prev_flag = -1;
    int save;
    int start = 1;
    ssd_element *elem;
    ioreq_event *tmp;
    int perform = 1;
    unsigned int cbn;
    int temp_flag;
    int temp_range;
    /* **** CAREFUL ... HIJACKING tempint2 and tempptr2 fields here **** */
    curr->tempint2 = count;
    
    adivim_assign_judgement ((void *) currdisk, curr);
    
    while(count != 0) {
        
        adivim_assign_flag_by_blkno (currdisk->timing_t, blkno, &flag);
        //fprintf(stdout, "ssd_media_access_request_element: flag is %d\n", flag);
        //0 : cold -> cold
        //1 : hot -> hot
        //2 : cold -> hot
        //3 : hot -> cold
        switch(flag) {
            case 0 : //cold -> cold
                //fprintf(stdout, "Case 0 : cold -> cold \n"); //for debug ADIVIM
                
                if(prev_flag != 0){
                    switch(prev_flag) {
                        case (-1) :
                        case 1 :
                            break;
                        case 2 :
                            //cold_invalid();
                            temp_range = range;
                            
                            while (range >= 0) {
                                
                                // find the element (package) to direct the request
                                elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, save);
                                ssd_element *elem = &currdisk->elements[elem_num];
                                
                                // create a new sub-request for the element
                                tmp = (ioreq_event *)getfromextraq();
                                tmp->devno = curr->devno;
                                tmp->busno = curr->busno;
                                tmp->flags = curr->flags;
                                tmp->blkno = save;
                                tmp->bcount = currdisk->params.page_size;
                                
                                tmp->hc_flag = prev_flag;
                                
                                
                                if(range == 0)
                                {
                                    tmp->perform = 1;
                                    tmp->range = temp_range;
                                }
                                else
                                {
                                    tmp->perform = 0;
                                }
                                /*
                                 if(perform == 1)
                                 {
                                 tmp->perform = 1;
                                 tmp->range = range;
                                 perform = 0;
                                 }
                                 else{
                                 tmp->perform = 0;
                                 }
                                 */
                                
                                tmp->tempptr2 = curr;
                                save += tmp->bcount;
                                range --;
                                
                                elem->metadata.reqs_waiting ++;
                                
                                // add the request to the corresponding element's queue
                                ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
                                ssd_activate_elem(currdisk, elem_num);
                            }
                            
                            start = 1;
                            perform = 1;
                            break;
                        case 3 :
                            //hot_invalid();
                            
                            elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, cbn * currdisk->params.page_size);
                            
       						elem = &currdisk->elements[elem_num];
                            
                            // create a new sub-request for the element
       						tmp = (ioreq_event *)getfromextraq();
      						tmp->devno = curr->devno;
       						tmp->busno = curr->busno;
       						tmp->flags = curr->flags;
       						tmp->blkno = save;
       						tmp->bcount = currdisk->params.page_size;
                            
                            tmp->hc_flag = prev_flag;
		       				tmp->range = range;
                            
                            tmp->tempptr2 = curr;
                            
                            elem->metadata.reqs_waiting ++;
                            
                            // add the request to the corresponding element's queue
                            ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
	       					ssd_activate_elem(currdisk, elem_num);
                            
                            start = 1;
                            
                            break;
                            
                        default :
                            fprintf(stderr, "ssd_media_access_request_element: Wrong hot/cold type\n");
                    }
                }
                
                if(start == 1)
                {
                    save = blkno;
                    cbn = (adivim_get_judgement_by_blkno (currdisk->timing_t, save)).adivim_capn / (currdisk->params.pages_per_block -1);
                    start = 0;
                    range = -1;
                }
                
                blkno += currdisk->params.page_size;
                count -= currdisk->params.page_size;
                range++;
                
                prev_flag = flag;
                
                if(count != 0)
                {
                    adivim_assign_flag_by_blkno (currdisk->timing_t, blkno, &temp_flag);
                    
                    if(flag == temp_flag){
                        
                        if(cbn != ((adivim_get_judgement_by_blkno (currdisk->timing_t, blkno)).adivim_capn / (currdisk->params.pages_per_block -1)))
                        {
                            elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, cbn * currdisk->params.page_size);
                            
                            elem = &currdisk->elements[elem_num];
                            
                            // create a new sub-request for the element
                            tmp = (ioreq_event *)getfromextraq();
                            tmp->devno = curr->devno;
                            tmp->busno = curr->busno;
                            tmp->flags = curr->flags;
                            tmp->blkno = save;
                            tmp->bcount = currdisk->params.page_size;
                            
                            tmp->hc_flag = flag;
                            tmp->range = range;
                            
                            tmp->tempptr2 = curr;
                            
                            elem->metadata.reqs_waiting ++;
                            
                            // add the request to the corresponding element's queue
                            ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
                            ssd_activate_elem(currdisk, elem_num);
                            
                            cbn  = (adivim_get_judgement_by_blkno (currdisk->timing_t, blkno)).adivim_capn / (currdisk->params.pages_per_block -1);
                            save = blkno;
                            range = -1;
                            
                        }
                        
                    }
                    
                }
                else
                {
                   	elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, cbn * currdisk->params.page_size);
                    
                    elem = &currdisk->elements[elem_num];
                    
                    // create a new sub-request for the element
       	       		tmp = (ioreq_event *)getfromextraq();
                    tmp->devno = curr->devno;
                    tmp->busno = curr->busno;
                    tmp->flags = curr->flags;
                    tmp->blkno = save;
                    tmp->bcount = currdisk->params.page_size;
                    
                    tmp->hc_flag = flag;
                    tmp->range = range;
                    
                    tmp->tempptr2 = curr;
                    
                    elem->metadata.reqs_waiting ++;
                    
                    // add the request to the corresponding element's queue
                    ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
                    ssd_activate_elem(currdisk, elem_num);
                    
                }
                
                break;
                
            case 1 : //hot -> hot
                
                // fprintf(stdout, "Case 1 : hot -> hot\n"); //for debug ADIVIM
                
                if(prev_flag != 1){ //accumulate treat process
                    switch(prev_flag) {
                        case (-1) :
                            break;
                        case 0 :
                            elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, cbn * currdisk->params.page_size);
                            
                            elem = &currdisk->elements[elem_num];
                            
                            // create a new sub-request for the element
                            tmp = (ioreq_event *)getfromextraq();
                            tmp->devno = curr->devno;
                            tmp->busno = curr->busno;
                            tmp->flags = curr->flags;
                            tmp->blkno = save;
                            tmp->bcount = currdisk->params.page_size;
                            
                            tmp->hc_flag = prev_flag;
                            tmp->range = range;
                            
                            tmp->tempptr2 = curr;
                            
                            elem->metadata.reqs_waiting ++;
                            
                            // add the request to the corresponding element's queue
                            ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
                            ssd_activate_elem(currdisk, elem_num);
                            
                            start = 1;
                            
                            break;
                        case 2 :
                            //cold_invalid();
                            
                            temp_range = range;
                            
                            while (range >= 0) {
                                
                                // find the element (package) to direct the request
                                elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, save);
                                ssd_element *elem = &currdisk->elements[elem_num];
                                
                                // create a new sub-request for the element
                                tmp = (ioreq_event *)getfromextraq();
                                tmp->devno = curr->devno;
                                tmp->busno = curr->busno;
                                tmp->flags = curr->flags;
                                tmp->blkno = save;
                                tmp->bcount = currdisk->params.page_size;
                                
                                tmp->hc_flag = prev_flag;
                                
                                if(range == 0)
                                {
                                    tmp->perform = 1;
                                    tmp->range = temp_range;
                                }
                                else
                                {
                                    tmp->perform = 0;
                                }
                                /*
                                 if(perform == 1)
                                 {
                                 tmp->perform = 1;
                                 perform = 0;
                                 tmp->range = range;
                                 }
                                 else{
                                 tmp->perform = 0;
                                 }
                                 */
                                tmp->tempptr2 = curr;
                                save += tmp->bcount;
                                range --;
                                
                                elem->metadata.reqs_waiting ++;
                                
                                // add the request to the corresponding element's queue
                                ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
                                ssd_activate_elem(currdisk, elem_num);
                            }
                            
                            start = 1;
                            perform = 0;
                            break;
                            
                        case 3 :
                            //hot_invalid();
                            
                            elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, cbn * currdisk->params.page_size);
                            
       						elem = &currdisk->elements[elem_num];
                            
                            // create a new sub-request for the element
       						tmp = (ioreq_event *)getfromextraq();
      						tmp->devno = curr->devno;
       						tmp->busno = curr->busno;
       						tmp->flags = curr->flags;
       						tmp->blkno = save;
       						tmp->bcount = currdisk->params.page_size;
                            
                            tmp->hc_flag = prev_flag;
		       				tmp->range = range;
                            
                            tmp->tempptr2 = curr;
                            
                            elem->metadata.reqs_waiting ++;
                            
                            // add the request to the corresponding element's queue
                            ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
	       					ssd_activate_elem(currdisk, elem_num);
                            
                            start = 1;
                            
                            break;
                            
                        default :
                            fprintf(stderr, "ssd_media_access_request_element: Wrong hot/cold type\n");
                    }
                }
                
                elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, blkno);
       			elem = &currdisk->elements[elem_num];
       			
                // create a new sub-request for the element
       			tmp = (ioreq_event *)getfromextraq();
      			tmp->devno = curr->devno;
       			tmp->busno = curr->busno;
       			tmp->flags = curr->flags;
       			tmp->blkno = blkno;
       			tmp->bcount = ssd_choose_aligned_count(currdisk->params.page_size, blkno, count);
       			ASSERT(tmp->bcount == currdisk->params.page_size);
                
                tmp->hc_flag = flag;
                
	       		tmp->tempptr2 = curr;
	       		blkno += tmp->bcount;
	       		count -= tmp->bcount;
                
                elem->metadata.reqs_waiting ++;
                
	     		// add the request to the corresponding element's queue
	      		ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
	       		ssd_activate_elem(currdisk, elem_num);
                
                prev_flag = flag;
                
                break;
                
            case 2 : //cold -> hot
                //fprintf(stdout, "Case 2 : cold -> hot\n"); //for debug ADIVIM
                
                if(prev_flag != 2){
                    switch(prev_flag){
                        case (-1):
                            break;
                        case 0 :
                            elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, cbn * currdisk->params.page_size);
                            
                            elem = &currdisk->elements[elem_num];
                            
                            // create a new sub-request for the element
                            tmp = (ioreq_event *)getfromextraq();
                            tmp->devno = curr->devno;
                            tmp->busno = curr->busno;
                            tmp->flags = curr->flags;
                            tmp->blkno = save;
                            tmp->bcount = currdisk->params.page_size;
                            
                            tmp->hc_flag = prev_flag;
                            tmp->range = range;
                            
                            tmp->tempptr2 = curr;
                            
                            elem->metadata.reqs_waiting ++;
                            
                            // add the request to the corresponding element's queue
                            ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
                            ssd_activate_elem(currdisk, elem_num);
                            
                            start = 1;
                            
                            break;
                        case 1 :
                            break;
                        case 3 :
                            //hot_invalid();
                            
                            elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, cbn * currdisk->params.page_size);
                            
       						elem = &currdisk->elements[elem_num];
                            
                            // create a new sub-request for the element
       						tmp = (ioreq_event *)getfromextraq();
      						tmp->devno = curr->devno;
       						tmp->busno = curr->busno;
       						tmp->flags = curr->flags;
       						tmp->blkno = save;
       						tmp->bcount = currdisk->params.page_size;
                            
                            tmp->hc_flag = prev_flag;
		       				tmp->range = range;
                            
                            tmp->tempptr2 = curr;
                            
                            elem->metadata.reqs_waiting ++;
                            
                            // add the request to the corresponding element's queue
                            ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
	       					ssd_activate_elem(currdisk, elem_num);
                            
                            start = 1;
                            
                            break;
                        default :
                            fprintf(stderr, "ssd_media_access_request_element: Wrong hot/cold type\n");
                    }
                }
                
                if(start == 1)
                {
                    save = blkno;
                    range = -1;
                    start = 0;
                }
                
                blkno += currdisk->params.page_size;
                count -= currdisk->params.page_size;
                range++;
                
                prev_flag = flag;
                
                if(count == 0){
                    //cold_invalid();
                    //fprintf(stdout, "range : %d\n", range); //for debug ADIVIM
                    temp_range = range;
                    
                    while (range >= 0) {
                        //	    fprintf(stdout, "blkno : %d\n", save); //for debug ADIVIM
                        
 			      		// find the element (package) to direct the request
 			      		elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, save);
                        //	    fprintf(stdout, "selected elem_num : %d\n", elem_num); //for debug ADIVIM
 			      		ssd_element *elem = &currdisk->elements[elem_num];
                        
                        // create a new sub-request for the element
			       		tmp = (ioreq_event *)getfromextraq();
			       		tmp->devno = curr->devno;
			       		tmp->busno = curr->busno;
			       		tmp->flags = curr->flags;
			       		tmp->blkno = save;
			       		tmp->bcount = currdisk->params.page_size;
			       		
                        tmp->hc_flag = flag;
                        
                        //	fprintf(stdout, "perform : %d\n", perform); //for debug ADIVIM
                        
                        
                        if(range == 0)
                        {
                            tmp->perform = 1;
                            tmp->range = temp_range;
                        }
                        else
                        {
                            tmp->perform = 0;
                        }
                        /*
                         if(perform == 1)
                         {
                         tmp->perform = 1;
                         tmp->range = range;
                         perform = 0;
                         }
                         else{
                         tmp->perform = 0;
                         }
                         */
			       		tmp->tempptr2 = curr;
			       		save += currdisk->params.page_size;
			       		range --;
                        
			       		elem->metadata.reqs_waiting ++;
                        
			       		// add the request to the corresponding element's queue
			       		ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
			       		ssd_activate_elem(currdisk, elem_num);
                    }
                }
                break;
            case 3 : //hot -> cold
                
                //  fprintf(stdout,"Case 3 : hot -> cold\n"); // for debug ADIVIM
                
                if(prev_flag != 3){
                    switch(prev_flag){
                        case (-1):
                            break;
                        case 0 :
                            elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, cbn * currdisk->params.page_size);
                            
       						elem = &currdisk->elements[elem_num];
                            
                            // create a new sub-request for the element
       						tmp = (ioreq_event *)getfromextraq();
      						tmp->devno = curr->devno;
       						tmp->busno = curr->busno;
       						tmp->flags = curr->flags;
       						tmp->blkno = save;
       						tmp->bcount = currdisk->params.page_size;
                            
                            tmp->hc_flag = prev_flag;
                            tmp->range = range;
                            
				       		tmp->tempptr2 = curr;
                            
                            elem->metadata.reqs_waiting ++;
                            
				     		// add the request to the corresponding element's queue
	      					ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
	       					ssd_activate_elem(currdisk, elem_num);
                            
                            start = 1;
                            
                            break;
                            
                        case 1 :
                            break;
                        case 2 :
                            //cold_invalid();
                            temp_range = range;
                            while (range >= 0) {
                                
                                // find the element (package) to direct the request
                                elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, save);
                                ssd_element *elem = &currdisk->elements[elem_num];
                                
                                // create a new sub-request for the element
                                tmp = (ioreq_event *)getfromextraq();
                                tmp->devno = curr->devno;
                                tmp->busno = curr->busno;
                                tmp->flags = curr->flags;
                                tmp->blkno = save;
                                tmp->bcount = currdisk->params.page_size;
                                
                                tmp->hc_flag = prev_flag;
                                
                                if(range == 0)
                                {
                                    tmp->perform = 1;
                                    tmp->range = temp_range;
                                }
                                else
                                {
                                    tmp->perform = 0;
                                }
                                /*
                                 if(perform == 1)
                                 {
                                 tmp->perform = 1;
                                 tmp->range = range;
                                 perform = 0;
                                 }
                                 else{
                                 tmp->perform = 0;
                                 }
                                 */
                                tmp->tempptr2 = curr;
                                save += tmp->bcount;
                                range --;
                                
                                elem->metadata.reqs_waiting ++;
                                
                                // add the request to the corresponding element's queue
                                ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
                                ssd_activate_elem(currdisk, elem_num);
			       			}
                            
                            start = 1;
                            perform = 1;
                            break;
                            
                        default :
                            fprintf(stderr, "ssd_media_access_request_element: Wrong hot/cold type\n");
                    }
                }
                
                if(start == 1)
                {
                    save = blkno;
                    cbn = (adivim_get_judgement_by_blkno (currdisk->timing_t, save)).adivim_capn / (currdisk->params.pages_per_block -1);
                    range = -1;
                    start = 0;
                }
                
                blkno += currdisk->params.page_size;
                count -= currdisk->params.page_size;
                range++;
                
                prev_flag = flag;
                
                if(count != 0)
                {
                    adivim_assign_flag_by_blkno (currdisk->timing_t, blkno, &temp_flag);
                    
                    if(flag == temp_flag){
                        
                        if(cbn != ((adivim_get_judgement_by_blkno (currdisk->timing_t, blkno)).adivim_capn / (currdisk->params.pages_per_block -1)))
                        {
                            elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, cbn * currdisk->params.page_size);
                            
                            elem = &currdisk->elements[elem_num];
                            
                    		// create a new sub-request for the element
                            tmp = (ioreq_event *)getfromextraq();
                            tmp->devno = curr->devno;
                            tmp->busno = curr->busno;
                            tmp->flags = curr->flags;
                            tmp->blkno = save;
                            tmp->bcount = currdisk->params.page_size;
                            
                    		tmp->hc_flag = flag;
                            tmp->range = range;
                            
                    		tmp->tempptr2 = curr;
                            
                    		elem->metadata.reqs_waiting ++;
                            
                    		// add the request to the corresponding element's queue
                    		ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
                            ssd_activate_elem(currdisk, elem_num);
                            
                            cbn  = (adivim_get_judgement_by_blkno (currdisk->timing_t, blkno)).adivim_capn / (currdisk->params.pages_per_block -1);
                            save = blkno;
                            range = -1;
                            
                        }
                        
                    }
                    
                }
                else
                {
                   	elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, cbn * currdisk->params.page_size);
                    
                    elem = &currdisk->elements[elem_num];
                    
                    // create a new sub-request for the element
       	       		tmp = (ioreq_event *)getfromextraq();
                    tmp->devno = curr->devno;
                    tmp->busno = curr->busno;
                    tmp->flags = curr->flags;
                    tmp->blkno = save;
                    tmp->bcount = currdisk->params.page_size;
                    
                    tmp->hc_flag = flag;
                    tmp->range = range;
                    
                    tmp->tempptr2 = curr;
                    
                    elem->metadata.reqs_waiting ++;
                    
                    // add the request to the corresponding element's queue
                    ioqueue_add_new_request(elem->queue, (ioreq_event *)tmp);
                    ssd_activate_elem(currdisk, elem_num);
                    
                }
                
                break;
                
            default :
                fprintf(stderr, "ssd_media_access_request_element: unknown hot/cold class change\n");
                exit(1);
        }
    }
    
#endif
}

static void ssd_media_access_request (ioreq_event *curr)
{
    ssd_t *currdisk = getssd(curr->devno);
    
    switch(currdisk->params.alloc_pool_logic) {
        case SSD_ALLOC_POOL_PLANE:
        case SSD_ALLOC_POOL_CHIP:
            ssd_media_access_request_element(curr);
            break;
            
        case SSD_ALLOC_POOL_GANG:
#if SYNC_GANG
            ssd_media_access_request_gang_sync(curr);
#else
            ssd_media_access_request_gang(curr);
#endif
            break;
            
        default:
            printf("Unknown alloc pool logic %d\n", currdisk->params.alloc_pool_logic);
            ASSERT(0);
    }
}

static void ssd_reconnect_done (ioreq_event *curr)
{
    ssd_t *currdisk;
    
    // fprintf (outputfile, "Entering ssd_reconnect_done for disk %d: %12.6f\n", curr->devno, simtime);
    
    currdisk = getssd (curr->devno);
    ssd_assert_current_activity(currdisk, curr);
    
    if (curr->flags & READ) {
        if (currdisk->neverdisconnect) {
            /* Just holding on to bus; data transfer will be initiated when */
            /* media access is complete.                                    */
            addtoextraq((event *) curr);
            ssd_check_channel_activity (currdisk);
        } else {
            /* data transfer: curr->bcount, which is still set to original */
            /* requested value, indicates how many blks to transfer.       */
            curr->type = DEVICE_DATA_TRANSFER_COMPLETE;
            ssd_send_event_up_path(curr, (double) 0.0);
        }
        
    } else {
        if (currdisk->reconnect_reason == DEVICE_ACCESS_COMPLETE) {
            ssd_request_complete (curr);
            
        } else {
            /* data transfer: curr->bcount, which is still set to original */
            /* requested value, indicates how many blks to transfer.       */
            curr->type = DEVICE_DATA_TRANSFER_COMPLETE;
            ssd_send_event_up_path(curr, (double) 0.0);
        }
    }
}

static void ssd_request_arrive (ioreq_event *curr)
{
    ssd_t *currdisk;
    
    // fprintf (outputfile, "Entering ssd_request_arrive: %12.6f\n", simtime);
    // fprintf (outputfile, "ssd = %d, blkno = %d, bcount = %d, read = %d\n",curr->devno, curr->blkno, curr->bcount, (READ & curr->flags));
    
    currdisk = getssd(curr->devno);
    
    /* verify that request is valid. */
    if ((curr->blkno < 0) || (curr->bcount <= 0) ||
        ((curr->blkno + curr->bcount) > currdisk->numblocks)) {
        fprintf(stderr, "Invalid set of blocks requested from ssd - blkno %d, bcount %d, numblocks %d\n", curr->blkno, curr->bcount, currdisk->numblocks);
        exit(1);
    }
    
    /* create a new request, set it up for initial interrupt */
    ioqueue_add_new_request(currdisk->queue, curr);
    if (currdisk->channel_activity == NULL) {
        
        curr = ioqueue_get_next_request(currdisk->queue);
        currdisk->busowned = ssd_get_busno(curr);
        currdisk->channel_activity = curr;
        currdisk->reconnect_reason = IO_INTERRUPT_ARRIVE;
        
        if (curr->flags & READ) {
            if (!currdisk->neverdisconnect) {
                ssd_media_access_request (ioreq_copy(curr));
                curr->cause = DISCONNECT;
                curr->type = IO_INTERRUPT_ARRIVE;
                ssd_send_event_up_path(curr, currdisk->bus_transaction_latency);
            } else {
                ssd_media_access_request (curr);
                ssd_check_channel_activity(currdisk);
            }
        } else {
            curr->cause = READY_TO_TRANSFER;
            curr->type = IO_INTERRUPT_ARRIVE;
            ssd_send_event_up_path(curr, currdisk->bus_transaction_latency);
        }
    }
}

/*
 * cleaning in an element is over.
 */
static void ssd_clean_element_complete(ioreq_event *curr)
{
    ssd_t *currdisk;
    int elem_num;
    
    currdisk = getssd (curr->devno);
    elem_num = curr->ssd_elem_num;
    ASSERT(currdisk->elements[elem_num].media_busy == TRUE);
    
    // release this event
    addtoextraq((event *) curr);
    
    // activate the gang to serve the next set of requests
    currdisk->elements[elem_num].media_busy = 0;
    ssd_activate_elem(currdisk, elem_num);
}

void ssd_complete_parent(ioreq_event *curr, ssd_t *currdisk)
{
    ioreq_event *parent;
    
    /* **** CAREFUL ... HIJACKING tempint2 and tempptr2 fields here **** */
    parent = curr->tempptr2;
    parent->tempint2 -= curr->bcount;
    
    if (parent->tempint2 == 0) {
        ioreq_event *prev;
        
        assert(parent != currdisk->channel_activity);
        prev = currdisk->completion_queue;
        if (prev == NULL) {
            currdisk->completion_queue = parent;
            parent->next = prev;
        } else {
            while (prev->next != NULL)
                prev = prev->next;
            parent->next = prev->next;
            prev->next = parent;
        }
        if (currdisk->channel_activity == NULL) {
            ssd_check_channel_activity (currdisk);
        }
    }
}

static void ssd_access_complete_element(ioreq_event *curr)
{
    ssd_t *currdisk;
    int elem_num;
    ssd_element  *elem;
    ioreq_event *x;
#ifdef ADIVIM
    
    int flag;
    unsigned int apn;
    unsigned int cbn;
    
#endif
    
    currdisk = getssd (curr->devno);
    
#ifndef ADIVIM
    
    elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, curr->blkno);
#else
    
    adivim_assign_flag_by_blkno (currdisk->timing_t, curr->blkno, &flag);
    
    switch(flag){
	    case 0 : //cold -> cold
	    case 3 : //hot -> cold
            apn = adivim_get_judgement_by_blkno(currdisk->timing_t, curr->blkno).adivim_capn;
            cbn = (apn / (currdisk->params.pages_per_block -1));
            elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, cbn * currdisk->params.page_size);
            
            break;
	    case 1 : // hot -> hot
	    case 2 : // cold->hot
            elem_num = currdisk->timing_t->choose_element(currdisk->timing_t, curr->blkno);
            break;
	    default :
            fprintf(stderr, "Wrong hot/cold type\n");
            exit(1);
	    	break;
    }
    
#endif
    
#ifndef ADIVIM
    ASSERT(elem_num == curr->ssd_elem_num);
#else
    if(elem_num != curr->ssd_elem_num)
    {
	    fprintf(stderr, "curr->blkno : %d\n", curr->blkno);
	    fprintf(stderr, "elem_num : %d\ncurr->ssd_elem_num : %d\n", elem_num, curr->ssd_elem_num);
	    //ASSERT(elem_num == curr->ssd_elem_num);
        //curr->ssd_elem_num = elem_num;
    }
#endif
    
    elem = &currdisk->elements[/*elem_num*/curr->ssd_elem_num];
    
    if ((x = ioqueue_physical_access_done(elem->queue,curr)) == NULL) {
        fprintf(stderr, "ssd_access_complete:  ioreq_event not found by ioqueue_physical_access_done call\n");
        exit(1);
    }
    
    // all the reqs are over
    if (ioqueue_get_reqoutstanding(elem->queue) == 0) {
        elem->media_busy = FALSE;
    }
    
    ssd_complete_parent(curr, currdisk);
    addtoextraq((event *) curr);
    ssd_activate_elem(currdisk, elem_num);
}

static void ssd_access_complete(ioreq_event *curr)
{
    ssd_t *currdisk = getssd (curr->devno);;
    
    switch(currdisk->params.alloc_pool_logic) {
        case SSD_ALLOC_POOL_PLANE:
        case SSD_ALLOC_POOL_CHIP:
            ssd_access_complete_element(curr);
            break;
            
        case SSD_ALLOC_POOL_GANG:
#if SYNC_GANG
            ssd_access_complete_gang_sync(curr);
#else
            ssd_access_complete_gang(curr);
#endif
            break;
            
        default:
            printf("Unknown alloc pool logic %d\n", currdisk->params.alloc_pool_logic);
            ASSERT(0);
    }
}

/* intermediate disconnect done */
static void ssd_disconnect_done (ioreq_event *curr)
{
    ssd_t *currdisk;
    
    currdisk = getssd (curr->devno);
    ssd_assert_current_activity(currdisk, curr);
    
    // fprintf (outputfile, "Entering ssd_disconnect for disk %d: %12.6f\n", currdisk->devno, simtime);
    
    addtoextraq((event *) curr);
    
    if (currdisk->busowned != -1) {
        bus_ownership_release(currdisk->busowned);
        currdisk->busowned = -1;
    }
    ssd_check_channel_activity (currdisk);
}

/* completion disconnect done */
static void ssd_completion_done (ioreq_event *curr)
{
    ssd_t *currdisk = getssd (curr->devno);
    ssd_assert_current_activity(currdisk, curr);
    
    // fprintf (outputfile, "Entering ssd_completion for disk %d: %12.6f\n", currdisk->devno, simtime);
    
    addtoextraq((event *) curr);
    
    if (currdisk->busowned != -1) {
        bus_ownership_release(currdisk->busowned);
        currdisk->busowned = -1;
    }
    
    ssd_check_channel_activity (currdisk);
}

static void ssd_interrupt_complete (ioreq_event *curr)
{
    // fprintf (outputfile, "Entered ssd_interrupt_complete - cause %d\n", curr->cause);
    
    switch (curr->cause) {
            
        case RECONNECT:
            ssd_reconnect_done(curr);
            break;
            
        case DISCONNECT:
            ssd_disconnect_done(curr);
            break;
            
        case COMPLETION:
            ssd_completion_done(curr);
            break;
            
        default:
            ddbg_assert2(0, "bad event type");
    }
}

void ssd_event_arrive (ioreq_event *curr)
{
    ssd_t *currdisk;
    
    // fprintf (outputfile, "Entered ssd_event_arrive: time %f (simtime %f)\n", curr->time, simtime);
    // fprintf (outputfile, " - devno %d, blkno %d, type %d, cause %d, read = %d\n", curr->devno, curr->blkno, curr->type, curr->cause, curr->flags & READ);
    
    currdisk = getssd (curr->devno);
#ifdef ADIVIM
    adivim_ssd_print_image (currdisk);
     
     switch (curr->type) {
     
     case IO_ACCESS_ARRIVE:
     printf ("ssd_event_arrive: IO_ACCESS_ARRIVE\n");
     break;
     
     case DEVICE_OVERHEAD_COMPLETE:
     printf ("ssd_event_arrive: DEVICE_OVERHEAD_COMPLETE\n");
     break;
     
     case DEVICE_ACCESS_COMPLETE:
     printf ("ssd_event_arrive: DEVICE_ACCESS_COMPLETE\n");
     break;
     
     case DEVICE_DATA_TRANSFER_COMPLETE:
     printf ("ssd_event_arrive: DEVICE_DATA_TRANSFER_COMPLETE\n");
     break;
     
     case IO_INTERRUPT_COMPLETE:
     printf ("ssd_event_arrive: IO_INTERRUPT_COMPLETE\n");
     break;
     
     case IO_QLEN_MAXCHECK:
     // Used only at initialization time to set up queue stuff
     printf ("ssd_event_arrive: IO_QLEN_MAXCHECK\n");
     break;
     
     case SSD_CLEAN_GANG:
     printf ("ssd_event_arrive: SSD_CLEAN_GANG\n");
     break;
     
     case SSD_CLEAN_ELEMENT:
     printf ("ssd_event_arrive: SSD_CLEAN_ELEMENT\n");
     break;
     }
     
#endif
    
    switch (curr->type) {
            
        case IO_ACCESS_ARRIVE:
            curr->time = simtime + currdisk->overhead;
            curr->type = DEVICE_OVERHEAD_COMPLETE;
            addtointq((event *) curr);
            break;
            
        case DEVICE_OVERHEAD_COMPLETE:
            ssd_request_arrive(curr);
            break;
            
        case DEVICE_ACCESS_COMPLETE:
            ssd_access_complete (curr);
            break;
            
        case DEVICE_DATA_TRANSFER_COMPLETE:
            ssd_bustransfer_complete(curr);
            break;
            
        case IO_INTERRUPT_COMPLETE:
            ssd_interrupt_complete(curr);
            break;
            
        case IO_QLEN_MAXCHECK:
            /* Used only at initialization time to set up queue stuff */
            curr->tempint1 = -1;
            curr->tempint2 = ssd_get_maxoutstanding(curr->devno);
            curr->bcount = 0;
            break;
            
        case SSD_CLEAN_GANG:
            ssd_clean_gang_complete(curr);
            break;
            
        case SSD_CLEAN_ELEMENT:
            ssd_clean_element_complete(curr);
            break;
            
        default:
            fprintf(stderr, "Unrecognized event type at ssd_event_arrive\n");
            exit(1);
    }
    
    // fprintf (outputfile, "Exiting ssd_event_arrive\n");
}


int ssd_get_number_of_blocks (int devno)
{
    ssd_t *currdisk = getssd (devno);
    return (currdisk->numblocks);
}


int ssd_get_numcyls (int devno)
{
    ssd_t *currdisk = getssd (devno);
    return (currdisk->numblocks);
}


void ssd_get_mapping (int maptype, int devno, int blkno, int *cylptr, int *surfaceptr, int *blkptr)
{
    ssd_t *currdisk = getssd (devno);
    
    if ((blkno < 0) || (blkno >= currdisk->numblocks)) {
        fprintf(stderr, "Invalid blkno at ssd_get_mapping: %d\n", blkno);
        exit(1);
    }
    
    if (cylptr) {
        *cylptr = blkno;
    }
    if (surfaceptr) {
        *surfaceptr = 0;
    }
    if (blkptr) {
        *blkptr = 0;
    }
}


int ssd_get_avg_sectpercyl (int devno)
{
    return (1);
}


int ssd_get_distance (int devno, ioreq_event *req, int exact, int direction)
{
    /* just return an arbitrary constant, since acctime is constant */
    return 1;
}


// returning 0 to remove warning
double  ssd_get_servtime (int devno, ioreq_event *req, int checkcache, double maxtime)
{
    fprintf(stderr, "device_get_seektime not supported for ssd devno %d\n",  devno);
    assert(0);
    return 0;
}


// returning 0 to remove warning
double  ssd_get_acctime (int devno, ioreq_event *req, double maxtime)
{
    fprintf(stderr, "device_get_seektime not supported for ssd devno %d\n",  devno);
    assert(0);
    return 0;
}


int ssd_get_numdisks (void)
{
    return(numssds);
}


void ssd_cleanstats (void)
{
    int i, j;
    
    for (i=0; i<MAXDEVICES; i++) {
        ssd_t *currdisk = getssd (i);
        if (currdisk) {
            ioqueue_cleanstats(currdisk->queue);
            for (j=0; j<currdisk->params.nelements; j++)
                ioqueue_cleanstats(currdisk->elements[j].queue);
        }
    }
}

void ssd_setcallbacks ()
{
    ioqueue_setcallbacks();
}

int ssd_add(struct ssd *d) {
    int c;
    
    if(!disksim->ssdinfo) ssd_initialize_diskinfo();
    
    for(c = 0; c < disksim->ssdinfo->ssds_len; c++) {
        if(!disksim->ssdinfo->ssds[c]) {
            disksim->ssdinfo->ssds[c] = d;
            numssds++;
            return c;
        }
    }
    
    /* note that numdisks must be equal to diskinfo->disks_len */
    disksim->ssdinfo->ssds =
    realloc(disksim->ssdinfo->ssds,
            2 * c * sizeof(struct ssd *));
    
    bzero(disksim->ssdinfo->ssds + numssds,
          numssds);
    
    disksim->ssdinfo->ssds[c] = d;
    numssds++;
    disksim->ssdinfo->ssds_len *= 2;
    return c;
}


struct ssd *ssdmodel_ssd_loadparams(struct lp_block *b, int *num)
{
    /* temp vars for parameters */
    int n;
    struct ssd *result;
    
    if(!disksim->ssdinfo) ssd_initialize_diskinfo();
    
    result = malloc(sizeof(struct ssd));
    if(!result) return 0;
    bzero(result, sizeof(struct ssd));
    
    n = ssd_add(result);
    
    result->hdr = ssd_hdr_initializer;
    if(b->name)
        result->hdr.device_name = _strdup(b->name);
    
    lp_loadparams(result, b, &ssdmodel_ssd_mod);
    
    device_add((struct device_header *)result, n);
    if (num != NULL)
        *num = n;
    return result;
}


struct ssd *ssd_copy(struct ssd *orig) {
    int i;
#ifdef ADIVIM
    int elem, block;
#endif
    struct ssd *result = malloc(sizeof(struct ssd));
    bzero(result, sizeof(struct ssd));
    memcpy(result, orig, sizeof(struct ssd));
    result->queue = ioqueue_copy(orig->queue);
    for (i=0;i<orig->params.nelements;i++)
        result->elements[i].queue = ioqueue_copy(orig->elements[i].queue);
#ifdef ADIVIM
    for (elem = 0; elem < orig->params.nelements; elem++)
    {
        result->elements[elem].metadata.block_usage = (block_metadata *) malloc (orig->params.blocks_per_element * sizeof (block_metadata));
        for (block = 0; block < orig->params.blocks_per_element; block++)
        {
            result->elements[elem].metadata.block_usage[block].page = (int *) malloc (orig->params.pages_per_block * sizeof (int));
            memcpy (result->elements[elem].metadata.block_usage[block].page, orig->elements[elem].metadata.block_usage[block].page, orig->params.pages_per_block * sizeof (int));
        }
    }
#endif
    return result;
}

void ssd_set_syncset (int setstart, int setend)
{
}


static void ssd_acctime_printstats (int *set, int setsize, char *prefix)
{
    int i;
    statgen * statset[MAXDEVICES];
    
    if (device_printacctimestats) {
        for (i=0; i<setsize; i++) {
            ssd_t *currdisk = getssd (set[i]);
            statset[i] = &currdisk->stat.acctimestats;
        }
        stat_print_set(statset, setsize, prefix);
    }
}


static void ssd_other_printstats (int *set, int setsize, char *prefix)
{
    int i;
    int numbuswaits = 0;
    double waitingforbus = 0.0;
    
    for (i=0; i<setsize; i++) {
        ssd_t *currdisk = getssd (set[i]);
        numbuswaits += currdisk->stat.numbuswaits;
        waitingforbus += currdisk->stat.waitingforbus;
    }
    
    fprintf(outputfile, "%sTotal bus wait time: %f\n", prefix, waitingforbus);
    fprintf(outputfile, "%sNumber of bus waits: %d\n", prefix, numbuswaits);
}

void ssd_print_block_lifetime_distribution(int elem_num, ssd_t *s, int ssdno, double avg_lifetime, char *sourcestr)
{
    const int bucket_size = 20;
    int no_buckets = (100/bucket_size + 1);
    int i;
    int *hist;
    int dead_blocks = 0;
    int n;
    double sum;
    double sum_sqr;
    double mean;
    double variance;
    ssd_element_metadata *metadata = &(s->elements[elem_num].metadata);
    
    // allocate the buckets
    hist = (int *) malloc(no_buckets * sizeof(int));
    memset(hist, 0, no_buckets * sizeof(int));
    
    // to calc the variance
    n = s->params.blocks_per_element;
    sum = 0;
    sum_sqr = 0;
    
    for (i = 0; i < s->params.blocks_per_element; i ++) {
        int bucket;
        int rem_lifetime = metadata->block_usage[i].rem_lifetime;
        double perc = (rem_lifetime * 100.0) / avg_lifetime;
        
        // find out how many blocks have completely been erased.
        if (metadata->block_usage[i].rem_lifetime == 0) {
            dead_blocks ++;
        }
        
        if (perc >= 100) {
            // this can happen if a particular block was not
            // cleaned at all and so its remaining life time
            // is greater than the average life time. put these
            // blocks in the last bucket.
            bucket = no_buckets - 1;
        } else {
            bucket = (int) perc / bucket_size;
        }
        
        hist[bucket] ++;
        
        // calculate the variance
        sum = sum + rem_lifetime;
        sum_sqr = sum_sqr + (rem_lifetime*rem_lifetime);
    }
    
    
    fprintf(outputfile, "%s #%d elem #%d   ", sourcestr, ssdno, elem_num);
    fprintf(outputfile, "Block Lifetime Distribution\n");
    
    // print the bucket size
    fprintf(outputfile, "%s #%d elem #%d   ", sourcestr, ssdno, elem_num);
    for (i = bucket_size; i <= 100; i += bucket_size) {
        fprintf(outputfile, "< %d\t", i);
    }
    fprintf(outputfile, ">= 100\t\n");
    
    // print the histogram bar lengths
    fprintf(outputfile, "%s #%d elem #%d   ", sourcestr, ssdno, elem_num);
    for (i = bucket_size; i <= 100; i += bucket_size) {
        fprintf(outputfile, "%d\t", hist[i/bucket_size - 1]);
    }
    fprintf(outputfile, "%d\t\n", hist[no_buckets - 1]);
    
    mean = sum/n;
    variance = (sum_sqr - sum*mean)/(n - 1);
    fprintf(outputfile, "%s #%d elem #%d   Average of life time:\t%f\n",
            sourcestr, ssdno, elem_num, mean);
    fprintf(outputfile, "%s #%d elem #%d   Variance of life time:\t%f\n",
            sourcestr, ssdno, elem_num, variance);
    fprintf(outputfile, "%s #%d elem #%d   Total dead blocks:\t%d\n",
            sourcestr, ssdno, elem_num, dead_blocks);
}

//prints the cleaning algo statistics
void ssd_printcleanstats(int *set, int setsize, char *sourcestr)
{
    int i;
    int tot_ssd = 0;
    int elts_count = 0;
    double iops = 0;
    
    fprintf(outputfile, "\n\nSSD CLEANING STATISTICS\n");
    fprintf(outputfile, "---------------------------------------------\n\n");
    for (i = 0; i < setsize; i ++) {
        int j;
        int tot_elts = 0;
        ssd_t *s = getssd(set[i]);
        
        if (s->params.write_policy == DISKSIM_SSD_WRITE_POLICY_OSR) {
            
            elts_count += s->params.nelements;
            
#ifdef ADIVIM
	        int sum_lifetime =0;
#endif
            for (j = 0; j < s->params.nelements; j ++) {
                int plane_num;
                double avg_lifetime;
                double elem_iops = 0;
                double elem_clean_iops = 0;
                
                ssd_element_stat *stat = &(s->elements[j].stat);
                
                avg_lifetime = ssd_compute_avg_lifetime(-1, j, s);
                
#ifdef ADIVIM
				sum_lifetime+= avg_lifetime;
				if (j+1==s->params.nelements) {
					s->stat.avg_lifetime = sum_lifetime/(s->params.nelements);
					fprintf(outputfile_adv, "Average Lifetime : \t%d times remaining out of 50\n", s->stat.avg_lifetime);
				}
#endif
                
                fprintf(outputfile, "%s #%d elem #%d   Total reqs issued:\t%d\n",
                        sourcestr, set[i], j, s->elements[j].stat.tot_reqs_issued);
                fprintf(outputfile, "%s #%d elem #%d   Total time taken:\t%f\n",
                        sourcestr, set[i], j, s->elements[j].stat.tot_time_taken);
                if (s->elements[j].stat.tot_time_taken > 0) {
                    elem_iops = ((s->elements[j].stat.tot_reqs_issued*1000.0)/s->elements[j].stat.tot_time_taken);
                    fprintf(outputfile, "%s #%d elem #%d   IOPS:\t%f\n",
                            sourcestr, set[i], j, elem_iops);
                }
                
                fprintf(outputfile, "%s #%d elem #%d   Total cleaning reqs issued:\t%d\n",
                        sourcestr, set[i], j, s->elements[j].stat.num_clean);
                fprintf(outputfile, "%s #%d elem #%d   Total cleaning time taken:\t%f\n",
                        sourcestr, set[i], j, s->elements[j].stat.tot_clean_time);
                fprintf(outputfile, "%s #%d elem #%d   Total migrations:\t%d\n",
                        sourcestr, set[i], j, s->elements[j].metadata.tot_migrations);
                fprintf(outputfile, "%s #%d elem #%d   Total pages migrated:\t%d\n",
                        sourcestr, set[i], j, s->elements[j].metadata.tot_pgs_migrated);
                fprintf(outputfile, "%s #%d elem #%d   Total migrations cost:\t%f\n",
                        sourcestr, set[i], j, s->elements[j].metadata.mig_cost);
                
                
                if (s->elements[j].stat.tot_clean_time > 0) {
                    elem_clean_iops = ((s->elements[j].stat.num_clean*1000.0)/s->elements[j].stat.tot_clean_time);
                    fprintf(outputfile, "%s #%d elem #%d   clean IOPS:\t%f\n",
                            sourcestr, set[i], j, elem_clean_iops);
                }
                
                fprintf(outputfile, "%s #%d elem #%d   Overall IOPS:\t%f\n",
                        sourcestr, set[i], j, ((s->elements[j].stat.num_clean+s->elements[j].stat.tot_reqs_issued)*1000.0)/(s->elements[j].stat.tot_clean_time+s->elements[j].stat.tot_time_taken));
                
                iops += elem_iops;
                
                fprintf(outputfile, "%s #%d elem #%d   Number of free blocks:\t%d\n",
                        sourcestr, set[i], j, s->elements[j].metadata.tot_free_blocks);
                fprintf(outputfile, "%s #%d elem #%d   Number of cleans:\t%d\n",
                        sourcestr, set[i], j, stat->num_clean);
                fprintf(outputfile, "%s #%d elem #%d   Pages moved:\t%d\n",
                        sourcestr, set[i], j, stat->pages_moved);
                fprintf(outputfile, "%s #%d elem #%d   Total xfer time:\t%f\n",
                        sourcestr, set[i], j, stat->tot_xfer_cost);
                if (stat->tot_xfer_cost > 0) {
                    fprintf(outputfile, "%s #%d elem #%d   Xfer time per page:\t%f\n",
                            sourcestr, set[i], j, stat->tot_xfer_cost/(1.0*stat->pages_moved));
                } else {
                    fprintf(outputfile, "%s #%d elem #%d   Xfer time per page:\t0\n",
                            sourcestr, set[i], j);
                }
                fprintf(outputfile, "%s #%d elem #%d   Average lifetime:\t%f\n",
                        sourcestr, set[i], j, avg_lifetime);
                fprintf(outputfile, "%s #%d elem #%d   Plane Level Statistics\n",
                        sourcestr, set[i], j);
                fprintf(outputfile, "%s #%d elem #%d   ", sourcestr, set[i], j);
                for (plane_num = 0; plane_num < s->params.planes_per_pkg; plane_num ++) {
                    fprintf(outputfile, "%d:(%d)  ",
                            plane_num, s->elements[j].metadata.plane_meta[plane_num].num_cleans);
                }
                fprintf(outputfile, "\n");
                
                
                ssd_print_block_lifetime_distribution(j, s, set[i], avg_lifetime, sourcestr);
                fprintf(outputfile, "\n");
                
                tot_elts += stat->pages_moved;
            }
            
            //fprintf(outputfile, "%s SSD %d average # of pages moved per element %d\n",
            //  sourcestr, set[i], tot_elts / s->params.nelements);
            
            tot_ssd += tot_elts;
            fprintf(outputfile, "\n");
        }
    }
    
    if (elts_count > 0) {
        fprintf(outputfile, "%s   Total SSD IOPS:\t%f\n",
                sourcestr, iops);
        fprintf(outputfile, "%s   Average SSD element IOPS:\t%f\n",
                sourcestr, iops/elts_count);
    }
    
    //fprintf(outputfile, "%s SSD average # of pages moved per ssd %d\n\n",
    //  sourcestr, tot_ssd / setsize);
}

void ssd_printsetstats (int *set, int setsize, char *sourcestr)
{
    int i;
    struct ioq * queueset[MAXDEVICES*SSD_MAX_ELEMENTS];
    int queuecnt = 0;
    int reqcnt = 0;
    char prefix[80];
    
    //using more secure functions
    sprintf_s4(prefix, 80, "%sssd ", sourcestr);
    for (i=0; i<setsize; i++) {
        ssd_t *currdisk = getssd (set[i]);
        struct ioq *q = currdisk->queue;
        queueset[queuecnt] = q;
        queuecnt++;
        reqcnt += ioqueue_get_number_of_requests(q);
    }
    if (reqcnt == 0) {
        fprintf (outputfile, "\nNo ssd requests for members of this set\n\n");
        return;
    }
    ioqueue_printstats(queueset, queuecnt, prefix);
    
    ssd_acctime_printstats(set, setsize, prefix);
    ssd_other_printstats(set, setsize, prefix);
}


void ssd_printstats (void)
{
    struct ioq * queueset[MAXDEVICES*SSD_MAX_ELEMENTS];
    int set[MAXDEVICES];
    int i,j;
    int reqcnt = 0;
    char prefix[80];
    int diskcnt;
    int queuecnt;
    
    fprintf(outputfile, "\nSSD STATISTICS\n");
    fprintf(outputfile, "---------------------\n\n");
#ifdef ADIVIM
    fprintf(outputfile_adv, "\n-------------------\nADIVIM STATISTICS\n-------------------\n\n");
    adivim_print_threshold (outputfile_adv);
    
    int write_page_sum=0;
    int write_req_sum=0;
	int clean_page_sum=0;
	int w_req_sum = 0;
    for (i=0; i<MAXDEVICES; i++) {
        ssd_t *currdisk = getssd(i);
        if (currdisk) {
            write_page_sum+=currdisk->stat.write_page_num;
            write_req_sum+=currdisk->stat.write_req_num;
			clean_page_sum+=currdisk->stat.clean_page_num;
	    w_req_sum += currdisk->stat.w_req_n;
        }
    }
    fprintf(outputfile_adv, "Total Write Count : \t%d times\n", write_page_sum);
    fprintf(outputfile_adv, "Total Write Req : \t%d times\n", write_req_sum);
    fprintf(outputfile_adv, "Total Write Req by s.s : \t%d times\n", w_req_sum);
    if (write_req_sum != 0) fprintf(outputfile_adv, "Write Amplification Factor : \t%f\n", (float) write_page_sum/ (float) write_req_sum);
    if (w_req_sum != 0) fprintf(outputfile_adv, "Write Amplification Factor by s.s : \t%f\n", (float) write_page_sum/ (float) w_req_sum);

   	fprintf(outputfile_adv, "Total Clean Count : \t%d times\n", clean_page_sum); 
#endif
    
    sprintf_s3(prefix, 80, "ssd ");
    
    diskcnt = 0;
    queuecnt = 0;
    for (i=0; i<MAXDEVICES; i++) {
        ssd_t *currdisk = getssd (i);
        if (currdisk) {
            struct ioq *q = currdisk->queue;
            queueset[queuecnt] = q;
            queuecnt++;
            reqcnt += ioqueue_get_number_of_requests(q);
            diskcnt++;
        }
    }
    assert (diskcnt == numssds);
    
    if (reqcnt == 0) {
        fprintf(outputfile, "No ssd requests encountered\n");
        return;
    }
    
    ioqueue_printstats(queueset, queuecnt, prefix);
    
    diskcnt = 0;
    for (i=0; i<MAXDEVICES; i++) {
        ssd_t *currdisk = getssd (i);
        if (currdisk) {
            set[diskcnt] = i;
            diskcnt++;
        }
    }
    assert (diskcnt == numssds);
    
    ssd_acctime_printstats(set, numssds, prefix);
    ssd_other_printstats(set, numssds, prefix);
    
    ssd_printcleanstats(set, numssds, prefix);
    
    fprintf (outputfile, "\n\n");
    
    for (i=0; i<numssds; i++) {
        ssd_t *currdisk = getssd (set[i]);
        if (currdisk->printstats == FALSE) {
            continue;
        }
        reqcnt = 0;
        {
            struct ioq *q = currdisk->queue;
            reqcnt += ioqueue_get_number_of_requests(q);
        }
        if (reqcnt == 0) {
            fprintf(outputfile, "No requests for ssd #%d\n\n\n", set[i]);
            continue;
        }
        fprintf(outputfile, "ssd #%d:\n\n", set[i]);
        sprintf_s4(prefix, 80, "ssd #%d ", set[i]);
        {
            struct ioq *q;
            q = currdisk->queue;
            ioqueue_printstats(&q, 1, prefix);
        }
        for (j=0;j<currdisk->params.nelements;j++) {
            char pprefix[100];
            struct ioq *q;
            sprintf_s5(pprefix, 100, "%s elem #%d ", prefix, j);
            q = currdisk->elements[j].queue;
            ioqueue_printstats(&q, 1, pprefix);
        }
        ssd_acctime_printstats(&set[i], 1, prefix);
        ssd_other_printstats(&set[i], 1, prefix);
        fprintf (outputfile, "\n\n");
    }
}

// returning 0 to remove warning
double ssd_get_seektime (int devno,
                         ioreq_event *req,
                         int checkcache,
                         double maxtime)
{
    fprintf(stderr, "device_get_seektime not supported for ssd devno %d\n",  devno);
    assert(0);
    return 0;
}

/* default ssd dev header */
struct device_header ssd_hdr_initializer = {
    DEVICETYPE_SSD,
    sizeof(struct ssd),
    "unnamed ssd",
    (void *)ssd_copy,
    ssd_set_depth,
    ssd_get_depth,
    ssd_get_inbus,
    ssd_get_busno,
    ssd_get_slotno,
    ssd_get_number_of_blocks,
    ssd_get_maxoutstanding,
    ssd_get_numcyls,
    ssd_get_blktranstime,
    ssd_get_avg_sectpercyl,
    ssd_get_mapping,
    ssd_event_arrive,
    ssd_get_distance,
    ssd_get_servtime,
    ssd_get_seektime,
    ssd_get_acctime,
    ssd_bus_delay_complete,
    ssd_bus_ownership_grant
};

#ifdef ADIVIM
void print (char c, int n)
{
    int i;
    for (i = 0; i < n; i++)
    {
        printf ("%c", c);
    }
}
bool skippable (int mapped_lpn)
{
    return mapped_lpn == -1 || mapped_lpn == -2;
}
void adivim_ssd_print_image (ssd_t *s)
{
    /*
     e0
     =======================================
     p0                 | p1 ...
     ---------------------------------------
     HOT block1         | COLD block2
     pageno->apn        | pageno->apn
     pageno->apn        | ...
     ...                | ...
     ---------------------------------------
     
     e1
     ...
     */
    int elem, block, page, blockno, pageno, mapped_lpn, prev_mapped_lpn, column, columnwidth=153;
    bool skip, starting_line_printed = false;
    
    bool elem_and_plane_header_printed, block_header_printed, this_line_is_skipable;
    
    // if it is first time to print, don't print. because printing the whole mapping is inefficient. we will print only difference
    if (prev_ssd == NULL)
    {
        prev_ssd = ssd_copy (s);
        return;
    }
    // print only if mapping is modified.
    if (!s->is_updated)
    {
        return;
    }
    
    // now... shall we go?
    s->is_updated = false;
    
    // Gangnam style!
    for (elem = 0; elem < s->params.nelements; elem++)
    {
        elem_and_plane_header_printed = false;
        for (block = 0; block < s->params.blocks_per_plane; block++)
        {
            bool is_ommiting_going_on[s->params.planes_per_pkg], is_ommiting_just_started[s->params.planes_per_pkg];
            
            for (column = 0; column < s->params.planes_per_pkg; column++)
            {
                is_ommiting_going_on[column] = false;
                is_ommiting_just_started[column] = false;
            }
            block_header_printed = false;
            for (page = 0; page < s->params.pages_per_block; page++)
            {
                this_line_is_skipable = true;
                for (column = 0; column < s->params.planes_per_pkg; column++)
                {
                    blockno = block * s->params.planes_per_pkg + column;
                    pageno = blockno * s->params.pages_per_block + page;
                    mapped_lpn = s->elements[elem].metadata.block_usage[blockno].page[pageno % s->params.pages_per_block];
                    prev_mapped_lpn = prev_ssd->elements[elem].metadata.block_usage[blockno].page[pageno % s->params.pages_per_block];
                    
                    if((pageno > 0) && (pageno < s->params.planes_per_pkg - 2))
                    {
                        int upper_mapped_lpn = s->elements[elem].metadata.block_usage[blockno].page[(pageno-1) % s->params.pages_per_block];
                        int lower_mapped_lpn = s->elements[elem].metadata.block_usage[blockno].page[(pageno+1) % s->params.pages_per_block];
                        
                        if ((mapped_lpn - upper_mapped_lpn == 1 && lower_mapped_lpn - mapped_lpn == 1) || (mapped_lpn == upper_mapped_lpn && lower_mapped_lpn == mapped_lpn))
                        {
                            if (!is_ommiting_going_on[column])
                            {
                                is_ommiting_just_started[column] = true;
                            }
                            
                            is_ommiting_going_on[column] = true;
                        }
                        else
                        {
                            is_ommiting_going_on[column] = false;
                        }
                    }
                    
                    if (prev_mapped_lpn != mapped_lpn && !skippable (mapped_lpn) && !is_ommiting_going_on[column])
                    {
                        this_line_is_skipable = false;
                    }
                }
                
                if (!this_line_is_skipable)
                {
                    if (!elem_and_plane_header_printed)
                    {
                        printf ("e%d\n", elem);
                        print ('=', columnwidth); printf ("\n");
                        for (column = 0; column < s->params.planes_per_pkg; column++)
                        {
                            if (column == 0) printf("|");
                            print (' ', 6);
                            printf ("%d plane", column);
                            print (' ', 5);
                            printf ("|");
                        }
                        printf ("\n");
                        
                        elem_and_plane_header_printed = true;
                    }
                    
                    if (!block_header_printed)
                    {
                        print ('-', columnwidth); printf ("\n");
                        for (column = 0; column < s->params.planes_per_pkg; column++)
                        {
                            if (column == 0) printf("|");
                            
                            blockno = block * s->params.planes_per_pkg + column;
                            printf (" %5d block", blockno);
                            if (s->elements[elem].metadata.block_usage[blockno].type == 0)
                            {
                                printf (" COLD |");
                            }
                            else
                            {
                                printf (" HOT  |");
                            }
                        }
                        printf ("\n");
                        
                        block_header_printed = true;
                    }
                    
                    //print page
                    for (column = 0; column < s->params.planes_per_pkg; column++)
                    {
                        blockno = block * s->params.planes_per_pkg + column;
                        pageno = blockno * s->params.pages_per_block + page;
                        mapped_lpn = s->elements[elem].metadata.block_usage[blockno].page[pageno % s->params.pages_per_block];
                        
                        if (column == 0) printf("|");
                        
                        if (is_ommiting_just_started[column])
                        {
                            print (' ', 8);
                            printf ("...");
                            print (' ', 7);
                            printf ("|");
                            
                            is_ommiting_just_started[column] = false;
                        }
                        else if (is_ommiting_going_on[column])
                        {
                            print (' ', 18);
                            printf ("|");
                        }
                        if (!skippable (mapped_lpn))
                        {
                            printf (" %7d->%7d |", pageno, mapped_lpn);
                        }
                        else
                        {
                            print (' ', 18);
                            printf ("|");
                        }
                    }
                    printf ("\n");
                    
                }
            }
        }
        
        if (elem_and_plane_header_printed)
        {
            print ('=', columnwidth); printf ("\n");
        }
    }
    
    
    /* old style
     for (elem = 0; elem < s->params.nelements; elem++)
     {
     // skip check
     skip = true;
     for (block = 0; block < s->params.blocks_per_plane; block++)
     {
     // skip invalidated mapping
     for (page = 0; page < s->params.pages_per_block; page++)
     {
     for (column = 0; column < s->params.planes_per_pkg; column++)
     {
     blockno = block * s->params.planes_per_pkg + column;
     pageno = blockno * s->params.pages_per_block + page;
     mapped_lpn = s->elements[elem].metadata.block_usage[blockno].page[pageno % s->params.pages_per_block];
     
     if (!skippable (mapped_lpn))
     {
     skip = false;
     }
     }
     }
     }
     
     if (!skip)
     {
     if (!starting_line_printed)
     {
     print ('\n', 10);
     printf ("\n");
     starting_line_printed = true;
     }
     
     // print element header
     printf ("e%d\n", elem);
     print ('=', columnwidth); printf ("\n");
     for (column = 0; column < s->params.planes_per_pkg; column++)
     {
     if (column == 0) printf("|");
     print (' ', 6);
     printf ("%d plane", column);
     print (' ', 5);
     printf ("|");
     }
     printf ("\n");
     
     // print block
     for (block = 0; block < s->params.blocks_per_plane; block++)
     {
     // want to skip printing block header when whole block can be skippable
     bool skip_block_header[s->params.planes_per_pkg];
     
     // initialize skip_block_header
     for (column = 0; column < s->params.planes_per_pkg; column++)
     {
     skip_block_header[column] = true;
     }
     
     // skip check
     skip = true;
     for (column = 0; column < s->params.planes_per_pkg; column++)
     {
     for (page = 0; page < s->params.pages_per_block; page++)
     {
     blockno = block * s->params.planes_per_pkg + column;
     pageno = blockno * s->params.pages_per_block + page;
     mapped_lpn = s->elements[elem].metadata.block_usage[blockno].page[pageno % s->params.pages_per_block];
     
     if (!skippable (mapped_lpn))
     {
     skip = false;
     skip_block_header[column] = false;
     }
     }
     }
     
     // print block
     if (!skip)
     {
     // print block header
     print ('-', columnwidth); printf ("\n");
     for (column = 0; column < s->params.planes_per_pkg; column++)
     {
     if (column == 0) printf("|");
     
     // print block header
     if (!skip_block_header[column])
     {
     blockno = block * s->params.planes_per_pkg + column;
     printf (" %5d block", blockno);
     if (s->elements[elem].metadata.block_usage[blockno].type == 0)
     {
     printf (" COLD |");
     }
     else
     {
     printf (" HOT  |");
     }
     }
     // skip
     else
     {
     print (' ', 18);
     printf ("|");
     }
     
     }
     printf ("\n");
     
     // to ommit rest of mapping when page mapping is contineous
     ADIVIM_APN diff;
     bool ommiting_dot_printing[s->params.planes_per_pkg];
     int ommiting_dot_num[s->params.planes_per_pkg];
     const int ommiting_dot_max = 4;
     bool ommitable;
     
     // initialize ommiting stuff.
     for (column = 0; column < s->params.planes_per_pkg; column++)
     {
     ommiting_dot_printing[column] = false;
     ommiting_dot_num[column] = 0;
     }
     
     // print page
     for (page = 0; page < s->params.pages_per_block; page++)
     {
     // skip check
     skip = true;
     ommitable = true;
     for (column = 0; column < s->params.planes_per_pkg; column++)
     {
     blockno = block * s->params.planes_per_pkg + column;
     pageno = blockno * s->params.pages_per_block + page;
     mapped_lpn = s->elements[elem].metadata.block_usage[blockno].page[pageno % s->params.pages_per_block];
     diff = ((page>(s->params.pages_per_block - 2)) ? -1 : s->elements[elem].metadata.block_usage[blockno].page[(pageno+1) % s->params.pages_per_block]) - mapped_lpn;
     
     if (!skippable (mapped_lpn) && (diff == 1 || diff == 0))
     {
     // if the very beginning of ommition, we need to print the starting line.
     if (ommiting_dot_num[column] == 0)
     {
     ommitable = false;
     ommiting_dot_printing[column] = false;
     ommiting_dot_num[column]++;
     }
     else if (ommiting_dot_num[column] < ommiting_dot_max)
     {
     // not yet. we need to print omming_dot.
     ommitable = false;
     ommiting_dot_printing[column] = true;
     ommiting_dot_num[column]++;
     }
     else
     {
     ommiting_dot_printing[column] = false;
     }
     }
     else if (!skippable (mapped_lpn))
     {
     // init ommiting stuf
     ommitable = false;
     ommiting_dot_printing[column] = false;
     ommiting_dot_num[column] = 0;
     }
     
     if (!ommitable && !skippable (mapped_lpn))
     {
     skip = false;
     }
     }
     
     // print
     if (!skip)
     {
     for (column = 0; column < s->params.planes_per_pkg; column++)
     {
     blockno = block * s->params.planes_per_pkg + column;
     pageno = blockno * s->params.pages_per_block + page;
     mapped_lpn = s->elements[elem].metadata.block_usage[blockno].page[pageno % s->params.pages_per_block];
     //diff = ((page==0 || (page>(s->params.pages_per_block - 2))) ? -1 : s->elements[elem].metadata.block_usage[blockno].page[(pageno+1) % s->params.pages_per_block]) - mapped_lpn;
     
     if (column == 0) printf("|");
     
     // print ommiting dot
     if (ommiting_dot_printing[column])
     {
     print (' ', 8);
     printf ("...");
     print (' ', 7);
     printf ("|");
     }
     else if (!skippable (mapped_lpn))
     {
     printf (" %7d->%7d |", pageno, mapped_lpn);
     }
     else
     {
     print (' ', 18);
     printf ("|");
     }
     }
     printf ("\n");
     }
     }
     }
     }
     
     print ('=', columnwidth); printf ("\n\n");
     }
     }
     */
    
    for (elem = 0; elem < s->params.nelements; elem++)
    {
        for (block = 0; block < s->params.blocks_per_element; block++)
        {
            free (prev_ssd->elements[elem].metadata.block_usage[block].page);
        }
        free (prev_ssd->elements[elem].metadata.block_usage);
    }
    free (prev_ssd);
    prev_ssd = ssd_copy (s);
}
#endif
