// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/// $Id: http.h,v 1.12 2002/04/18 05:09:38 jgg Exp $
// $Id: http.h,v 1.12 2002/04/18 05:09:38 jgg Exp $
/* ######################################################################

   HTTP Acquire Method - This is the HTTP aquire method for APT.

   ##################################################################### */
									/*}}}*/

#ifndef APT_HTTP_H
#define APT_HTTP_H

#define MAXLEN 360

#include <iostream>

using std::cout;
using std::endl;

class HttpMethod : public pkgAcqMethod
{
   virtual bool Configuration(string Message);
   
   // In the event of a fatal signal this file will be closed and timestamped.
   static string FailFile;
   static int FailFd;
   static time_t FailTime;
   static void SigTerm(int);
   
   string NextURI;
   
   public:
   FileFd *File;
   
   int Loop();
   
   HttpMethod() : pkgAcqMethod("1.2",Pipeline | SendConfig) 
   {
      File = 0;
   };
};

#endif

