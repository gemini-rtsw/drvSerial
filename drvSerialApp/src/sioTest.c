/*
 * ANSI C
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
 * EPICS
 */
#include <epicsPrint.h>
#include <epicsAssert.h>
#include <epicsThread.h>
#include <epicsExport.h>
#include <iocsh.h>

#include "drvSerial.h"


#define OK  0
#define ERROR  (-1)

int sioTxDelay = 1,
    sioDisplay = 0,
    sioTxFaultSim = 0,
    sioRxFaultSim = 0;

drvSerialLinkId sioId = 0;

static  int sioStreamIn( FILE *pf, drvSerialResponse *pResp, 
		       void *pAppPrivate)
{
  int c;

  while ( (c=getc(pf))!=EOF) {

    if ( sioDisplay ) { 
      if ( c < 32 || c > 126 ) 
         printf("[%2d]", c ); 
      else printf("%c",c);
    }

  /* 
   * this causes drvSerial to delete and restart the io tasks
   * this is for testing said behavior
   */
    if ( sioRxFaultSim ) return EOF;
  }
  return c;
}

static int sioStreamOut (FILE *pf, drvSerialRequest *pReq)
{
  unsigned char *pBuf;
  int s=0;

  for (pBuf=pReq->buf; pBuf<&pReq->buf[pReq->bufCount]; pBuf++) {
    s = putc(*pBuf, pf);
    if (s == EOF) {
      errlogPrintf("\nEOF returned from putc()\n");
      break;
    }
  }

  /* 
   * this causes drvSerial to delete and restart the io tasks
   * this is for testing said behavior
   */
  if ( sioTxFaultSim ) return EOF;
  return s;
}


void sioInit( char *pName )
{
  int status;
  void *pId;
  

/* this should already be done by iocInit()  */
#if 0
  status = drvSerialInitSts();
  
  if ( status == S_drvSerial_noInit ) {
    status = drvSerialInit();
    assert(status==OK);
  }
#endif
 
  status = drvSerialAttachLink( pName,
				sioStreamIn,
				(void **)&pId );
  
  if ( status == S_drvSerial_noneAttached ) {
    status = drvSerialCreateLink( pName, sioStreamIn, NULL, &sioId );
    assert(status==OK);
  }
  else {
    printf("\nLink already exists for %s\n", pName);
  }
  
}

void sioTx( char *pBfr, int count )
{
  drvSerialRequest req;
  int status;

  memset(&req, '\0', sizeof(req));
  if ( !count ) 
    count = strlen(pBfr);

  memcpy( req.buf, pBfr, count);
  req.bufCount = count;
  req.pCB = sioStreamOut;

  status = drvSerialSendRequest( sioId, dspLow, &req );

  if (	status != S_drvSerial_OK && 
	status != S_drvSerial_queueFull ) {
    printf("sending of request failed with status = %x\n", status);
  }    
}

void sioTest( char *port )
{
  int idx1, idx2;
  char wrBfr[256];

  if (port == NULL) {
     epicsPrintf("Specify port name.\n");
     return;
  }

  if (sioId == 0) {
    /* drvSerialInit();  */ /* This should already be done by iocInit() */
    sioInit( port );
  }

/*
  for (idx1=0; idx1<256; idx1++) {
    for (idx2=0;idx2<10;idx2++)
      wrBfr[idx2] = idx1;
    sioTx( wrBfr, 10 );
    if (sioTxDelay) epicsThreadSleep(0.1);
  }
*/

   sprintf(wrBfr, "Hello World!\n");
   sioTx( wrBfr, strlen(wrBfr)); 

}


static const iocshArg sioTest_Arg0 = { "port", iocshArgString };
static const iocshArg *sioTest_Args[] = { &sioTest_Arg0 };
static const iocshFuncDef sioTest_FuncDef =
   {"sioTest", 1, sioTest_Args};
static void sioTest_CallFunc(const iocshArgBuf *args )
{
   sioTest(args[0].sval);
}
static void sioTest_RegisterCommands(void)
{
   static int firstTime = 1;
   if (firstTime) {
      iocshRegister(&sioTest_FuncDef, sioTest_CallFunc);
      firstTime = 0;
   }
}
epicsExportRegistrar(sioTest_RegisterCommands);

