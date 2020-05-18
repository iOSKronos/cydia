// -*- mode: cpp; mode: fold -*-
// Description								/*{{{*/
// $Id: http.cc,v 1.59 2004/05/08 19:42:35 mdz Exp $
/* ######################################################################

   HTTP Acquire Method - This is the HTTP aquire method for APT.
   
   It uses HTTP/1.1 and many of the fancy options there-in, such as
   pipelining, range, if-range and so on. 

   It is based on a doubly buffered select loop. A groupe of requests are 
   fed into a single output buffer that is constantly fed out the 
   socket. This provides ideal pipelining as in many cases all of the
   requests will fit into a single packet. The input socket is buffered 
   the same way and fed into the fd for the file (may be a pipe in future).
   
   This double buffering provides fairly substantial transfer rates,
   compared to wget the http method is about 4% faster. Most importantly,
   when HTTP is compared with FTP as a protocol the speed difference is
   huge. In tests over the internet from two sites to llug (via ATM) this
   program got 230k/s sustained http transfer rates. FTP on the other 
   hand topped out at 170k/s. That combined with the time to setup the
   FTP connection makes HTTP a vastly superior protocol.
      
   ##################################################################### */
									/*}}}*/
// Include Files							/*{{{*/
#include <apt-pkg/fileutl.h>
#include <apt-pkg/acquire-method.h>
#include <apt-pkg/error.h>
#include <apt-pkg/hashes.h>
#include <apt-pkg/netrc.h>

#include <sys/sysctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <utime.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <iostream>
#include <map>
#include <set>
#include <apti18n.h>


// Internet stuff
#include <netdb.h>
#include <arpa/inet.h>

#include <dlfcn.h>
#include <lockdown.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CFNetwork/CFNetwork.h>
#include <SystemConfiguration/SystemConfiguration.h>

#include "config.h"
#include "http.h"
									/*}}}*/
using namespace std;

static CFStringRef Firmware_;
static const char *Machine_;
static CFStringRef UniqueID_;

void CfrsError(const char *name, CFReadStreamRef rs) {
    CFStreamError se = CFReadStreamGetError(rs);

    if (se.domain == kCFStreamErrorDomainCustom) {
    } else if (se.domain == kCFStreamErrorDomainPOSIX) {
        _error->Error("POSIX: %s", strerror(se.error));
    } else if (se.domain == kCFStreamErrorDomainMacOSStatus) {
        _error->Error("MacOSStatus: %ld", se.error);
    } else if (se.domain == kCFStreamErrorDomainNetDB) {
        _error->Error("NetDB: %s %s", name, gai_strerror(se.error));
    } else if (se.domain == kCFStreamErrorDomainMach) {
        _error->Error("Mach: %ld", se.error);
    } else if (se.domain == kCFStreamErrorDomainHTTP) {
        switch (se.error) {
            case kCFStreamErrorHTTPParseFailure:
                _error->Error("Parse failure");
            break;

            case kCFStreamErrorHTTPRedirectionLoop:
                _error->Error("Redirection loop");
            break;

            case kCFStreamErrorHTTPBadURL:
                _error->Error("Bad URL");
            break;

            default:
                _error->Error("Unknown HTTP error: %ld", se.error);
            break;
        }
    } else if (se.domain == kCFStreamErrorDomainSOCKS) {
        _error->Error("SOCKS: %ld", se.error);
    } else if (se.domain == kCFStreamErrorDomainSystemConfiguration) {
        _error->Error("SystemConfiguration: %ld", se.error);
    } else if (se.domain == kCFStreamErrorDomainSSL) {
        _error->Error("SSL: %ld", se.error);
    } else {
        _error->Error("Domain #%ld: %ld", se.domain, se.error);
    }
}

string HttpMethod::FailFile;
int HttpMethod::FailFd = -1;
time_t HttpMethod::FailTime = 0;
unsigned long PipelineDepth = 10;
unsigned long TimeOut = 120;
bool AllowRedirect = false;
bool Debug = false;
URI Proxy;

static const CFOptionFlags kNetworkEvents =
    kCFStreamEventOpenCompleted |
    kCFStreamEventHasBytesAvailable |
    kCFStreamEventEndEncountered |
    kCFStreamEventErrorOccurred |
0;

static void CFReadStreamCallback(CFReadStreamRef stream, CFStreamEventType event, void *arg) {
    switch (event) {
        case kCFStreamEventOpenCompleted:
        break;

        case kCFStreamEventHasBytesAvailable:
        case kCFStreamEventEndEncountered:
            *reinterpret_cast<int *>(arg) = 1;
            CFRunLoopStop(CFRunLoopGetCurrent());
        break;

        case kCFStreamEventErrorOccurred:
            *reinterpret_cast<int *>(arg) = -1;
            CFRunLoopStop(CFRunLoopGetCurrent());
        break;
    }
}

/* http://lists.apple.com/archives/Macnetworkprog/2006/Apr/msg00014.html */
int CFReadStreamOpen(CFReadStreamRef stream, double timeout) {
    CFStreamClientContext context;
    int value(0);

    memset(&context, 0, sizeof(context));
    context.info = &value;

    if (CFReadStreamSetClient(stream, kNetworkEvents, CFReadStreamCallback, &context)) {
        CFReadStreamScheduleWithRunLoop(stream, CFRunLoopGetCurrent(), kCFRunLoopCommonModes);
        if (CFReadStreamOpen(stream))
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, timeout, false);
        else
            value = -1;
        CFReadStreamSetClient(stream, kCFStreamEventNone, NULL, NULL);
    }

    return value;
}

// HttpMethod::SigTerm - Handle a fatal signal				/*{{{*/
// ---------------------------------------------------------------------
/* This closes and timestamps the open file. This is neccessary to get 
   resume behavoir on user abort */
void HttpMethod::SigTerm(int)
{
   if (FailFd == -1)
      _exit(100);
   close(FailFd);
   
   // Timestamp
   struct utimbuf UBuf;
   UBuf.actime = FailTime;
   UBuf.modtime = FailTime;
   utime(FailFile.c_str(),&UBuf);
   
   _exit(100);
}
									/*}}}*/
// HttpMethod::Configuration - Handle a configuration message		/*{{{*/
// ---------------------------------------------------------------------
/* We stash the desired pipeline depth */
bool HttpMethod::Configuration(string Message)
{
   if (pkgAcqMethod::Configuration(Message) == false)
      return false;
   
   AllowRedirect = _config->FindB("Acquire::http::AllowRedirect",true);
   TimeOut = _config->FindI("Acquire::http::Timeout",TimeOut);
   PipelineDepth = _config->FindI("Acquire::http::Pipeline-Depth",
				  PipelineDepth);
   Debug = _config->FindB("Debug::Acquire::http",false);
   
   return true;
}
									/*}}}*/
// HttpMethod::Loop - Main loop						/*{{{*/
// ---------------------------------------------------------------------
/* */
int HttpMethod::Loop()
{
   typedef vector<string> StringVector;
   typedef vector<string>::iterator StringVectorIterator;
   map<string, StringVector> Redirected;

   signal(SIGTERM,SigTerm);
   signal(SIGINT,SigTerm);
   
   std::set<std::string> cached;
   
   int FailCounter = 0;
   while (1)
   {      
      // We have no commands, wait for some to arrive
      if (Queue == 0)
      {
	 if (WaitFd(STDIN_FILENO) == false)
	    return 0;
      }
      
      /* Run messages, we can accept 0 (no message) if we didn't
         do a WaitFd above.. Otherwise the FD is closed. */
      int Result = Run(true);
      if (Result != -1 && (Result != 0 || Queue == 0))
	 return 100;

      if (Queue == 0)
	 continue;

      CFStringEncoding se = kCFStringEncodingUTF8;

      char *url = strdup(Queue->Uri.c_str());
    url:
      URI uri = std::string(url);
      std::string hs = uri.Host;

      if (cached.find(hs) != cached.end()) {
         _error->Error("Cached Failure");
         Fail(true);
         free(url);
         FailCounter = 0;
         continue;
      }

      std::string urs = uri;

      for (;;) {
         size_t bad = urs.find_first_of("+");
         if (bad == std::string::npos)
            break;
         // XXX: generalize
         urs = urs.substr(0, bad) + "%2b" + urs.substr(bad + 1);
      }

      CFStringRef sr = CFStringCreateWithCString(kCFAllocatorDefault, urs.c_str(), se);
      CFURLRef ur = CFURLCreateWithString(kCFAllocatorDefault, sr, NULL);
      CFRelease(sr);
      CFHTTPMessageRef hm = CFHTTPMessageCreateRequest(kCFAllocatorDefault, CFSTR("GET"), ur, kCFHTTPVersion1_1);
      CFRelease(ur);

      struct stat SBuf;
      if (stat(Queue->DestFile.c_str(), &SBuf) >= 0 && SBuf.st_size > 0) {
         sr = CFStringCreateWithFormat(kCFAllocatorDefault, NULL, CFSTR("bytes=%li-"), (long) SBuf.st_size - 1);
         CFHTTPMessageSetHeaderFieldValue(hm, CFSTR("Range"), sr);
         CFRelease(sr);

         sr = CFStringCreateWithCString(kCFAllocatorDefault, TimeRFC1123(SBuf.st_mtime).c_str(), se);
         CFHTTPMessageSetHeaderFieldValue(hm, CFSTR("If-Range"), sr);
         CFRelease(sr);

         CFHTTPMessageSetHeaderFieldValue(hm, CFSTR("Cache-Control"), CFSTR("no-cache"));
      } else if (Queue->LastModified != 0) {
         sr = CFStringCreateWithCString(kCFAllocatorDefault, TimeRFC1123(Queue->LastModified).c_str(), se);
         CFHTTPMessageSetHeaderFieldValue(hm, CFSTR("If-Modified-Since"), sr);
         CFRelease(sr);

         CFHTTPMessageSetHeaderFieldValue(hm, CFSTR("Cache-Control"), CFSTR("no-cache"));
      } else
         CFHTTPMessageSetHeaderFieldValue(hm, CFSTR("Cache-Control"), CFSTR("max-age=0"));

      if (Firmware_ != NULL)
         CFHTTPMessageSetHeaderFieldValue(hm, CFSTR("X-Firmware"), Firmware_);

      sr = CFStringCreateWithCString(kCFAllocatorDefault, Machine_, se);
      CFHTTPMessageSetHeaderFieldValue(hm, CFSTR("X-Machine"), sr);
      CFRelease(sr);

      if (UniqueID_ != NULL)
         CFHTTPMessageSetHeaderFieldValue(hm, CFSTR("X-Unique-ID"), UniqueID_);

      CFHTTPMessageSetHeaderFieldValue(hm, CFSTR("User-Agent"), CFSTR("Telesphoreo APT-HTTP/1.0.592"));

      CFReadStreamRef rs = CFReadStreamCreateForHTTPRequest(kCFAllocatorDefault, hm);
      CFRelease(hm);

#define _kCFStreamPropertyReadTimeout CFSTR("_kCFStreamPropertyReadTimeout")
#define _kCFStreamPropertyWriteTimeout CFSTR("_kCFStreamPropertyWriteTimeout")
#define _kCFStreamPropertySocketImmediateBufferTimeOut CFSTR("_kCFStreamPropertySocketImmediateBufferTimeOut")

      /*SInt32 to(TimeOut);
      CFNumberRef nm(CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &to));*/
      double to(TimeOut);
      CFNumberRef nm(CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &to));

      CFReadStreamSetProperty(rs, _kCFStreamPropertyReadTimeout, nm);
      CFReadStreamSetProperty(rs, _kCFStreamPropertyWriteTimeout, nm);
      CFReadStreamSetProperty(rs, _kCFStreamPropertySocketImmediateBufferTimeOut, nm);
      CFRelease(nm);

      CFDictionaryRef dr = SCDynamicStoreCopyProxies(NULL);
      CFReadStreamSetProperty(rs, kCFStreamPropertyHTTPProxy, dr);
      CFRelease(dr);

      //CFReadStreamSetProperty(rs, kCFStreamPropertyHTTPShouldAutoredirect, kCFBooleanTrue);
      CFReadStreamSetProperty(rs, kCFStreamPropertyHTTPAttemptPersistentConnection, kCFBooleanTrue);

      FetchResult Res;
      CFIndex rd;
      UInt32 sc;

      uint8_t data[10240];
      size_t offset = 0;

      Status("Connecting to %s", hs.c_str());

      switch (CFReadStreamOpen(rs, to)) {
         case -1:
            CfrsError("Open", rs);
         goto fail;

         case 0:
            _error->Error("Host Unreachable");
            cached.insert(hs);
         goto fail;

         case 1:
            /* success */
         break;

         fail:
            Fail(true);
         goto done;
      }

      rd = CFReadStreamRead(rs, data, sizeof(data));

      if (rd == -1) {
         CfrsError(uri.Host.c_str(), rs);
         cached.insert(hs);
         Fail(true);
         goto done;
      }

      Res.Filename = Queue->DestFile;

      hm = (CFHTTPMessageRef) CFReadStreamCopyProperty(rs, kCFStreamPropertyHTTPResponseHeader);
      sc = CFHTTPMessageGetResponseStatusCode(hm);

      if (sc == 301 || sc == 302) {
         sr = CFHTTPMessageCopyHeaderFieldValue(hm, CFSTR("Location"));
         if (sr == NULL) {
            Fail();
            goto done_;
         } else {
            size_t ln = CFStringGetLength(sr) + 1;
            free(url);
            url = static_cast<char *>(malloc(ln));

            if (!CFStringGetCString(sr, url, ln, se)) {
               Fail();
               goto done_;
            }

            CFRelease(sr);
            goto url;
         }
      }

      sr = CFHTTPMessageCopyHeaderFieldValue(hm, CFSTR("Content-Range"));
      if (sr != NULL) {
         size_t ln = CFStringGetLength(sr) + 1;
         char cr[ln];

         if (!CFStringGetCString(sr, cr, ln, se)) {
            Fail();
            goto done_;
         }

         CFRelease(sr);

         if (sscanf(cr, "bytes %lu-%*u/%lu", &offset, &Res.Size) != 2) {
	    _error->Error(_("The HTTP server sent an invalid Content-Range header"));
            Fail();
            goto done_;
         }

         if (offset > Res.Size) {
	    _error->Error(_("This HTTP server has broken range support"));
            Fail();
            goto done_;
         }
      } else {
         sr = CFHTTPMessageCopyHeaderFieldValue(hm, CFSTR("Content-Length"));
         if (sr != NULL) {
            Res.Size = CFStringGetIntValue(sr);
            CFRelease(sr);
         }
      }

      time(&Res.LastModified);

      sr = CFHTTPMessageCopyHeaderFieldValue(hm, CFSTR("Last-Modified"));
      if (sr != NULL) {
         size_t ln = CFStringGetLength(sr) + 1;
         char cr[ln];

         if (!CFStringGetCString(sr, cr, ln, se)) {
            Fail();
            goto done_;
         }

         CFRelease(sr);

         if (!StrToTime(cr, Res.LastModified)) {
	    _error->Error(_("Unknown date format"));
            Fail();
            goto done_;
         }
      }

      if (sc < 200 || sc >= 300 && sc != 304) {
         sr = CFHTTPMessageCopyResponseStatusLine(hm);

         size_t ln = CFStringGetLength(sr) + 1;
         char cr[ln];

         if (!CFStringGetCString(sr, cr, ln, se)) {
            Fail();
            goto done;
         }

         CFRelease(sr);

         _error->Error("%s", cr);

         Fail();
         goto done_;
      }

      CFRelease(hm);

      if (sc == 304) {
         unlink(Queue->DestFile.c_str());
         Res.IMSHit = true;
         Res.LastModified = Queue->LastModified;
	 URIDone(Res);
      } else {
         Hashes hash;

         File = new FileFd(Queue->DestFile, FileFd::WriteAny);
         if (_error->PendingError() == true) {
            delete File;
            File = NULL;
            Fail();
            goto done;
         }

         FailFile = Queue->DestFile;
         FailFile.c_str();   // Make sure we dont do a malloc in the signal handler
         FailFd = File->Fd();
         FailTime = Res.LastModified;

         Res.ResumePoint = offset;
         ftruncate(File->Fd(), offset);

         if (offset != 0) {
            lseek(File->Fd(), 0, SEEK_SET);
            if (!hash.AddFD(File->Fd(), offset)) {
	       _error->Errno("read", _("Problem hashing file"));
               delete File;
               File = NULL;
               Fail();
               goto done;
            }
         }

         lseek(File->Fd(), 0, SEEK_END);

	 URIStart(Res);

         read: if (rd == -1) {
            CfrsError("rd", rs);
            Fail(true);
         } else if (rd == 0) {
	    if (Res.Size == 0)
	       Res.Size = File->Size();

	    struct utimbuf UBuf;
	    time(&UBuf.actime);
	    UBuf.actime = Res.LastModified;
	    UBuf.modtime = Res.LastModified;
	    utime(Queue->DestFile.c_str(), &UBuf);

	    Res.TakeHashes(hash);
	    URIDone(Res);
         } else {
	    hash.Add(data, rd);

            uint8_t *dt = data;
            while (rd != 0) {
               int sz = write(File->Fd(), dt, rd);

               if (sz == -1) {
                   delete File;
                   File = NULL;
                   Fail();
                   goto done;
               }

               dt += sz;
               rd -= sz;
            }

            rd = CFReadStreamRead(rs, data, sizeof(data));
            goto read;
         }
      }

     goto done;
    done_:
      CFRelease(hm);
    done:
      CFReadStreamClose(rs);
      CFRelease(rs);
      free(url);

      FailCounter = 0;
   }
   
   return 0;
}
									/*}}}*/

int main()
{
   setlocale(LC_ALL, "");
   // ignore SIGPIPE, this can happen on write() if the socket
   // closes the connection (this is dealt with via ServerDie())
   signal(SIGPIPE, SIG_IGN);

   HttpMethod Mth;

    size_t size;
    sysctlbyname("hw.machine", NULL, &size, NULL, 0);
    char *machine = new char[size];
    sysctlbyname("hw.machine", machine, &size, NULL, 0);
    Machine_ = machine;

    const char *path = "/System/Library/CoreServices/SystemVersion.plist";
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, (uint8_t *) path, strlen(path), false);

    CFPropertyListRef plist; {
        CFReadStreamRef stream = CFReadStreamCreateWithFile(kCFAllocatorDefault, url);
        CFReadStreamOpen(stream);
        plist = CFPropertyListCreateFromStream(kCFAllocatorDefault, stream, 0, kCFPropertyListImmutable, NULL, NULL);
        CFReadStreamClose(stream);
    }

    CFRelease(url);

    if (plist != NULL) {
        Firmware_ = (CFStringRef) CFRetain(CFDictionaryGetValue((CFDictionaryRef) plist, CFSTR("ProductVersion")));
        CFRelease(plist);
    }

    if (UniqueID_ == NULL)
    if (void *libMobileGestalt = dlopen("/usr/lib/libMobileGestalt.dylib", RTLD_GLOBAL | RTLD_LAZY))
    if (CFStringRef (*$MGCopyAnswer)(CFStringRef) = (CFStringRef (*)(CFStringRef)) dlsym(libMobileGestalt, "MGCopyAnswer"))
        UniqueID_ = $MGCopyAnswer(CFSTR("UniqueDeviceID"));

    if (UniqueID_ == NULL)
    if (void *lockdown = lockdown_connect()) {
        UniqueID_ = lockdown_copy_value(lockdown, NULL, kLockdownUniqueDeviceIDKey);
        lockdown_disconnect(lockdown);
    }

   return Mth.Loop();
}

