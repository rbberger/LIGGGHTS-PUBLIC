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

#include "math.h"
#include "stdlib.h"
#include "string.h"
#include "atom.h"
#include "modify.h"
#include "memory.h"
#include "error.h"
#include "comm.h"
#include "cfd_datacoupling.h"
#include "fix_cfd_coupling.h"
#include "fix_multisphere.h"
#include "multisphere.h"
#include "fix_property_atom.h"
#include "fix_property_global.h"

using namespace LAMMPS_NS;

#define MAXLENGTH 30

/* ---------------------------------------------------------------------- */

CfdDatacoupling::CfdDatacoupling(class LAMMPS *lmp, int jarg, int, char **, class FixCfdCoupling*) :
    Pointers(lmp)
{
      iarg_ = jarg;
      this->fc_ = fc_;
      is_parallel = true;

      npull_ = 0;
      npush_ = 0;
      nvalues_max_ = 0;

      pullnames_ = NULL;
      pulltypes_ = NULL;
      pushnames_ = NULL;
      pushtypes_ = NULL;
      pushinvoked_ = NULL;
      pullinvoked_ = NULL;

      ms_ = NULL;
      ms_data_ = NULL;

      grow_();
}

/* ---------------------------------------------------------------------- */

CfdDatacoupling::~CfdDatacoupling()
{
        memory->destroy(pullnames_);
        memory->destroy(pulltypes_);
        memory->destroy(pushnames_);
        memory->destroy(pushtypes_);
}

/* ---------------------------------------------------------------------- */

void CfdDatacoupling::init()
{
    // multisphere - can be NULL
    ms_ = static_cast<FixMultisphere*>(modify->find_fix_style_strict("multisphere",0));

    if(ms_) ms_data_ = &ms_->data();

    // empty list of requested properties
    // models do their init afterwards so list will be filled
    for(int i = 0; i < nvalues_max_; i++)
      pushinvoked_[i] = pullinvoked_[i] = 0;
}

/* ---------------------------------------------------------------------- */

void CfdDatacoupling::grow_()
{
    nvalues_max_ +=10;
    memory->grow(pullnames_,nvalues_max_,MAXLENGTH,"FixCfdCoupling:valnames");
    memory->grow(pulltypes_,nvalues_max_,MAXLENGTH,"FixCfdCoupling:valtypes");
    memory->grow(pushinvoked_,MAXLENGTH,"FixCfdCoupling:pushinvoked_");

    memory->grow(pushnames_,nvalues_max_,MAXLENGTH,"FixCfdCoupling:pushnames_");
    memory->grow(pushtypes_,nvalues_max_,MAXLENGTH,"FixCfdCoupling:pushtypes_");
    memory->grow(pullinvoked_,MAXLENGTH,"FixCfdCoupling:pullinvoked_");
}

/* ----------------------------------------------------------------------
   pull property from other code
------------------------------------------------------------------------- */

void CfdDatacoupling::pull(const char *name, const char *type, void *&, const char *)
{
    // for MPI this is called by the library interface
    // check if the requested property was registered by a LIGGGHTS model
    // ie this checks if the model settings of OF and LIGGGHTS fit together
    int found = 0;
    for(int i = 0; i < npull_; i++)
    {
        // both name and type matches - ok
        if(strcmp(name,pullnames_[i]) == 0 && strcmp(type,pulltypes_[i]) == 0)
        {
            found = 1;
            pullinvoked_[i] = 1;
        }
        // name matches, but type not
        else if(strcmp(name,pullnames_[i]) == 0)
        {
            if(comm->me == 0 && screen)
                fprintf(screen,"LIGGGHTS could find property %s requested by calling program, type %s is requested, but type set in LIGGGHTS is %s?\n",
                        name,type,pulltypes_[i]);
            error->all(FLERR,"This error is fatal");
        }
    }
    if(!found)
    {
        if(comm->me == 0 && screen)
            fprintf(screen,"LIGGGHTS could not find property %s requested by calling program. Check your model settings in LIGGGHTS.\n",name);
        error->all(FLERR,"This error is fatal");
    }
}

/* ----------------------------------------------------------------------
   push property to other code
------------------------------------------------------------------------- */

void CfdDatacoupling::push(const char *name, const char *type, void *&, const char *)
{
    // for MPI this is called by the library interface
    // check if the requested property was registered by a LIGGGHTS model
    // ie this checks if the model settings of OF and LIGGGHTS fit together
    int found = 0;
    for(int i = 0; i < npush_; i++)
    {
        if(strcmp(name,pushnames_[i]) == 0 && strcmp(type,pushtypes_[i]) == 0)
        {
            found = 1;
            pushinvoked_[i] = 1;
        }
        // name matches, but type not
        else if(strcmp(name,pushnames_[i]) == 0)
        {
            if(comm->me == 0 && screen)
                fprintf(screen,"LIGGGHTS could find property %s requested by calling program, but type %s is wrong, did you mean %s?\n",
                        name,type,pushtypes_[i]);
            error->all(FLERR,"This error is fatal");
        }
    }
    if(!found && error_push())
    {
        if(comm->me == 0 && screen)
            fprintf(screen,"LIGGGHTS could not find property %s requested by calling program. Check your model settings in LIGGGHTS.\n",name);
        lmp->error->all(FLERR,"This error is fatal");
    }
}

/* ----------------------------------------------------------------------
   check if all properties that were requested are actually communicated
------------------------------------------------------------------------- */

void CfdDatacoupling::check_datatransfer()
{
    for(int i = 0; i < npull_; i++)
    {
       if(!pullinvoked_[i])
       {
            if(comm->me == 0 && screen)
                fprintf(screen,"Communication of property %s from OF to LIGGGHTS was not invoked, but needed by "
                                "a LIGGGHTS model. Check your model settings in OF.\n",pullnames_[i]);
            lmp->error->all(FLERR,"This error is fatal");
       }
    }

    for(int i = 0; i < npush_; i++)
    {
       if(!pushinvoked_[i] && error_push())
       {
            if(comm->me == 0 && screen)
                fprintf(screen,"Communication of property %s from LIGGGHTS to OF was not invoked, but needed by "
                                "a LIGGGHTS model. Check your model settings in OF.\n",pushnames_[i]);
            lmp->error->all(FLERR,"This error is fatal");
       }
    }
}

/* ----------------------------------------------------------------------
   request a property to be pulled. called by models that implement physics
------------------------------------------------------------------------- */

void CfdDatacoupling::add_pull_property(const char *name, const char *type)
{
    
    if(strlen(name) >= MAXLENGTH) error->all(FLERR,"Fix couple/cfd: Maximum string length for a variable exceeded");
    if(npull_ >= nvalues_max_) grow_();

    for(int i = 0; i < npull_; i++)
    {
        if(strcmp(pullnames_[i],name) == 0 && strcmp(pulltypes_[i],type) == 0)
            return;
        if(strcmp(pullnames_[i],name) == 0 && strcmp(pulltypes_[i],type))
            error->all(FLERR,"Properties added via CfdDatacoupling::add_pull_property are inconsistent");
    }

    // test if property is available

    int len1,len2;
    void *ptr = find_pull_property(name,type,len1,len2);
    if (atom->nlocal && (!ptr || len1 < 0 || len2 < 0))
    {
        if(screen) fprintf(screen,"Property %s added via CfdDatacoupling::add_pull_property not found.\n",name);
        error->one(FLERR,"This is fatal");
    }

    strcpy(pullnames_[npull_],name);
    strcpy(pulltypes_[npull_],type);
    npull_++;
}

/* ----------------------------------------------------------------------
   request a property to be pushed. called by models that implement physics
------------------------------------------------------------------------- */

void CfdDatacoupling::add_push_property(const char *name, const char *type)
{
    
    if(strlen(name) >= MAXLENGTH)
        error->all(FLERR,"Fix couple/cfd: Maximum string length for a variable exceeded");
    if(npush_ >= nvalues_max_) grow_();

    for(int i = 0; i < npush_; i++)
    {
        if(strcmp(pushnames_[i],name) == 0 && strcmp(pushtypes_[i],type) == 0)
            return;
        if(strcmp(pushnames_[i],name) == 0 && strcmp(pushtypes_[i],type))
            error->all(FLERR,"Properties added via CfdDatacoupling::add_push_property are inconsistent");
    }

    // test if property is available

    int len1,len2;
    void *ptr = find_push_property(name,type,len1,len2);
    if (atom->nlocal && (!ptr || len1 < 0 || len2 < 0))
    {
        if(screen) fprintf(screen,"Property %s added via CfdDatacoupling::add_push_property not found.\n",name);
        error->one(FLERR,"This is fatal");
    }

    strcpy(pushnames_[npush_],name);
    strcpy(pushtypes_[npush_],type);
    npush_++;
}

/* ---------------------------------------------------------------------- */

void* CfdDatacoupling::find_pull_property(const char *name, const char *type, int &len1, int &len2)
{
    return find_property(0,name,type,len1,len2);
}

/* ---------------------------------------------------------------------- */
void* CfdDatacoupling::find_push_property(const char *name, const char *type, int &len1, int &len2)
{
    return find_property(1,name,type,len1,len2);
}

/* ----------------------------------------------------------------------
   find a property that was requested
   called from data exchange model
   property may come from atom class, from a fix property, or fix rigid
   last 2 args are the data length and are used for all data
------------------------------------------------------------------------- */

void* CfdDatacoupling::find_property(int, const char *name, const char *type, int &len1, int &len2)
{
    
    void *ptr = NULL;

    // possiblility 1
    // may be atom property - look up in atom class

    ptr = atom->extract(name,len2);
    // if nlocal == 0 and property found atom->extract return NULL and len2 >= 0
    if(ptr || len2 >= 0)
    {
        len1 = atom->tag_max();
        // check if length correct
        if(((strcmp(type,"scalar-atom") == 0) && (len2 != 1)) || ((strcmp(type,"vector-atom") == 0) && (len2 != 3)))
            return NULL;
        return ptr;
    }

    // possiblility 2
    // may come from a fix multisphere
    // also handles scalar-multisphere and vector-multisphere

    if(ms_)
    {
        ptr = ms_->extract(name,len1,len2);
        if(((strcmp(type,"scalar-multisphere") == 0) && (len2 != 1)) || ((strcmp(type,"vector-multisphere") == 0) && (len2 != 3)))
            return NULL;

        if(ptr || ((len1 >= 0) && (len2 >= 0)))
        return ptr;
    }

    // possiblility 3
    // may be fix property per atom - look up in modify class

    Fix *fix = NULL;

    if(strcmp(type,"scalar-atom") == 0)
    {
       fix = modify->find_fix_property(name,"property/atom","scalar",0,0,"cfd coupling",false);
       if(fix)
       {
           len1 = atom->tag_max();
           len2 = 1;
           return (void*) static_cast<FixPropertyAtom*>(fix)->vector_atom;
       }
    }
    else if(strcmp(type,"vector-atom") == 0)
    {
       fix = modify->find_fix_property(name,"property/atom","vector",0,0,"cfd coupling",false);
       if(fix)
       {
           len1 = atom->tag_max();
           len2 = 3;
           return (void*) static_cast<FixPropertyAtom*>(fix)->array_atom;
       }
    }
    else if(strcmp(type,"scalar-global") == 0)
    {
       fix = modify->find_fix_property(name,"property/global","scalar",0,0,"cfd coupling",false);
       len1 = len2 = 1;
       if(fix) return (void*) static_cast<FixPropertyGlobal*>(fix)->values;
    }
    else if(strcmp(type,"vector-global") == 0)
    {
       fix = modify->find_fix_property(name,"property/global","vector",0,0,"cfd coupling",false);
       if(fix)
       {
           len1 = static_cast<FixPropertyGlobal*>(fix)->nvalues;
           len2 = 1;
           return (void*) static_cast<FixPropertyGlobal*>(fix)->values;
       }
    }
    else if(strcmp(type,"matrix-global") == 0)
    {
       fix = modify->find_fix_property(name,"property/global","matrix",0,0,"cfd coupling",false);
       if(fix)
       {
           len1  = static_cast<FixPropertyGlobal*>(fix)->size_array_rows;
           len2  = static_cast<FixPropertyGlobal*>(fix)->size_array_cols;
           return (void*) static_cast<FixPropertyGlobal*>(fix)->array;
       }
    }
    else if(strcmp(name,"ex") == 0) 
    {
        // possiblility 4A - Dipole is specified as atom property (requires DIPOLE package)
        ptr = atom->extract("mu",len2); 
        printf("len2 of mu: %d \n", len2);
        if(ptr)
        {
            len1 = atom->tag_max();
            // check if length correct
            if( (strcmp(type,"vector-atom") == 0) && (len2 != 3) )
                return NULL;
            return ptr;
        }
        
        // possiblility 4B - Quaternion is specified as atom property (requires ASPHERE package)
        // requires a fix that computes orientation data from quaternion,
        // or another fix that provides orientationEx (e.g., from POEMS)
        //TODO: Write fix that computes orientation from quaternion
        //TODO: Check if correct data is drawn in case a FixPOEMS is used
        fix = modify->find_fix_property("orientationEx","property/atom","vector",0,0,"cfd coupling",false);
        if(fix)
        {
               len1 = atom->tag_max();
               len2 = 3;
               return (void*) static_cast<FixPropertyAtom*>(fix)->array_atom;
        }
        else printf("WARNING: Fix with name 'orientationEx' not found that stores orientation information. \n");
    }
    return NULL;
}

/* ---------------------------------------------------------------------- */

void CfdDatacoupling::allocate_external(int**&, int, int, int)
{
    error->all(FLERR,"CFD datacoupling setting used in LIGGGHTS is incompatible with setting in OF");
}

/* ---------------------------------------------------------------------- */

void CfdDatacoupling::allocate_external(double**&, int, int, double)
{
    error->all(FLERR,"CFD datacoupling setting used in LIGGGHTS is incompatible with setting in OF");
}

/* ---------------------------------------------------------------------- */

void CfdDatacoupling::allocate_external(int**&, int, const char *, int)
{
    error->all(FLERR,"CFD datacoupling setting used in LIGGGHTS is incompatible with setting in OF");
}

/* ---------------------------------------------------------------------- */

void CfdDatacoupling::allocate_external(double**&, int, const char *, double)
{
    error->all(FLERR,"CFD datacoupling setting used in LIGGGHTS is incompatible with setting in OF");
}
