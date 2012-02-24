/* -*- mode: c; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; c-file-style: "stroustrup"; -*-
 *
 *
 *                This source code is part of
 *
 *                 G   R   O   M   A   C   S
 *
 *          GROningen MAchine for Chemical Simulations
 *
 * Written by David van der Spoel, Erik Lindahl, Berk Hess, and others.
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2012, The GROMACS development team,
 * check out http://www.gromacs.org for more information.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * If you want to redistribute modifications, please consider that
 * scientific software is very special. Version control is crucial -
 * bugs must be traceable. We will be happy to consider code for
 * inclusion in the official distribution, but derived work must not
 * be called official GROMACS. Details are found in the README & COPYING
 * files - if they are missing, get the official version at www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the papers on the package - you can find them in the top README file.
 *
 * For more info, check our website at http://www.gromacs.org
 *
 * And Hey:
 * Gallium Rubidium Oxygen Manganese Argon Carbon Silicon
 */

#ifndef NBNXN_CUDA_TYPES_H
#define NBNXN_CUDA_TYPES_H

#include "types/nbnxn_pairlist.h"
#include "types/nbnxn_cuda_types_ext.h"
#include "cudautils.cuh"

#ifdef __cplusplus
extern "C" {
#endif

/*! Types of electrostatics available in the CUDA nonbonded force kernels. */
enum { cu_eelEWALD, cu_eelRF, cu_eelCUT };

/* All structs prefixed with "cu_" hold data used in GPU calculations and
 * are passed to the kernels, except cu_timers_t. */
typedef struct cu_plist     cu_plist_t;
typedef struct cu_atomdata  cu_atomdata_t;
typedef struct cu_nbparam   cu_nbparam_t;
typedef struct cu_timers    cu_timers_t;
typedef struct nb_staging   nb_staging_t;


/*! Staging area for temporary data. The energies get downloaded here first, 
 *  before getting added to the CPU-side aggregate values.
 */
struct nb_staging
{
    float   *e_lj;      /* LJ energy */
    float   *e_el;      /* electrostatic energy */
    float4  *fshift;    /* shift forces */
};

/*! Nonbonded atom data -- both inputs and outputs. */
struct cu_atomdata
{
    int     natoms;             /* number of atoms                      */
    int     natoms_local;       /* number of local atoms                */
    int     nalloc;             /* allocation size for the atom data (xq, f) */
    
    float4  *xq;                /* atom coordinates + charges, size natoms  */
    float4  *f;                 /* force output array, size natoms          */
    /* TODO: try float2 for the energies */
    float   *e_lj,              /* LJ energy output, size 1                 */
            *e_el;              /* Electrostatics energy intput, size 1     */

    float4  *fshift;            /* shift forces */

    int     ntypes;             /* number of atom types             */
    int     *atom_types;        /* atom type indices, size natoms   */
 
    float3  *shift_vec;         /* shifts */
    gmx_bool shift_vec_uploaded;/* has the shift vector already been transfered? */
};

/*! Paramters required for the CUDA nonbonded calculations. */
struct cu_nbparam
{
    int     eeltype;        /* type of electrostatics                       */
    
    float   epsfac;         /* charge multiplication factor                 */
    float   c_rf, two_k_rf; /* Reaction-Field constants                     */
    float   ewald_beta;     /* Ewald/PME parameter                          */
    float   rvdw_sq;        /* VdW cut-off                                  */
    float   rcoulomb_sq;    /* Coulomb cut-off                              */
    float   rlist_sq;       /* pair-list cut-off                            */
    float   lj_shift;       /* LJ potential correction term                 */

    float   *nbfp;          /* nonbonded parameter table with C6/C12 pairs  */

    /* Ewald Coulomb force table */
    int     coulomb_tab_size;
    float   coulomb_tab_scale;
    float   *coulomb_tab;
};

/*! Pair list data */
struct cu_plist
{
    int             na_c;       /* number of atoms per cluster                  */
    
    int             nsci;       /* size of sci, # of i clusters in the list     */
    int             sci_nalloc; /* allocation size of sci                       */
    nbnxn_sci_t     *sci;       /* list of i-cluster ("superclusters")          */

    int             ncj4;       /* total # of 4*j clusters                      */
    int             cj4_nalloc; /* allocation size of cj4                       */
    nbnxn_cj4_t     *cj4;       /* 4*j cluster list, contains j cluster number
                                   and index into the i cluster list            */
    nbnxn_excl_t    *excl;      /* atom interaction bits                        */
    int             nexcl;      /* count for excl                               */
    int             excl_nalloc;/* allocation size of excl                      */

    gmx_bool        do_prune;   /* true if pair-list pruning needs to be
                                   done during the  current step                */
};

/* CUDA events used for timing GPU kernels and H2D/D2H transfers.
 * The two-sized arrays hold the local and non-local values and should always
 * be indexed with eintLocal/eintNonlocal.
 */
struct cu_timers
{
    cudaEvent_t start_atdat, stop_atdat;         /* atom data transfer (every PS step)      */
    cudaEvent_t start_nb_h2d[2], stop_nb_h2d[2]; /* x/q H2D transfer (every step)           */
    cudaEvent_t start_nb_d2h[2], stop_nb_d2h[2]; /* f D2H transfer (every step)             */
    cudaEvent_t start_pl_h2d[2], stop_pl_h2d[2]; /* pair-list H2D transfer (every PS step)  */
    cudaEvent_t start_nb_k[2], stop_nb_k[2];     /* non-bonded kernels (every step)         */
};

/* Main data structure for CUDA nonbonded force calculations. */
struct nbnxn_cuda
{
    cu_dev_info_t   *dev_info;      /* CUDA device information                              */
    gmx_bool        dd_run;         /* true if running with domain-decomposition            */
    gmx_bool        use_stream_sync; /* if true use memory polling-based waiting instead 
                                        of cudaStreamSynchronize                            */
    cu_atomdata_t   *atdat;         /* atom data */
    cu_nbparam_t    *nbparam;       /* parameters required for the non-bonded calc.         */
    cu_plist_t      *plist[2];      /* pair-list data structures (local and non-local)      */
    nb_staging_t    nbst;           /* staging area where fshift/energies get downloaded    */
    
    cudaStream_t    stream[2];      /* local and non-local GPU streams                      */

    /* events used for synchronization */
    cudaEvent_t    nonlocal_done, misc_ops_done;

    gmx_bool        do_time;        /* true if CUDA event-based timing is enabled, off with DD */
    cu_timers_t     *timers;        /* CUDA event-based timers.                             */
    wallclock_gpu_t *timings;       /* Timing data.                                         */
};

#ifdef __cplusplus
}
#endif

#endif	/* NBNXN_CUDA_TYPES_H */