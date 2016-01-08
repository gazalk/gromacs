/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2004, The GROMACS development team.
 * Copyright (c) 2012,2014,2015, by the GROMACS development team, led by
 * Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
 * and including many others, as listed in the AUTHORS file in the
 * top-level source directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */
#include "gmxpre.h"

#include "statistics.h"

#include <cmath>

#include "gromacs/math/vec.h"
#include "gromacs/utility/fatalerror.h"
#include "gromacs/utility/real.h"
#include "gromacs/utility/smalloc.h"

static int gmx_dnint(double x)
{
    return static_cast<int>(x+0.5);
}

typedef struct gmx_stats {
    double  aa, a, b, sigma_aa, sigma_a, sigma_b, aver, sigma_aver, error;
    double  rmsd, Rdata, Rfit, Rfitaa, chi2, chi2aa, mse, mae;
    double *x, *y, *dx, *dy;
    int     computed;
    int     np, np_c, nalloc;
} gmx_stats;

gmx_stats_t gmx_stats_init()
{
    gmx_stats *stats;

    snew(stats, 1);

    return (gmx_stats_t) stats;
}

int gmx_stats_get_npoints(gmx_stats_t gstats, int *N)
{
    gmx_stats *stats = (gmx_stats *) gstats;

    *N = stats->np;

    return estatsOK;
}

void gmx_stats_free(gmx_stats_t gstats)
{
    gmx_stats *stats = (gmx_stats *) gstats;

    sfree(stats->x);
    sfree(stats->y);
    sfree(stats->dx);
    sfree(stats->dy);
    sfree(stats);
}

int gmx_stats_add_point_ydy(gmx_stats_t gstats, double y, double dy)
{
    return gmx_stats_add_point(gstats, gstats->np, y, 0, dy);
}

int gmx_stats_add_point(gmx_stats_t gstats, double x, double y,
                        double dx, double dy)
{
    gmx_stats *stats = gstats;

    if (stats->np+1 >= stats->nalloc)
    {
        if (stats->nalloc == 0)
        {
            stats->nalloc = 1024;
        }
        else
        {
            stats->nalloc *= 2;
        }
        srenew(stats->x, stats->nalloc);
        srenew(stats->y, stats->nalloc);
        srenew(stats->dx, stats->nalloc);
        srenew(stats->dy, stats->nalloc);
        for (int i = stats->np; (i < stats->nalloc); i++)
        {
            stats->x[i]  = 0;
            stats->y[i]  = 0;
            stats->dx[i] = 0;
            stats->dy[i] = 0;
        }
    }
    stats->x[stats->np]  = x;
    stats->y[stats->np]  = y;
    stats->dx[stats->np] = dx;
    stats->dy[stats->np] = dy;
    stats->np++;
    stats->computed = 0;

    return estatsOK;
}

int gmx_stats_get_point(gmx_stats_t gstats, real *x, real *y,
                        real *dx, real *dy, real level)
{
    gmx_stats *stats = gstats;
    int        ok, outlier;
    real       rmsd, r;

    if ((ok = gmx_stats_get_rmsd(gstats, &rmsd)) != estatsOK)
    {
        return ok;
    }
    outlier = 0;
    while ((outlier == 0) && (stats->np_c < stats->np))
    {
        r       = std::abs(stats->x[stats->np_c] - stats->y[stats->np_c]);
        outlier = (r > rmsd*level);
        if (outlier)
        {
            if (NULL != x)
            {
                *x  = stats->x[stats->np_c];
            }
            if (NULL != y)
            {
                *y  = stats->y[stats->np_c];
            }
            if (NULL != dx)
            {
                *dx = stats->dx[stats->np_c];
            }
            if (NULL != dy)
            {
                *dy = stats->dy[stats->np_c];
            }
        }
        stats->np_c++;

        if (outlier)
        {
            return estatsOK;
        }
    }

    stats->np_c = 0;

    return estatsNO_POINTS;
}

int gmx_stats_add_points(gmx_stats_t gstats, int n, real *x, real *y,
                         real *dx, real *dy)
{
    for (int i = 0; (i < n); i++)
    {
        int ok;
        if ((ok = gmx_stats_add_point(gstats, x[i], y[i],
                                      (NULL != dx) ? dx[i] : 0,
                                      (NULL != dy) ? dy[i] : 0)) != estatsOK)
        {
            return ok;
        }
    }
    return estatsOK;
}

static int gmx_stats_compute(gmx_stats *stats, int weight)
{
    double yy, yx, xx, sx, sy, dy, chi2, chi2aa, d2;
    double ssxx, ssyy, ssxy;
    double w, wtot, yx_nw, sy_nw, sx_nw, yy_nw, xx_nw, dx2, dy2;

    int    N = stats->np;

    if (stats->computed == 0)
    {
        if (N < 1)
        {
            return estatsNO_POINTS;
        }

        xx   = xx_nw = 0;
        yy   = yy_nw = 0;
        yx   = yx_nw = 0;
        sx   = sx_nw = 0;
        sy   = sy_nw = 0;
        wtot = 0;
        d2   = 0;
        double mae = 0, mse = 0;
        for (int i = 0; (i < N); i++)
        {
            double dd = stats->y[i]-stats->x[i];
            d2 += dsqr(dd);
            
            mae += fabs(dd);
            mse += dd;
            if ((stats->dy[i]) && (weight == elsqWEIGHT_Y))
            {
                w = 1/dsqr(stats->dy[i]);
            }
            else
            {
                w = 1;
            }

            wtot  += w;

            xx    += w*dsqr(stats->x[i]);
            xx_nw += dsqr(stats->x[i]);

            yy    += w*dsqr(stats->y[i]);
            yy_nw += dsqr(stats->y[i]);

            yx    += w*stats->y[i]*stats->x[i];
            yx_nw += stats->y[i]*stats->x[i];

            sx    += w*stats->x[i];
            sx_nw += stats->x[i];

            sy    += w*stats->y[i];
            sy_nw += stats->y[i];
        }

        /* Compute average, sigma and error */
        stats->mae        = mae/N;
        stats->mse        = mse/N; 
        stats->aver       = sy_nw/N;
        double dd = yy_nw/N - dsqr(sy_nw/N);
        if (dd > 0)
        {
            stats->sigma_aver = std::sqrt(dd);
        }
        else if (N == 1)
        {
            stats->sigma_aver = stats->dy[0];
        }
        else
        {
            stats->sigma_aver = 0;
        }
        stats->error      = stats->sigma_aver/std::sqrt(N);

        /* Compute RMSD between x and y */
        stats->rmsd = std::sqrt(d2/N);

        /* Correlation coefficient for data */
        yx_nw       /= N;
        xx_nw       /= N;
        yy_nw       /= N;
        sx_nw       /= N;
        sy_nw       /= N;
        ssxx         = N*(xx_nw - dsqr(sx_nw));
        ssyy         = N*(yy_nw - dsqr(sy_nw));
        ssxy         = N*(yx_nw - (sx_nw*sy_nw));
        stats->Rdata = std::sqrt(dsqr(ssxy)/(ssxx*ssyy));

        /* Compute straight line through datapoints, either with intercept
           zero (result in aa) or with intercept variable (results in a
           and b) */
        yx = yx/wtot;
        xx = xx/wtot;
        sx = sx/wtot;
        sy = sy/wtot;

        stats->aa = (yx/xx);
        stats->a  = (yx-sx*sy)/(xx-sx*sx);
        stats->b  = (sy)-(stats->a)*(sx);

        /* Compute chi2, deviation from a line y = ax+b. Also compute
           chi2aa which returns the deviation from a line y = ax. */
        chi2   = 0;
        chi2aa = 0;
        for (int i = 0; (i < N); i++)
        {
            if (stats->dy[i] > 0)
            {
                dy = stats->dy[i];
            }
            else
            {
                dy = 1;
            }
            chi2aa += dsqr((stats->y[i]-(stats->aa*stats->x[i]))/dy);
            chi2   += dsqr((stats->y[i]-(stats->a*stats->x[i]+stats->b))/dy);
        }
        if (N > 2)
        {
            stats->chi2   = std::sqrt(chi2/(N-2));
            stats->chi2aa = std::sqrt(chi2aa/(N-2));

            /* Look up equations! */
            dx2            = (xx-sx*sx);
            dy2            = (yy-sy*sy);
            stats->sigma_a = std::sqrt(stats->chi2/((N-2)*dx2));
            stats->sigma_b = stats->sigma_a*std::sqrt(xx);
            stats->Rfit    = std::abs(ssxy)/std::sqrt(ssxx*ssyy);
            stats->Rfitaa  = stats->aa*std::sqrt(dx2/dy2);
        }
        else
        {
            stats->chi2    = 0;
            stats->chi2aa  = 0;
            stats->sigma_a = 0;
            stats->sigma_b = 0;
            stats->Rfit    = 0;
            stats->Rfitaa  = 0;
        }

        stats->computed = 1;
    }

    return estatsOK;
}

int gmx_stats_get_ab(gmx_stats_t gstats, int weight,
                     real *a, real *b, real *da, real *db,
                     real *chi2, real *Rfit)
{
    gmx_stats *stats = gstats;
    int        ok;

    if ((ok = gmx_stats_compute(stats, weight)) != estatsOK)
    {
        return ok;
    }
    if (NULL != a)
    {
        *a    = stats->a;
    }
    if (NULL != b)
    {
        *b    = stats->b;
    }
    if (NULL != da)
    {
        *da   = stats->sigma_a;
    }
    if (NULL != db)
    {
        *db   = stats->sigma_b;
    }
    if (NULL != chi2)
    {
        *chi2 = stats->chi2;
    }
    if (NULL != Rfit)
    {
        *Rfit = stats->Rfit;
    }

    return estatsOK;
}

int gmx_stats_get_a(gmx_stats_t gstats, int weight, real *a, real *da,
                    real *chi2, real *Rfit)
{
    gmx_stats *stats = gstats;
    int        ok;

    if ((ok = gmx_stats_compute(stats, weight)) != estatsOK)
    {
        return ok;
    }
    if (NULL != a)
    {
        *a    = stats->aa;
    }
    if (NULL != da)
    {
        *da   = stats->sigma_aa;
    }
    if (NULL != chi2)
    {
        *chi2 = stats->chi2aa;
    }
    if (NULL != Rfit)
    {
        *Rfit = stats->Rfitaa;
    }

    return estatsOK;
}

int gmx_stats_get_average(gmx_stats_t gstats, real *aver)
{
    gmx_stats *stats = gstats;
    int        ok;

    if ((ok = gmx_stats_compute(stats, elsqWEIGHT_NONE)) != estatsOK)
    {
        return ok;
    }

    *aver = stats->aver;

    return estatsOK;
}

int gmx_stats_get_mse_mae(gmx_stats_t gstats, real *mse, real *mae)
{
    gmx_stats *stats = (gmx_stats *) gstats;
    int        ok;

    if ((ok = gmx_stats_compute(stats, elsqWEIGHT_NONE)) != estatsOK)
    {
        return ok;
    }

    if (NULL != mse)
    {
        *mse = stats->mse;
    }
    if (NULL != mae)
    {
        *mae = stats->mae;
    }

    return estatsOK;
}

int gmx_stats_get_ase(gmx_stats_t gstats, real *aver, real *sigma, real *error)
{
    gmx_stats *stats = gstats;
    int        ok;

    if ((ok = gmx_stats_compute(stats, elsqWEIGHT_NONE)) != estatsOK)
    {
        return ok;
    }

    if (NULL != aver)
    {
        *aver  = stats->aver;
    }
    if (NULL != sigma)
    {
        *sigma = stats->sigma_aver;
    }
    if (NULL != error)
    {
        *error = stats->error;
    }

    return estatsOK;
}

int gmx_stats_get_sigma(gmx_stats_t gstats, real *sigma)
{
    gmx_stats *stats = gstats;
    int        ok;

    if ((ok = gmx_stats_compute(stats, elsqWEIGHT_NONE)) != estatsOK)
    {
        return ok;
    }

    *sigma = stats->sigma_aver;

    return estatsOK;
}

int gmx_stats_get_error(gmx_stats_t gstats, real *error)
{
    gmx_stats *stats = gstats;
    int        ok;

    if ((ok = gmx_stats_compute(stats, elsqWEIGHT_NONE)) != estatsOK)
    {
        return ok;
    }

    *error = stats->error;

    return estatsOK;
}

int gmx_stats_get_corr_coeff(gmx_stats_t gstats, real *R)
{
    gmx_stats *stats = gstats;
    int        ok;

    if ((ok = gmx_stats_compute(stats, elsqWEIGHT_NONE)) != estatsOK)
    {
        return ok;
    }

    *R = stats->Rdata;

    return estatsOK;
}

int gmx_stats_get_rmsd(gmx_stats_t gstats, real *rmsd)
{
    gmx_stats *stats = gstats;
    int        ok;

    if ((ok = gmx_stats_compute(stats, elsqWEIGHT_NONE)) != estatsOK)
    {
        return ok;
    }

    *rmsd = stats->rmsd;

    return estatsOK;
}

int gmx_stats_dump_xy(gmx_stats_t gstats, FILE *fp)
{
    gmx_stats *stats = gstats;

    for (int i = 0; (i < stats->np); i++)
    {
        fprintf(fp, "%12g  %12g  %12g  %12g\n", stats->x[i], stats->y[i],
                stats->dx[i], stats->dy[i]);
    }

    return estatsOK;
}

int gmx_stats_remove_outliers(gmx_stats_t gstats, double level)
{
    gmx_stats *stats = gstats;
    int        iter  = 1, done = 0, ok;
    real       rmsd, r;

    while ((stats->np >= 10) && !done)
    {
        if ((ok = gmx_stats_get_rmsd(gstats, &rmsd)) != estatsOK)
        {
            return ok;
        }
        done = 1;
        for (int i = 0; (i < stats->np); )
        {
            r = std::abs(stats->x[i]-stats->y[i]);
            if (r > level*rmsd)
            {
                fprintf(stderr, "Removing outlier, iter = %d, rmsd = %g, x = %g, y = %g\n",
                        iter, rmsd, stats->x[i], stats->y[i]);
                if (i < stats->np-1)
                {
                    stats->x[i]  = stats->x[stats->np-1];
                    stats->y[i]  = stats->y[stats->np-1];
                    stats->dx[i] = stats->dx[stats->np-1];
                    stats->dy[i] = stats->dy[stats->np-1];
                }
                stats->np--;
                done = 0;
            }
            else
            {
                i++;
            }
        }
        iter++;
    }

    return estatsOK;
}

int gmx_stats_make_histogram(gmx_stats_t gstats, real binwidth, int *nb,
                             int ehisto, int normalized, real **x, real **y)
{
    gmx_stats *stats = gstats;
    int        index = 0, nbins = *nb, *nindex;
    double     minx, maxx, maxy, miny, delta, dd, minh;

    if (((binwidth <= 0) && (nbins <= 0)) ||
        ((binwidth > 0) && (nbins > 0)))
    {
        return estatsINVALID_INPUT;
    }
    if (stats->np <= 2)
    {
        return estatsNO_POINTS;
    }
    minx = maxx = stats->x[0];
    miny = maxy = stats->y[0];
    for (int i = 1; (i < stats->np); i++)
    {
        miny = (stats->y[i] < miny) ? stats->y[i] : miny;
        maxy = (stats->y[i] > maxy) ? stats->y[i] : maxy;
        minx = (stats->x[i] < minx) ? stats->x[i] : minx;
        maxx = (stats->x[i] > maxx) ? stats->x[i] : maxx;
    }
    if (ehisto == ehistoX)
    {
        delta = maxx-minx;
        minh  = minx;
    }
    else if (ehisto == ehistoY)
    {
        delta = maxy-miny;
        minh  = miny;
    }
    else
    {
        return estatsINVALID_INPUT;
    }

    if (binwidth == 0)
    {
        binwidth = (delta)/nbins;
    }
    else
    {
        nbins = gmx_dnint((delta)/binwidth + 0.5);
    }
    snew(*x, nbins);
    snew(nindex, nbins);
    for (int i = 0; (i < nbins); i++)
    {
        (*x)[i] = minh + binwidth*(i+0.5);
    }
    if (normalized == 0)
    {
        dd = 1;
    }
    else
    {
        dd = 1.0/(binwidth*stats->np);
    }

    snew(*y, nbins);
    for (int i = 0; (i < stats->np); i++)
    {
        if (ehisto == ehistoY)
        {
            index = static_cast<int>((stats->y[i]-miny)/binwidth);
        }
        else if (ehisto == ehistoX)
        {
            index = static_cast<int>((stats->x[i]-minx)/binwidth);
        }
        if (index < 0)
        {
            index = 0;
        }
        if (index > nbins-1)
        {
            index = nbins-1;
        }
        (*y)[index] += dd;
        nindex[index]++;
    }
    if (*nb == 0)
    {
        *nb = nbins;
    }
    for (int i = 0; (i < nbins); i++)
    {
        if (nindex[i] > 0)
        {
            (*y)[i] /= nindex[i];
        }
    }

    sfree(nindex);

    return estatsOK;
}

static const char *stats_error[estatsNR] =
{
    "All well in STATS land",
    "No points",
    "Not enough memory",
    "Invalid histogram input",
    "Unknown error",
    "Not implemented yet"
};

const char *gmx_stats_message(int estats)
{
    if ((estats >= 0) && (estats < estatsNR))
    {
        return stats_error[estats];
    }
    else
    {
        return stats_error[estatsERROR];
    }
}

/* Old convenience functions, should be merged with the core
   statistics above. */
int lsq_y_ax(int n, real x[], real y[], real *a)
{
    gmx_stats_t lsq = gmx_stats_init();
    int         ok;
    real        da, chi2, Rfit;

    gmx_stats_add_points(lsq, n, x, y, 0, 0);
    ok = gmx_stats_get_a(lsq, elsqWEIGHT_NONE, a, &da, &chi2, &Rfit);
    gmx_stats_free(lsq);

    return ok;
}

static int low_lsq_y_ax_b(int n, real *xr, double *xd, real yr[],
                          real *a, real *b, real *r, real *chi2)
{
    gmx_stats_t lsq = gmx_stats_init();
    int         ok;

    for (int i = 0; (i < n); i++)
    {
        double pt;

        if (xd != NULL)
        {
            pt = xd[i];
        }
        else if (xr != NULL)
        {
            pt = xr[i];
        }
        else
        {
            gmx_incons("Either xd or xr has to be non-NULL in low_lsq_y_ax_b()");
        }

        if ((ok = gmx_stats_add_point(lsq, pt, yr[i], 0, 0)) != estatsOK)
        {
            gmx_stats_free(lsq);
            return ok;
        }
    }
    ok = gmx_stats_get_ab(lsq, elsqWEIGHT_NONE, a, b, NULL, NULL, chi2, r);
    gmx_stats_free(lsq);

    return ok;
}

int lsq_y_ax_b(int n, real x[], real y[], real *a, real *b, real *r, real *chi2)
{
    return low_lsq_y_ax_b(n, x, NULL, y, a, b, r, chi2);
}

int lsq_y_ax_b_xdouble(int n, double x[], real y[], real *a, real *b,
                       real *r, real *chi2)
{
    return low_lsq_y_ax_b(n, NULL, x, y, a, b, r, chi2);
}

int lsq_y_ax_b_error(int n, real x[], real y[], real dy[],
                     real *a, real *b, real *da, real *db,
                     real *r, real *chi2)
{
    gmx_stats_t lsq = gmx_stats_init();
    int         ok;

    for (int i = 0; (i < n); i++)
    {
        ok = gmx_stats_add_point(lsq, x[i], y[i], 0, dy[i]);
        if (ok != estatsOK)
        {
            gmx_stats_free(lsq);
            return ok;
        }
    }
    ok = gmx_stats_get_ab(lsq, elsqWEIGHT_Y, a, b, da, db, chi2, r);
    gmx_stats_free(lsq);

    return ok;
}