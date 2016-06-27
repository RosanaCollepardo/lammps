/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   This software is distributed under the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------
   Contributing author: Axel Kohlmeyer (Temple U)
------------------------------------------------------------------------- */

#include <math.h>
#include "pair_coul_dsf_omp.h"
#include "atom.h"
#include "comm.h"
#include "force.h"
#include "math_special.h"
#include "math_const.h"
#include "neighbor.h"
#include "neigh_list.h"

#include "suffix.h"
#include "math_const.h"
using namespace LAMMPS_NS;
using namespace MathSpecial;
using namespace MathConst;

/* ---------------------------------------------------------------------- */

PairCoulDSFOMP::PairCoulDSFOMP(LAMMPS *lmp) :
  PairCoulDSF(lmp), ThrOMP(lmp, THR_PAIR)
{
  suffix_flag |= Suffix::OMP;
  respa_enable = 0;
}

/* ---------------------------------------------------------------------- */

void PairCoulDSFOMP::compute(int eflag, int vflag)
{
  if (eflag || vflag) {
    ev_setup(eflag,vflag);
  } else evflag = vflag_fdotr = 0;

  const int nall = atom->nlocal + atom->nghost;
  const int nthreads = comm->nthreads;
  const int inum = list->inum;

#if defined(_OPENMP)
#pragma omp parallel default(none) shared(eflag,vflag)
#endif
  {
    int ifrom, ito, tid;

    loop_setup_thr(ifrom, ito, tid, inum, nthreads);
    ThrData *thr = fix->get_thr(tid);
    thr->timer(Timer::START);
    ev_setup_thr(eflag, vflag, nall, eatom, vatom, thr);

    if (evflag) {
      if (eflag) {
        if (force->newton_pair) eval<1,1,1>(ifrom, ito, thr);
        else eval<1,1,0>(ifrom, ito, thr);
      } else {
        if (force->newton_pair) eval<1,0,1>(ifrom, ito, thr);
        else eval<1,0,0>(ifrom, ito, thr);
      }
    } else {
      if (force->newton_pair) eval<0,0,1>(ifrom, ito, thr);
      else eval<0,0,0>(ifrom, ito, thr);
    }

    thr->timer(Timer::PAIR);
    reduce_thr(this, eflag, vflag, thr);
  } // end of omp parallel region
}

/* ---------------------------------------------------------------------- */

template <int EVFLAG, int EFLAG, int NEWTON_PAIR>
void PairCoulDSFOMP::eval(int iifrom, int iito, ThrData * const thr)
{
  int i,j,ii,jj,jnum;
  double qtmp,xtmp,ytmp,ztmp,delx,dely,delz,ecoul,fpair;
  double r,rsq,r2inv,forcecoul,factor_coul;
  double prefactor,erfcc,erfcd,grij;
  int *ilist,*jlist,*numneigh,**firstneigh;

  ecoul = 0.0;

  const dbl3_t * _noalias const x = (dbl3_t *) atom->x[0];
  dbl3_t * _noalias const f = (dbl3_t *) thr->get_f()[0];
  const double * _noalias const q = atom->q;
  const int nlocal = atom->nlocal;
  const double * _noalias const special_coul = force->special_coul;
  const double qqrd2e = force->qqrd2e;
  double fxtmp,fytmp,fztmp;

  ilist = list->ilist;
  numneigh = list->numneigh;
  firstneigh = list->firstneigh;

  // loop over neighbors of my atoms

  for (ii = iifrom; ii < iito; ++ii) {

    i = ilist[ii];
    qtmp = q[i];
    xtmp = x[i].x;
    ytmp = x[i].y;
    ztmp = x[i].z;
    jlist = firstneigh[i];
    jnum = numneigh[i];
    fxtmp=fytmp=fztmp=0.0;

    if (EFLAG) {
      double e_self = -(e_shift/2.0 + alpha/MY_PIS) * qtmp*qtmp*qqrd2e;
      ev_tally_thr(this,i,i,nlocal,0,0.0,e_self,0.0,0.0,0.0,0.0,thr);
    }

    for (jj = 0; jj < jnum; jj++) {
      j = jlist[jj];
      factor_coul = special_coul[sbmask(j)];
      j &= NEIGHMASK;

      delx = xtmp - x[j].x;
      dely = ytmp - x[j].y;
      delz = ztmp - x[j].z;
      rsq = delx*delx + dely*dely + delz*delz;

      if (rsq < cut_coulsq) {
        r2inv = 1.0/rsq;

        r = sqrt(rsq);
        prefactor = qqrd2e*qtmp*q[j]/r;
        grij = alpha*r;
        erfcd = expmsq(grij);
        erfcc = my_erfcx(grij) * erfcd;
        forcecoul = prefactor * (erfcc/r + 2.0*alpha/MY_PIS * erfcd +
                                 r*f_shift) * r;
        if (factor_coul < 1.0) forcecoul -= (1.0-factor_coul)*prefactor;
        fpair = forcecoul * r2inv;

        fxtmp += delx*fpair;
        fytmp += dely*fpair;
        fztmp += delz*fpair;
        if (NEWTON_PAIR || j < nlocal) {
          f[j].x -= delx*fpair;
          f[j].y -= dely*fpair;
          f[j].z -= delz*fpair;
        }

        if (EFLAG) {
          ecoul = prefactor * (erfcc - r*e_shift - rsq*f_shift);
          if (factor_coul < 1.0) ecoul -= (1.0-factor_coul)*prefactor;
        } else ecoul = 0.0;

        if (EVFLAG) ev_tally_thr(this, i,j,nlocal,NEWTON_PAIR,
                                 0.0,ecoul,fpair,delx,dely,delz,thr);
      }
    }
    f[i].x += fxtmp;
    f[i].y += fytmp;
    f[i].z += fztmp;
  }
}

/* ---------------------------------------------------------------------- */

double PairCoulDSFOMP::memory_usage()
{
  double bytes = memory_usage_thr();
  bytes += PairCoulDSF::memory_usage();

  return bytes;
}
