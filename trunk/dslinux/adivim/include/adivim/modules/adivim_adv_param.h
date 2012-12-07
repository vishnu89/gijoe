
#ifndef _ADIVIM_ADV_PARAM_H
#define _ADIVIM_ADV_PARAM_H

#include <libparam/libparam.h>
#ifdef __cplusplus
extern"C"{
#endif
    struct dm_disk_if;
    
    /* prototype for ADIVIM_ADV param loader function */
    struct adv *adivim_adv_loadparams(struct lp_block *b, int *num);
    
    typedef enum {
        ADIVIM_ADV_SCHEDULER,
        ADIVIM_ADV_POINTS_IN_PRECOMPUTED_SEEK_CURVE,
        ADIVIM_ADV_SEEK_FUNCTION,
        ADIVIM_ADV_MAX_QUEUE_LENGTH,
        ADIVIM_ADV_BULK_SECTOR_TRANSFER_TIME,
        ADIVIM_ADV_SEGMENT_SIZE_,
        ADIVIM_ADV_NUMBER_OF_BUFFER_SEGMENTS,
        ADIVIM_ADV_PRINT_STATS,
        ADIVIM_ADV_COMMAND_OVERHEAD,
        ADIVIM_ADV_NUMBER_OF_SLEDS,
        ADIVIM_ADV_LAYOUT_POLICY,
        ADIVIM_ADV_SLED_MOVEMENT_X,
        ADIVIM_ADV_SLED_MOVEMENT_Y,
        ADIVIM_ADV_BIT_CELL_LENGTH,
        ADIVIM_ADV_TIP_SECTOR_LENGTH,
        ADIVIM_ADV_SERVO_BURST_LENGTH,
        ADIVIM_ADV_TIP_SECTORS_PER_LBN,
        ADIVIM_ADV_NUMBER_OF_USABLE_TIPS,
        ADIVIM_ADV_SIMULTANEOUSLY_ACTIVE_TIPS,
        ADIVIM_ADV_BIDIRECTIONAL_ACCESS,
        ADIVIM_ADV_SLED_ACCELERATION_X,
        ADIVIM_ADV_SLED_ACCELERATION_Y,
        ADIVIM_ADV_SLED_ACCESS_SPEED,
        ADIVIM_ADV_SLED_RESONANT_FREQUENCY,
        ADIVIM_ADV_SETTLING_TIME_CONSTANTS,
        ADIVIM_ADV_SPRING_CONSTANT_FACTOR,
        ADIVIM_ADV_PREFETCH_DEPTH,
        ADIVIM_ADV_TIME_BEFORE_SLED_INACTIVE,
        ADIVIM_ADV_STARTUP_DELAY,
        ADIVIM_ADV_SLED_ACTIVE_POWER,
        ADIVIM_ADV_SLED_INACTIVE_POWER,
        ADIVIM_ADV_TIP_ACCESS_POWER
    } ADIVIM_ADV_param_t;
    
#define ADIVIM_ADV_MAX_PARAM		ADIVIM_ADV_TIP_ACCESS_POWER
    extern void * ADIVIM_ADV_loaders[];
    extern lp_paramdep_t ADIVIM_ADV_deps[];
    
    
    static struct lp_varspec adivim_adv_params [] = {
        {"Scheduler", BLOCK, 1 },
        {"Points in precomputed seek curve", I, 1 },
        {"Seek function", I, 1 },
        {"Max queue length", I, 1 },
        {"Bulk sector transfer time", D, 1 },
        {"Segment size (in blks)", I, 1 },
        {"Number of buffer segments", I, 1 },
        {"Print stats", I, 1 },
        {"Command overhead", D, 1 },
        {"Number of sleds", I, 1 },
        {"Layout policy", I, 1 },
        {"Sled movement X", I, 1 },
        {"Sled movement Y", I, 1 },
        {"Bit cell length", I, 1 },
        {"Tip sector length", I, 1 },
        {"Servo burst length", I, 1 },
        {"Tip sectors per lbn", I, 1 },
        {"Number of usable tips", I, 1 },
        {"Simultaneously active tips", I, 1 },
        {"Bidirectional access", I, 1 },
        {"Sled acceleration X", D, 1 },
        {"Sled acceleration Y", D, 1 },
        {"Sled access speed", I, 1 },
        {"Sled resonant frequency", I, 1 },
        {"Settling time constants", D, 1 },
        {"Spring constant factor", D, 1 },
        {"Prefetch depth", I, 1 },
        {"Time before sled inactive", D, 1 },
        {"Startup delay", D, 1 },
        {"Sled active power", D, 1 },
        {"Sled inactive power", D, 1 },
        {"Tip access power", D, 1 },
        {0,0,0}
    };
#define ADIVIM_ADV_MAX 32
    static struct lp_mod adivim_adv_mod = { "adivim_adv", adivim_adv_params, ADIVIM_ADV_MAX, (lp_modloader_t)adivim_adv_loadparams,  0, 0, ADIVIM_ADV_loaders, ADIVIM_ADV_deps };
    
    
#ifdef __cplusplus
}
#endif
#endif // _ADIVIM_ADV_PARAM_H
