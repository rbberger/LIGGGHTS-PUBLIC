/* ----------------------------------------------------------------------
   LIGGGHTS - LAMMPS Improved for General Granular and Granular Heat
   Transfer Simulations

   LIGGGHTS is part of the CFDEMproject
   www.liggghts.com | www.cfdem.com

   Christoph Kloss, christoph.kloss@cfdem.com
   Copyright 2009-2012 JKU Linz
   Copyright 2012-     DCS Computing GmbH, Linz

   LIGGGHTS is based on LAMMPS
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   This software is distributed under the GNU General Public License.

   See the README file in the top-level directory.
------------------------------------------------------------------------- */

#ifdef CFD_DATACOUPLING_CLASS

   CfdDataCouplingStyle(mpi,CfdDatacouplingMPI)

#else

#ifndef LMP_CFD_DATACOUPLING_MPI_H
#define LMP_CFD_DATACOUPLING_MPI_H

#include "cfd_datacoupling.h"
#include "multisphere.h"
#include "error.h"
#include "mpi.h"

namespace LAMMPS_NS {

class CfdDatacouplingMPI : public CfdDatacoupling {
 public:
  CfdDatacouplingMPI(class LAMMPS *, int,int, char **,class FixCfdCoupling*);
  ~CfdDatacouplingMPI();

  void exchange();

  virtual void pull(const char *name, const char *type, void *&ptr, const char *datatype);
  virtual void push(const char *name, const char *type, void *&ptr, const char *datatype);

  template <typename T> void pull_mpi(const char *,const char *,void *&);
  template <typename T> void push_mpi(const char *,const char *,void *&);

  virtual bool error_push()
  { return false;}

  virtual void allocate_external(int    **&data, int len2,int len1,     int initvalue);
  virtual void allocate_external(int    **&data, int len2,const char *keyword,int initvalue);
  virtual void allocate_external(double **&data, int len2,int len1,     double initvalue);
  virtual void allocate_external(double **&data, int len2,const char *keyword,double initvalue);

 private:
  template <typename T> T* check_grow(int len);
  template <typename T> MPI_Datatype mpi_type_dc();

  // 1D helper array needed to allreduce the quantities
  int len_allred_double;
  double *allred_double;

  int len_allred_int;
  int *allred_int;
};

/* ---------------------------------------------------------------------- */

template <typename T>
void CfdDatacouplingMPI::pull_mpi(const char *name,const char *type,void *&from)
{
    int len1 = -1, len2 = -1, m;

    // get reference where to write the data
    void * to = find_pull_property(name,type,len1,len2);

    if (atom->nlocal && (!to || len1 < 0 || len2 < 0))
    {
        if(screen) fprintf(screen,"LIGGGHTS could not find property %s to write data from calling program to.\n",name);
        lmp->error->one(FLERR,"This is fatal");
    }

    // return if no data to transmit
    if(len1*len2 < 1) return;

    // check memory allocation
    T* allred = check_grow<T>(len1*len2);

    // zeroize before using allreduce
    vectorZeroizeN(allred,len1*len2);

    // perform allreduce on incoming data
    T **from_t = (T**)from;
    MPI_Allreduce(&(from_t[0][0]),&(allred[0]),len1*len2,mpi_type_dc<T>(),MPI_SUM,world);

    // copy data - loops over max # global atoms, bodies
    if(strcmp(type,"scalar-atom") == 0)
    {
        T *to_t = (T*) to;
        for (int i = 0; i < len1; i++)
            if ((m = atom->map(i+1)) >= 0)
                to_t[m] = allred[i];
    }
    else if(strcmp(type,"vector-atom") == 0)
    {
        T **to_t = (T**) to;
        for (int i = 0; i < len1; i++)
            if ((m = atom->map(i+1)) >= 0)
                for (int j = 0; j < len2; j++)
                    to_t[m][j] = allred[i*len2 + j];
    }
    else if(strcmp(type,"scalar-multisphere") == 0)
    {
        T *to_t = (T*) to;
        if(!ms_data_)
            error->one(FLERR,"Transferring a multisphere property from/to LIGGGHTS requires a fix multisphere");
        for (int i = 0; i < len1; i++)
            if ((m = ms_data_->map(i+1)) >= 0)
                to_t[m] = allred[i];
    }
    else if(strcmp(type,"vector-multisphere") == 0)
    {
        T **to_t = (T**) to;
        if(!ms_data_)
            error->one(FLERR,"Transferring a multisphere property from/to LIGGGHTS requires a fix multisphere");
        for (int i = 0; i < len1; i++)
            if ((m = ms_data_->map(i+1)) >= 0)
                for (int j = 0; j < len2; j++)
                    to_t[m][j] = allred[i*len2 + j];
    }
    else if(strcmp(type,"scalar-global") == 0 || strcmp(type,"vector-global") == 0 || strcmp(type,"matrix-global") == 0)
    {
        T **to_t = (T**) to;
        for (int i = 0; i < len1; i++)
            for (int j = 0; j < len2; j++)
                to_t[i][j] = allred[i*len2 + j];
    }
    else error->one(FLERR,"Illegal data type in CfdDatacouplingMPI::pull");
}

/* ---------------------------------------------------------------------- */

template <typename T>
void CfdDatacouplingMPI::push_mpi(const char *name,const char *type,void *&to)
{
    int len1 = -1, len2 = -1, id;

    int *tag = atom->tag;
    int nlocal = atom->nlocal;
    int nbodies = 0;
    if(ms_data_) nbodies = ms_data_->n_body();

    // get reference where to write the data
    void * from = find_push_property(name,type,len1,len2);

    if (atom->nlocal && (!from || len1 < 0 || len2 < 0))
    {
        
        if(screen) fprintf(screen,"LIGGGHTS could not find property %s to write data from calling program to.\n",name);
        lmp->error->one(FLERR,"This is fatal");
    }

    // return if no data to transmit
    if(len1*len2 < 1) return;

    // check memory allocation
    T * allred = check_grow<T>(len1*len2);

    // zeroize before using allreduce
    vectorZeroizeN(allred,len1*len2);

    // copy data - loop local # atoms, bodies
    if(strcmp(type,"scalar-atom") == 0)
    {
        T *from_t = (T*) from;
        for (int i = 0; i < nlocal; i++)
        {
            id = tag[i];
            allred[id-1] = from_t[i];
        }
    }
    else if(strcmp(type,"vector-atom") == 0)
    {
        T **from_t = (T**) from;
        for (int i = 0; i < nlocal; i++)
        {
            id = tag[i];
            for (int j = 0; j < len2; j++)
                allred[(id-1)*len2 + j] = from_t[i][j];
        }
    }
    else if(strcmp(type,"scalar-multisphere") == 0)
    {
        T *from_t = (T*) from;
        if(!ms_data_)
            error->one(FLERR,"Transferring a multisphere property from/to LIGGGHTS requires a fix multisphere");
        for (int i = 0; i < nbodies; i++) // loops over # local bodies
        {
            id = ms_data_->tag(i);
            allred[id-1] = from_t[i];
        }
    }
    else if(strcmp(type,"vector-multisphere") == 0)
    {
        T **from_t = (T**) from;
        if(!ms_data_)
            error->one(FLERR,"Transferring a multisphere property from/to LIGGGHTS requires a fix multisphere");
        for (int i = 0; i < nbodies; i++) // loops over # local bodies
        {
            id = ms_data_->tag(i);
            for (int j = 0; j < len2; j++)
            {
                allred[(id-1)*len2 + j] = from_t[i][j];
                
            }
        }
        
    }
    else if(strcmp(type,"scalar-global") == 0 || strcmp(type,"vector-global") == 0 || strcmp(type,"matrix-global") == 0)
    {
        T **from_t = (T**) from;
        for (int i = 0; i < len1; i++)
            for (int j = 0; j < len2; j++)
                allred[i*len2 + j] = from_t[i][j];
    }
    else error->one(FLERR,"Illegal data type in CfdDatacouplingMPI::pull");

    // perform allreduce on outgoing data
    T **to_t = (T**)to;
    MPI_Allreduce(&(allred[0]),&(to_t[0][0]),len1*len2,mpi_type_dc<T>(),MPI_SUM,world);
}

/* ---------------------------------------------------------------------- */

template<typename T>
inline MPI_Datatype CfdDatacouplingMPI::mpi_type_dc()
{
    error->all(FLERR,"Illegal call to mpi_type_dc(), valid types are int and double");
    return 0;
}

template<>
inline MPI_Datatype CfdDatacouplingMPI::mpi_type_dc<double>()
{
    return MPI_DOUBLE;
}

template<>
inline MPI_Datatype CfdDatacouplingMPI::mpi_type_dc<int>()
{
    return MPI_INT;
}

/* ---------------------------------------------------------------------- */

template <typename T>
T* CfdDatacouplingMPI::check_grow(int len)
{
    error->all(FLERR,"Illegal call to template <typename T> T* check_grow(int len)");
    return NULL;
}

template <>
inline double* CfdDatacouplingMPI::check_grow<double>(int len)
{
    while(len > len_allred_double)
        len_allred_double += 10000;

    allred_double = (double*) memory->srealloc(allred_double,len_allred_double*sizeof(double),"fix_cfd_coupling:allred_double");
    vectorZeroizeN(allred_double,len_allred_double);
    return allred_double;
}

template <>
inline int* CfdDatacouplingMPI::check_grow<int>(int len)
{
    while(len > len_allred_int)
        len_allred_int += 10000;

    allred_int = (int*) memory->srealloc(allred_int,len_allred_int*sizeof(int),"fix_cfd_coupling:allred_int");
    vectorZeroizeN(allred_int,len_allred_int);
    return allred_int;
}

}

#endif
#endif
