
#ifndef _ADIVIM_MODULES_H
#define _ADIVIM_MODULES_H


#include "adivim_adv_param.h"


static struct lp_mod *adivim_mods[] = {
    &adivim_adv_mod
};

typedef enum {
    ADIVIM_MOD_ADV
} adivim_mod_t;

#define ADIVIM_MAX_MODULE 0
#endif // _ADIVIML_MODULES_H
