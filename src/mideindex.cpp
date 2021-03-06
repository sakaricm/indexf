/*
 * Copyright 2008-2013 Nicolas Cardiel
 *
 * This file is part of indexf.
 *
 * Indexf is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Indexf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with indexf.  If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <iostream>
#include <iomanip>
#include <cmath>
#include <cstdlib>
#include <vector>
#include "indexdef.h"

#ifdef HAVE_CPGPLOT_H
#include "cpgplot.h"
#include "cpgplot_d.h"
#endif /* HAVE_CPGPLOT_H */
#include "genericpixel.h"

using namespace std;

bool fpercent(vector <GenericPixel> &, const long, const bool, 
              double *, double *);
bool boundaryfit(const long , vector <GenericPixel> &, const bool, 
                 vector <GenericPixel> &, vector <GenericPixel> &);

bool mideindex(const bool &lerr, const double *sp_data, const double *sp_error, 
               const long &naxis1,
               const double &crval1, 
               const double &cdelt1, 
               const double &crpix1,
               const IndexDef &myindex,
               const long &contperc,
               const long &boundfit,
               const bool &flattened,
               const bool &logindex,
               const double &rvel,
               const double &biaserr, const double &linearerr,
               const long &plotmode, const long &plottype,
               const double &xmin_user, const double &xmax_user,
               const double &ymin_user, const double &ymax_user,
               const bool &pyindexf,
               bool &out_of_limits, bool &negative_error, bool &log_negative,
               double &findex, double &eindex, double &sn)
{
  //---------------------------------------------------------------------------
  //protecciones
  if ( (contperc >= 0) && (boundfit != 0) )
  {
    cout << "ERROR: contperc and boundfit cannot be used simultaneously"
         << endl;
    exit(1);
  }
  if ( (contperc >= 0) && (flattened) )
  {
    cout << "ERROR: contperc and flattened cannot be used simultaneously"
         << endl;
    exit(1);
  }
  if ( (boundfit != 0) && (flattened) )
  {
    cout << "ERROR: boundfit and flattened cannot be used simultaneously"
         << endl;
    exit(1);
  }
  //---------------------------------------------------------------------------
  //mientras no se demuestre lo contario, no hay problemas al medir
  out_of_limits=false;
  negative_error=false;
  log_negative=false;
  findex=0;
  eindex=0;
  sn=0;
  //---------------------------------------------------------------------------
  //proteccion para velocidades radiales superiores a c
  if (rvel > 2.9979240E+5)
  {
    out_of_limits=true;
    return(false);
  }
  //---------------------------------------------------------------------------
  //constantes numericas
  const double c = 2.9979246E+5; //velocidad de la luz (km/s)
  const double cte_log_exp = 2.5 * log10 (exp(1.0));
  //(1+z) corregido de efecto relativista
  const double rcvel = rvel/c; // v/c
  const double rcvel1 = (1.0+rcvel)/sqrt(1.0-rcvel*rcvel);
  //longitud de onda inferior del primer pixel
  const double wlmin = crval1-cdelt1/2.0-(crpix1-1.0)*cdelt1;
  //Nota: recordar que la longitud de onda en un pixel arbitrario j-esimo 
  //se calcula como:
  //lambda=crval1+(j-crpix1)*cdelt1
  //con j definido en el intervalo [1,NAXIS1]
  //---------------------------------------------------------------------------
  //calculamos parametros de cada banda a medir
  const long nbands = myindex.getnbands();
  double *ca = new double [nbands];
  double *cb = new double [nbands];
  double *c3 = new double [nbands];
  double *c4 = new double [nbands];
  long *j1 = new long [nbands];
  long *j2 = new long [nbands];
  double *d1 = new double [nbands];
  double *d2 = new double [nbands];
  double *rl = new double [nbands];
  double *rg = new double [nbands];
  for (long nb=0; nb < nbands; nb++)
  {
    ca[nb] = myindex.getldo1(nb)*rcvel1;             //redshifted wavelength
    cb[nb] = myindex.getldo2(nb)*rcvel1;             //redshifted wavelength
    if(pyindexf)
    {
      cout << "python> {'w_ini" << setw(3) << setfill('0') << nb+1 << "': " 
           <<           ca[nb] << ", "
           <<          "'w_end" << setw(3) << setfill('0') << nb+1 << "': " 
           <<           cb[nb] << "}"
           << endl;
      if (myindex.gettype() == 10)
      {
        cout << "python> {'factor" << setw(3) << setfill('0') << nb+1 << "': "
             <<           myindex.getfactor_el(nb) << "}"
             << endl;
      }
      else if ((myindex.gettype() >= 101) & (myindex.gettype() <= 9999))
      {
        long nconti = myindex.getnconti();
        if (nb >= nconti)
        {
          cout << "python> {'factor" << setw(3) << setfill('0') << nb+1 << "': "
               <<           myindex.getfactor(nb-nconti) << "}"
               << endl;
        }
      }
    }
    c3[nb] = (ca[nb]-wlmin)/cdelt1+1.0;              //band limit (channel)
    c4[nb] = (cb[nb]-wlmin)/cdelt1;                  //band limit (channel)
    if ( (c3[nb] < 1.0) || (c4[nb] > static_cast<double>(naxis1-1)) )
    {
      out_of_limits=true;          //indice fuera de limites: no se puede medir
    }
    if (!out_of_limits)
    {
      j1[nb] = static_cast<long>(c3[nb]);       //band limit: integer (channel)
      j2[nb] = static_cast<long>(c4[nb]);       //band limit: integer (channel)
      d1[nb] = c3[nb]-static_cast<double>(j1[nb]);//fraction (excess left chan)
      d2[nb] = c4[nb]-static_cast<double>(j2[nb]);//fraction (defect right chan)
      rl[nb] = cb[nb]-ca[nb];               //redshifted band width (angstroms)
      rg[nb] = c4[nb]-c3[nb]+1;             //redshifted band width (channels)
    }
  }
  //comprobamos que al usar errores no hay errores negativos ni nulos
  if(lerr)
  {
    if (!out_of_limits)
    {
      for (long nb=0; nb < nbands; nb++)
      {
        for (long j=j1[nb]; j <= j2[nb]+1; j++)
        {
          if (sp_error[j-1] <= 0)
          {
            negative_error=true;
          }
        }
      }
    }
  }
  //calculamos S/N_A promedio en cada banda por separado (solo cuando
  //ejecutamos el programa a traves de pyindexf y no se ha producido el error
  //debido a que el indice cae fuera de limites o a que existan errores
  //negativos o nulos)
  if(pyindexf)
  {
    for (long nb=0; nb < nbands; nb++)
    {
      double meansn = 0;
      if(lerr)
      {
        if (!out_of_limits)
        {
          if (!negative_error)
          {
            for (long j=j1[nb]; j <= j2[nb]+1; j++)
            {
              meansn+=sp_data[j-1]/sp_error[j-1];
            }
            meansn/=static_cast<double>(j2[nb]-j1[nb]+2);
            meansn/=sqrt(cdelt1); //calculamos senal/ruido por angstrom
          }
        }
      }
      cout << "python> {'meansn" << setw(3) << setfill('0') << nb+1 << "': "
           <<           meansn << "}" << endl;
    }
  }
  // si el indice cae fuera de limites, no se puede medir
  if (out_of_limits)
  {
    return(false);
  }
  // si hemos encontrado errores negativos o nulos, no medimos el índice
  if (negative_error)
  {
    return(false);
  }
  //calculamos limites en j1 y j2, por si las bandas no estan en orden
  long j1min = j1[0];
  long j2max = j2[0];
  for (long nb=0; nb < nbands; nb++ )
  {
    if (j1min > j1[nb]) j1min=j1[nb];
    if (j2max < j2[nb]) j2max=j2[nb];
  }
  //calculamos limites en longitud de onda (rest frame)
  double wvmin= myindex.getldo1(0);
  double wvmax= myindex.getldo2(0);
  double wv1temp,wv2temp;
  for (long nb=0; nb < nbands; nb++ )
  {
    wv1temp=myindex.getldo1(nb);
    wv2temp=myindex.getldo2(nb);
    if(wvmin > wv1temp) wvmin=wv1temp;
    if(wvmax < wv2temp) wvmax=wv2temp;
  }

#ifdef HAVE_CPGPLOT_H
  //===========================================================================
  //dibujamos
  bool xlim_auto = ((xmin_user == 0) && (xmax_user == 0));
  bool ylim_auto = ((ymin_user == 0) && (ymax_user == 0));
  if(plotmode != 0)
  {
    //calculamos un array temporal para el eje X (en unidades de pixeles)
    double *x = new double [naxis1];
    for (long j=1; j<=naxis1; j++)
    {
      x[j-1]=static_cast<double>(j);
    }
    //calculamos limites en el eje X (en pixels)
    double xmin,xmax,dx;
    if (xlim_auto)
    {
      xmin=(ca[0]-crval1)/cdelt1+crpix1;
      xmax=(cb[0]-crval1)/cdelt1+crpix1;
      double chan1, chan2;
      for (long nb=0; nb < nbands; nb++)
      {
        chan1=(ca[nb]-crval1)/cdelt1+crpix1;
        chan2=(cb[nb]-crval1)/cdelt1+crpix1;
        if(chan1 < xmin) xmin=chan1;
        if(chan2 > xmax) xmax=chan2;
      }
      dx=xmax-xmin;
      xmin=xmin-dx/3.0;
      xmax=xmax+dx/3.0;
    }
    else
    {
      xmin=crpix1+(xmin_user-crval1)/cdelt1;
      xmax=crpix1+(xmax_user-crval1)/cdelt1;
    }
    //calculamos limites en el eje Y (flujo)
    double ymin,ymax,dy;
    if (ylim_auto)
    {
      bool firstpixel=true;
      for (long j=1; j<=naxis1; j++)
      {
        if((j >= xmin) && (j <= xmax))
        {
          if(firstpixel)
          {
            if((lerr) && (plotmode < 0))
            {
              firstpixel=false;
              ymin=sp_data[j-1]-sp_error[j-1];
              ymax=sp_data[j-1]+sp_error[j-1];
            }
            else
            {
              firstpixel=false;
              ymin=ymax=sp_data[j-1];
            }
          }
          else
          {
            if((lerr) && (plotmode < 0))
            {
              if(ymin > sp_data[j-1]-sp_error[j-1]) 
               ymin=sp_data[j-1]-sp_error[j-1];
              if(ymax < sp_data[j-1]+sp_error[j-1]) 
               ymax=sp_data[j-1]+sp_error[j-1];
            }
            else
            {
              if(ymin > sp_data[j-1]) ymin=sp_data[j-1];
              if(ymax < sp_data[j-1]) ymax=sp_data[j-1];
            }
          }
        }
      }
      if(fabs(biaserr) !=0)
      {
        if(ymin > ymin*(1+1.5*biaserr/100) ) ymin*=(1+1.5*biaserr/100);
        if(ymax < ymax*(1+biaserr/100) ) ymax*=(1+biaserr/100);
      }
      else if(fabs(linearerr) !=0)
      {
        if(ymin > ymin*pow(ymin,linearerr) ) ymin*=pow(ymin,linearerr);
        if(ymax < ymax*pow(ymax,linearerr) ) ymax*=pow(ymax,linearerr);
      }
      dy=ymax-ymin;
      ymin=ymin-dy/10.0;
      ymax=ymax+dy/10.0;
    }
    else
    {
      ymin=ymin_user;
      ymax=ymax_user;
    }
    //dibujamos caja del plot con diferentes escalas en X
    double xminl,xmaxl;
    xminl=crval1+(xmin-crpix1)*cdelt1;
    xmaxl=crval1+(xmax-crpix1)*cdelt1;
    cpgenv_d(xminl,xmaxl,ymin,ymax,0,-2);
    if(plottype)
    {
      float xv1,xv2,yv1,yv2,dyv;
      cpgqvp(0,&xv1,&xv2,&yv1,&yv2);
      dyv=yv2-yv1;
      cpgsvp(xv1,xv2,yv1,yv2-dyv*0.20);
      cpgswin(xminl,xmaxl,ymin,ymax);
      cpgbox("btsn",0.0,0,"bctsn",0.0,0);
      cpgswin(xminl/rcvel1,xmaxl/rcvel1,ymin,ymax);
      cpgbox("ctsm",0.0,0," ",0.0,0);
      cpgmtxt("t",2.0,0.5,0.5,"rest-frame wavelength (\\A)");
      cpgsvp(xv1,xv2,yv1,yv2);
      cpgswin_d(xmin,xmax,ymin,ymax);
      cpgsci(15);
      cpgbox("ctsm",0.0,0," ",0.0,0);
      float ch;
      cpgqch(&ch);
      cpgsch(0.9);
      cpgmtxt("t",-1.5,0.0,0.0,"pixel (wavelength direction)");
      cpgmtxt("t",-1.5,1.0,1.0,"pixel (wavelength direction)");
      cpgsch(ch);
      cpgsci(1);
      cpgsvp(xv1,xv2,yv1,yv2-dyv*0.20);
    }
    else
    {
      cpgbox("bctsn",0.0,0,"bctsn",0.0,0);
    }
    cpglab("observed wavelength (\\A)","flux"," ");
    cpgswin_d(xmin,xmax,ymin,ymax);
    //dibujamos errores
    if((lerr) && (plotmode < 0))
    {
      cpgbbuf();
      cpgsci(15);
      cpgslw(1);
      for (long j=1; j<=naxis1; j++)
      {
        cpgmove_d(x[j-1],sp_data[j-1]-sp_error[j-1]);
        cpgdraw_d(x[j-1],sp_data[j-1]+sp_error[j-1]);
        cpgmove_d(x[j-1]-0.2,sp_data[j-1]-sp_error[j-1]);
        cpgdraw_d(x[j-1]+0.2,sp_data[j-1]-sp_error[j-1]);
        cpgmove_d(x[j-1]-0.2,sp_data[j-1]+sp_error[j-1]);
        cpgdraw_d(x[j-1]+0.2,sp_data[j-1]+sp_error[j-1]);
      }
      cpgslw(2);
      cpgsci(1);
      cpgebuf();
    }
    //dibujamos el espectro
    cpgbin_d(naxis1,x,sp_data,true);
    //dibujamos bandas
    dy=ymax-ymin;
    for (long nb=0; nb<nbands; nb++)
    {
      double ddy=0.00;
      if((myindex.gettype() == 1) || //........................indice molecular
         (myindex.gettype() == 2))   //..........................indice atomico
      {
        ddy=0.04;
        if(nb==0)
          cpgsci(4);
        else if(nb==1)
          cpgsci(3);
        else
          cpgsci(2);
      }
      else if((myindex.gettype() == 3) || //..............................D4000
              (myindex.gettype() == 4) || //..............................B4000
              (myindex.gettype() == 5))   //..............................CO_KH
      {
        ddy=0.04;
        if(nb==0)
          cpgsci(4);
        else
          cpgsci(2);
      }
      else if(myindex.gettype() == 10)//..........................emission line
      {
        if(myindex.getfactor_el(nb) == 0.0) //continuo
        {
          ddy=0.04;
          cpgsci(5);
        }
        else //linea
        {
          ddy=0.06;
          cpgsci(3);
        }
      }
      else if((myindex.gettype() >= 11) && //...........discontinuidad generica
              (myindex.gettype()<=99))
      {
        if(nb < myindex.getnconti())
        {
          ddy=0.04;
          cpgsci(5);
        }
        else
        {
          ddy=0.06;
          cpgsci(3);
        }
      }
      else if((myindex.gettype() >= 101) && //..................indice generico
              (myindex.gettype()<=9999))
      {
        if(nb < myindex.getnconti())
        {
          ddy=0.04;
          cpgsci(5);
        }
        else
        {
          ddy=0.06;
          cpgsci(3);
        }
      }
      else if((myindex.gettype() >= -99) && //...........................slopes
              (myindex.gettype()<=-2))
      {
        cpgsci(5);
        ddy=0.04;
      }
      else //.......................................................sin definir
      {
        cout << "Invalid index type=" << myindex.gettype() << endl;
        exit(1);
      }
      const double xc1=(ca[nb]-crval1)/cdelt1+crpix1;
      const double xc2=(cb[nb]-crval1)/cdelt1+crpix1;
      cpgrect_d(xc1,xc2,ymin+dy*0.04,ymin+dy*0.05);
      cpgrect_d(xc1,xc2,ymax-dy*(ddy+0.01),ymax-dy*ddy);
      cpgmove_d(xc1,ymin+dy*0.04); cpgdraw_d(xc1,ymax-dy*ddy);
      cpgmove_d(xc2,ymin+dy*0.04); cpgdraw_d(xc2,ymax-dy*ddy);
    }
    cpgsci(1);
    //borramos array temporal para el eje X
    delete [] x;
  }//==========================================================================
#endif /* HAVE_CPGPLOT_H */

  //---------------------------------------------------------------------------
  //fijamos los canales a usar para medir el indice (usando la variable
  //logica evitamos el problema de la posible superposicion de las bandas)
  bool *ifchan = new bool [naxis1];
  for (long j=1; j <= naxis1; j++)
  {
    ifchan[j-1] = false;
  }
  for (long nb=0; nb < nbands; nb++)
  {
    for (long j=j1[nb]; j <= j2[nb]+1; j++)
    {
      ifchan[j-1]=true;
    }
  }
  long nceff=0;
  for (long j=1; j <= naxis1; j++)
  {
    if (ifchan[j-1]) nceff++;
  }

  //---------------------------------------------------------------------------
  //normalizamos datos usando la senal solo en la region del indice a medir
  //(si flattened=true, no lo hacemos)
  double *s = new double [naxis1];
  double *es = new double [naxis1];
  double smean;
  if (flattened)
  {
    smean=1.0;
  }
  else
  {
    smean=0.0;
    for (long j=1; j <= naxis1; j++)
    {
      if (ifchan[j-1]) smean+=sp_data[j-1];
    }
    smean/=static_cast<double>(nceff);
    smean = ( smean != 0 ? smean : 1.0); //evitamos division por cero
  }
  for (long j=1; j <= naxis1; j++)
  {
    s[j-1]=sp_data[j-1]/smean;
  }
  if(lerr)
  {
    for (long j=1; j <= naxis1; j++)
    {
      es[j-1]=sp_error[j-1]/smean;
    }
  }

  //---------------------------------------------------------------------------
  //senal/ruido promedio en las bandas del indice (ya hemos vigilado antes de
  //que no haya valores de error <= 0)
  sn=0.0;
  if(lerr)
  {
    for (long j=1; j <= naxis1; j++)
    {
      if (ifchan[j-1])
      {
        sn+=s[j-1]/es[j-1];
      }
    }
    sn/=static_cast<double>(nceff);
    sn/=sqrt(cdelt1); //calculamos senal/ruido por angstrom
  }

  //---------------------------------------------------------------------------
  //Si se ha solicitado, introducimos un error sistematico modificando el 
  //espectro por un factor aditivo. Para ello calculamos el valor promedio del
  //pseudocontinuo y anadimos una fraccion de dicho flujo a todo el espectro
  if ( fabs(biaserr) != 0.0 )
  {
    //protecciones
    if ( contperc >= 0 )
    {
      cout << "ERROR: biaserr and contperc cannot be used simultaneously"
           << endl;
      exit(1);
    }
    if ( boundfit != 0 )
    {
      cout << "ERROR: biaserr and boundfit cannot be used simultaneously"
           << endl;
      exit(1);
    }
    if ( flattened )
    {
      cout << "ERROR: biaserr and flattened cannot be used simultaneously"
           << endl;
      exit(1);
    }
    //indice atomico o molecular
    if ( (myindex.gettype() == 1) || (myindex.gettype() == 2) )
    {
      //cuentas promedio en el continuo azul
      double sb=0.0;
      for (long j=j1[0]; j<=j2[0]+1; j++)
      {
        double f;
        if (j == j1[0])
          f=1.0-d1[0];
        else if (j == j2[0]+1)
          f=d2[0];
        else
          f=1.0;
        sb+=f*s[j-1];
      }
      sb*=cdelt1;
      sb/=rl[0];
      //cuentas promedio en la banda roja
      double sr=0.0;
      for (long j=j1[2]; j<=j2[2]+1; j++)
      {
        double f;
        if (j == j1[2])
          f=1.0-d1[2];
        else if (j == j2[2]+1)
          f=d2[2];
        else
          f=1.0;
        sr+=f*s[j-1];
      }
      sr*=cdelt1;
      sr/=rl[2];
      //calculamos pseudo-continuo en el centro de la banda central
      double mwb = (myindex.getldo1(0)+myindex.getldo2(0))/2.0;
      mwb*=rcvel1;
      double mwr = (myindex.getldo1(2)+myindex.getldo2(2))/2.0;
      mwr*=rcvel1;
      long j = (j1[1]+(j2[1]+1))/2; //pixel central de la banda central
      double wla=static_cast<double>(j-1)*cdelt1+crval1-(crpix1-1.0)*cdelt1;
      double sc = (sb*(mwr-wla)+sr*(wla-mwb))/(mwr-mwb);
      //anadimos el efecto sistematico al espectro de datos (el espectro de
      //errores no se modifica)
      for (long j=1; j <= naxis1; j++)
      {
        s[j-1]+=sc*biaserr/100.0;
      }
      if (pyindexf)
      {
        cout << "python> {'biaserrfactor': " << sc*biaserr/100.0
             << "}" << endl;
      }
    }
    //D4000 o B4000
    else if ( (myindex.gettype() == 3) || 
              (myindex.gettype() == 4) ||
              (myindex.gettype() == 5) )
    {
      //cuentas promedio en la banda azul
      double sb=0.0;
      for (long j=j1[0]; j<=j2[0]+1; j++)
      {
        double f;
        if (j == j1[0])
          f=1.0-d1[0];
        else if (j == j2[0]+1)
          f=d2[0];
        else
          f=1.0;
        sb+=f*s[j-1];
      }
      sb*=cdelt1;
      sb/=rl[0];
      //cuentas promedio en la banda roja
      double sr=0.0;
      for (long j=j1[1]; j<=j2[1]+1; j++)
      {
        double f;
        if (j == j1[1])
          f=1.0-d1[1];
        else if (j == j2[1]+1)
          f=d2[1];
        else
          f=1.0;
        sr+=f*s[j-1];
      }
      sr*=cdelt1;
      sr/=rl[1];
      //valor promedio
      double sc = (sb+sr)/2.0;
      //anadimos el efecto sistematico al espectro de datos (el espectro de
      //errores no se modifica)
      for (long j=1; j <= naxis1; j++)
      {
        s[j-1]+=sc*biaserr/100.0;
      }
      if (pyindexf)
      {
        cout << "python> {'biaserrfactor': " << sc*biaserr/100.0
             << "}" << endl;
      }
    }
    //indices para los cuales no se ha incluido el efecto de biaserr
    else
    {
      cout << "FATAL ERROR: biaserr=" << biaserr
           << " cannot be handled for this type of index." << endl;
      exit(1);
    }
  }
  else
  {
    if (pyindexf)
    {
      cout << "python> {'biaserrfactor': 0.0}" << endl;
    }
  }

  //---------------------------------------------------------------------------
  //Si se ha solicitado, introducimos un error de linealidad de la forma:
  //flux_real=flux_observed^(1+linearerr). Modificamos asimismo el espectro 
  //de errores. Nota: para evitar "NaN", forzamos utilizar una senal positiva.
  if ( fabs(linearerr) != 0.0 )
  {
    //proteccion
    if ( contperc >= 0 )
    {
      cout << "ERROR: linearerr and contperc cannot be used simultaneously"
           << endl;
      exit(1);
    }
    if ( boundfit != 0 )
    {
      cout << "ERROR: linearerr and boundfit cannot be used simultaneously"
           << endl;
      exit(1);
    }
    if ( flattened )
    {
      cout << "ERROR: linearerr and flattened cannot be used simultaneously"
           << endl;
      exit(1);
    }
    double scale_factor;
    for (long j=1; j <= naxis1; j++)
    {
      if (s[j-1] >= 0.0 )
      {
        scale_factor=pow(s[j-1],linearerr)*pow(smean,linearerr);
      }
      else
      {
        scale_factor=pow(-s[j-1],linearerr)*pow(smean,linearerr);
      }
      s[j-1]*=scale_factor;
      es[j-1]*=scale_factor;
    }
  }

  //***************************************************************************
  //Indices atomicos y moleculares
  //***************************************************************************
  if ( (myindex.gettype() == 1) || (myindex.gettype() == 2) )
  {
    //-------------------------------------------------------------------------
    //protecciones
    if ( contperc >= 0 )
    {
      cout << "#WARNING: contperc is being implemented for this index"
           << endl;
    }
    if ( boundfit != 0 )
    {
      cout << "#WARNING: boundfit is being implemented for this index"
           << endl;
      cout << "#smean: " << smean << endl;
    }
    //calculamos longitudes de onda en el centro de las bandas de continuo
    double mwb = (myindex.getldo1(0)+myindex.getldo2(0))/2.0;
    mwb*=rcvel1;
    double mwr = (myindex.getldo1(2)+myindex.getldo2(2))/2.0;
    mwr*=rcvel1;
    if(pyindexf)
    {
      cout << "python> {'w_blue_center': " << mwb << ", "
                       "'w_red_center': "  << mwr << "}" << endl;
    }
    //declaramos las variables en las que incluiremos el pseudo-continuo
    //evaluado en la banda central
    double *sc = new double [naxis1];
    double *esc2 = new double [naxis1];
    //-------------------------------------------------------------------------
    double sb=0.0;                 //flujo "promedio" para centro de banda azul
    double esb2=0.0;               //error en el flujo anterior
    double sr=0.0;                 //flujo "promedio" para centro de banda roja
    double esr2=0.0;               //error en el flujo anterior
    //-------------------------------------------------------------------------
    if(fabs(boundfit) == 1) //..boundfit independiente a cada banda de continuo
    {
      //...................................................incluimos banda azul
      vector <GenericPixel> fluxpix_blue;  //datos a ajustar
      vector <GenericPixel> boundfit_blue; //ajuste a los datos
      for (long j=j1[0]; j<=j2[0]+1; j++)
      {
        double f;
        if (j == j1[0])
          f=1.0-d1[0];
        else if (j == j2[0]+1)
          f=d2[0];
        else
          f=1.0;
        double wave=static_cast<double>(j-1)*cdelt1+
                    crval1-(crpix1-1.0)*cdelt1;
        GenericPixel temppix(wave,s[j-1],es[j-1],f);
        fluxpix_blue.push_back(temppix);
        temppix.setflux(0.0);
        temppix.seteflux(0.0);
        boundfit_blue.push_back(temppix);
      }
      GenericPixel evalpix; //pixel puntual a ser evaluado
      vector <GenericPixel> evaluate_blue; //vector de pixeles a evaluar
      evalpix.setwave(mwb);
      evaluate_blue.push_back(evalpix);
      if(!boundaryfit(boundfit,fluxpix_blue,lerr,boundfit_blue,evaluate_blue))
      {
        cout << "ERROR: while computing boundary fit in blue band" << endl;
        exit(1);
      }
      sb=evaluate_blue[0].getflux();
      esb2=evaluate_blue[0].geteflux();
      esb2*=esb2;
#ifdef HAVE_CPGPLOT_H
      //=======================================================================
      //dibujamos boundary fit de la banda azul
      if((plotmode != 0) && (plottype == 2))
      {
        long npixels = boundfit_blue.size();
        double *x_  = new double [npixels];
        double *fit_ = new double [npixels];
        for(long j=0; j<npixels; j++)
        {
          x_[j] = (boundfit_blue[j].getwave()-crval1)/cdelt1+crpix1;
          fit_[j]=boundfit_blue[j].getflux()*smean;
        }
        cpgsci(4);
        cpgbin_d(npixels,x_,fit_,true);
        cpgsci(1);
        delete [] x_;
        delete [] fit_;
      }//======================================================================
#endif /* HAVE_CPGPLOT_H */     
      //...................................................incluimos banda roja
      vector <GenericPixel> fluxpix_red;
      vector <GenericPixel> boundfit_red;
      for (long j=j1[2]; j<=j2[2]+1; j++)
      {
        double f;
        if (j == j1[2])
          f=1.0-d1[2];
        else if (j == j2[2]+1)
          f=d2[2];
        else
          f=1.0;
        double wave=static_cast<double>(j-1)*cdelt1+
                    crval1-(crpix1-1.0)*cdelt1;
        GenericPixel temppix(wave,s[j-1],es[j-1],f);
        fluxpix_red.push_back(temppix);
        temppix.setflux(0.0);
        temppix.seteflux(0.0);
        boundfit_red.push_back(temppix);
      }
      vector <GenericPixel> evaluate_red; //vector de pixeles a evaluar
      evalpix.setwave(mwr);
      evaluate_red.push_back(evalpix);
      if(!boundaryfit(boundfit,fluxpix_red,lerr,boundfit_red,evaluate_red))
      {
        cout << "ERROR: while computing boundary fit in red band" << endl;
        exit(1);
      }
      sr=evaluate_red[0].getflux();
      esr2=evaluate_red[0].geteflux();
      esr2*=esr2;
#ifdef HAVE_CPGPLOT_H
      //=======================================================================
      //dibujamos boundary fit de la banda roja
      if((plotmode != 0) && (plottype == 2))
      {
        long npixels = boundfit_red.size();
        double *x_  = new double [npixels];
        double *fit_ = new double [npixels];
        for(long j=0; j<npixels; j++)
        {
          x_[j] = (boundfit_red[j].getwave()-crval1)/cdelt1+crpix1;
          fit_[j]=boundfit_red[j].getflux()*smean;
        }
        cpgsci(2);
        cpgbin_d(npixels,x_,fit_,true);
        cpgsci(1);
        delete [] x_;
        delete [] fit_;
      }//======================================================================
#endif /* HAVE_CPGPLOT_H */
    }
    //dos o tres bandas
    else if( (fabs(boundfit) == 2) || (fabs(boundfit) == 3) )
    {
      //...................................................incluimos banda azul
      vector <GenericPixel> fluxpix_all;  //datos a ajustar
      vector <GenericPixel> boundfit_all; //ajuste a los datos
      for (long j=j1[0]; j<=j2[0]+1; j++)
      {
        double f;
        if (j == j1[0])
          f=1.0-d1[0];
        else if (j == j2[0]+1)
          f=d2[0];
        else
          f=1.0;
        double wave=static_cast<double>(j-1)*cdelt1+
                    crval1-(crpix1-1.0)*cdelt1;
        GenericPixel temppix(wave,s[j-1],es[j-1],f);
        fluxpix_all.push_back(temppix);
        temppix.setflux(0.0);
        temppix.seteflux(0.0);
        boundfit_all.push_back(temppix);
      }
      if(fabs(boundfit) == 3) //........incluimos en el ajuste la banda central
      {
        for (long j=j1[1]; j<=j2[1]+1; j++)
        {
          double f;
          if (j == j1[1])
            f=1.0-d1[1];
          else if (j == j2[1]+1)
            f=d2[1];
          else
            f=1.0;
          double wave=static_cast<double>(j-1)*cdelt1+
                      crval1-(crpix1-1.0)*cdelt1;
          GenericPixel temppix(wave,s[j-1],es[j-1],f);
          fluxpix_all.push_back(temppix);
          temppix.setflux(0.0);
          temppix.seteflux(0.0);
          boundfit_all.push_back(temppix);
        }
      }
      //...................................................incluimos banda roja
      for (long j=j1[2]; j<=j2[2]+1; j++)
      {
        double f;
        if (j == j1[2])
          f=1.0-d1[2];
        else if (j == j2[2]+1)
          f=d2[2];
        else
          f=1.0;
        double wave=static_cast<double>(j-1)*cdelt1+
                    crval1-(crpix1-1.0)*cdelt1;
        GenericPixel temppix(wave,s[j-1],es[j-1],f);
        fluxpix_all.push_back(temppix);
        temppix.setflux(0.0);
        temppix.seteflux(0.0);
        boundfit_all.push_back(temppix);
      }
      GenericPixel evalpix; //pixel puntual a ser evaluado
      vector <GenericPixel> evaluate; //vector de pixeles a evaluar
      evalpix.setwave(mwb);
      evaluate.push_back(evalpix);
      evalpix.setwave(mwr);
      evaluate.push_back(evalpix);
      if(!boundaryfit(boundfit,fluxpix_all,lerr,boundfit_all,evaluate))
      {
        cout << "ERROR: while computing boundary fit in several bands" << endl;
        exit(1);
      }
      sb=evaluate[0].getflux();
      esb2=evaluate[0].geteflux();
      esb2*=esb2;
      sr=evaluate[1].getflux();
      esr2=evaluate[1].geteflux();
      esr2*=esr2;
#ifdef HAVE_CPGPLOT_H
      //=======================================================================
      //dibujamos boundary fit
      if((plotmode != 0) && (plottype == 2))
      {
        long npixels = boundfit_all.size();
        double *x_  = new double [npixels];
        double *fit_ = new double [npixels];
        for(long j=0; j<npixels; j++)
        {
          x_[j] = (boundfit_all[j].getwave()-crval1)/cdelt1+crpix1;
          fit_[j]=boundfit_all[j].getflux()*smean;
        }
        cpgsci(3);
        cpgbin_d(npixels,x_,fit_,true);
        cpgsci(1);
        delete [] x_;
        delete [] fit_;
      }//======================================================================
#endif /* HAVE_CPGPLOT_H */
    }
    //..................................desde la primera hasta la tercera banda
    //boundfit=4 calcula recta entre valores medios en bandas laterales
    //boundfit=5 calcula integral entre el boundary fit y el espectro
    else if( (fabs(boundfit) == 4) || (fabs(boundfit) == 5) )
    {
      vector <GenericPixel> fluxpix_all;  //datos a ajustar
      vector <GenericPixel> boundfit_all; //ajuste a los datos
      for (long j=j1[0]; j<=j2[2]+1; j++) //.....................incluimos todo
      {
        double f;
        if (j == j1[0])
          f=1.0-d1[0];
        else if (j == j2[2]+1)
          f=d2[2];
        else
          f=1.0;
        double wave=static_cast<double>(j-1)*cdelt1+
                    crval1-(crpix1-1.0)*cdelt1;
        GenericPixel temppix(wave,s[j-1],es[j-1],f);
        fluxpix_all.push_back(temppix);
        temppix.setflux(0.0);
        temppix.seteflux(0.0);
        boundfit_all.push_back(temppix);
      }
      GenericPixel evalpix; //pixel puntual a ser evaluado
      vector <GenericPixel> evaluate; //vector de pixeles a evaluar
      if (fabs(boundfit) == 4) //evaluamos puntos centrales de bandas laterales
      {
        evalpix.setwave(mwb);
        evaluate.push_back(evalpix);
        evalpix.setwave(mwr);
        evaluate.push_back(evalpix);
      }
      else if (fabs(boundfit) == 5) //evaluamos los puntos de la banda central
      {
        for (long j=j1[1]; j<=j2[1]+1; j++)
        {
          //las lineas que siguen no son necesarias si no queremos usar "f"
          /*
          double f;
          if (j == j1[1])
            f=1.0-d1[1];
          else if (j == j2[1]+1)
            f=d2[1];
          else
            f=1.0;
          */
          double wave=static_cast<double>(j-1)*cdelt1+
                      crval1-(crpix1-1.0)*cdelt1;
          evalpix.setwave(wave);
          evaluate.push_back(evalpix);
        }
      }
      if(!boundaryfit(boundfit,fluxpix_all,lerr,boundfit_all,evaluate))
      {
        cout << "ERROR: while computing boundary fit in several bands" << endl;
        exit(1);
      }
      if (fabs(boundfit) == 4)
      {
        sb=evaluate[0].getflux();
        esb2=evaluate[0].geteflux();
        esb2*=esb2;
        sr=evaluate[1].getflux();
        esr2=evaluate[1].geteflux();
        esr2*=esr2;
      }
      else if (fabs(boundfit) == 5)
      {
        for (long j=j1[1]; j<=j2[1]+1; j++)
        {
          sc[j-1]=evaluate[j-j1[1]].getflux();
          esc2[j-1]=evaluate[j-j1[1]].geteflux();
          esc2[j-1]*=esc2[j-1];
        }
      }
#ifdef HAVE_CPGPLOT_H
      //=======================================================================
      //dibujamos boundary fit
      if((plotmode != 0) && (plottype == 2))
      {
        long npixels = boundfit_all.size();
        double *x_  = new double [npixels];
        double *fit_ = new double [npixels];
        for(long j=0; j<npixels; j++)
        {
          x_[j] = (boundfit_all[j].getwave()-crval1)/cdelt1+crpix1;
          fit_[j]=boundfit_all[j].getflux()*smean;
        }
        cpgsci(3);
        cpgbin_d(npixels,x_,fit_,true);
        cpgsci(1);
        delete [] x_;
        delete [] fit_;
      }//======================================================================
#endif /* HAVE_CPGPLOT_H */    
    }
    else if (contperc >= 0) //.................................usamos percentil
    {
      //banda azul
      if (j2[0]-j1[0] < 2)
      {
        cout << "ERROR: number of pixels in blue continuum bandpass too low "
             << "to use contperc"
             << endl;
        exit(1);
      }
      else
      {
        vector <GenericPixel> fluxpix_blue;
        for (long j=j1[0]; j<=j2[0]+1; j++)
        {
          double f;
          if (j == j1[0])
            f=1.0-d1[0];
          else if (j == j2[0]+1)
            f=d2[0];
          else
            f=1.0;
          double wave=static_cast<double>(j-1)*cdelt1+
                      crval1-(crpix1-1.0)*cdelt1;
          GenericPixel temppix(wave,s[j-1],es[j-1],f);
          fluxpix_blue.push_back(temppix);
        }
        if(!fpercent(fluxpix_blue,contperc,lerr,&sb,&esb2))
        {
          cout << "ERROR: while computing percentile" << endl;
          exit(1);
        }
      }
      //banda roja
      if (j2[2]-j1[2] < 2)
      {
        cout << "ERROR: number of pixels in red continuum bandpass too low "
             << "to use contperc"
             << endl;
        exit(1);
      }
      else
      {
        vector <GenericPixel> fluxpix_red;
        for (long j=j1[2]; j<=j2[2]+1; j++)
        {
          double f;
          if (j == j1[2])
            f=1.0-d1[2];
          else if (j == j2[2]+1)
            f=d2[2];
          else
            f=1.0;
          double wave=static_cast<double>(j-1)*cdelt1+
                      crval1-(crpix1-1.0)*cdelt1;
          GenericPixel temppix(wave,s[j-1],es[j-1],f);
          fluxpix_red.push_back(temppix);
        }
        if(!fpercent(fluxpix_red,contperc,lerr,&sr,&esr2))
        {
          cout << "ERROR: while computing percentile" << endl;
          exit(1);
        }
      }
    }
    else //...............................................usamos metodo clasico
    {
      //banda azul
      if (flattened)
      {
        sb = 1.0;
        esb2 = 0.0;
      }
      else
      {
        for (long j=j1[0]; j<=j2[0]+1; j++)
        {
          double f;
          if (j == j1[0])
            f=1.0-d1[0];
          else if (j == j2[0]+1)
            f=d2[0];
          else
            f=1.0;
          sb+=f*s[j-1];
          if(lerr) esb2+=f*f*es[j-1]*es[j-1];
        }
        sb*=cdelt1;
        sb/=rl[0];
        if(lerr)
        {
          esb2*=cdelt1*cdelt1;
          esb2/=(rl[0]*rl[0]);
        }
      }
      //banda roja
      if (flattened)
      {
        sr = 1.0;
        esr2 = 0.0;
      }
      else
      {
        for (long j=j1[2]; j<=j2[2]+1; j++)
        {
          double f;
          if (j == j1[2])
            f=1.0-d1[2];
          else if (j == j2[2]+1)
            f=d2[2];
          else
            f=1.0;
          sr+=f*s[j-1];
          if(lerr) esr2+=f*f*es[j-1]*es[j-1];
        }
        sr*=cdelt1;
        sr/=rl[2];
        if(lerr)
        {
          esr2*=cdelt1*cdelt1;
          esr2/=(rl[2]*rl[2]);
        }
      }
      if(pyindexf)
      {
        cout << "python> {'f_blue_center': " << sb*smean << ", "
             <<          "'f_red_center': "  << sr*smean << "}" << endl;
      }
    }
    //.........................................................................
    //calculamos pseudo-continuo como una recta uniendo los flujos "promedio"
    //en las bandas de continuo
    if (fabs(boundfit) != 5) //calculamos la recta
    {
      for (long j = j1min; j <= j2max+1; j++)
      {
        double wla=static_cast<double>(j-1)*cdelt1+crval1-(crpix1-1.0)*cdelt1;
        sc[j-1] = (sb*(mwr-wla)+sr*(wla-mwb))/(mwr-mwb);
      }
      if(lerr)
      {
        for (long j = j1min; j <= j2max+1; j++)
        {
          double wla=static_cast<double>(j-1)*cdelt1+crval1-(crpix1-1.0)*cdelt1;
          esc2[j-1] = (esb2*(mwr-wla)*(mwr-wla)+esr2*(wla-mwb)*(wla-mwb))/
                      ((mwr-mwb)*(mwr-mwb));
        }
      }
    }
    //recorremos la banda central
    double tc=0.0;
    double etc2=0.0,etc=0.0;
    for (long j=j1[1]; j<=j2[1]+1; j++)
    {
      double f;
      if (j == j1[1])
        f=1.0-d1[1];
      else if (j == j2[1]+1)
        f=d2[1];
      else
        f=1.0;
      tc+=f*s[j-1]/sc[j-1];
#ifdef HAVE_CPGPLOT_H
      //================================================
      //dibujamos lineas verticales uniendo el flujo en
      //el continuo con el flujo en el espectro
      if((plotmode != 0) && (plottype == 2))
      {
        cpgsci(8);
        cpgmove_d(static_cast<double>(j),s[j-1]*smean);
        cpgdraw_d(static_cast<double>(j),sc[j-1]*smean);
      }//===============================================
#endif /* HAVE_CPGPLOT_H */      
      if(lerr) 
      {
        etc2+=f*f*(s[j-1]*s[j-1]*esc2[j-1]+sc[j-1]*sc[j-1]*es[j-1]*es[j-1])/
        (sc[j-1]*sc[j-1]*sc[j-1]*sc[j-1]);
        double wla1=static_cast<double>(j-1)*cdelt1
                    +crval1-(crpix1-1.0)*cdelt1;
        for (long jj=j1[1]; jj<=j2[1]+1; jj++)
        {
          if (jj != j)
          {
            double ff;
            if (jj == j1[1])
              ff=1.0-d1[1];
            else if (j == j2[1]+1)
              ff=d2[1];
            else
              ff=1.0;
            double wla2=static_cast<double>(jj-1)*cdelt1
                        +crval1-(crpix1-1.0)*cdelt1;
            double cov=((mwr-wla1)*(mwr-wla2)*esb2+
                        (wla1-mwb)*(wla2-mwb)*esr2)/
                       ((mwr-mwb)*(mwr-mwb));
            etc2+=ff*f*s[j-1]*s[jj-1]*cov/(sc[j-1]*sc[j-1]*sc[jj-1]*sc[jj-1]);
          }
        }
      }
    }
    tc*=cdelt1;
    etc=sqrt(etc2)*cdelt1;
    if (myindex.gettype() == 1) //indice molecular
    {
      if (tc/rl[1] <= 0.0)
      {
        log_negative=true;
        findex=0.0;
        eindex=0.0;
        return(false);
      }
      findex = -2.5*log10(tc/rl[1]);
      if(lerr) eindex = cte_log_exp/pow(10,-0.4*findex)*etc/rl[1];
    }
    else //indice atomico
    {
      if(logindex) //indice atomico medido en magnitudes
      {
        if (tc/rl[1] <= 0.0)
        {
          log_negative=true;
          findex=0.0;
          eindex=0.0;
          return(false);
        }
        findex = -2.5*log10(tc/rl[1]);
        if(lerr) eindex = cte_log_exp/pow(10,-0.4*findex)*etc/rl[1];
      }
      else //indice atomico medido como indice atomico
      {
        findex = (rl[1]-tc)/rcvel1;
        if(lerr) eindex=etc/rcvel1;
      }
    }
    delete [] sc;
    delete [] esc2;
#ifdef HAVE_CPGPLOT_H
    //=======================================================================
    //dibujamos
    if((plotmode !=0) || (plottype == 2))
    {
      //dibujamos los dos puntos usados para calcular el continuo
      if(plottype == 2)
      {
        cpgsci(8);
        double *wdum = new double [2];
        double *ydum = new double [2];
        wdum[0]=(mwb-crval1)/cdelt1+crpix1;
        ydum[0]=sb*smean;
        wdum[1]=(mwr-crval1)/cdelt1+crpix1;
        ydum[1]=sr*smean;
        cpgpt_d(2,wdum,ydum,17);
        cout << "#Continuum, point #1: " << mwb << ", " << ydum[0] << endl;
        cout << "#Continuum, point #2: " << mwr << ", " << ydum[1] << endl;
        delete [] wdum;
        delete [] ydum;
      }
      //dibujamos el continuo
      if(plottype == 2)
        cpgsci(7);
      else
        cpgsci(6);
      const double wla=wvmin*rcvel1;
      const double yduma=sb*(mwr-wla)/(mwr-mwb)+sr*(wla-mwb)/(mwr-mwb);
      const double xca=(wla-crval1)/cdelt1+crpix1;
      cpgmove_d(xca,yduma*smean);
      const double wlb=wvmax*rcvel1;
      const double ydumb=sb*(mwr-wlb)/(mwr-mwb)+sr*(wlb-mwb)/(mwr-mwb);
      const double xcb=(wlb-crval1)/cdelt1+crpix1;
      cpgdraw_d(xcb,ydumb*smean);
      cpgsci(1);
      //repetimos dibujo del espectro si hemos utilizado biaserr o linearerr
      if ((fabs(biaserr) != 0) || (fabs(linearerr) != 0))
      {
        double *x  = new double [naxis1];
        double *s_ = new double [naxis1];
        for(long j=1; j<=naxis1; j++)
        {
          x[j-1] = static_cast<double>(j);
          s_[j-1]=s[j-1]*smean;
        }
        cpgsci(15);
        cpgbin_d(naxis1,x,s_,true);
        cpgsci(1);
        delete [] x;
        delete [] s_;
      }
    }//======================================================================
#endif /* HAVE_CPGPLOT_H */
  }
  //***************************************************************************
  //D4000, B4000 y colores
  //***************************************************************************
  else if ( (myindex.gettype() == 3) || 
            (myindex.gettype() == 4) || 
            (myindex.gettype() == 5) )
  {
    //protecciones
    if ( contperc >= 0 )
    {
      cout << "#WARNING: contperc is being implemented for this index"
           << endl;
    }
    if ( boundfit != 0 )
    {
      cout << "#WARNING: boundfit is being implemented for this index"
           << endl;
    }
    if ( flattened )
    {
      cout << "ERROR: flattened has not been implemented yet for this index"
           << endl;
      exit(1);
    }
    //pesos para la discontinuidad
    double *wl = new double [naxis1];
    double *wl2 = new double [naxis1];
    if (myindex.gettype() == 3) //D4000
    {
      //for (long j = j1min; j <= j2max+1; j++)
      for (long j = 1; j <= naxis1; j++)
      {
        double wla=static_cast<double>(j-1)*cdelt1+crval1-(crpix1-1.0)*cdelt1;
        wla/=rcvel1;
        wla/=4000.0;
        wl[j-1] = wla*wla;
        if (lerr) wl2[j-1] = wl[j-1]*wl[j-1];
      }
    }
    else //B4000 y colores
    {
      for (long j = j1min; j <= j2max+1; j++)
      {
        wl[j-1] = 1.0;
        if (lerr) wl2[j-1] = 1.0;
      }
    }
    //-------------------------------------------------------------------------
    //calculamos las integrales
    double *fx = new double [nbands];
    double *efx = new double [nbands];
    //.........................................................................
    //boundfit independiente en cada banda
    if (fabs(boundfit) == 1)
    {
      for (long nb=0; nb < nbands; nb++)
      {
        vector <GenericPixel> fluxpix_band;  //datos a ajustar
        vector <GenericPixel> boundfit_band; //ajuste a los datos
        for (long j=j1[nb]; j<=j2[nb]+1; j++)
        {
          double f;
          if (j == j1[nb])
            f=1.0-d1[nb];
          else if (j == j2[nb]+1)
            f=d2[nb];
          else
            f=1.0;
          double wave=static_cast<double>(j-1)*cdelt1+
                      crval1-(crpix1-1.0)*cdelt1;
          GenericPixel temppix(wave,s[j-1],es[j-1],f);
          fluxpix_band.push_back(temppix);
          temppix.setflux(0.0);
          temppix.seteflux(0.0);
          boundfit_band.push_back(temppix);
        }
        vector <GenericPixel> evaluate; //vector de pixeles a evaluar
        //el vector anterior estara vacio en este caso
        if(!boundaryfit(boundfit,fluxpix_band,lerr,boundfit_band,evaluate))
        {
          cout << "ERROR: while computing boundary fit in band" << endl;
          exit(1);
        }
#ifdef HAVE_CPGPLOT_H
        //=====================================================================
        //dibujamos boundary fit de la banda considerada
        if((plotmode != 0) && (plottype ==2))
        {
          long npixels = boundfit_band.size();
          double *x_  = new double [npixels];
          double *fit_ = new double [npixels];
          for(long j=0; j<npixels; j++)
          {
            x_[j] = (boundfit_band[j].getwave()-crval1)/cdelt1+crpix1;
            fit_[j]=boundfit_band[j].getflux()*smean;
          }
          if (nb == 0)
          {
            cpgsci(4);
          }
          else
          {
            cpgsci(2);
          }
          cpgbin_d(npixels,x_,fit_,true);
          cpgsci(1);
          delete [] x_;
          delete [] fit_;
        }
        //=====================================================================
#endif /* HAVE_CPTPLOT_H */
        double tc=0.0;
        double etc=0.0;
        for (long j=j1[nb]; j<=j2[nb]+1; j++)
        {
          double f;
          if (j == j1[nb])
            f=1.0-d1[nb];
          else if (j == j2[nb]+1)
            f=d2[nb];
          else
            f=1.0;
          tc+=boundfit_band[j-j1[nb]].getflux()*wl[j-1];
          if(lerr) etc+=f*f*boundfit_band[j-j1[nb]].geteflux()*
                            boundfit_band[j-j1[nb]].geteflux()*wl2[j-1];
        }
        fx[nb]=tc;
        if(lerr) efx[nb]=etc;
      }
    }
    //.........................................................................
    //boundfit con los datos de las dos bandas
    else if (fabs(boundfit) == 2)
    {
      vector <GenericPixel> fluxpix_all;  //datos a ajustar
      vector <GenericPixel> boundfit_all; //ajuste a los datos
      for (long nb=0; nb < nbands; nb++)
      {
        for (long j=j1[nb]; j<=j2[nb]+1; j++)
        {
          double f;
          if (j == j1[nb])
            f=1.0-d1[nb];
          else if (j == j2[nb]+1)
            f=d2[nb];
          else
            f=1.0;
          double wave=static_cast<double>(j-1)*cdelt1+
                      crval1-(crpix1-1.0)*cdelt1;
          GenericPixel temppix(wave,s[j-1],es[j-1],f);
          fluxpix_all.push_back(temppix);
          temppix.setflux(0.0);
          temppix.seteflux(0.0);
          boundfit_all.push_back(temppix);
        }
      }
      for (long nb=0; nb < nbands; nb++)
      {
        GenericPixel evalpix; //pixel puntual a ser evaluado
        vector <GenericPixel> evaluate; //vector de pixeles a evaluar
        for (long j=j1[nb]; j<=j2[nb]+1; j++)
        {
          /*
          double f;
          if (j == j1[nb])
            f=1.0-d1[nb];
          else if (j == j2[nb]+1)
            f=d2[nb];
          else
            f=1.0;
          */
          double wave=static_cast<double>(j-1)*cdelt1+
                      crval1-(crpix1-1.0)*cdelt1;
          evalpix.setwave(wave);
          evaluate.push_back(evalpix);
        }
        if(!boundaryfit(boundfit,fluxpix_all,lerr,boundfit_all,evaluate))
        {
          cout << "ERROR: while computing boundary fit in band" << endl;
          exit(1);
        }
#ifdef HAVE_CPGPLOT_H
        //=====================================================================
        //dibujamos boundary fit de la banda considerada
        if((plotmode != 0) && (plottype ==2))
        {
          long npixels = evaluate.size();
          double *x_  = new double [npixels];
          double *fit_ = new double [npixels];
          for(long j=0; j<npixels; j++)
          {
            x_[j] = (evaluate[j].getwave()-crval1)/cdelt1+crpix1;
            fit_[j]=evaluate[j].getflux()*smean;
          }
          if (nb == 0)
          {
            cpgsci(4);
          }
          else
          {
            cpgsci(2);
          }
          cpgbin_d(npixels,x_,fit_,true);
          cpgsci(1);
          delete [] x_;
          delete [] fit_;
        }
        //=====================================================================
#endif /* HAVE_CPTPLOT_H */
        double tc=0.0;
        double etc=0.0;
        for (long j=j1[nb]; j<=j2[nb]+1; j++)
        {
          double f;
          if (j == j1[nb])
            f=1.0-d1[nb];
          else if (j == j2[nb]+1)
            f=d2[nb];
          else
            f=1.0;
          tc+=evaluate[j-j1[nb]].getflux()*wl[j-1];
          if(lerr) etc+=f*f*evaluate[j-j1[nb]].geteflux()*
                            evaluate[j-j1[nb]].geteflux()*wl2[j-1];
        }
        fx[nb]=tc;
        if(lerr) efx[nb]=etc;
      }
    }
    //.........................................................................
    //boundfit con los datos entre la primera y la segunda banda
    else if (fabs(boundfit) == 4)
    {
      vector <GenericPixel> fluxpix_all;  //datos a ajustar
      vector <GenericPixel> boundfit_all; //ajuste a los datos
      for (long j=j1[0]; j<=j2[1]+1; j++)
      {
        double f;
        if (j == j1[0])
          f=1.0-d1[0];
        else if (j == j2[1]+1)
          f=d2[1];
        else
          f=1.0;
        double wave=static_cast<double>(j-1)*cdelt1+
                    crval1-(crpix1-1.0)*cdelt1;
        GenericPixel temppix(wave,s[j-1],es[j-1],f);
        fluxpix_all.push_back(temppix);
        temppix.setflux(0.0);
        temppix.seteflux(0.0);
        boundfit_all.push_back(temppix);
      }
//---- (borrar desde aqui)
//el siguiente bloque se puede borrar; lo he insertado para comprobar
//graficamente que el calculo esta bien (dibuja el boundary fit en todo el
//intervalo ajustado, de modo que al pintar encima el boundary fit en cada
//banda, permanece de otro color el boundary fit en la region intermedia
#ifdef HAVE_CPGPLOT_H
      //=======================================================================
      if (true)
      {
        vector <GenericPixel> evaluate; //vector de pixeles a evaluar
        if(!boundaryfit(boundfit,fluxpix_all,lerr,boundfit_all,evaluate))
        {
          cout << "ERROR: while computing boundary fit in band" << endl;
          exit(1);
        }
        //dibujamos boundary fit de la banda considerada
        if((plotmode != 0) && (plottype ==2))
        {
          long npixels = boundfit_all.size();
          double *x_  = new double [npixels];
          double *fit_ = new double [npixels];
          for(long j=0; j<npixels; j++)
          {
            x_[j] = (boundfit_all[j].getwave()-crval1)/cdelt1+crpix1;
            fit_[j]=boundfit_all[j].getflux()*smean;
          }
          cpgsci(3);
          cpgbin_d(npixels,x_,fit_,true);
          cpgsci(1);
          delete [] x_;
          delete [] fit_;
        }
      }
      //=======================================================================
#endif /* HAVE_CPTPLOT_H */
//---- (borrar hasta aqui)
      for (long nb=0; nb < nbands; nb++)
      {
        GenericPixel evalpix; //pixel puntual a ser evaluado
        vector <GenericPixel> evaluate; //vector de pixeles a evaluar
        for (long j=j1[nb]; j<=j2[nb]+1; j++)
        {
          /*
          double f;
          if (j == j1[nb])
            f=1.0-d1[nb];
          else if (j == j2[nb]+1)
            f=d2[nb];
          else
            f=1.0;
          */
          double wave=static_cast<double>(j-1)*cdelt1+
                      crval1-(crpix1-1.0)*cdelt1;
          evalpix.setwave(wave);
          evaluate.push_back(evalpix);
        }
        if(!boundaryfit(boundfit,fluxpix_all,lerr,boundfit_all,evaluate))
        {
          cout << "ERROR: while computing boundary fit in band" << endl;
          exit(1);
        }
#ifdef HAVE_CPGPLOT_H
        //=====================================================================
        //dibujamos boundary fit de la banda considerada
        if((plotmode != 0) && (plottype ==2))
        {
          long npixels = evaluate.size();
          double *x_  = new double [npixels];
          double *fit_ = new double [npixels];
          for(long j=0; j<npixels; j++)
          {
            x_[j] = (evaluate[j].getwave()-crval1)/cdelt1+crpix1;
            fit_[j]=evaluate[j].getflux()*smean;
          }
          if (nb == 0)
          {
            cpgsci(4);
          }
          else
          {
            cpgsci(2);
          }
          cpgbin_d(npixels,x_,fit_,true);
          cpgsci(1);
          delete [] x_;
          delete [] fit_;
        }
        //=====================================================================
#endif /* HAVE_CPTPLOT_H */
        double tc=0.0;
        double etc=0.0;
        for (long j=j1[nb]; j<=j2[nb]+1; j++)
        {
          double f;
          if (j == j1[nb])
            f=1.0-d1[nb];
          else if (j == j2[nb]+1)
            f=d2[nb];
          else
            f=1.0;
          tc+=evaluate[j-j1[nb]].getflux()*wl[j-1];
          if(lerr) etc+=f*f*evaluate[j-j1[nb]].geteflux()*
                            evaluate[j-j1[nb]].geteflux()*wl2[j-1];
        }
        fx[nb]=tc;
        if(lerr) efx[nb]=etc;
      }
    }
    //.........................................................................
    //Valores adicionales de boundfit estan pendientes
    else if (fabs(boundfit) != 0)
    {
      cout << "ERROR: this value of boundfit is not implemented for this index"
           << endl;
      exit(1);
    }
    else if (contperc >= 0) //.................................usamos percentil
    //En este caso, imponemos que el flujo en cada banda sea igual al percentil
    //solicitado (constante en todos los pixels de la banda). Esto puede no
    //tener mucho sentido, pero aqui esta.
    {
      for (long nb=0; nb < nbands; nb++)
      {
        if (j2[nb]-j1[nb] < 2)
        {
          cout << "ERROR: number of pixels in bandpass too low "
               << "to use contperc"
               << endl;
          exit(1);
        }
        double sdum=0.0;
        double esdum2=0.0;
        vector <GenericPixel> fluxpix_band;
        for (long j=j1[nb]; j<=j2[nb]+1; j++)
        {
          double f;
          if (j == j1[nb])
            f=1.0-d1[nb];
          else if (j == j2[nb]+1)
            f=d2[nb];
          else
            f=1.0;
          double wave=static_cast<double>(j-1)*cdelt1+
                      crval1-(crpix1-1.0)*cdelt1;
          GenericPixel temppix(wave,s[j-1],es[j-1],f);
          fluxpix_band.push_back(temppix);
        }
        if(!fpercent(fluxpix_band,contperc,lerr,&sdum,&esdum2))
        {
          cout << "ERROR: while computing percentile" << endl;
          exit(1);
        }
#ifdef HAVE_CPGPLOT_H
        //=====================================================================
        //dibujamos percentil
        if(plotmode != 0)
        {
          cpgsci(6);
          cpgmove_d(static_cast<double>(j1[nb]),sdum*smean);
          cpgdraw_d(static_cast<double>(j2[nb]+1),sdum*smean);
        }
        //=====================================================================
#endif /* HAVE_CPGPLOT_H */
        double tc=0.0;
        double etc=0.0;
        for (long j=j1[nb]; j<=j2[nb]+1; j++)
        {
          double f;
          if (j == j1[nb])
            f=1.0-d1[nb];
          else if (j == j2[nb]+1)
            f=d2[nb];
          else
            f=1.0;
          tc+=f*sdum*wl[j-1];
          if(lerr) etc+=f*f*esdum2*wl2[j-1];
        }
        fx[nb]=tc;
        if(lerr) efx[nb]=etc;
      }
    }
    else //...............................................usamos metodo clasico
    {
      for (long nb=0; nb < nbands; nb++)
      {
        double tc=0.0;
        double etc=0.0;
        for (long j=j1[nb]; j<=j2[nb]+1; j++)
        {
          double f;
          if (j == j1[nb])
            f=1.0-d1[nb];
          else if (j == j2[nb]+1)
            f=d2[nb];
          else
            f=1.0;
          tc+=f*s[j-1]*wl[j-1];
          if(lerr) etc+=f*f*es[j-1]*es[j-1]*wl2[j-1];
        }
        fx[nb]=tc;
        if(lerr) efx[nb]=etc;
      }
    }
    //.........................................................................
    findex=fx[1]/fx[0];
    if(lerr) eindex=sqrt(fx[0]*fx[0]*efx[1]+fx[1]*fx[1]*efx[0])/
                    (fx[0]*fx[0]);
    if (myindex.gettype() == 5) //colores
    {
      findex=findex*rl[0]/rl[1];
      eindex=eindex*rl[0]/rl[1];
    }
    if(logindex) //si medimos en escala logaritmica
    {
      eindex= cte_log_exp*eindex/findex;
      if (findex <= 0.0)
      {
        log_negative=true;
        findex=0.0;
        eindex=0.0;
        return(false);
      }
      findex=2.5*log10(findex);
    }
    delete [] wl;
    delete [] wl2;
    delete [] fx;
    delete [] efx;
    // generate output for pyindexf
    if(pyindexf)
    {
      double sb = 0.0;
      for (long j=j1[0];j<=j2[0]+1;j++)
      {
        sb +=s[j-1];
      }
      sb /= static_cast<double>(j2[0]-j1[0]+2);
      double sr = 0.0;
      for (long j=j1[1];j<=j2[1]+1;j++)
      {
        sr +=s[j-1];
      }
      sr /= static_cast<double>(j2[1]-j1[1]+2);
      cout << "python> {'f_mean_blue': " << sb*smean << ", "
           <<          "'f_mean_red': "  << sr*smean << "}" << endl;
    }
#ifdef HAVE_CPGPLOT_H
    //=========================================================================
    //dibujamos
    if(plotmode !=0)
    {
      if (contperc >= 0)
      {
        //do nothing; we have already plotted the percentiles
      }
      else if (fabs(boundfit) == 1)
      {
        //do nothing; we have already plotted the boundary fits
      }
      else if (fabs(boundfit) == 2)
      {
        //do nothing; we have already plotted the boundary fits
      }
      else if (fabs(boundfit) == 4)
      {
        //do nothing; we have already plotted the boundary fits
      }
      else
      {
        cpgsci(6);
        //continuo en banda azul
        const double wla=myindex.getldo1(0)*rcvel1;
        double yduma=0.0;
        for (long j=j1[0];j<=j2[0]+1;j++)
        {
          yduma+=s[j-1];
        }
        yduma/=static_cast<double>(j2[0]-j1[0]+2);
        const double xca=(wla-crval1)/cdelt1+crpix1;
        cpgmove_d(xca,yduma*smean);
        const double wlb=myindex.getldo2(0)*rcvel1;
        const double xcb=(wlb-crval1)/cdelt1+crpix1;
        cpgdraw_d(xcb,yduma*smean);
        //continuo en banda roja
        const double wlc=myindex.getldo1(1)*rcvel1;
        double ydumb=0.0;
        for (long j=j1[1];j<=j2[1]+1;j++)
        {
          ydumb+=s[j-1];
        }
        ydumb/=static_cast<double>(j2[1]-j1[1]+2);
        const double xcc=(wlc-crval1)/cdelt1+crpix1;
        cpgmove_d(xcc,ydumb*smean);
        const double wld=myindex.getldo2(1)*rcvel1;
        const double xcd=(wld-crval1)/cdelt1+crpix1;
        cpgdraw_d(xcd,ydumb*smean);
        cpgsci(1);
      }
    }//========================================================================
#endif /* HAVE_CPGPLOT_H */  
  }
  //***************************************************************************
  //Lineas de emision (se ajusta el continuo a una recta)
  //***************************************************************************
  else if (myindex.gettype() == 10)
  {
    //protecciones
    if ( contperc >= 0 )
    {
      cout << "ERROR: contperc has not been implemented yet for this index"
           << endl;
      exit(1);
    }
    if ( boundfit != 0 )
    {
      cout << "ERROR: boundfit has not been implemented yet for this index"
           << endl;
      exit(1);
    }
    if ( flattened )
    {
      cout << "ERROR: flattened has not been implemented yet for this index"
           << endl;
      exit(1);
    }
    //si no hay errores, hacemos todos iguales a uno para utilizar las mismas
    //formulas
    if(!lerr)
      for (long j = j1min; j <= j2max+1; j++)
        es[j-1]=1.0;
    //calculamos la recta del continuo mediante minimos cuadrados (y=amc*x+bmc)
    //(para la variable x usamos el numero de pixel en lugar de la longitud
    //de onda porque, en principio, seran numeros mas pequenos)
    double sigma2;
    double sum0=0.0;
    double sumx=0.0;
    double sumy=0.0;
    double sumxy=0.0;
    double sumxx=0.0;
    for (long nb=0; nb < nbands; nb++)//recorremos todas las bandas porque
    {                                 //los continuos pueden estar intercalados
      double factor = myindex.getfactor_el(nb);
      if( factor == 0.0 ) //es una banda de continuo
      {
        for (long j=j1[nb]; j<=j2[nb]+1; j++)
        {
          double f;
          if (j == j1[nb])
            f=1.0-d1[nb];
          else if (j == j2[nb]+1)
            f=d2[nb];
          else
            f=1.0;
          sigma2=es[j-1]*es[j-1];
          sum0+=f/sigma2;
          sumx+=f*static_cast<double>(j)/sigma2;
          sumy+=f*s[j-1]/sigma2;
          sumxy+=f*static_cast<double>(j)*s[j-1]/sigma2;
          sumxx+=f*static_cast<double>(j)*static_cast<double>(j)/sigma2;
        }
      }
    }
    double deter=sum0*sumxx-sumx*sumx;
    double amc=(sum0*sumxy-sumx*sumy)/deter;
    double bmc=(sumxx*sumy-sumx*sumxy)/deter;
    //calculamos el pseudo-continuo
    double *sc = new double [naxis1];
    double *esc2 = new double [naxis1];
    for (long j = j1min; j <= j2max+1; j++)
    {
      sc[j-1] = amc*static_cast<double>(j)+bmc;
    }
    // generate output for pyindexf
    if(pyindexf)
    {
      cout << "python> {'f_cont_bluest': " << sc[j1min-1]*smean << ", "
           <<          "'f_cont_reddest': "  << sc[j2max]*smean << "}"
           << endl;
    }
#ifdef HAVE_CPGPLOT_H
    //=======================================================================
    //dibujamos continuo
    if (plotmode !=0)
    {
      cpgsci(6);
      double fdum = sc[j1min-1]*smean;
      cpgmove_d(static_cast<double>(j1min),fdum);
      for (long j = j1min; j <= j2max+1; j++)
      {
        fdum = sc[j-1]*smean;
        cpgdraw_d(static_cast<double>(j),fdum);
      }
      cpgsci(1);
    }
    //=======================================================================
#endif /* HAVE_CPGPLOT_H */    
    if(lerr)
    {
      for (long j = j1min; j <= j2max+1; j++)
      {
        esc2[j-1] = 0.0;
        for (long nb=0; nb < nbands; nb++)
        {
          double factor = myindex.getfactor_el(nb);
          if( factor == 0.0 ) //es una banda de continuo
          {
            for (long jj=j1[nb]; jj<=j2[nb]+1; jj++)
            {
              sigma2=es[jj-1]*es[jj-1];
              double fdum=(sum0*static_cast<double>(jj)/sigma2-sumx/sigma2)*
                          static_cast<double>(j)/deter+
                          (sumxx/sigma2-
                           sumx*static_cast<double>(jj)/sigma2)/deter;
              esc2[j-1]+=fdum*fdum*sigma2;
            }
          }
        }
      }
#ifdef HAVE_CPGPLOT_H
      //=======================================================================
      //dibujamos continuo y su error
      if (plotmode < 0)
      {
        cpgsci(7);
        //error superior
        double fdum = (sc[j1min-1]+sqrt(esc2[j1min-1]))*smean;
        cpgmove_d(static_cast<double>(j1min),fdum);
        for (long j = j1min; j <= j2max+1; j++)
        {
          fdum = (sc[j-1]+sqrt(esc2[j-1]))*smean;
          cpgdraw_d(static_cast<double>(j),fdum);
        }
        //error inferior
        fdum = (sc[j1min-1]-sqrt(esc2[j1min-1]))*smean;
        cpgmove_d(static_cast<double>(j1min),fdum);
        for (long j = j1min; j <= j2max+1; j++)
        {
          fdum = (sc[j-1]-sqrt(esc2[j-1]))*smean;
          cpgdraw_d(static_cast<double>(j),fdum);
        }
        cpgsci(1);
      }
      //=======================================================================
#endif /* HAVE_CPGPLOT_H */
    }
    //calculamos unos sumatorios auxiliares
    double sumli=0.0; //ver notas a mano
    double sumni=0.0; //ver notas a mano
    for (long nb=0; nb < nbands; nb++)
    {
      double factor = myindex.getfactor_el(nb);
      if( factor != 0.0 ) //es una banda de linea
      {
        for (long j=j1[nb]; j<=j2[nb]+1; j++)
        {
          double f;
          if (j == j1[nb])
            f=1.0-d1[nb];
          else if (j == j2[nb]+1)
            f=d2[nb];
          else
            f=1.0;
          sumli+=f*static_cast<double>(j);
          sumni+=f;
        }
      }
    }
    //calculamos el flujo y su error
    findex=0.0;
    eindex=0.0;
    for (long nb=0; nb < nbands; nb++)
    {
      double factor = myindex.getfactor_el(nb);
      if( factor != 0.0 ) //es una banda de linea
      {
        for (long j=j1[nb]; j<=j2[nb]+1; j++)
        {
          double f;
          if (j == j1[nb])
            f=1.0-d1[nb];
          else if (j == j2[nb]+1)
            f=d2[nb];
          else
            f=1.0;
          findex+=f*(s[j-1]-sc[j-1]);
          if(lerr)
          {
            //eindex+=f*f*(es[j-1]*es[j-1]+esc2[j-1]);
            eindex+=f*f*es[j-1]*es[j-1];
          }
        }
      }
      else //es una banda de continuo
      {
        if(lerr)
        {
          for (long jj=j1[nb]; jj<=j2[nb]+1; jj++)
          {
            double f;
            if (jj == j1[nb])
              f=1.0-d1[nb];
            else if (jj == j2[nb]+1)
              f=d2[nb];
            else
              f=1.0;
            sigma2=es[jj-1]*es[jj-1];
            double fduma=(sum0*static_cast<double>(jj)/sigma2-sumx/sigma2)
                         /deter;
            double fdumb=(sumxx/sigma2-sumx*static_cast<double>(jj)/sigma2)
                         /deter;
            double fdum=sumli*fduma+sumni*fdumb;
            eindex+=f*f*fdum*fdum*sigma2;
          }
        }
      }
    }
    findex=findex*cdelt1*smean;
    eindex=sqrt(eindex)*cdelt1*smean;
    if(logindex)
    {
      //no tiene sentido
    }
    delete [] sc;
    delete [] esc2;
  }
  //***************************************************************************
  //Discontinuidades genericas
  //***************************************************************************
  else if ( (myindex.gettype() >= 11) && (myindex.gettype() <= 99) )
  {
    //protecciones
    if ( contperc >= 0 )
    {
      cout << "ERROR: contperc has not been implemented yet for this index"
           << endl;
      exit(1);
    }
    if ( boundfit != 0 )
    {
      cout << "ERROR: boundfit has not been implemented yet for this index"
           << endl;
      exit(1);
    }
    if ( flattened )
    {
      cout << "ERROR: flattened has not been implemented yet for this index"
           << endl;
      exit(1);
    }
    long nconti = myindex.getnconti();
    long nlines = myindex.getnlines();
    //calculamos flujo promedio en las bandas de continuo
    double fconti=0.0;
    double econti2=0.0;
    double rltot_conti=0.0;
    for (long nb=0; nb < nconti; nb++)
    {
      for (long j=j1[nb]; j<=j2[nb]+1; j++)
      {
        double f;
        if (j == j1[nb])
          f=1.0-d1[nb];
        else if (j == j2[nb]+1)
          f=d2[nb];
        else
          f=1.0;
        fconti+=f*s[j-1];
        if(lerr) econti2+=f*f*es[j-1]*es[j-1];
      }
      rltot_conti+=rl[nb];
    }
    fconti*=cdelt1;
    fconti/=rltot_conti;
    if(lerr)
    {
      econti2*=cdelt1*cdelt1;
      econti2/=(rltot_conti*rltot_conti);
    }
    //calculamos flujo promedio en las bandas de absorcion
    double flines=0.0;
    double elines2=0.0;
    double rltot_lines=0.0;
    for (long nb=0; nb < nlines; nb++)
    {
      for (long j=j1[nconti+nb]; j<=j2[nconti+nb]+1; j++)
      {
        double f;
        if (j == j1[nconti+nb])
          f=1.0-d1[nconti+nb];
        else if (j == j2[nconti+nb]+1)
          f=d2[nconti+nb];
        else
          f=1.0;
        flines+=f*s[j-1];
        if(lerr) elines2+=f*f*es[j-1]*es[j-1];
      }
      rltot_lines+=rl[nconti+nb];
    }
    flines*=cdelt1;
    flines/=rltot_lines;
    if(lerr)
    {
      elines2*=cdelt1*cdelt1;
      elines2/=(rltot_lines*rltot_lines);
    }
    //calculamos el indice
    findex=flines/fconti;
    if(lerr)
    {
      eindex=sqrt(fconti*fconti*elines2+flines*flines*econti2)/(fconti*fconti);
    }
    if(logindex) //si medimos en escala logaritmica
    {
      eindex=cte_log_exp*eindex/findex;
      if (findex <= 0.0)
      {
        log_negative=true;
        findex=0.0;
        eindex=0.0;
        return(false);
      }
      findex=2.5*log10(findex);
    }
    // generate output for pyindexf
    if(pyindexf)
    {
      cout << "python> {'f_mean_denominator': " << fconti*smean << ", "
           <<          "'f_mean_numerator': "  << flines*smean << "}"
           << endl;
    }
#ifdef HAVE_CPGPLOT_H
    //=========================================================================
    //dibujamos
    if(plotmode !=0)
    {
      cpgsci(6);
      //flujo medio en las bandas de continuo
      double wlmin=myindex.getldo1(0)*rcvel1;
      double wlmax=myindex.getldo2(0)*rcvel1;
      if (nconti > 1)
      {
        for (long nb=1; nb < nconti; nb++)
        {
          double wl1=myindex.getldo1(nb)*rcvel1;
          double wl2=myindex.getldo2(nb)*rcvel1;
          if(wl1 < wlmin)
            wlmin=wl1;
          if(wl2 > wlmin)
            wlmax=wl2;
        }
      }
      cpgmove_d((wlmin-crval1)/cdelt1+crpix1,fconti*smean);
      cpgdraw_d((wlmax-crval1)/cdelt1+crpix1,fconti*smean);
      //flujo medio en las bandas con lineas
      wlmin=myindex.getldo1(nconti+0)*rcvel1;
      wlmax=myindex.getldo2(nconti+0)*rcvel1;
      if (nlines > 1)
      {
        for (long nb=1; nb < nlines; nb++)
        {
          double wl1=myindex.getldo1(nconti+nb)*rcvel1;
          double wl2=myindex.getldo2(nconti+nb)*rcvel1;
          if(wl1 < wlmin)
            wlmin=wl1;
          if(wl2 > wlmin)
            wlmax=wl2;
        }
      }
      cpgmove_d((wlmin-crval1)/cdelt1+crpix1,flines*smean);
      cpgdraw_d((wlmax-crval1)/cdelt1+crpix1,flines*smean);
      cpgsci(1);
    }//========================================================================
#endif /* HAVE_CPGPLOT_H */
  }
  //***************************************************************************
  //Indices genericos
  //***************************************************************************
  else if ( (myindex.gettype() >= 101) && (myindex.gettype() <= 9999) )
  {
    //protecciones
    if ( contperc >= 0 )
    {
      cout << "ERROR: contperc has not been implemented yet for this index"
           << endl;
      exit(1);
    }
    if ( boundfit != 0 )
    {
      cout << "ERROR: boundfit has not been implemented yet for this index"
           << endl;
      exit(1);
    }
    if ( flattened )
    {
      cout << "ERROR: flattened has not been implemented yet for this index"
           << endl;
      exit(1);
    }
    long nconti = myindex.getnconti();
    long nlines = myindex.getnlines();
    //si no hay errores, hacemos todos iguales a uno para utilizar las mismas
    //formulas
    if(!lerr)
      for (long j = j1min; j <= j2max+1; j++)
        es[j-1]=1.0;
    //calculamos la recta del continuo mediante minimos cuadrados (y=amc*x+bmc)
    //(para la variable x usamos el numero de pixel en lugar de la longitud
    //de onda porque, en principio, seran numeros mas pequenos)
    double sigma2;
    double sum0=0.0;
    double sumx=0.0;
    double sumy=0.0;
    double sumxy=0.0;
    double sumxx=0.0;
    for (long nb=0; nb < nconti; nb++)
    {
      for (long j=j1[nb]; j<=j2[nb]+1; j++)
      {
        double f;
        if (j == j1[nb])
          f=1.0-d1[nb];
        else if (j == j2[nb]+1)
          f=d2[nb];
        else
          f=1.0;
        sigma2=es[j-1]*es[j-1];
        sum0+=f/sigma2;
        sumx+=f*static_cast<double>(j)/sigma2;
        sumy+=f*s[j-1]/sigma2;
        sumxy+=f*static_cast<double>(j)*s[j-1]/sigma2;
        sumxx+=f*static_cast<double>(j)*static_cast<double>(j)/sigma2;
      }
    }
    double deter=sum0*sumxx-sumx*sumx;
    double amc=(sum0*sumxy-sumx*sumy)/deter;
    double bmc=(sumxx*sumy-sumx*sumxy)/deter;
    //calculamos el pseudo-continuo
    double *sc = new double [naxis1];
    double *esc2 = new double [naxis1];
    for (long j = j1min; j <= j2max+1; j++)
    {
      sc[j-1] = amc*static_cast<double>(j)+bmc;
    }
    if(lerr)
    {
      for (long j = j1min; j <= j2max+1; j++)
      {
        esc2[j-1] = 0.0;
        for (long nb=0; nb < nconti; nb++)
        {
          for (long jj=j1[nb]; jj<=j2[nb]+1; jj++)
          {
            sigma2=es[jj-1]*es[jj-1];
            double fdum=(sum0*static_cast<double>(jj)/sigma2-sumx/sigma2)*
                        static_cast<double>(j)/deter+
                        (sumxx/sigma2-
                         sumx*static_cast<double>(jj)/sigma2)/deter;
            esc2[j-1]+=fdum*fdum*sigma2;
          }
        }
      }
    }
    //recorremos las bandas con lineas
    double tc=0.0;
    double etc=0.0;
    double sumrl=0.0;
    for (long nb=0; nb < nlines; nb++)
    {
      double factor = myindex.getfactor(nb);
      for (long j=j1[nb+nconti]; j<=j2[nb+nconti]+1; j++)
      {
        double f;
        if (j == j1[nb+nconti])
          f=1.0-d1[nb+nconti];
        else if (j == j2[nb+nconti]+1)
          f=d2[nb+nconti];
        else
          f=1.0;
        tc+=f*factor*s[j-1]/sc[j-1];
        if(lerr)
        {
          etc+=f*f*factor*factor*(s[j-1]*s[j-1]*esc2[j-1]+
               sc[j-1]*sc[j-1]*es[j-1]*es[j-1])/
               (sc[j-1]*sc[j-1]*sc[j-1]*sc[j-1]);
          for (long nbb=0; nbb < nlines; nbb++)
          {
            double factorr = myindex.getfactor(nbb);
            for (long jj=j1[nbb+nconti]; jj<=j2[nbb+nconti]+1; jj++)
            {
              if ( (nbb != nb) || (jj != j) )
              {
                double ff;
                if (jj == j1[nbb+nconti])
                  ff=1.0-d1[nbb+nconti];
                else if (jj == j2[nbb+nconti]+1)
                  ff=d2[nbb+nconti];
                else
                  ff=1.0;
                double cov=static_cast<double>(j)*static_cast<double>(jj)*
                           (sum0*sum0*sumxx-sum0*sumx*sumx)/(deter*deter)+
                           (static_cast<double>(j)+static_cast<double>(jj))*
                           (sumx*sumx*sumx-sum0*sumx*sumxx)/(deter*deter)+
                           (sumxx*sumxx*sum0-sumxx*sumx*sumx)/(deter*deter);
                etc+=factor*factorr*ff*f*s[j-1]*s[jj-1]*cov/
                     (sc[j-1]*sc[j-1]*sc[j-1]*sc[j-1]);
              }
            }
          }
        }
      }
      sumrl+=factor*rl[nb+nconti];
    }
    if(logindex) //indice generico medido en magnitudes
    {
      if (tc*cdelt1/sumrl <= 0.0)
      {
        log_negative=true;
        findex=0.0;
        eindex=0.0;
        return(false);
      }
      findex=-2.5*log10(tc*cdelt1/sumrl);
      if(lerr) eindex=cte_log_exp/pow(10,-0.4*findex)*sqrt(etc)*cdelt1/sumrl;
    }
    else //indice generico medido como indice atomico
    {
      findex=(sumrl-tc*cdelt1)/rcvel1;
      if(lerr) eindex=sqrt(etc)*cdelt1/rcvel1;
    }
    delete [] sc;
    delete [] esc2;
    if(pyindexf)
    {
      const double wla=wvmin*rcvel1;
      const double wlb=wvmax*rcvel1;
      const double xca=(wla-crval1)/cdelt1+crpix1;
      const double xcb=(wlb-crval1)/cdelt1+crpix1;
      const double yduma=amc*static_cast<double>(xca)+bmc;
      const double ydumb=amc*static_cast<double>(xcb)+bmc;
      cout << "python> {'w_min_cont': " << wla << ", "
                       "'w_max_cont': " << wlb << "}" << endl;
      cout << "python> {'f_min_cont': " << yduma*smean << ", "
                       "'f_max_cont': " << ydumb*smean << "}" << endl;
    }
#ifdef HAVE_CPGPLOT_H
    //=========================================================================
    //dibujamos
    if(plotmode !=0)
    {
      cpgsci(6);
      const double wla=wvmin*rcvel1;
      const double wlb=wvmax*rcvel1;
      const double xca=(wla-crval1)/cdelt1+crpix1;
      const double xcb=(wlb-crval1)/cdelt1+crpix1;
      const double yduma=amc*static_cast<double>(xca)+bmc;
      const double ydumb=amc*static_cast<double>(xcb)+bmc;
      cpgmove_d(xca,yduma*smean);
      cpgdraw_d(xcb,ydumb*smean);
      cpgsci(1);
    }//=========================================================================
#endif /* HAVE_CPGPLOT_H */  
  }
  //***************************************************************************
  //Indices pendiente
  //***************************************************************************
  else if ( (myindex.gettype() >= -99) && (myindex.gettype() <= -2) )
  {
    //protecciones
    if ( contperc >= 0 )
    {
      cout << "ERROR: contperc has not been implemented yet for this index"
           << endl;
      exit(1);
    }
    if ( boundfit != 0 )
    {
      cout << "ERROR: boundfit has not been implemented yet for this index"
           << endl;
      exit(1);
    }
    if ( flattened )
    {
      cout << "ERROR: flattened has not been implemented yet for this index"
           << endl;
      exit(1);
    }
    long nconti = myindex.getnconti();
    //si no hay errores, hacemos todos iguales a uno para utilizar las mismas
    //formulas
    if(!lerr)
      for (long j = j1min; j <= j2max+1; j++)
        es[j-1]=1.0;
    //calculamos la recta del continuo mediante minimos cuadrados (y=amc*x+bmc)
    //(para la variable x usamos el numero de pixel en lugar de la longitud
    //de onda porque, en principio, seran numeros mas pequenos)
    double sigma2;
    double sum0=0.0;
    double sumx=0.0;
    double sumy=0.0;
    double sumxy=0.0;
    double sumxx=0.0;
    for (long nb=0; nb < nconti; nb++)
    {
      for (long j=j1[nb]; j<=j2[nb]+1; j++)
      {
        double f;
        if (j == j1[nb])
          f=1.0-d1[nb];
        else if (j == j2[nb]+1)
          f=d2[nb];
        else
          f=1.0;
        sigma2=es[j-1]*es[j-1];
        sum0+=f/sigma2;
        sumx+=f*static_cast<double>(j)/sigma2;
        sumy+=f*s[j-1]/sigma2;
        sumxy+=f*static_cast<double>(j)*s[j-1]/sigma2;
        sumxx+=f*static_cast<double>(j)*static_cast<double>(j)/sigma2;
      }
    }
    double deter=sum0*sumxx-sumx*sumx;
    double amc=(sum0*sumxy-sumx*sumy)/deter;
    double bmc=(sumxx*sumy-sumx*sumxy)/deter;
    double xa=0.0,xb=0.0;
    for (long nb=0; nb < nconti; nb++)
    {
      double ca = myindex.getldo1(nb)*rcvel1;
      double cb = myindex.getldo2(nb)*rcvel1;
      double c3 = (ca-wlmin)/cdelt1+1.0;
      double c4 = (cb-wlmin)/cdelt1;
      if (nb==0)
      {
        xa=(c3+c4)/2.0;
        xb=(c3+c4)/2.0;
      }
      else
      {
        double xa_,xb_;
        xa_=(c3+c4)/2.0;
        xb_=(c3+c4)/2.0;
        if(xa_ > xa) xa=xa_;
        if(xb_ < xb) xb=xb_;
      }
    }
    double flujoa=amc*xa+bmc;
    double flujob=amc*xb+bmc;
    findex=flujoa/flujob;
    if(lerr)
    {
      double covar_aa=(sum0*sum0*sumxx-sum0*sumx*sumx)/deter/deter;
      double covar_ab=(sumx*sumx*sumx-sum0*sumx*sumxx)/deter/deter;
      double covar_bb=(sumxx*sumxx*sum0-sumxx*sumx*sumx)/deter/deter;
      eindex=sqrt(bmc*bmc*covar_aa+amc*amc*covar_bb-2.0*amc*bmc*covar_ab)*
             (xa-xb)/((amc*xb+bmc)*(amc*xb+bmc));
    }
    if(logindex) //indice pendiente medido en magnitudes
    {
      eindex=cte_log_exp*eindex/findex;
      if (findex <= 0.0)
      {
        log_negative=true;
        findex=0.0;
        eindex=0.0;
        return(false);
      }
      findex=2.5*log10(findex);
    }
    if(pyindexf)
    {
      const double wla=wvmin*rcvel1;
      const double wlb=wvmax*rcvel1;
      const double xca=(wla-crval1)/cdelt1+crpix1;
      const double xcb=(wlb-crval1)/cdelt1+crpix1;
      const double yduma=amc*static_cast<double>(xca)+bmc;
      const double ydumb=amc*static_cast<double>(xcb)+bmc;
      cout << "python> {'w_min_cont': " << wla << ", "
                       "'w_max_cont': " << wlb << "}" << endl;
      cout << "python> {'f_min_cont': " << yduma*smean << ", "
                       "'f_max_cont': " << ydumb*smean << "}" << endl;
    }
#ifdef HAVE_CPGPLOT_H
    //=========================================================================
    //dibujamos
    if(plotmode !=0)
    {
      cpgsci(6);
      const double wla=wvmin*rcvel1;
      const double wlb=wvmax*rcvel1;
      const double xca=(wla-crval1)/cdelt1+crpix1;
      const double xcb=(wlb-crval1)/cdelt1+crpix1;
      const double yduma=amc*static_cast<double>(xca)+bmc; 
      const double ydumb=amc*static_cast<double>(xcb)+bmc; 
      cpgmove_d(xca,yduma*smean);
      cpgdraw_d(xcb,ydumb*smean);
      cpgsci(1);
    }//========================================================================
#endif /* HAVE_CPGPLOT_H */  
  }
  //***************************************************************************
  //otros indices futuros
  //***************************************************************************
  else
  {
    cout << "FATAL ERROR: index type=" << myindex.gettype()
         << " is not valid" << endl;
    exit(1);
  }

  //---------------------------------------------------------------------------
  //liberamos memoria y retornamos con exito
  delete [] ca;
  delete [] cb;
  delete [] c3;
  delete [] c4;
  delete [] j1;
  delete [] j2;
  delete [] d1;
  delete [] d2;
  delete [] rl;
  delete [] rg;
  delete [] ifchan;
  delete [] s;
  delete [] es;
  return(true);
}
