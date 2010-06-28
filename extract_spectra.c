/* Copyright (c) 2005, J. Bolton
 *      Modified 2009 by Simeon Bird <spb41@cam.ac.uk>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "global_vars.h"
#include "parameters.h"

/*****************************************************************************/
void SPH_interpolation(int NumLos, int Ntype,los *los_table,  pdata* P)
{
  const double Hz=100.0*h100 * sqrt(1.+omega0*(1./atime-1.)+omegaL*((atime*atime) -1.))/atime;
#ifdef RAW_SPECTRA
  const double H0 = 1.0e5/MPC; /* 100kms^-1Mpc^-1 in SI */ 
    /* Critical matter/energy density at z = 0.0 */
  const double rhoc = 3.0 * (H0*h100)*(H0*h100) / (8.0 * M_PI * GRAVITY); /* kgm^-3 */
  /* Mean hydrogen mass density of the Universe */
  const double critH = (rhoc * OMEGAB * XH) / (atime*atime*atime); /* kgm^-3*/
#endif
  /* Conversion factors from internal units */
  const double rscale = (KPC*atime)/h100;   /* convert length to m */
  const double vscale = sqrt(atime);        /* convert velocity to kms^-1 */
  const double mscale = (1.0e10*SOLAR_MASS)/h100; /* convert mass to kg */
  const double escale = 1.0e6;           /* convert energy/unit mass to J kg^-1 */
  const double tscale = ((GAMMA-1.0) * HMASS * PROTONMASS * escale ) / BOLTZMANN; /* convert (with mu) T to K */
  /*const double hscale = rscale * 0.5;*/ /* Note the factor of 0.5 for this kernel definition */
  /*    Calculate the length scales to be used in the box */
  const double zmingrid = 0.0;
  const double zmaxgrid = box100;  /* box sizes in kpc */
  const double dzgrid   = (zmaxgrid-zmingrid) / (double)NBINS; /* bin size (kpc) */
  const double dzinv    = 1. / dzgrid;
  const double boxsize  = zmaxgrid;   
  const double box2     = 0.5 * boxsize;
  const double dzbin = box100/ (double)NBINS; /* bin size (comoving kpc/h) */
  const double vmax = box100 * Hz * rscale/ MPC; /* box size (kms^-1) */
  const double vmax2 = vmax/2.0; /* kms^-1 */
  const double dvbin = vmax / (double)NBINS; /* bin size (kms^-1) */

  /* Absorption cross-sections m^2 */
  const double sigma_Lya_H1  = sqrt(3.0*M_PI*SIGMA_T/8.0) * LAMBDA_LYA_H1  * FOSC_LYA;
  /* Prefactor for optical depth  */
  const double A_H1 = rscale*sigma_Lya_H1*C*dzgrid/sqrt(M_PI);  
#ifdef HELIUM
  const double sigma_Lya_He2 = sqrt(3.0*M_PI*SIGMA_T/8.0) * LAMBDA_LYA_HE2 * FOSC_LYA;
  const double A_He2 =  sigma_Lya_He2*C*dzgrid/sqrt(M_PI);
#endif
  int iproc;
  /*   Initialise distance coordinate for iaxis */
  posaxis[0]=0.0;
  velaxis[0]=0.0;
  
  for(iproc=0;iproc<NBINS-1;iproc++)
    {
      posaxis[iproc+1] = posaxis[iproc] + dzbin; /* comoving kpc/h */
      velaxis[iproc+1] = velaxis[iproc] + dvbin; /* physical km s^-1 */
    }
  
#pragma omp parallel
  { 
#if 0 
    int i;
  /*   Convert to SI units from GADGET-3 units */
  #pragma omp for schedule(static, 128)
  for(i=0;i<Ntype;i++)
  {
      double mu;
      int ic;
      for(ic=0;ic<3;ic++)
	{
	  (*P).Pos[3*i+ic] *= rscale; /* m, physical */
	  (*P).Vel[3*i+ic] *= vscale; /* km s^-1, physical */
	}
      
      (*P).h[i] *= hscale;   /* m, physical */
/*      (*P).Mass[i] = (*P).Mass[i] * mscale; *//* kg */
      /*We leave mass in GADGET units, to prevent a floating overflow
       * when we have poor resolution. (*P).Mass[i] only affects rhoker, 
       * so we simply rescale rhoker later.*/ 

      /* Mean molecular weight */
      mu = 1.0/(XH*(0.75+(*P).Ne[i]) + 0.25);
      (*P).U[i] *= ((GAMMA-1.0) * mu * HMASS * PROTONMASS * escale ) / BOLTZMANN; /* K */
  }
  #pragma omp master
  {
    printf("Converted units.\n");
  }
  #pragma omp barrier
#endif
  /*    Generate random coordinates for a point in the box */
  #pragma omp for schedule(static, THREAD_ALLOC)
  for(iproc=0;iproc<NumLos;iproc++)
    { 
      double xproj,yproj,zproj;
      int iaxis,iz,ioff,j,iiz,ii,i;
#ifdef RAW_SPECTRA
      double rhoker_H[NBINS];
#endif
      double rhoker_H1[NBINS],velker_H1[NBINS],temker_H1[NBINS];
      double temp_H1_local[NBINS],veloc_H1_local[NBINS], tau_H1_local[NBINS];
#ifdef HELIUM
      double rhoker_He2[NBINS], velker_He2[NBINS],temker_He2[NBINS];
      double temp_He2_local[NBINS],veloc_He2_local[NBINS], tau_He2_local[NBINS];
#endif
      for(i=0; i<NBINS; i++)
      {
#ifdef RAW_SPECTRA
         rhoker_H[i]=0;
#endif
         rhoker_H1[i]=0;
         velker_H1[i]=0;
         temker_H1[i]=0;
         temp_H1_local[i]=0;
         veloc_H1_local[i]=0;
         tau_H1_local[i]=0;
      }
      /*Load a sightline from the table.*/
      iaxis = los_table[iproc].axis;
      xproj = los_table[iproc].xx;
      yproj = los_table[iproc].yy;
      zproj = los_table[iproc].zz;
     
      if((NumLos <20) ||  ((iproc % (NumLos/20)) ==0))
        printf("Interpolating line of sight %d...%g %g %g\n",iproc,xproj,yproj,zproj);
      
      /* Loop over particles in LOS and do the SPH interpolation */
      /* This first finds which particles are near this sight line. 
       * Probably a faster way to do that. 
       * Then adds the total density, temp. and velocity for near particles to 
       * the binned totals for that sightline*/
      for(i=0;i<Ntype;i++)
	{
	  double xx,yy,zz,hh,h2,h4,dr,dr2;
          double dzmax,zgrid;
	  /*     Positions (kpc) */
	  xx = (*P).Pos[3*i+0];
	  yy = (*P).Pos[3*i+1];
	  zz = (*P).Pos[3*i+2];
              
	  /* Resolution length (kpc) */
	  hh = (*P).h[i]*0.5; /*Factor of two in this kernel definition*/
	  h2 = hh*hh; 
	  h4 = 4.*h2;           /* 2 smoothing lengths squared */
	  
	  /*    Distance to projection axis */	  
	  if (iaxis == 1) 
	    dr = fabs(yy-yproj);
          else if (iaxis ==  2)
	    dr = fabs(xx-xproj);
          else 
            dr = fabs(xx-xproj);
	  
	  if (dr > box2) 
	    dr = boxsize - dr; /* Keep dr between 0 and box/2 */
	  
	  if (dr <= 2.*hh) /* dr less than 2 smoothing lengths */
	    {
	      dr2 = dr*dr;
	      
	      if (iaxis == 1)
		dr = fabs(zz - zproj);
              else if (iaxis == 2)
		dr = fabs(zz - zproj);
              else if(iaxis == 3)
		dr = fabs(yy - yproj);
	      
	      if (dr > box2)  
		dr = boxsize - dr; /* between 0 and box/2 */
              
	      dr2 = dr2 + (dr*dr);
	      
	      if (dr2 <= h4)
		{
		   const double H1frac = (*P).NH0[i]; /* nHI/nH */ 
                #ifdef HELIUM
                   const double He2frac = (*P).NHep[i]; /* nHeII/nH */
                #endif
	           const double hinv2 = 1. / h2; /* 1/h^2 */
		   const double hinv3 = hinv2 / hh; /* 1/h^3 */
		   
		   const double vr = (*P).Vel[3*i+iaxis-1]; /* peculiar velocity in GII units */
                   const double mu = 1.0/(XH*(0.75+(*P).Ne[i]) + 0.25);
		   const double temp = (*P).U[i]*mu; /* T in some strange units */
		   
		   /* Central vertex to contribute to */
		   if (iaxis == 1)
		     iz = (xx - zmingrid) * dzinv +1  ;
		   else if (iaxis == 2) 
		     iz = (yy - zmingrid) * dzinv +1 ;
		   else 
		     iz = (zz - zmingrid) * dzinv +1;
		   
		   dzmax = sqrt(fabs(h4 - dr2));
		   ioff = (int)(dzmax * dzinv) +1;
		   
		   /* Loop over contributing vertices */
		   for(iiz = iz-ioff; iiz < iz+ioff+1 ; iiz++)
		     {
                       double deltaz,dz,dist2,q,kernel,velker,temker;
		       j = iiz;
		       j = ((j-1+10*NBINS) % NBINS);
		       
		       zgrid = zmingrid + (double)(j) * dzgrid;
		       
		      if (iaxis == 1)
                        deltaz = zgrid - xx;
		      else if (iaxis == 2)
                        deltaz = zgrid - yy;
		      else
                        deltaz = zgrid - zz;
                     
		      if (deltaz > box2) 
			deltaz = deltaz - boxsize;
		      if (deltaz < -box2) 
			deltaz = deltaz + boxsize;
		      
		      dz = fabs(deltaz);
		      if(dz > box2) 
			dz = boxsize - dz;
		      
		      dist2 = dr2 + (dz*dz);		 

		      if (dist2 <= h4)
			{
			  q = sqrt(dist2 * hinv2);
			  if (q <= 1.)
			    kernel = (1.+ (q*q) * (-1.5 + 0.75 * q) )/M_PI;
			  else
			    kernel = 0.25*(2.0-q)*(2.0-q)*(2.0-q)/M_PI;
			  
			  kernel *= hinv3; 

			  kernel *= (*P).Mass[i]; /* kg (kpc)^-3 */
			  velker = vr * kernel; /* kg (kpc)^-3 * km s^-1 */
			  temker = temp * kernel; /* kg (kpc)^-3 * K */

                        #ifdef RAW_SPECTRA 
			  rhoker_H[j]  += kernel * XH;		 
                        #endif
			  rhoker_H1[j] += kernel * XH * H1frac;
			  velker_H1[j] += velker * XH * H1frac;
			  temker_H1[j] += temker * XH * H1frac;
                        #ifdef HELIUM
                          rhoker_He2[j] += kernel * XH * He2frac;
                          velker_He2[j] += velker * XH * He2frac;
                          temker_He2[j] += temker * XH * He2frac;
                        #endif

			}      /* dist2 < 4h^2 */
		     }        /* loop over contributing vertices */
		 }           /* dx^2+dy^2 < 4h^2 */
	    }               /* dx < 2h */
	}                  /* Loop over particles in LOS */
      
      for(i = 0;i<NBINS;i++)
	{
          /* If there are no particles in this bin, rhoker will be zero. 
           * In this case, we set temp and veloc arbitrarily to one, 
           * to avoid nans propagating. Zero rho will imply zero absorption 
           * anyway. */
          if(rhoker_H1[i]){       
        	  veloc_H1_local[i]  = vscale*velker_H1[i]/rhoker_H1[i]; /* HI weighted km s^-1 */ 
        	  temp_H1_local[i]   = tscale*temker_H1[i]/rhoker_H1[i]; /* HI weighted K */
                  rhoker_H1[i] *= mscale*pow(rscale,-3); /*Put rhoker in m units*/
             #ifdef RAW_SPECTRA
                  rhoker_H[i] *= mscale*pow(rscale,-3);
             #endif
          }
          else{
                  veloc_H1_local[i]=1;
                  temp_H1_local[i]=1;
          }
      	}
      
      /* Compute the HI Lya spectra */
      for(i=0;i<NBINS;i++)
	{
	  for(j=0;j<NBINS;j++)
	    {
              double T0,T1,T2,tau_H1j,aa_H1,u_H1,b_H1,profile_H1;
              double vdiff_H1;
	      
              u_H1  = velaxis[j]*1.0e3;
          #ifdef PECVEL 
              u_H1 +=veloc_H1_local[j]*1.0e3;
          #endif
              /* Note this is indexed with i, above with j! 
               * This is the difference in velocities between two clouds 
               * on the same sightline*/
	      vdiff_H1  = fabs((velaxis[i]*1.0e3) - u_H1); /* ms^-1 */
           #ifdef PERIODIC  
		  if (vdiff_H1 > (vmax2*1.0e3))
		    vdiff_H1 = (vmax*1.0e3) - vdiff_H1;
           #endif
	      b_H1   = sqrt(2.0*BOLTZMANN*temp_H1_local[j]/(HMASS*PROTONMASS));
	      T0 = pow(vdiff_H1/b_H1,2);
	      T1 = exp(-T0);
	      /* Voigt profile: Tepper-Garcia, 2006, MNRAS, 369, 2025 */ 
            #ifdef VOIGT
	      aa_H1 = GAMMA_LYA_H1*LAMBDA_LYA_H1/(4.0*M_PI*b_H1);
	      T2 = 1.5/T0;	
	      if(T0 < 1.0e-6)
	        profile_H1  = T1;
	      else
	        profile_H1  = T1 - aa_H1/sqrt(M_PI)/T0 
	          *(T1*T1*(4.0*T0*T0 + 7.0*T0 + 4.0 + T2) - T2 -1.0);
            #else   
	      profile_H1 = T1;
            #endif
	      tau_H1j  = A_H1  * rhoker_H1[j]  * profile_H1 /(HMASS*PROTONMASS*b_H1) ;
	      tau_H1_local[i]  += tau_H1j;
	    }
	}             /* Spectrum convolution */
      /* Compute the HeI Lya spectra: Probably doesn't work now */
#ifdef HELIUM
      for(i = 0;i<NBINS;i++)
	{
	  veloc_He2_local[i]  = velker_He2[i]/rhoker_He2[i]; /* HI weighted km s^-1 */ 
	  temp_He2_local[i]   = temker_He2[i]/rhoker_He2[i]; /* HI weighted K */
      	}
      for(i=0;i<NBINS;i++)
	{
	  for(j=0;j<NBINS;j++)
	    {
              double T3,T4,T5,tau_He2j,aa_He2,u_He2,b_He2,profile_He2;
              double vdiff_He2;
	      
              /* Note this is indexed with i, above with j! 
               * This is the difference in velocities between two clouds 
               * on the same sightline*/
              u_He2 = velaxis[j]*1.0e3;
           #ifdef PECVEL
              u_He2 += veloc_He2_local[j]*1.0e3;
           #endif
              vdiff_He2 = fabs((velaxis[i]*1.0e3) - u_He2); /* ms^-1 */
	      
           #ifdef PERIODIC  
		  if (vdiff_He2 > (vmax2*1.0e3))
		     vdiff_He2 = (vmax*1.0e3) - vdiff_He2; 
           #endif
	      
	      b_He2  = sqrt(2.0*BOLTZMANN*temp_He2_local[j]/(HEMASS*PROTONMASS));
	      T3 = (vdiff_He2/b_He2)*(vdiff_He2/b_He2);
	      T4 = exp(-T3);
	      
	      /* Voigt profile: Tepper-Garcia, 2006, MNRAS, 369, 2025 */ 
#ifdef VOIGT
		  aa_He2 = GAMMA_LYA_HE2*LAMBDA_LYA_HE2/(4.0*M_PI*b_He2);
		  T5 = 1.5/T3; 
		   if(T3 < 1.0e-6)
		       profile_He2  = T4;
 		  else
		    profile_He2 = T4 - aa_He2/sqrt(M_PI)/T3 
		    *(T4*T4*(4.0*T3*T3 + 7.0*T3 + 4.0 + T5) - T5 -1.0);
#else   
		  profile_He2 = T4;
#endif
	      tau_He2j = A_He2 * rhoker_He2[j] * profile_He2 /(HMASS*PROTONMASS*b_He2);
	      tau_He2_local[i] += tau_He2j;
	    }
	}             /* HeI Spectrum convolution */
#endif //HELIUM
      
      /*All non-thread-local memory writing should happen here*/
      for(i = 0;i<NBINS;i++)
	{
	  ii = i + (NBINS*iproc);
          tau_H1[ii]    = tau_H1_local[i];
        #ifdef RAW_SPECTRA	  
	  Delta[ii]     = log10(mscale*rhoker_H[i]/critH);   /* log H density normalised by mean 
                                                          H density of universe */
	  n_H1[ii]      = rhoker_H1[i]/rhoker_H[i];  /* HI/H */
          veloc_H1[ii]    = veloc_H1_local[i]; /* HI weighted km s^-1 */ 
	  temp_H1[ii]   = temp_H1_local[i]; /* HI weighted K */
        #endif
        #ifdef HELIUM
        #ifdef RAW_SPECTRA
	  n_He2[ii]      = rhoker_He2[i]/rhoker_H[i];  /* HI/H */
          veloc_He2[ii]  = veloc_He2_local[i]; /* HI weighted km s^-1 */ 
	  temp_He2[ii]   = temp_He2_local[i]; /* HI weighted K */
        #endif
          tau_He2[ii]    = tau_He2_local[i];
        #endif
      	}
    }                /* Loop over numlos random LOS */
  }/*End of parallel block*/
  return;
}

/*****************************************************************************/
void InitLOSMemory(int NumLos)
{  
  #ifdef RAW_SPECTRA
  Delta        = (double *) calloc((NumLos * NBINS) , sizeof(double));
  n_H1         = (double *) calloc((NumLos * NBINS) , sizeof(double));
  veloc_H1     = (double *) calloc((NumLos * NBINS) , sizeof(double));
  temp_H1      = (double *) calloc((NumLos * NBINS) , sizeof(double));
  #endif
  tau_H1       = (double *) calloc((NumLos * NBINS) , sizeof(double));
  posaxis      = (double *) calloc(NBINS , sizeof(double));
  velaxis      = (double *) calloc(NBINS , sizeof(double));
  flux_power   = (float *) calloc(NumLos * (NBINS+1)/2, sizeof(float));
  if(!posaxis  ||   !velaxis || 
  #ifdef RAW_SPECTRA
     !Delta ||  !n_H1 || !veloc_H1 || !temp_H1 || 
  #endif
     !tau_H1 || !flux_power  )
  {
      fprintf(stderr, "Failed to allocate memory!\n");
      exit(1);
  }
#ifdef HELIUM 
  n_He2         = (double *) calloc((NumLos * NBINS) , sizeof(double)); 
  veloc_He2     = (double *) calloc((NumLos * NBINS) , sizeof(double)); 
  temp_He2      = (double *) calloc((NumLos * NBINS) , sizeof(double)); 
  tau_He2       = (double *) calloc((NumLos * NBINS) , sizeof(double)); 
  if(!n_He2  || !veloc_He2 || ! temp_He2  || ! tau_He2 )
  {
      fprintf(stderr, "Failed to allocate helium memory!\n");
      exit(1);
  }
#endif
}
/*****************************************************************************/

/*****************************************************************************/
void FreeLOSMemory(void)
{  
#ifdef RAW_SPECTRA
  free(Delta)     ;
  free(n_H1     ) ;
  free(veloc_H1 ) ;
  free(temp_H1  ) ;
#else
  free(flux_power);
#endif
  free(posaxis)   ;
  free(velaxis)   ;
  free(tau_H1   ) ;
#ifdef HELIUM 
  free(n_He2     );
  free(veloc_He2 );
  free(temp_He2  );
  free(tau_He2   );
#endif
}
/*****************************************************************************/
