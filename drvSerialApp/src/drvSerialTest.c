/*
 *
 * ANSI C
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
 

/* EPICS includes */
#include <epicsPrint.h>
#include <epicsThread.h>
#include <epicsExport.h>
#include <epicsTypes.h>
#include <iocsh.h>

#include "drvSerial.h"


#define TEST_OK      0
#define TEST_ERROR   (-1)
#define   MAX_BUFFER   255    /* can be up to 255 bytes */
#define   MAX_ITERATIONS   10


/* flag to indicate whether to print out test buffer */
static int printbuf = 1;

struct privStruct { 
    char      port[100];   /* serial port name */ 
    drvSerialLinkId   id;      /* link id */ 
};

/* Link if for the port in use. Only one port can be used a time.
 */
static drvSerialLinkId   linkId;

/* Buffer where the message to send is created by fillBuffer()
 */
static unsigned char testDataBuffer[MAX_BUFFER];

/* printBuffer -- Print the contents of a data buffer in hex format.
 * The size of the buffer is assumed to be fixed.
 */
static void drvSerialTestPrintBuffer (char *label, char *buffer)
{
   int      n;
   unsigned char   ch;

   if(printbuf) {


      epicsPrintf ("----\n");

      epicsPrintf("drvSerialTestPrintBuffer (%s)\n", label);

      if (buffer == NULL) {
         epicsPrintf ("drvSerialTestPrintBuffer: null buffer pointer\n");
         return;
      }

      for (n = 0; n < MAX_BUFFER; n++) {
         ch = *(buffer + n);
         if (n > 0 && n % 16 == 0)
            epicsPrintf("\n");
         if (isprint (ch))
            epicsPrintf ("[ %c] ", ch);
         else
            epicsPrintf ("[%02x] ", ch);
      }
      epicsPrintf ("\n");
   }
}

/* drvSerialSendRequest -- This is the routine that does the actual writing of the
 * bytes to the serial port. It is called by dvrSerial (callback).
 */
static int drvSerialTestSendRequest (FILE *pFile, drvSerialRequest *pReq) 
{ 
   int   status, nwritten;

   nwritten = fwrite (pReq->buf, sizeof (epicsUInt8), pReq->bufCount, pFile);

   epicsPrintf ("sendRequest: nwritten=%d\n", nwritten);

   if (nwritten == pReq->bufCount)
       status = TEST_OK;
   else
       status = TEST_ERROR;
   
   return status;
} 

/* drvSerialTestParser -- This is the routine in charge of reading (parsing) characters
 * from the serial port. It's the responsibility of this routine to decide when
 * to stop reading and return. The current implementation reads a fixed number
 * of bytes (MAX_BUFFER). It is called by drvSerial (callback).
 */
static int drvSerialTestParser (FILE *fp, drvSerialResponse *pResp, void *pPriv) 
{ 
   int      c;
   char    *pBuf = pResp->buf;

   pResp->bufCount = 0;

   while ((c = getc (fp)) != EOF) {
       *(pBuf++) = (unsigned char) c;
       pResp->bufCount++;
       if (pResp->bufCount >= MAX_BUFFER)
      break;
   }

   epicsPrintf("drvSerialTestParser() called, bufCount = %d \n", pResp->bufCount);

   return pResp->bufCount;
}

/* --- end of local routines --*/

/* drvSerialTestDataInitialize --.
 * Initializing the test data buffer with random data.
 */
int drvSerialTestDataInitialize ()
{
   int   n;

   memset (testDataBuffer, 0, MAX_BUFFER);
   for (n = 0; n < MAX_BUFFER; n++)
       *(testDataBuffer + n) = rand()%255;
   return TEST_OK;
}


/* drvSerialTestCreateLink -- Routine to test drvSerialCreateLink(). The link id
 * is stored in the global variable linkId for later use.
 */
int drvSerialTestCreateLink (char *port)
{
   int         status;
   struct privStruct   priv, *ppriv = &priv;

   epicsPrintf("drvSerialCreateLink(): ");

   status = drvSerialCreateLink (port, drvSerialTestParser, ppriv, &ppriv->id);
        
   if(status != S_drvSerial_OK) 
      epicsPrintf("failed.\n");
   else {
      linkId = ppriv->id;
      epicsPrintf("link created on port %s: ", port);
      epicsPrintf("linkId=%p: ", linkId);
      epicsPrintf("status=%d\n", status);
   }

   return status;
}

/* drvSerialTestAttachLink -- Routine to test drvSerialAttachLink. It will return an
 * error if called before serialTestCreateLink().
 */
int drvSerialTestAttachLink (char *port)
{
   int         status;
   struct privStruct   priv, *ppriv = &priv;

   epicsPrintf("drvSerialAttachLink(): ");

   status = drvSerialAttachLink (port, drvSerialTestParser, (void **) &ppriv);
        
   if(status == S_drvSerial_noneAttached) {
      epicsPrintf("No link exists to which to attach\n");
   }
      
   else if(status != S_drvSerial_OK)
       epicsPrintf("unknown failure.\n");

   else epicsPrintf("attached to link on port %s\n", port);

   return status;
}

/* drvSerialTestSend -- Routine to test drvSerialSendRequest(). It should be called
 * after calling serialTestCreateLink().
 */
int drvSerialTestSend ()
{
   drvSerialRequest   req; 

   epicsPrintf("drvSerialTestSend(): sizeof(req)=%lu, sizeof(req.buf)=%lu\n", (long)sizeof(req), (long)sizeof(req.buf));

   if (MAX_BUFFER > sizeof(req.buf) ) {
       epicsPrintf ("Test buffer too big!\n");
       return TEST_ERROR;
   }

   drvSerialTestDataInitialize();
   memcpy((char *) req.buf, testDataBuffer, MAX_BUFFER);
   drvSerialTestPrintBuffer("serialTestSend", req.buf);
   req.bufCount    = MAX_BUFFER;
   req.pCB         = drvSerialTestSendRequest; 
   req.pAppPrivate = NULL; 

   drvSerialSendRequest (linkId, dspLow, &req);

   epicsPrintf("\n");

   return 0;
}

/* drvSerialTestReceive -- Routine to test drvSerialNextResponse(). Note that the actual
 * reading of the bytes is done by drvSerialTestParser, which puts them into a temporary buffer
 * inside drvSerial. It should be called after calling serialTestCreateLink().
 */
int drvSerialTestReceive()
{ 
   int         status; 
   drvSerialResponse   resp; 

   status = drvSerialNextResponse (linkId, &resp); 

   if (status == S_drvSerial_noEntry) {
      epicsPrintf ("drvSerialTestReceive(): No data\n");
       return TEST_ERROR;
   } else if (status != S_drvSerial_OK) {
       return TEST_ERROR;
   }

   drvSerialTestPrintBuffer("serialTestReceive", resp.buf);
   epicsPrintf ("bufCount=%d\n", resp.bufCount);

   if (memcmp(resp.buf, testDataBuffer, MAX_BUFFER) != 0) {
       epicsPrintf ("Buffers don't agree\n");
       return TEST_ERROR;
   } else {
      epicsPrintf ("Buffers agree\n");
      return TEST_OK;
   }

   return TEST_OK;
}

/* drvSerialTestAll -- End to end test. It creates a link, and send and recives
 * date multiple times to check for transmission errors.
 */
int drvSerialTestAll(char *port)
{
   int          n;
   int     status;


   status = drvSerialTestAttachLink(port);

   if(status == S_drvSerial_noneAttached) {
      epicsPrintf("drvSerialTest: No pre-existing link, will try creating one.\n");
      if((status =  drvSerialTestCreateLink(port)) == S_drvSerial_OK)
         epicsPrintf("drvSerialTest: Created link to port %s\n", port);
   }
      
   if(status != S_drvSerial_OK) {
      epicsPrintf ("Failed to open port\n");
      return TEST_ERROR;
   }
   else 
      epicsPrintf("Attached to link on port %s\n", port);

   printbuf = 0;

   for (n = 0; n < MAX_ITERATIONS; n++) {
      epicsPrintf ("-- %d ------------------\n", n);
      if (drvSerialTestSend() != TEST_OK)
         return TEST_ERROR;
      epicsThreadSleep(2.0);
      if (drvSerialTestReceive() != TEST_OK)
         return TEST_ERROR;
   }
   
   printbuf = 1;
   return TEST_OK;
}



static const iocshArg drvSerialTestCreateArg0 = {"portname", iocshArgString};
static const iocshArg * const drvSerialTestCreateArgs[] = {&drvSerialTestCreateArg0};
static const iocshFuncDef drvSerialTestCreateFuncDef = {"drvSerialTestCreateLink",  1,  drvSerialTestCreateArgs};
static void drvSerialTestCreateCallFunc(const iocshArgBuf *args )
{
   drvSerialTestCreateLink(args[0].sval);
}


static const iocshArg drvSerialTestAttachArg0 = {"portname", iocshArgString};
static const iocshArg * const drvSerialTestAttachArgs[] = {&drvSerialTestAttachArg0};
static const iocshFuncDef drvSerialTestAttachFuncDef = {"drvSerialTestAttachLink",  1,  drvSerialTestAttachArgs};
static void drvSerialTestAttachCallFunc(const iocshArgBuf *args )
{
   drvSerialTestAttachLink(args[0].sval);
}


static const iocshArg drvSerialTestArg0 = {"portname", iocshArgString};
static const iocshArg * const drvSerialTestArgs[] = {&drvSerialTestArg0};
static const iocshFuncDef drvSerialTestFuncDef = {"drvSerialTest",  1,  drvSerialTestArgs};
static void drvSerialTestCallFunc(const iocshArgBuf *args )
{
   drvSerialTestAll(args[0].sval);
}



static const iocshFuncDef drvSerialTestSendFuncDef = {"drvSerialTestSend",  0,  NULL};
static void drvSerialTestSendCallFunc(const iocshArgBuf *args )
{
   drvSerialTestSend();
}


static const iocshFuncDef drvSerialTestReceiveFuncDef = {"drvSerialTestReceive",  0,  NULL};
static void drvSerialTestReceiveCallFunc(const iocshArgBuf *args )
{
   drvSerialTestReceive();
}


static void drvSerialTestRegisterCommands(void)
{
   static int firstTime = 1;
   if (firstTime) {
      iocshRegister(&drvSerialTestFuncDef, drvSerialTestCallFunc);
      iocshRegister(&drvSerialTestCreateFuncDef, drvSerialTestCreateCallFunc);
      iocshRegister(&drvSerialTestAttachFuncDef, drvSerialTestAttachCallFunc);
      iocshRegister(&drvSerialTestSendFuncDef, drvSerialTestSendCallFunc);
      iocshRegister(&drvSerialTestReceiveFuncDef, drvSerialTestReceiveCallFunc);
      firstTime = 0;
   }
}
epicsExportRegistrar(drvSerialTestRegisterCommands);

