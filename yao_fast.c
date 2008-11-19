/*
 * yao_fast.c
 * 
 * This file is part of the yao package, an adaptive optics
 * simulation tool.
 *
 * $Id: yao_fast.c,v 1.5 2008-11-19 00:53:19 frigaut Exp $
 *
 * Copyright (c) 2002-2007, Francois Rigaut
 *
 * This program is free software; you can redistribute it and/or  modify it
 * under the terms of the GNU General Public License  as  published  by the
 * Free Software Foundation; either version 2 of the License,  or  (at your
 * option) any later version.
 *
 * This program is distributed in the hope  that  it  will  be  useful, but
 * WITHOUT  ANY   WARRANTY;   without   even   the   implied   warranty  of
 * MERCHANTABILITY or  FITNESS  FOR  A  PARTICULAR  PURPOSE.   See  the GNU
 * General Public License for more details (to receive a  copy  of  the GNU
 * General Public License, write to the Free Software Foundation, Inc., 675
 * Mass Ave, Cambridge, MA 02139, USA).
 *
 * $Log: yao_fast.c,v $
 * Revision 1.5  2008-11-19 00:53:19  frigaut
 * - fixed memory leak in yao_fast.c (thanks Damien for reporting that)
 * - fixed comments in newfits.i
 * - upped version to 4.2.6
 *
 * Revision 1.4  2008/01/02 13:54:53  frigaut
 * - correct size for the graphic inserts (no black border)
 * - updated spec files
 *
 * Revision 1.3  2007/12/19 14:02:08  frigaut
 * - gotten rid of annoying compilation warning (clean)
 *
 * Revision 1.2  2007/12/13 00:58:31  frigaut
 * added license and header
 *
 *
 *
 */



#include <stdlib.h>
#include <math.h>
#include <complex.h>
#include <fftw3.h>

#define FFTWOPTMODE FFTW_EXHAUSTIVE

void _eclat_float(float *ar, int nx, int ny);
void _poidev(float *xmv, long n);
void _gaussdev(float *xmv, long n);

void _import_wisdom(char *wisdom_file)
{
  FILE *fp;

  fp = fopen(wisdom_file, "r");
  if (fftwf_import_wisdom_from_file(fp)==0)
    printf("Error reading wisdom!\n");
  fclose(fp);
}

int _export_wisdom(char *wisdom_file)
{
  FILE *fp;

  if((fp = fopen(wisdom_file, "w"))==NULL) {
    printf("Error creating wisdom file\"%s\"\n",wisdom_file);
    fflush(stdout);
    return(0);
  }

  fftwf_export_wisdom_to_file(fp);

  fclose(fp);

  return(0);
}

int _init_fftw_plans(int nlimit)
{
  int n;
  int size;
  fftwf_complex *inf;
  fftwf_complex *outf;
  float *rinf;
  int plan_mode;


  size=1;

  plan_mode = FFTWOPTMODE;

  for (n=0;n<nlimit;n++) {
    printf("Optimizing 2D FFT - size = %d\n",size);
    fflush(stdout);
    rinf = fftwf_malloc(size*size*sizeof(float));
    inf  = fftwf_malloc(size*size*sizeof(fftwf_complex));
    outf = fftwf_malloc(size*size*sizeof(fftwf_complex));
    fftwf_plan_dft_2d(size, size, inf, outf, FFTW_FORWARD, plan_mode);
    fftwf_plan_dft_2d(size, size, inf, outf, FFTW_BACKWARD, plan_mode);
    fftwf_plan_dft_r2c_2d(size, size, rinf, outf, plan_mode);

    fftwf_free(rinf);
    fftwf_free(inf);
    fftwf_free(outf);

    size*=2;
  }

  size = 1;
  for (n=0;n<nlimit;n++) {
    printf("Optimizing 1D FFT - size = %d\n",size);
    fflush(stdout);
    rinf = fftwf_malloc(size*sizeof(float));
    inf  = fftwf_malloc(size*sizeof(fftwf_complex));
    outf = fftwf_malloc(size*sizeof(fftwf_complex));
    fftwf_plan_dft_1d(size, inf, outf, FFTW_FORWARD, plan_mode);
    fftwf_plan_dft_1d(size, inf, outf, FFTW_BACKWARD, plan_mode);
    fftwf_plan_dft_r2c_1d(size, rinf, outf, plan_mode);

    fftwf_free(rinf);
    fftwf_free(inf);
    fftwf_free(outf);

    size*=2;
  }

  return(0);
}

/**************************************************************
 * The following function computes the PSF, given a pupil and *
 * a phase, both float. phase can be a 3 dimensional array,   *
 * in which case the routine compute and returns an image for *
 * each plan. This routine uses the FFTW routines.            *
 * Called by calcPSFVE(pupil,phase,scale=) in yorickVE.i      *
 * Author: F.Rigaut                                           *
 * Date: Dec 12 2003.                                         *
 * modified 2004apr3 to adapt FFTW from apple veclib          *
 **************************************************************/

int _calcPSFVE(float *pupil, /* pupil image, dim [ 2^n2 , 2^n2 ] */
	       float *phase, /* phase cube,  dim [ 2^n2 , 2^n2 , nplans ] */
	       float *image, /* output images, dim [ 2^n2 , 2^n2 , nplans ] */
	       int n2,       /* log2 of side linear dimension (e.g. 6 for 64x64 arrays) */
	       int nplans,   /* number of plans (min 1, mostly to spped up calculations */
	                     /* by avoiding multiple setups and mallocs */
	       float scal)   /* phase scaling factor */
{
  /* Declarations */

  fftwf_complex *in, *out;
  float         *ptr;
  long          i,k,koff,n;
  fftwf_plan     p;
      
  /* Set the size of 2d FFT. */
  n = 1 << n2;

  /* Allocate memory for the input operands and check its availability. */
  in  = fftwf_malloc(sizeof(fftwf_complex) * n * n);
  out = fftwf_malloc(sizeof(fftwf_complex) * n * n );

  if ( in == NULL || out == NULL ) { return (-1); }
  
  /* Set up the required memory for the FFT routines */
  p = fftwf_plan_dft_2d(n, n, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
  
  /* Main loop on plan #, in case several phases are input to the routine */
  for ( k=0; k<nplans; k++ ) {

    koff = k*n*n;
//    ptr  = &(in[0]);
    ptr  = (void *)in;

    for ( i=0; i<n*n; i++ ) {
      if ( pupil[i] != 0.f ) {
	*(ptr)   = pupil[i] * cos( phase[koff+i] * scal );
	*(ptr+1) = pupil[i] * sin( phase[koff+i] * scal );
      } else {
	*(ptr)   = 0.0f;
	*(ptr+1) = 0.0f;
      }
      ptr +=2;
    }

    /* Carry out a Forward 2d FFT transform, check for errors. */

    fftwf_execute(p); /* repeat as needed */

//    ptr = &(out[0]);
    ptr  = (void *)out;
    for ( i = 0; i < n*n; i++ ) {
      image[koff+i] = ( *(ptr) * *(ptr) +
			*(ptr+1) * *(ptr+1) );
      ptr +=2;
      }
    /* swap quadrants. */
    _eclat_float(&(image[koff]),n,n);
  }

  /* Free allocated memory. */
  fftwf_destroy_plan(p);
  fftwf_free(in);
  fftwf_free(out);
  return (0);
}




int _fftVE(float *rp,
	   float *ip,
	   int n2,       /* log2 of side linear dimension (e.g. 6 for 64x64 arrays) */
	   int dir)      /* forward (1) or reverse (-1) */
{
  /* Declarations */

  fftwf_complex *in, *out;
  float         *ptr;
  long          i,n;
  fftwf_plan     p;
      
  /* Set the size of 2d FFT. */
  n = 1 << n2;

  /* Allocate memory for the input operands and check its availability. */
  in  = fftwf_malloc(sizeof(fftwf_complex) * n * n);
  out = fftwf_malloc(sizeof(fftwf_complex) * n * n );

  if ( in == NULL || out == NULL ) { return (-1); }
  
  /* Set up the required memory for the FFT routines */
  if (dir == 1) {
    p = fftwf_plan_dft_2d(n, n, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
  } else {
    p = fftwf_plan_dft_2d(n, n, in, out, FFTW_BACKWARD, FFTW_ESTIMATE);
  }

  /* fill input */
  ptr  = (void *)in;

  for ( i=0; i<n*n; i++ ) {
    *(ptr)   = rp[i];
    *(ptr+1) = ip[i];
    ptr +=2;
  }

  /* Carry out a Forward 2d FFT transform, check for errors. */

  fftwf_execute(p); /* repeat as needed */

  ptr = (void *)out;
  for ( i = 0; i < n*n; i++ ) {
    rp[i] = *(ptr);
    ip[i] = *(ptr+1);
    ptr +=2;
  }

  /* Free allocated memory. */
  fftwf_destroy_plan(p);
  fftwf_free(in);
  fftwf_free(out);
  return (0);
}



void _fftVE2(fftwf_complex *in,
	    fftwf_complex *out,
	    int n,       /* log2 of side linear dimension (e.g. 6 for 64x64 arrays) */
	    int dir)      /* forward (1) or reverse (-1) */
     /* do not use this one, it was an experiment, but _fftVE is actually faster */
{
  /* Declarations */
  fftwf_plan     p;
      
  /* Set up the required memory for the FFT routines */
  if (dir == 1) {
    p = fftwf_plan_dft_2d(n, n, in, out, FFTW_FORWARD, FFTW_ESTIMATE);
  } else {
    p = fftwf_plan_dft_2d(n, n, in, out, FFTW_BACKWARD, FFTW_ESTIMATE);
  }
  
  /* Carry out a Forward 2d FFT transform, check for errors. */
  fftwf_execute(p); /* repeat as needed */

  /* Free allocated memory. */
  fftwf_destroy_plan(p);

}




/* Shack- Hartmann coded in C
pass one phase array and a set of indices (start and end of each subapertures)
then this routine puts the phase sections in one larger phase and does a serie
of FFTs to compute the images. Then each image is binned according to the map
"binindices". It then put these images back into a large image
that contains all subaperture image, suitable for visualization.
flow:
- set up of a couple of constants and memory allocation
- loop on subaperture number:
  - put pupil and phase in complex A[ns,ns]
  - do FFT
  - compute image as square(FFT result complex) (no eclat)
  - compute binned/resampled subaperture image using binindices (has 
    to made up for the missing eclat at previous step).
  - compute X and Y centroids
  - optionaly stuff this image in a larger image (fimage) for display
- free memory

The array used in here are:
pupil : pupil image, float, e.g. 64x64
phase : phase image, same dimension as pupil
A     : complex array into which the pupil and phase corresponding to one 
        subaperture are stuffed. e.g. 16x16 if 5x5 pixels/subaperture
simage: image obtained after FFT of A. Same dimension than A, e.g. 16x16
bimage: image obtained after binning of simage to take into account bigger
        pixels than the one delivered after the FFT. e.g. 2x2
fimage: final image (mostly for display) into which all the subaperture images
        have been placed at pre-defined positions.
*/
int _shwfs(float *pupil,      // input pupil
	   float *phase,      // input phase
	   float phasescale,  // phase scaling factor
	   float *phaseoffset,// input phase offset
	   int dimx,          // X dim of phase. Used as stride for extraction

	   int *istart,       // vector of i starts of each subaperture
	   int *jstart,       // vector of j starts of each subaperture
	   int nx,            // subaperture i size
	   int ny,            // subaperture j size
	   int nsubs,         // # of subapertures

	   int sdimpow2,      // dimension of small array for FFTs, in power of 2

           long domask,       // go through amplitude mask loop (0/1).
	   float *submask,    // subaperture mask. Corresponds/applied to simage.
           
	   float *kernel,     // to convolve the (s)image with. dim 2^sdimpow2
	                      // ocmpute dynamically at each call, i.e. can change 
	   float *kernels,    // to convolve the (s)image with, one per subaperture
	                      // dimension: 2^sdimpow2 * 2 * nsubs. FFTs precomputed
                              // at init call.
	   float *kerfftr,    // real part of kernels FFT. dim: same as kernels
	   float *kerffti,    // imaginary part of kernels FFT. same dim as kerfftr
	   int initkernels,   // init kernels: pre-compute FFTs
	   int kernconv,      // convolve with kernel?

	   int *binindices,   // int 2d array containing the indices of the binned 
	                      // subaperture image, i.e. to which pixel in the binned
	                      // image should one pixel in the FFT'd image be added.
	   int binxy,         // side size of binned subaperture image
	   float *centroidw,  // centroid weight vector for centroid computations, X & Y

	   float *fimage,     // final image with spots
	   int *imistart,     // vector of i starts of each image
	   int *imjstart,     // vector of j starts of each image
	   int fimnx,         // final image X dimension
	   int fimny,         // final image Y dimension

	   float *flux,       // vector of flux (input), dim nsubs
	   float *rayleighflux,// vector of flux for rayleigh (input), dim nsubs
	   float *skyflux,    // vector of flux for sky (input), dim nsubs
	   float *threshold,  // vector of threshold (input), dim nsubs
	   float *bias,       // bias array, dim = nsubs*binxy*binxy, in e-
	   float *flat,       // flat array, dim = nsubs*binxy*binxy, normalized @ 1.
	   float ron,         // read-out noise
	   float darkcurrent, // dark current
	   int noise,         // compute noise

	   int rayleighflag,  // enable rayleigh processing
	   float *rayleigh,   // rayleigh background, ns*ns*nsubs
	   // here I separated background and rayleigh. the background includes
	   // not only the rayleigh, but also the sky and the dark current.
	   float *bckgrdcalib,// background calib array, binxy*binxy*nsubs
	   int bckgrdinit,    // init background processing. fill bckgrdcalib
	   int bckgrdsub,     // subtract background

	   float *mesvec,     // final measurement vector

	   int counter,       // current counter (in number of cycles)
	   int niter)         // total # of cycles over which to integrate

{
  /* Declarations */

  fftwf_complex *A, *K, *result;
  float         *ptr,*ptr1,*ptr2;
  float         *simage;
  float         *bimage;
  float         *brayleigh;
  float         *gnoise;
  fftwf_plan    p,p1;
  float         centx, centy, centi, tot, totrayleigh, krp, kip, sky;
  long          log2nr, log2nc, n, ns, nb;
  int           i,j,k,l,koff,integrate;

  /*
    Global setup for FFTs:
  */

  // Set the size of 2d FFT.
  log2nr = sdimpow2; 
  log2nc = sdimpow2;
  n  = 1 << ( log2nr + log2nc ); // total number of pixels in small array
  ns = 1 << log2nr;  // side of small array, pixels, n=ns*ns
  nb = binxy * binxy;

  integrate = 0;
  if (niter > 1) {integrate = 1;}  // we are in "integrating mode"

  // Allocate memory for the input operands and check its availability.
  bimage       = ( float* ) malloc ( nb * sizeof ( float ) );
  brayleigh    = ( float* ) malloc ( nb * sizeof ( float ) );
  gnoise       = ( float* ) malloc ( nb * sizeof ( float ) );
  simage       = ( float* ) malloc ( n * sizeof ( float ) );
  A            = fftwf_malloc ( n * sizeof ( fftwf_complex ) );
  K            = fftwf_malloc ( n * sizeof ( fftwf_complex ) );
  result       = fftwf_malloc ( n * sizeof ( fftwf_complex ) );

  if ( A == NULL || K == NULL || result == NULL || 
       bimage == NULL || simage == NULL || 
       brayleigh == NULL || gnoise == NULL ) { return (1); }

  //Zero out final image if first iteration
  if (counter == 1) {
    for (i=0;i<(fimnx*fimny);i++) { fimage[i] = 0.0f; }
  }

  // zero out mesurement vector
  for ( l=0 ; l<nsubs ; l++ ) {
	mesvec[l]   = 0.0f;
	mesvec[nsubs+l] = 0.0f;
  }

  // Set up the required memory for the FFT routines and 
  // check its availability.
  p = fftwf_plan_dft_2d(ns, ns, A, K, FFTW_FORWARD, FFTW_ESTIMATE);

  if (initkernels == 1) {
    // Transform kernels, store and return for future use
    for ( l=0 ; l<nsubs ; l++ ) {
      ptr = (void *)A;
      for ( i = 0; i < n; i++ ) {
	*(ptr) = kernels[i+l*n];
	*(ptr+1) = 0.0f;
	ptr +=2;
      }
      fftwf_execute(p); /* repeat as needed */

      ptr = (void *)K;
      for ( i = 0; i < n; i++ ) {
	kerfftr[i+l*n] = *(ptr);
	kerffti[i+l*n] = *(ptr+1);
	ptr +=2;
      }
    }
  }

  if (kernconv == 1) {
    // Transform kernel
    ptr = (void *)A;
    for ( i = 0; i < n; i++ ) {
      *(ptr)   = kernel[i];
      *(ptr+1) = 0.0f;
      ptr +=2;
    }
    fftwf_execute(p); /* repeat as needed */
  }
  fftwf_destroy_plan(p); /* mem leak fixed 2008nov18 ! */

  //=====================
  // LOOP ON SUBAPERTURES
  //=====================
  p  = fftwf_plan_dft_2d(ns, ns, A, result, FFTW_FORWARD, FFTW_ESTIMATE);
  p1 = fftwf_plan_dft_2d(ns, ns, A, result, FFTW_BACKWARD, FFTW_ESTIMATE);

  for ( l=0 ; l<nsubs ; l++ ) {

    // reset A and result
    ptr = (void *)A;
    for ( i=0; i<n ; i++ ) {	
      *(ptr)   = 0.0f; *(ptr+1) = 0.0f; ptr +=2;
    }
    ptr = (void *)result;
    for ( i=0; i<n ; i++ ) {	
      *(ptr)   = 0.0f; *(ptr+1) = 0.0f; ptr +=2;
    }

    koff = istart[l] + jstart[l]*dimx;

    ptr = (void *)A;
    for ( j=0; j<ny ; j++ ) {
      for ( i=0; i<nx ; i++ ) {

	k = koff + i + j*dimx;
	*(ptr + 2*(i+j*ns))   = pupil[k] * cos( phasescale*(phase[k] + phaseoffset[k]) );
	*(ptr + 2*(i+j*ns)+1) = pupil[k] * sin( phasescale*(phase[k] + phaseoffset[k]) );
      }
    }

    // Carry out a Forward 2d FFT transform, check for errors.
    fftwf_execute(p); /* repeat as needed */

    ptr = (void *)result;
    for ( i = 0; i < n; i++ ) {
      simage[i] = (*(ptr) * *(ptr) + *(ptr+1) * *(ptr+1) );
      ptr +=2;
    }

    if (kernconv == 1) {
      // Transform simage
      ptr = (void *)A;
      for ( i = 0; i < n; i++ ) {
	*(ptr)   = simage[i]; 
	*(ptr+1) = 0.0f;
	ptr +=2;
      }
      fftwf_execute(p); /* repeat as needed */

      // multiply by kernel transform:
      ptr  = (void *)K;
      ptr1 = (void *)A;
      ptr2 = (void *)result;
      for ( i = 0; i < n; i++ ) {
	// this is FFT(kernel) * FFT(kernels)
	krp = *(ptr)*kerfftr[i+l*n]- *(ptr+1)*kerffti[i+l*n];
	kip = *(ptr)*kerffti[i+l*n]+ *(ptr+1)*kerfftr[i+l*n];
	// and next we multiply by FFT(image):
	*(ptr1)   = *(ptr2)*krp   - *(ptr2+1)*kip;
	*(ptr1+1) = *(ptr2+1)*krp + *(ptr2)*kip;
	ptr +=2; ptr1 +=2; ptr2 +=2;
      }
      // Transform back:
      fftwf_execute(p1); /* repeat as needed */

      ptr = (void *)result;
      for ( i = 0; i < n; i++ ) {
	simage[i] = ( *(ptr) * *(ptr) + *(ptr+1) * *(ptr+1) );
	ptr +=2;
      }
    }

    // amplitude mask implemention
    // for instance to take into account the central dark spot of STRAP.
    if (domask == 1) {
      for ( i = 0; i < n; i++ ) {
        simage[i] = simage[i] * submask[i];
      }
    }
    
    // prerequisite for calibrations: we have to null the useful signal.
    if (bckgrdinit == 1) {
      for ( i = 0; i < n; i++ ) { simage[i] = 0.0f; }
    }

    // put this image into bimage (binned image)
    for ( i = 0; i < nb; i++ ) { bimage[i] = 0.0f; }

    for ( i = 0; i < n; i++ ) {
      if (binindices[i] >= 0) {
	bimage[binindices[i]] += simage[i];
      }
    }

    // Compute rayleigh background
    // I have to do this after the convolution because otherwise there is a lot
    // of wrapping/ringing. I could do it once the image is binned. Saved for future
    // upgrade (will save a bit of time).
    for ( i = 0; i < nb; i++ ) { brayleigh[i] = 0.0f; }
    if (rayleighflag == 1) {
      for ( i = 0; i < n; i++ ) {
	if (binindices[i] >= 0) {
	  brayleigh[binindices[i]] += rayleigh[i+l*n];
	}
      }
      // flux normalization for rayleigh:
      totrayleigh = 0.0f;
      for ( i = 0; i < nb; i++ ) { totrayleigh += brayleigh[i]; }

      if (totrayleigh > 0.0f) {
	totrayleigh = rayleighflux[l]/totrayleigh;
	for ( i = 0; i < nb; i++ ) { brayleigh[i] = brayleigh[i]*totrayleigh; }
      }
    }

    // flux normalization
    // for star:
    tot = 0.0f;
    sky = skyflux[l] / (float)(nb);  // sky per pixel, e-/frame

    for ( i = 0; i < nb; i++ ) { tot += bimage[i]; }

    if (tot > 0.0f) {
      tot = flux[l]/tot;
      for ( i = 0; i < nb; i++ ) { bimage[i] = bimage[i]*tot; }
    }

    for ( i = 0; i < nb; i++ ) { bimage[i] += sky + darkcurrent + brayleigh[i]; }

    // compute noise and apply bias, flat and threshold, compute centroids
    // only at last iteration of integration time on the WFS   
    if (integrate == 0) {

      // Calibration of background (rayleigh + sky+ darkcurrent):
      if (bckgrdinit == 1) {
	for ( i = 0; i < nb; i++ ) { bckgrdcalib[i+l*nb] = bimage[i]; }
      }

      // Apply photon and gaussian (read-out) noise
      if (noise == 1) {
	_poidev(bimage,nb);
	_gaussdev(gnoise,nb);
	for ( i = 0; i < nb; i++ ) { bimage[i] += ron*gnoise[i]; }
      }

      // Subtract estimate of the background
      if (bckgrdsub == 1) {
	for ( i = 0; i < nb; i++ ) { bimage[i] -= bckgrdcalib[i+l*nb]; }
      }

      // Subtract Bias, divide by flat and apply threshold
      for ( i = 0; i < nb; i++ ) { 
	bimage[i] = (bimage[i] - bias[l*nb+i])/flat[l*nb+i];
	bimage[i] = bimage[i] - threshold[l]; 
	if (bimage[i] < 0.0f) bimage[i] = 0.0f;
      }

      // Compute centroids
      centx = centy = centi = 0.0f;
      for ( i=0 ; i<binxy ; i++ ) {
	for ( j=0 ; j<binxy ; j++ ) {
	  centx += centroidw[i]*bimage[i+j*binxy];
	  centy += centroidw[j]*bimage[i+j*binxy];
	  centi += bimage[i+j*binxy];
	}
      }

      // Compute measurements
      if (centi > 0.0f) {
	mesvec[l]   = centx/centi;
	mesvec[nsubs+l] = centy/centi;
      }
    }

    // put image where it belongs in large image
    koff = imistart[l] + imjstart[l]*fimnx;
    
    for ( j=0 ; j<binxy ;j++) {
      for ( i=0 ; i<binxy ;i++) {
	k = koff + i + j*fimnx;
	fimage[k] += bimage[i+j*binxy];
      }
    }
  }

  //============================
  // END OF LOOP ON SUBAPERTURES
  //============================

  // last cycle in integrate mode:
  if ( (integrate == 1) && (counter == niter) ) {
    for ( l=0 ; l<nsubs ; l++ ) {

      // retrieve the final/integrated subaperture image from fimage:
      koff = imistart[l] + imjstart[l]*fimnx;    
      for ( j=0 ; j<binxy ;j++) {
	for ( i=0 ; i<binxy ;i++) {
	  k = koff + i + j*fimnx;
	  bimage[i+j*binxy] = fimage[k];
	}
      }

      // Apply photon and gaussian (read-out) noise
      if (noise == 1) {
	_poidev(bimage,nb);
	_gaussdev(gnoise,nb);
	for ( i = 0; i < nb; i++ ) { bimage[i] += ron*gnoise[i]; }
      }

      // Subtract Bias, divide by flat and apply threshold
      for ( i = 0; i < nb; i++ ) { 
	bimage[i] = (bimage[i] - bias[l*nb+i])/flat[l*nb+i];
	bimage[i] = bimage[i] - threshold[l]; 
	if (bimage[i] < 0.0f) bimage[i] = 0.0f;
      }

      // put back image where it belongs in large image for displays
      // beware: I suppressed the += in the fimage assignment below
      // so there can be no subaperture image overlap here.
      koff = imistart[l] + imjstart[l]*fimnx;
    
      for ( j=0 ; j<binxy ;j++) {
	for ( i=0 ; i<binxy ;i++) {
	  k = koff + i + j*fimnx;
	  fimage[k] = bimage[i+j*binxy];
	}
      }

      // Compute centroids
      centx = centy = centi = 0.0f;
      for ( i=0 ; i<binxy ; i++ ) {
	for ( j=0 ; j<binxy ; j++ ) {
	  centx += centroidw[i]*bimage[i+j*binxy];
	  centy += centroidw[j]*bimage[i+j*binxy];
	  centi += bimage[i+j*binxy];
	}
      }

      // Compute measurements
      if (centi > 0.0f) {
	mesvec[l]   = centx/centi;
	mesvec[nsubs+l] = centy/centi;
      }
    }
  }

  fftwf_destroy_plan(p);
  fftwf_destroy_plan(p1);

  free ( bimage );
  free ( brayleigh ); /* fixed memleak 2008nov18 */
  free ( gnoise );
  free ( simage );
  fftwf_free ( A );
  fftwf_free ( K );
  fftwf_free ( result );

  return (0);
}


int _shwfsSimple(float *pupil,      // input pupil
		 float *phase,      // input phase
		 float phasescale,  // phase scaling factor
		 float *phaseoffset,// input phase offset
		 int dimx,          // X dim of phase. Use as stride for extraction
		 int dimy,          // Y dim of phase. Use as stride for extraction
		 
		 int *istart,       // vector of i starts of each subaperture
		 int *jstart,       // vector of j starts of each subaperture
		 int nx,            // subaperture i size
		 int ny,            // subaperture j size
		 int nsubs,         // # of subapertures
		 
		 float toarcsec,    // conversion factor to arcsec
		 
		 float *mesvec)     // final measurement vector

{
  /* Declarations */
  float         avgx, avgy, avgi;
  long          N;
  int           i,j,k,l,koff;

  // Set sizes
  N  = dimx * dimy;  //total number of pixels in input phase

  // loop on subapertures:
  for ( l=0 ; l<nsubs ; l++ ) {

    koff = istart[l] + jstart[l]*dimx;
    
    avgx = 0.0f;
    avgy = 0.0f;
    avgi = 0.0f;
    
    for ( j=0; j<ny ; j++ ) {
      for ( i=0; i<nx ; i++ ) {
	
	k = koff + i + j*dimx;
	// the term between parenthesis is the 2 point estimate 
	// of the phase X derivative
	avgx += pupil[k] * phasescale * (phase[k+1]-phase[k-1]+phaseoffset[k+1]-phaseoffset[k-1])/2.;
	// same for y:
	avgy += pupil[k] * phasescale * (phase[k+dimx]- phase[k-dimx]+phaseoffset[k+dimx]-phaseoffset[k-dimx])/2.;
	avgi += pupil[k];
	
      }
    }
    
    if (avgi > 0.0f) {
      mesvec[l]   = avgx/avgi*toarcsec;
      mesvec[nsubs+l] = avgy/avgi*toarcsec;
    } else {
      mesvec[l]   = 0.0f;
      mesvec[nsubs+l] = 0.0f;
    }
  }

  return (0);
}


int _cwfs (float *pupil,      // input pupil
	   float *phase,      // input phase
	   float phasescale,  // phase scaling factor
	   float *phaseoffset,// input phase offset
	   float *cxdef,      // cos(defoc)
	   float *sxdef,      // sin(defoc)
	   int dimpow2,       // dim of phase in powers of 2
	   int *sind,         // array containing the indices of subaperture#i
	   int *nsind,        // nsind(i) = number of valid pixels in sind(i)
	   int nsubs,         // number of subapertures
	   float *fimage1,    // final image1 with spots
	   float *fimage2,    // final image2 with spots
	   float nphotons,    // total number of photons, for noise
	   float skynphotons, // total number of photons from sky, for noise
	   float ron,         // read-out noise, in case.
	   float darkcurrent, // dark current, per detector/full sample
                              // note it will be 1/2 of given value per image
                              // ron and darkcurrent are only added if noise = 1
	   int noise,         // enable noise ?
	   float *mesvec)     // final measurement vector

{
  fftwf_complex *A, *B, *result;
  float         *ptr,*ptr1;
  fftwf_plan     p,p1;
  float         *x1,*x2,*gnoise;
  float         tot;
  long          log2nr, log2nc, n, ns;
  int           i,k,sindstride,koff;


  /*
    Global setup for FFTs:
  */

  sindstride = 0;
  for ( i=0 ; i<nsubs ; i++ ) {
    if (nsind[i] > sindstride) { sindstride = nsind[i];}
  }

  // Set the size of 2d FFT.
  log2nr = dimpow2; 
  log2nc = dimpow2;
  n  = 1 << ( log2nr + log2nc ); // total number of pixels in small array
  ns = 1 << log2nr; // total number of pixels in small array

  // Allocate memory for the input operands and check its availability.
  x1     = ( float* ) malloc ( nsubs * sizeof ( float ) );
  x2     = ( float* ) malloc ( nsubs * sizeof ( float ) );
  gnoise = ( float* ) malloc ( nsubs * sizeof ( float ) );
  A      = fftwf_malloc ( n * sizeof ( fftwf_complex ) );
  B      = fftwf_malloc ( n * sizeof ( fftwf_complex ) );
  result = fftwf_malloc ( n * sizeof ( fftwf_complex ) );

  if ( A == NULL || B == NULL || result == NULL ) { return (1); }

  for (i=0;i<nsubs;i++) {
    x1[i] = 0.0f;
    x2[i] = 0.0f;
  }
      
  // Set up the required memory for the FFT routines and 
  // check its availability.
  p  = fftwf_plan_dft_2d(ns, ns, A, B, FFTW_FORWARD, FFTW_ESTIMATE);
  p1 = fftwf_plan_dft_2d(ns, ns, A, result, FFTW_BACKWARD, FFTW_ESTIMATE);


  // intermediate FFT, to find intermediate complex amplitude
  // fill A
  ptr = (void *)A;
  for ( i=0; i<n ; i++ ) {
    if (pupil[i] != 0.0f) {
      *(ptr)   = pupil[i]*cos(phasescale*(phase[i]+phaseoffset[i]));
      *(ptr+1) = pupil[i]*sin(phasescale*(phase[i]+phaseoffset[i]));
    } else {
      *(ptr)   = 0.0f;
      *(ptr+1) = 0.0f;
    }
    ptr += 2;
  }


  // Carry out a Forward 2d FFT transform, check for errors.
  fftwf_execute(p); /* repeat as needed */

  // image #1:
  ptr = (void *)A;
  ptr1 = (void *)B;
  for ( i=0; i<n ; i++ ) {
    *(ptr)   = *(ptr1)*cxdef[i] - *(ptr1+1)*sxdef[i];
    *(ptr+1) = *(ptr1)*sxdef[i] + *(ptr1+1)*cxdef[i];
    ptr += 2; ptr1 += 2;
  }

  fftwf_execute(p1); /* repeat as needed */

  ptr = (void *)result;
  for ( i=0; i<n; i++ ) {
    fimage1[i] = (*(ptr) * *(ptr) + *(ptr+1) * *(ptr+1) );
    ptr += 2;
  }

  // image #2:
  ptr = (void *)A;
  ptr1 = (void *)B;
  for ( i=0; i<n ; i++ ) {
    *(ptr)   = *(ptr1)*cxdef[i]   + *(ptr1+1)*sxdef[i];
    *(ptr+1) = *(ptr1+1)*cxdef[i] - *(ptr1)*sxdef[i];
    ptr += 2; ptr1 += 2;
  }

  fftwf_execute(p1); /* repeat as needed */

  ptr = (void *)result;
  for ( i=0; i<n; i++ ) {
    fimage2[i] = (*(ptr) * *(ptr) + *(ptr+1) * *(ptr+1) );
    ptr += 2;
  }

  // now we got to sum the relevant pixels:
  for ( k=0 ; k<nsubs ; k++ ) {
    koff = k*sindstride;
    //printf("nsind[%d]=%d\n",k,nsind[k]);
    for ( i=0 ; i<nsind[k] ; i++ ) {
      x1[k] += fimage1[sind[koff+i]];
      x2[k] += fimage2[sind[koff+i]];
    }
  }

  // NOISE:
  if (noise == 1) {
    // x1:
    tot = 0.0f;
    // compute total of current flux vector
    for ( i=0 ; i<nsubs ; i++ ) { tot += x1[i]; }
    if (tot > 0.0f) { 
      tot = nphotons/2.0f/tot;
      // normalize so that sum(x1) = nphotons
      for ( i=0 ; i<nsubs ; i++ ) { x1[i] = x1[i]*tot ; }
      // add darkcurrent:
      for ( i=0 ; i<nsubs ; i++ ) { x1[i] += darkcurrent/2.0f ; }
      // add sky:
      for ( i=0 ; i<nsubs ; i++ ) { x1[i] += skynphotons/2.0f ; }
      // apply poisson noise
      _poidev(x1,nsubs);
    }
    if (ron > 0.0f) {
      // set up gaussian noise vector
      for ( i=0 ; i<nsubs ; i++ ) { gnoise[i] = ron; }
      // draw gaussian noise
      _gaussdev(gnoise,nsubs);
      // add to x1
      for ( i=0 ; i<nsubs ; i++ ) { x1[i] += gnoise[i]; }
    }

    // x2, same comment as above for x1:
    tot = 0.0f;
    for ( i=0 ; i<nsubs ; i++ ) { tot += x2[i]; }
    if (tot > 0.0f) { 
      tot = nphotons/2.0f/tot;
      for ( i=0 ; i<nsubs ; i++ ) { x2[i] = x2[i]*tot ; }
      for ( i=0 ; i<nsubs ; i++ ) { x2[i] += darkcurrent/2.0f ; }
      for ( i=0 ; i<nsubs ; i++ ) { x2[i] += skynphotons/2.0f ; }
      _poidev(x2,nsubs);
    }
    if (ron > 0.0f) {
      for ( i=0 ; i<nsubs ; i++ ) { gnoise[i] = ron; }
      _gaussdev(gnoise,nsubs);
      for ( i=0 ; i<nsubs ; i++ ) { x2[i] += gnoise[i]; }
    }    
  }

  for ( i=0 ; i<nsubs ; i++ ) {
    if ( (x1[i]+x2[i]) == 0.0f ) {
      mesvec[i] = 0.0f;
    } else {
      mesvec[i] = (x1[i]-x2[i])/(x1[i]+x2[i]);
    }
  }

  fftwf_destroy_plan(p);
  fftwf_destroy_plan(p1);
  free ( x1 );
  free ( x2 );
  free ( gnoise );
  fftwf_free ( A );
  fftwf_free ( B );
  fftwf_free ( result );

  return (0);
}

