/*
 *  Copyright (c) 2012-2018 Laurent Vivier <laurent@vivier.eu>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_AUDIO_ASC_H
#define HW_AUDIO_ASC_H
enum
{
    ASC_TYPE_ASC    = 0,  /* original discrete Apple Sound Chip */
    ASC_TYPE_EASC   = 1,  /* discrete Enhanced Apple Sound Chip */
    ASC_TYPE_V8     = 2,  /* Subset of ASC included in the V8 ASIC (LC/LCII) */
    ASC_TYPE_EAGLE  = 3,  /* Subset of ASC included in the Eagle ASIC (Classic II) */
    ASC_TYPE_SPICE  = 4,  /* Subset of ASC included in the Spice ASIC (Color Classic) */
    ASC_TYPE_SONORA = 5,  /* Subset of ASC included in the Sonora ASIC (LCIII) */
    ASC_TYPE_VASP   = 6,  /* Subset of ASC included in the VASP ASIC  (IIvx/IIvi) */
    ASC_TYPE_ARDBEG = 7   /* Subset of ASC included in the Ardbeg ASIC (LC520) */
};
#endif
