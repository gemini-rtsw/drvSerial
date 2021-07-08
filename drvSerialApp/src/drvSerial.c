/*    drvSerial.c */
/*    $Id: drvSerial.c,v 1.3 2005/05/12 00:37:47 intdev Exp $ */
/*
 *      EPICS STDIO Driver Support (for serial devices)
 *
 *      Author:         Jeffrey O. Hill (LANL)
 *         johill@lanl.gov
 *         (505) 665 1831
 *         (for the KECK Astronomy Center)
 *
 *      Date:           6-6-95
 *
 * Modification Log:
 * -----------------
 *
 *
 *      Revision 2016-06-01 mdw
 *      Changed ioctl() FIOFLUSH call to fflush(), and
 *      changed ioctl() FIOCANCEL call to tcflush(), since these
 *      are specific to vxWorks. Also made other changes to make 
 *      the driver OSI compliant.
 *
 *     The revision numbering below is confusing.....
 *
 *    $Log: drvSerial.c,v $
 *    Revision 1.3  2005/05/12 00:37:47  intdev
 *    Works for if cryo - wdahl
 *    
 *    Revision 1.2  2005/01/13 00:29:34  wdahl
 *    Added epicsExprtAddress calls and include drvSup.h for 3.14.6
 *    
 *    Revision 1.1.1.1  2005/01/11 02:38:31  wdahl
 *    unixIoc for EPICS 3.14.6
 *    
 *    Revision 1.1  2003/04/02 02:56:37  ktsubota
 *    Initial insertion
 *    
 *    Revision 1.1  2003/01/14 00:24:03  ktsubota
 *    Initial insertion
 *    
 *    Revision 1.2  2002/09/05 21:07:18  ahoney
 *    Most recent version updated by J. Hill.
 *    
 *    Revision 1.6  1999/12/23 16:55:38  hill
 *    fixed warnings drvAbdf1.c
 *
 *    Revision 1.5  1999/09/14 15:17:54  hill
 *    increased queue size
 *
 *    Revision 1.4  1998/02/11 23:26:08  aptdvl
 *    1st production release
 *
 * Revision 1.4  1995/09/08  20:27:57  jhill
 * dont copy all unused bytes in buffer optimization
 *
 * Revision 1.3  1995/09/08  19:54:10  jhill
 * improved conversion between array index and enum priority
 *
 * Revision 1.2  1995/08/28  02:37:14  jhill
 * use VX_FP_TASK
 *
 * Revision 1.1  1995/08/24  07:12:41  jhill
 * installed into cvs
 *
 *
 *    FUNCTIONAL REQUIREMENTS
 *
 * .01   This source will shield applications from the complexities
 *   of non-blocking io. The database scan tasks must not be blocked
 *   by system io calls. Instead, the tasks created by this source
 *   are allowed to block.
 *
 * .02  This source will provide for framing of the input
 *   stream into individual messages from the attached serial devices 
 *   to higher level applications.
 *
 * .03    This source shall will detect when the link goes down and
 *    periodically attempt to restart the link if it is down.
 *
 * .04   This source will provide for multi-priority request queuing
 *   so that higher priority requests are processed first when
 *   communicating with slow devices
 *
 * NOTES:
 *
 * .01    I do _not_ turn off stdio buffering here because an fflush()
 *   call is made each time that the request queue is drained. 
 *   Therefore stdio buffer should not disable priority based 
 *   request queuing.
 *
 * .02   I open two file streams (one for the read task and one for 
 *   the write task). WRS indicates that file pointers should 
 *   be private to individual tasks because the stdio library isnt 
 *   thread safe. This requires two file descriptors on one device 
 *   (one for reading only and one for writing only). I suspect 
 *   that most vxWorks serial device drivers will support this.
 *  When the connection is reset then I use FIOCANCEL ioctl on the
 *  read/write file descriptors involved in order to force both 
 *  tasks to reopen their files.
 */

/*
 * ANSI C
 */
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
//#include <sys/ioctl.h>
#include <errno.h>
//#include <sys/termios.h>
#include <termios.h>

/*
 * EPICS
 */
#include <epicsInterrupt.h>
#include <epicsRingBytes.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsThread.h>
#include <epicsTime.h>
#include <epicsAssert.h>
#include <epicsPrint.h>
#include <dbAccess.h>
#include "epicsExport.h"
#include "iocsh.h"

#include <ellLib.h>      /* requires stdlib.h */
#include <drvSup.h>
#include <bucketLib.h>
#include <freeList.h>

/* S_dev_noMemory changed to be S_db_noMemory from dbAccessDefs.h included
 * by dbAccess.h
 */


#define NONE            (-1)    /* for times when NULL won't do */
#define EOS             '\0'    /* C string terminator */

/* spurious timeout defines */
#if 0
#define NO_WAIT         0.0
#define WAIT_FOREVER    (3600.0)  /* "forever" = 1 hour apparently */
#endif



/* return status values */
#define OK              0
#define ERROR           (-1)


#define drvSerialGlobal /* allocate space for externals here */
#include "drvSerial.h"

#define WAIT_N_SEC(N) {epicsThreadSleep(N);}

#define tp_drvSerialPriority  epicsThreadPriorityMedium   
#define tp_drvSerialStackSize epicsThreadGetStackSize(epicsThreadStackMedium)

/*
 * maximum size of the request and response queue
 * respectively
 */
#define requestQueueQuota 100
#define responseQueueQuota 100

typedef enum {dsReq, dsRes, dsResv, dsLimbo, dsFree}dsQueue;
typedef struct {
   ELLNODE node;
   union {
      drvSerialResponse res;
      drvSerialRequest req;
   }shr;
   char queue;
   char pri;
}freeListItem;


/*
 * The limbo queue here is where items are stored prior to being
 * added to an operational queue so that we dont lose them if the
 * task is deleted (I use task delete safe mutex lock).
 */
typedef struct drvSerialParmTag
{
   ELLNODE node;
   char pName[512];
   epicsMutexId mutexSem;
   epicsEventId writeQueueSem;
   epicsEventId readQueueSem;
   ELLLIST requestQueues[dspHighest - dspLowest + 1];
   ELLLIST reserveQueues[dspHighest - dspLowest + 1];
   ELLLIST respQueue;
   ELLLIST   limboQueue; 
   drvSerialParseInput *pInputParser;
   void *pFreeListPVT;
   void *pAppPrivate;
   FILE *pRF; /* only touched by the read task */
   FILE *pWF; /* only touched by the write task */
   ELLLIST *pHighestPriReqQue;
   epicsThreadId rdTaskId;
   epicsThreadId wtTaskId;
   unsigned readCanceled:1;
   unsigned writeCanceled:1;
} drvSerialParm;

/*
 * drvSerial.c globals
 */
struct
{
   char tmpName[512];
   ELLLIST list;
   epicsMutexId lock;
   BUCKET *pBucket;
} devNameTbl;

/*
 * driver support entry table
 */
typedef long drvInitFunc_t(void);
typedef long drvSerialReport_t(int level);
drvSerialReport_t drvSerialReport;
drvInitFunc_t drvSerialInit;

drvet drvSerial =
{
   2L,
   drvSerialReport,
   drvSerialInit
};
epicsExportAddress (drvet, drvSerial);

freeListItem    *drvSerialFetchFreeItem (drvSerialParm *pDev);
void drvSerialDisposeFreeItem (drvSerialParm *pDev, freeListItem * pItem);
void drvSerialLinkReset (drvSerialParm * pDev);
int sendAllRequests (drvSerialParm *pDev);
drvSerialParm *drvSerialTestExist (const char *pName);
int drvSerialLinkOpen (drvSerialParm * pDev);
int drvSerialWrite (drvSerialParm * pDev);
int drvSerialRead (drvSerialParm * pDev);
freeListItem *fetchHighestPriorityRequest (drvSerialParm *pDev);

void drvSerialCopyResponse( drvSerialResponse *pDest, const drvSerialResponse *pSrc);
void drvSerialCopyRequest( drvSerialRequest *pDest, const drvSerialRequest *pSrc);

/*
 * drvSerialCopyRequest()
 *
 * Copy a source request into a destination request
 * (dont copy unused bytes)
 */
void drvSerialCopyRequest(drvSerialRequest *pDest, const drvSerialRequest *pSrc)
{
   pDest->pCB = pSrc->pCB;
   pDest->pAppPrivate = pSrc->pAppPrivate;
   pDest->bufCount = pSrc->bufCount;
   if(pSrc->bufCount <= sizeof(pDest->buf))
      memcpy (pDest->buf, pSrc->buf, pSrc->bufCount);
   else
      errlogPrintf("%s %d: drvSerialCopyRequest(): "
                    "Destination buffer not large enough", 
                   __FILE__, __LINE__);
}

/*
 * drvSerialCopyResponse()
 *
 * Copy a source response into a destination response 
 * (dont copy unused bytes)
 */
void drvSerialCopyResponse(drvSerialResponse *pDest, const drvSerialResponse *pSrc)
{
   pDest->pAppPrivate = pSrc->pAppPrivate;
   pDest->bufCount = pSrc->bufCount;
   if(pSrc->bufCount <= sizeof(pDest->buf))
      memcpy (pDest->buf, pSrc->buf, pSrc->bufCount);
   else
      errlogPrintf("%s %d: drvSerialCopyResponse(): "
                    "Destination buffer not large enough", 
                   __FILE__, __LINE__);
}

/*
 * drvSerialInit()
 */
long drvSerialInit (void)
{
   long            status;

   /*
    * dont init twice
    */
   if (devNameTbl.lock) {
            epicsPrintf ("%s line %d drvSerialInit already called\n",
                         __FILE__,__LINE__ );
      return S_drvSerial_OK;
   }

   /*
    * create a hash table for the file 
    * name strings and associated MUTEX
    */
   devNameTbl.lock = epicsMutexCreate();
   if (!devNameTbl.lock)
   {
      status = S_db_noMemory;
      errMessage(status,
            ":drvSerialInit() epicsMutexOsdCreate()");
      return status;
   }

   devNameTbl.pBucket = bucketCreate(256);
   if (!devNameTbl.pBucket)
   {
      status = S_db_noMemory;
      errMessage(status,
            ":drvSerialInit() bucketCreate()");
      epicsMutexDestroy(devNameTbl.lock);
      return status;
   }

        epicsPrintf ("%s line %d drvSerialInit was successful (%d)\n",
                     __FILE__,__LINE__,S_drvSerial_OK );
   return S_drvSerial_OK;
}

/*
 * drvSerialReport()
 */
long drvSerialReport(int level)
{
  drvSerialParm        *pDev;
  drvSerialPriority   pri;

  if (devNameTbl.pBucket)
    {
      epicsMutexMustLock( devNameTbl.lock );

      pDev = (drvSerialParm *) ellFirst(&devNameTbl.list);
      while (pDev)
   {
     printf("\tDevice Name =%s\n", pDev->pName);
     if (level > 0)
       {
         printf("\t\tRequest Queue\n");
         for (pri = dspLowest; pri <= dspHighest; pri++)
      {
        printf(
          "\t\t\tPri=%d Pending=%d Reserved=%d\n",
          pri,
          ellCount(&pDev->requestQueues[pri-dspLowest]),
          ellCount(&pDev->reserveQueues[pri-dspLowest])
          );
      }
         printf("\t\tResponse Q cnt=%d\n",
           ellCount(&pDev->respQueue));
       }
     if (level > 3) {
       printf("\t\tmutexSem ");
       epicsMutexShow(pDev->mutexSem, level);
       printf("\t\twriteQueueSem");
       epicsEventShow(pDev->writeQueueSem, level);
       printf("\t\treadQueueSem");
       epicsEventShow(pDev->readQueueSem, level);
     }
     pDev = (drvSerialParm *) ellNext(&pDev->node);
   }
      if (level > 2)
   {
     bucketShow(devNameTbl.pBucket);
   }
      
      epicsMutexUnlock(devNameTbl.lock);
    }

  return S_drvSerial_OK;
}

/*
 * drvSerialAttachLink()
 */
long drvSerialAttachLink(
          const char *pName,
          drvSerialParseInput *pParser,
          void **ppAppPrivate
)
{

   drvSerialParm  *pDev;

   if (!pParser) {
      return S_drvSerial_noParser;
   }

   /*
    * lazy init
    */
   if (!devNameTbl.lock) {
      long status;
      status = drvSerialInit ();
      if (status) {
         return status;
      }
   }

   pDev = drvSerialTestExist(pName);
   if (pDev) {
      /*
       * check for in use by another app
       */
      if (pDev->pInputParser != pParser) {
         return S_drvSerial_linkInUse;
      }
      *ppAppPrivate = pDev->pAppPrivate;
      return S_drvSerial_OK;
   }
   else {
      return S_drvSerial_noneAttached;
   }
}

/*
 * drvSerialTestExist()
 */
drvSerialParm *drvSerialTestExist(const char *pName)
{
   drvSerialParm  *pDev;

   /*
    * no init chk 
    */
   if (!devNameTbl.pBucket)
   {
      long epicsStatus;
      epicsStatus = drvSerialInit ();
      if (epicsStatus) {
         return NULL;
      }
   }

   /*
    * MUTEX around use of hash table 
    */
   epicsMutexMustLock( devNameTbl.lock );

   pDev = (drvSerialParm *) bucketLookupItemStringId(
                 devNameTbl.pBucket, pName);
   /*
    * MUTEX off around use of hash table 
    */
   epicsMutexUnlock(devNameTbl.lock);

   return pDev;
}

/*
 * drvSerialCreateLink()
 */
long drvSerialCreateLink(
          const char *pName,
          drvSerialParseInput *pParser,
          void *pAppPrivate,
          drvSerialLinkId * pId
)
{
   drvSerialParm        *pDev;
   long               status;
   drvSerialPriority   pri;

   if (!pParser) {
      return S_drvSerial_noParser;
   }

   /*
    * verify a hash table for the file name strings and associated MUTEX
    * was initialized in drvSerialInit()
    */
   if (!devNameTbl.pBucket)
   {
      long epicsStatus;
      epicsStatus = drvSerialInit ();
      if (epicsStatus) {
         return epicsStatus;
      }
   }

   pDev = drvSerialTestExist (pName);

   if (pDev)
   {
      return S_drvSerial_linkInUse;
   }

   pDev = (drvSerialParm *) calloc (1, sizeof(*pDev));
   if (!pDev)
   {
           status = S_db_noMemory;
      errMessage(status, ":drvSerialCreateLink() calloc()");
      return status;
   }

   /*
    * create free list for messages
    * (preallocate all of them)
    */
   freeListInitPvt (&pDev->pFreeListPVT, sizeof(freeListItem), 
            requestQueueQuota+responseQueueQuota);

   pDev->pInputParser = pParser;
   pDev->pAppPrivate = pAppPrivate;
   pDev->readCanceled = FALSE;
   pDev->writeCanceled = FALSE;

   /*
    * MUTEX around use of hash table 
    */
   epicsMutexMustLock( devNameTbl.lock );

   /*
    * Add entry for this file into the hash table
    */
   if (strlen(pName) < sizeof(pDev->pName) - 1)
      strcpy(pDev->pName, pName);
   else {
      errlogPrintf("%s %d: drvSerialCreateLink(): device name too long",
         __FILE__, __LINE__);
      strncpy(pDev->pName, pName, sizeof(pDev->pName)-1);
   }
   status = bucketAddItemStringId(
                   devNameTbl.pBucket,
                   pDev->pName,
                   pDev);
   if (status)
   {
      status = S_db_noMemory;
      errMessage(status, "bucketAddItemStringId()");
      free(pDev);
      epicsMutexUnlock(devNameTbl.lock);
      return status;
   }

   /*
    * MUTEX off around use of hash table 
    */
   epicsMutexUnlock(devNameTbl.lock);

   pDev->readQueueSem = epicsEventCreate(epicsEventEmpty);
   if (pDev->readQueueSem == NULL)
   {
      bucketRemoveItemStringId(devNameTbl.pBucket, pDev->pName);
      free(pDev);
      status = S_db_noMemory;
      errMessage(status,
            ":drvSerialCreateLink() epicsEventCreate()");
      return status;
   }
   pDev->writeQueueSem = epicsEventCreate(epicsEventEmpty);

   if (pDev->writeQueueSem == NULL)
   {
      epicsEventDestroy(pDev->readQueueSem);
      bucketRemoveItemStringId(devNameTbl.pBucket, pDev->pName);
      free(pDev);
      status = S_db_noMemory;
      errMessage(status,
            ":drvSerialCreateLink() epicsEventCreate()");
      return status;
   }
   pDev->mutexSem = epicsMutexCreate();
   if (pDev->mutexSem == NULL)
   {
      epicsEventDestroy(pDev->readQueueSem);
      epicsEventDestroy(pDev->writeQueueSem);
      bucketRemoveItemStringId(devNameTbl.pBucket, pDev->pName);
      free(pDev);
      status = S_db_noMemory;
      errMessage(status,
            ":drvSerialCreateLink() epicsMutexOsdCreate()");
      return status;
   }

   for (pri = dspLowest; pri <= dspHighest; pri++)
   {
      unsigned index;

      index = pri - dspLowest;
      assert (index < NELEMENTS(pDev->requestQueues));
      ellInit(&pDev->requestQueues[index]);
      ellInit(&pDev->reserveQueues[index]);
   }
   pDev->pHighestPriReqQue = NULL; /* no req queue populated */

   ellInit (&pDev->respQueue);
   ellInit (&pDev->limboQueue);

   /*
    * MUTEX around use of hash table 
    */
   epicsMutexMustLock( devNameTbl.lock );

   ellAdd (&devNameTbl.list, &pDev->node);

   /*
    * MUTEX off around use of hash table 
    */
   epicsMutexUnlock(devNameTbl.lock);

   status = drvSerialLinkOpen (pDev);
   if (status) {
      epicsEventDestroy(pDev->readQueueSem);
      epicsEventDestroy(pDev->writeQueueSem);
      bucketRemoveItemStringId(devNameTbl.pBucket, pDev->pName);
      free(pDev);
      return status;
   }

   /*
    * set their handle to this serial link
    */
   *pId = (void *) pDev;

   return S_drvSerial_OK;
}

/*
 * drvSerialWrite()
 * (this is a task entry point)
 */
int drvSerialWrite (drvSerialParm * pDev)
{
   int status;
        int delay = 0;
   struct termios termios;
   int fd;

        /*
    *  Wait for the read task to start. If it doesn't within 
    *  a few seconds then this task must terminate.
    */
        do {

     WAIT_N_SEC( 1 );

     if ( !pDev->readCanceled ) break;

     if ( ++delay > 10 ) {

       errMessage(S_drvSerial_linkDown,"Read task never started!" );
       epicsPrintf ("%s.%d read task never started for %s",
          __FILE__,__LINE__,pDev->pName );
       exit(-1);
     }

   } while ( 1 );

   while (TRUE)
   {
#if 0
      status = epicsEventWaitWithTimeout(pDev->writeQueueSem, 
                     (double) WAIT_FOREVER);
                /* epicsPrintf( "drvSerialWrite - epicsEventWaitWithTimeout return = %d\n", status ); */
#endif

      status = epicsEventWait(pDev->writeQueueSem);
      assert (status == epicsEventWaitOK);

      while (!pDev->pWF) {
         pDev->pWF = fopen (pDev->pName, "w");
         if (pDev->pWF) {
            pDev->writeCanceled = FALSE;
            break;
         }
         fd = fileno(pDev->pWF);
         printf (" drvSerialWrite - setting termios for (%d) \n", fd);
         tcgetattr (fd, &termios);
         printf ("termios.c_iflag = [%o]\n", termios.c_iflag);
         printf ("termios.c_oflag = [%o]\n", termios.c_oflag);
         printf ("termios.c_cflag = [%o]\n", termios.c_cflag);
         printf ("termios.c_lflag = [%o]\n", termios.c_lflag);
         termios.c_lflag = 0;
         termios.c_cc[VTIME] = 0;
         termios.c_cc[VMIN] = 1;
         termios.c_cc[VSTOP] = 0;
         termios.c_cc[VSTART] = 0;
         termios.c_cc[VREPRINT] = 0;
         tcflush (fd, TCIFLUSH);
         tcsetattr(fd, TCSANOW, &termios);
         WAIT_N_SEC (10);
      }

      status = sendAllRequests (pDev);
      if (status<0) {
        /* epicsPrintf( "drvSerialWrite - drvSerialLinkReset\n"); */
         drvSerialLinkReset (pDev);
         fclose (pDev->pWF);
         pDev->pWF = NULL;
         /*
          * go ahead and reopen the file
          */
         epicsEventSignal(pDev->writeQueueSem);
      }
      else {
        /*
         * flush out the serial driver so that high priority events
         * are allowed to jump to the front of the queue
         * 
         * Under vxWorks this flushes stdio to the the write (driver)
         * level where characters might still be accumulating. We
         * prefer to accumulate in our buffers in this source so that
         * high priority requests are allowed to jump to the front of
         * the queue. However there is no standard way to do this via
         * ioctl(). Note that under vxWorks FIOFLUSH discards the
         * characters in tyLib.c instead of blocking the task until
         * the characters are delivered to the link as I would
         * expect
         */
         fflush (pDev->pWF);

      }
   }
}

/*
 * sendAllRequests()
 */
int sendAllRequests (drvSerialParm *pDev)
{
   freeListItem *pReq;
   int status;

   status = 0; 
   while (TRUE) {
      pReq = fetchHighestPriorityRequest(pDev);
      if (!pReq) {
         break;
      }

      status = (*pReq->shr.req.pCB) (pDev->pWF, &pReq->shr.req);

      drvSerialDisposeFreeItem(pDev, pReq);

      if (status < 0) {
         break;
      }
   }

   return status;
}

/*
 * fetchHighestPriorityRequest()
 *
 * Fetch a request from the highest priority populated queue.
 * Return NULL if there are currently no requests in the system.
 */
freeListItem *fetchHighestPriorityRequest(drvSerialParm *pDev)
{
   freeListItem *pReq = NULL;


   epicsMutexMustLock( pDev->mutexSem );

   while (pDev->pHighestPriReqQue>=pDev->requestQueues) {
      pReq = (freeListItem *) ellGet(pDev->pHighestPriReqQue);
      if (pReq) {
         break;
      }
      pDev->pHighestPriReqQue--;
   }

   if (pReq) {
      assert (pReq->queue == dsReq);
      pReq->queue = dsLimbo;
      ellAdd(&pDev->limboQueue, &pReq->node);
   }

   epicsMutexUnlock(pDev->mutexSem);

   return pReq;
}

/*
 * drvSerialRead()
 * (this is a task entry point)
 */
int
drvSerialRead (drvSerialParm * pDev)
{
   long            status;
   freeListItem    *pResp;
   freeListItem   resp;
   int fd;
   struct termios termios;


   while (TRUE)
   {

      while (!pDev->pRF) {
         pDev->pRF = fopen (pDev->pName, "r");
         if (pDev->pRF) {
            pDev->readCanceled = FALSE;
            break;
         }
         fd = fileno(pDev->pRF);
         printf (" drvSerialRead - setting termios for (%d) \n", fd);
         tcgetattr (fd, &termios);
         printf ("termios.c_iflag = [%o]\n", termios.c_iflag);
         printf ("termios.c_oflag = [%o]\n", termios.c_oflag);
         printf ("termios.c_cflag = [%o]\n", termios.c_cflag);
         printf ("termios.c_lflag = [%o]\n", termios.c_lflag);
         termios.c_lflag = 0;
         termios.c_cc[VTIME] = 0;
         termios.c_cc[VMIN] = 1;
         termios.c_cc[VSTOP] = 0;
         termios.c_cc[VSTART] = 0;
         termios.c_cc[VREPRINT] = 0;
         tcflush (fd, TCIFLUSH);
         tcsetattr(fd, TCSANOW, &termios);
         WAIT_N_SEC(10);
      }

      /*
       * call the applications parser to fill in the response
       */
      resp.shr.res.bufCount = 0;
      status = (*pDev->pInputParser)(pDev->pRF, &resp.shr.res, 
               pDev->pAppPrivate);
      if (status < 0) {
         drvSerialLinkReset (pDev);
         fclose (pDev->pRF);
         pDev->pRF = NULL;
         continue;  /* back to top of while(TRUE) loop */
      }
      else if (resp.shr.res.bufCount==0) {
         continue;  /* back to top of while(TRUE) loop */
      }

      /*
       * Dont continue until this response queue is
       * below its maximum size
       *
       * (app notifies here if an entry is removed 
       * from the response queue).
       */
      while (ellCount(&pDev->respQueue) >= responseQueueQuota) {
         epicsEventWaitWithTimeout(pDev->readQueueSem, 4.0 );
      }

      /*
       * obtain a free entry
       */
      while ( !(pResp = drvSerialFetchFreeItem (pDev)) ) {
         WAIT_N_SEC(1);
      }

      /*
       * copy the response
       */
      
      drvSerialCopyResponse (&pResp->shr.res, &resp.shr.res);

      /*
       * MUTEX on
       */
      epicsMutexMustLock( pDev->mutexSem );

      /*
       * add to the response queue
       */
      /* assert (pResp->queue == dsLimbo); */
      if (pResp->queue == dsLimbo) {
         ellDelete (&pDev->limboQueue, &pResp->node);
         pResp->queue = dsRes;
         ellAdd (&pDev->respQueue, &pResp->node);
      }
      else { errlogPrintf("%s %d: drvSerialRead(): pResp has wrong queue",
              __FILE__, __LINE__);
      }

      /*
       * MUTEX off
       */
      epicsMutexUnlock(pDev->mutexSem);
   }
}

/*
 * test to see if the input queue is full
 */
int
drvSerialInputQueueIsFull (drvSerialLinkId id)
{
   drvSerialParm *pDev = (drvSerialParm *) id;

   if (ellCount(&pDev->respQueue)>= responseQueueQuota) {
      return TRUE;
   }
   /*
    * no need to check to see if there is sufficent
    * memory because I preallocate all of items in
    * the queue quota
    */
   return FALSE;
}


/*
 * Pull the next response off the response queue
 * (return error if none are present)
 */
long
drvSerialNextResponse(
            drvSerialLinkId id,
            drvSerialResponse * pResponse)
{
   drvSerialParm *pDev = id;
   freeListItem *pResp;

   

   /*
    * MUTEX on
    */
   epicsMutexMustLock( pDev->mutexSem );

   /*
    * inform the read task that something was removed 
    * from the response queue when it was full
    * (in case it needs to add another item)
    */
   if (ellCount(&pDev->respQueue)>=responseQueueQuota) {
      epicsEventSignal(pDev->readQueueSem);
   }

   /*
    * obtain an event entry
    */
   pResp = (freeListItem *) ellGet(&pDev->respQueue);

   if (pResp) {
      assert (pResp->queue == dsRes);
      drvSerialCopyResponse(pResponse, &pResp->shr.res);

      pResp->queue = dsLimbo;
      ellAdd (&pDev->limboQueue, &pResp->node);;
      drvSerialDisposeFreeItem (pDev, pResp);
   }

   /*
    * MUTEX off
    */
   epicsMutexUnlock(pDev->mutexSem);

   if (!pResp)
   {

     
      return S_drvSerial_noEntry;
   }


   return S_drvSerial_OK;
}


/*
 * drvSerialFetchFreeItem()
 *
 * Obtain a new event block - look on the free list first then
 * look in system pool.
 */
freeListItem * drvSerialFetchFreeItem (drvSerialParm *pDev)
{
   freeListItem *pItem;

   /*
    * MUTEX on
    * (task delete safe mutex prevents loss of limbo items)
    */
   epicsMutexMustLock( pDev->mutexSem );

   pItem = (freeListItem *) freeListCalloc (pDev->pFreeListPVT);

   /*
    * so we dont loose it if the task is deleted
    */
   if (pItem) {
      pItem->queue = dsLimbo;
      ellAdd(&pDev->limboQueue, &pItem->node);
   }

   epicsMutexUnlock(pDev->mutexSem);

   return pItem;
}

/*
 * drvSerialDisposeFreeItem()
 *
 * Place excess event on the free list and perhaps someday
 * notify tasks that are waiting for a free event
 * block
 */
void
drvSerialDisposeFreeItem (drvSerialParm *pDev, freeListItem * pItem)
{

   /*
    * MUTEX on
    * (task delete safe mutex prevents loss of limbo items)
    */
   epicsMutexMustLock( pDev->mutexSem );

   assert (pItem->queue==dsLimbo);
   ellDelete(&pDev->limboQueue, &pItem->node);

   freeListFree (pDev->pFreeListPVT, pItem);

   epicsMutexUnlock(pDev->mutexSem);
}

/*
 * Place request in the request queue
 */
long
drvSerialSendRequest(
           drvSerialLinkId id,
           drvSerialPriority pri,
           const drvSerialRequest * pReq)
{
   drvSerialParm *pDev = (drvSerialParm *) id;
   freeListItem *pItem;
   ELLLIST *pReqList;
   ELLLIST *pResList;
   /*
    * check for application programmer error
    */

   if (pReq->bufCount >= sizeof(pReq->buf)) {
      return S_drvSerial_invalidArg;
   }
   if (pri > dspHighest) {
      return S_drvSerial_invalidArg;
   }
   if (pReq->pCB==NULL) {
      return S_drvSerial_invalidArg;
   }

   /*
    * add the request to the queue with MUTEX
    */
   epicsMutexMustLock( pDev->mutexSem );

   /*
    * if the request queue is full then return an error (instead of
    * blocking so the record can enter alarm state)
    */
   pReqList = &pDev->requestQueues[pri];
   pResList = &pDev->reserveQueues[pri];
   if (ellCount(pReqList) + ellCount(pResList) >= requestQueueQuota)
   {
      epicsMutexUnlock(pDev->mutexSem);
      return S_drvSerial_queueFull;
   }

   /*
    * obtain an event entry
    */
   pItem = drvSerialFetchFreeItem(pDev);
   if (!pItem)
   {
      epicsMutexUnlock(pDev->mutexSem);
      return S_db_noMemory;
   }

   /*
    * copy in their request
    */
   drvSerialCopyRequest(&pItem->shr.req, pReq);

   assert (pItem->queue==dsLimbo);
   ellDelete (&pDev->limboQueue, &pItem->node);

   /*
    * keep track of the highest priority
    * and populated request queue 
    */
   if (pReqList>pDev->pHighestPriReqQue) {
      pDev->pHighestPriReqQue = pReqList;
   }
   pItem->queue = dsReq;
   pItem->pri = pri;
   ellAdd(pReqList, &pItem->node);

   epicsMutexUnlock(pDev->mutexSem);

   /*
    * inform write task that there is a new entry
    */
   epicsEventSignal(pDev->writeQueueSem);

   return S_drvSerial_OK;
}

/*
 * drvSerialCreateRequest ()
 */
drvSerialRequest *
drvSerialCreateReservedRequest(
            drvSerialLinkId id,
            drvSerialPriority pri)
{
   drvSerialParm *pDev = (drvSerialParm *) id;
   freeListItem *pItem;
   ELLLIST *pReqList;
   ELLLIST *pResvList;
   
   if (pri > dspHighest) {
      return NULL;
   }
   
   /*
    * add the request to the queue with MUTEX
    */
   epicsMutexMustLock( pDev->mutexSem );

   /*
    * if the request queue is full then return an error (instead of
    * blocking so the record can enter alarm state)
    */
   pReqList = &pDev->requestQueues[pri];
   pResvList = &pDev->reserveQueues[pri];
   if (ellCount(pReqList) + ellCount(pResvList) >= requestQueueQuota)
   {
      epicsMutexUnlock(pDev->mutexSem);
      return NULL;
   }

   /*
    * obtain an event entry
    */
   pItem = drvSerialFetchFreeItem(pDev);
   if (!pItem)
   {
      epicsMutexUnlock(pDev->mutexSem);
      return NULL;
   }

   assert (pItem->queue==dsLimbo);
   ellDelete (&pDev->limboQueue, &pItem->node);

   pItem->queue = dsResv;
   pItem->pri = pri;
   ellAdd(pResvList, &pItem->node);

   epicsMutexUnlock(pDev->mutexSem);

   return &pItem->shr.req;
}

/*
 * drvSerialSendReservedRequest ()
 */
long
drvSerialSendReservedRequest(
            drvSerialLinkId id,
            const drvSerialRequest * pRequest)
{
   drvSerialParm *pDev = (drvSerialParm *) id;
   char *pChar = (char *) pRequest;
   freeListItem *pItem = (freeListItem *) (pChar - offsetof(freeListItem,shr));
   ELLLIST *pReqList;
   ELLLIST *pResvList;

   /*
    * add the request to the queue with MUTEX
    */
   epicsMutexMustLock( pDev->mutexSem );

   /*
    * verify that this was created with
    * drvSerialCreateRequest()
    */
   if (pItem->queue != dsResv || pItem->pri > dspHighest) {
      epicsMutexUnlock(pDev->mutexSem);
      return S_drvSerial_invalidArg;
   }

   pReqList = &pDev->requestQueues[(unsigned) pItem->pri];
   pResvList = &pDev->reserveQueues[(unsigned) pItem->pri];

   ellDelete (pResvList, &pItem->node);

   /*
    * keep track of the highest priority
    * and populated request queue 
    */
   if (pReqList>pDev->pHighestPriReqQue) {
      pDev->pHighestPriReqQue = pReqList;
   }
   pItem->queue = dsReq;
   ellAdd(pReqList, &pItem->node);

   epicsMutexUnlock(pDev->mutexSem);

   /*
    * inform write task that there is a new entry
    */
   epicsEventSignal(pDev->writeQueueSem);

   return S_drvSerial_OK;
}

/*
 * drvSerialLinkOpen ()
 *
 * shut down an existing link
 */
int 
drvSerialLinkOpen (drvSerialParm *pDev)
{
    int fd;

    struct termios termios;
    
   /*
    * we dont quit if we are unable to open the
    * file 
    *
    * instead we go ahead and spawn the tasks
    * and keep trying there in case the link comes
    * up later
    */
   pDev->pWF = fopen (pDev->pName, "w");

   /* but don't keep trying if the serial port doesn't even exist! */
   if((errno == ENOENT) || (errno == EISDIR)) {  /* EISDIR is error if no portname supplied */
     epicsPrintf("drvSerialLinkOpen: No such device \"%s\"\n", pDev->pName);
     epicsPrintf("drvSerialLinkOpen: (errno =  %d)\n", errno);
     return S_drvSerial_invalidArg;
   }

   if (!pDev->pWF) {
      errMessage(S_drvSerial_linkDown, strerror(errno));
   }
         fd = fileno(pDev->pWF);
         printf (" drvSerialLinkOpen - setting termios for %s (%d) \n", pDev->pName, fd);
         tcgetattr (fd, &termios);
         /* termios.c_iflag = 037757572010;*/ /* ?? OS dependant? */
         //termios.c_iflag &= ~(IGNBRK | BRKINT | ICRNL | INLCR | PARMRK | INPCK | ISTRIP | IXON);
         termios.c_iflag = 0;
         //termios.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | IEXTEN | ISIG);
         termios.c_lflag = 0;
         //termios.c_oflag  &= ~(OPOST);;
         termios.c_oflag  = 0;
         termios.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS); /* 8N1, no parity, no HW flow ctrl */
         termios.c_cflag |= CS8;
         cfsetispeed(&termios, B9600);
         cfsetospeed(&termios, B9600);
#if DEBUG
         printf ("termios.c_iflag = [%o]\n", termios.c_iflag);
         printf ("termios.c_oflag = [%o]\n", termios.c_oflag);
         printf ("termios.c_cflag = [%o]\n", termios.c_cflag);
         printf ("termios.c_lflag = [%o]\n", termios.c_lflag);
#endif
         termios.c_lflag = 0;
         termios.c_cc[VTIME] = 0;
         termios.c_cc[VMIN] = 1;
         termios.c_cc[VSTOP] = 0;
         termios.c_cc[VSTART] = 0;
         termios.c_cc[VREPRINT] = 0;
         tcflush (fd, TCIFLUSH);
         tcsetattr(fd, TCSANOW, &termios);

   pDev->pRF = fopen (pDev->pName, "r");
   if (!pDev->pRF) {
      errMessage (S_drvSerial_linkDown, strerror(errno));
   }
         fd = fileno(pDev->pRF);
         printf (" drvSerialLinkOpen - NOT setting termios for %s (%d) \n", pDev->pName, fd);
         
         tcgetattr (fd, &termios);
#if DEBUG
         printf ("termios.c_iflag = [%o]\n", termios.c_iflag);
         printf ("termios.c_oflag = [%o]\n", termios.c_oflag);
         printf ("termios.c_cflag = [%o]\n", termios.c_cflag);
         printf ("termios.c_lflag = [%o]\n", termios.c_lflag);
#endif
         termios.c_lflag = 0;
         termios.c_cc[VTIME] = 0;
         termios.c_cc[VMIN] = 1;
         tcflush (fd, TCIFLUSH);
         tcsetattr(fd, TCSANOW, &termios);
      

   pDev->readCanceled = 1;

   pDev->wtTaskId = 
     epicsThreadCreate(
             "SerialWT",   /* task name */
             tp_drvSerialPriority,/* priority */
             tp_drvSerialStackSize,/* stack size */
             (EPICSTHREADFUNC) drvSerialWrite, /* task entry point */
             (void *) pDev );
   if (!pDev->wtTaskId)
   {
      fclose (pDev->pRF);
      pDev->pRF = NULL;
      fclose (pDev->pWF);
      pDev->pWF = NULL;
      return S_db_noMemory;
   }

   pDev->rdTaskId = 
     epicsThreadCreate(
             "SerialRD",   /* task name */
             tp_drvSerialPriority,/* priority */
             tp_drvSerialStackSize,/* stack size */
             (EPICSTHREADFUNC) drvSerialRead, /* task entry point */
             (void *) pDev );
   if (!pDev->rdTaskId)
   {
      fclose (pDev->pRF);
      pDev->pRF = NULL;
      fclose (pDev->pWF);
      pDev->pWF = NULL;
      return S_db_noMemory;
   }

   pDev->readCanceled = 0;

   return S_drvSerial_OK;
}

/*
 * drvSerialLinkReset ()
 *
 * shut down an existing link
 */
void drvSerialLinkReset (drvSerialParm * pDev)
{
   freeListItem       *pItem;
   drvSerialPriority   pri;

   /*
    * MUTEX on
    */
   epicsMutexMustLock( pDev->mutexSem );

   /*
    * this emptys any characters in the buffers
    * and cancels any outstanding IO operations
    *
    * testing the read/write canceled flags here
    * prevents feedback (race) situations
    */
   if (!pDev->readCanceled && !pDev->writeCanceled) {
      epicsPrintf ("%s.%d resetting link \"%s\"\n", 
         __FILE__, __LINE__, pDev->pName);
      if (pDev->pRF) {

         /* ioctl (fileno(pDev->pRF), FIOFLUSH, 0); */   /* these only work under vxWorks */
         /* ioctl (fileno(pDev->pRF), FIOCANCEL, 0); */  /* replace with fflush() & tcflush() */
         fflush(pDev->pRF);
         tcflush(fileno(pDev->pRF), TCIFLUSH);
                   
         pDev->readCanceled = TRUE;
      }
      if (pDev->pWF) {
         /* ioctl (fileno(pDev->pWF), FIOFLUSH, 0); */   /* as above  */
         /* ioctl (fileno(pDev->pWF), FIOCANCEL, 0); */
         fflush(pDev->pWF);
         tcflush(fileno(pDev->pWF), TCOFLUSH);

         pDev->writeCanceled = TRUE;
      }
   }

   /*
    * drain outstanding requests
    */
   for (pri=dspLowest; pri<=dspHighest; pri++) 
   {
      unsigned index;

      index = pri-dspLowest;
      while ( (pItem = (freeListItem *) ellGet(&pDev->requestQueues[index])) ) {
         pItem->queue = dsLimbo;
         ellAdd (&pDev->limboQueue, &pItem->node);
         drvSerialDisposeFreeItem (pDev, pItem);
      }
   }

   /*
    * drain outstanding responses
    */
   while ( (pItem = (freeListItem *) ellGet(&pDev->respQueue)) ) {
      pItem->queue = dsLimbo;
      ellAdd (&pDev->limboQueue, &pItem->node);
      drvSerialDisposeFreeItem (pDev, pItem);
   }

   /*
    * MUTEX off
    */
   epicsMutexUnlock(pDev->mutexSem);

   /*
    * dont race
    */
   WAIT_N_SEC (1);
}


