/*
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


#define TEST_OK		0
#define TEST_ERROR	(-1)
#define	MAX_BUFFER	255	/* can be up to 255 bytes */
#define	MAX_ITERATIONS	10

static int drvSerialTestDebugLevel	= 1;

struct privStruct { 
    char		port[100];	/* serial port name */ 
    drvSerialLinkId	id;		/* link id */ 
};

/* Link if for the port in use. Only one port can be used a time.
 */
static drvSerialLinkId	linkId;

/* Buffer where the message to send is created by fillBuffer()
 */
static unsigned char testDataBuffer[MAX_BUFFER];

/* printBuffer -- Print the contents of a data buffer in hex format.
 * The size of the buffer is assumed to be fixed.
 */
static void drvSerialTestPrintBuffer (char *label, char *buffer)
{
	int		n;
	unsigned char	ch;

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

/* drvSerialSendRequest -- This is the routine that does the actual writing of the
 * bytes to the serial port. It is called by dvrSerial (callback).
 */
static int drvSerialTestSendRequest (FILE *pFile, drvSerialRequest *pReq) 
{ 
	int	status, nwritten;

	nwritten = fwrite (pReq->buf, sizeof (epicsUInt8), pReq->bufCount, pFile);

	if (drvSerialTestDebugLevel > 0)
	    epicsPrintf ("sendRequest: nwritten=%d\n", nwritten);

	if (nwritten == pReq->bufCount)
	    status = TEST_OK;
	else
	    status = TEST_ERROR;
	
	return status;
} 

/* drvSerialTestParser -- This is the routine in charge of reading (parsing) characters
 * from the serial port. It's the responsibility of this routine to decide when
 * to stop reading and return. The current implementation reads a fixe number
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

	if (pResp->bufCount > 0 && drvSerialTestDebugLevel > 1)
	    drvSerialTestPrintBuffer("drvSerialTestParser", pResp->buf);

	return pResp->bufCount;
}

/* --- end of local routines --*/

/* drvSerialTestInitialize -- Routine used to do all necesary initialization.
 * At this point it only takes care of initializing the test data.
 */
int drvSerialTestInitialize ()
{
	int	n;

	memset (testDataBuffer, 0, MAX_BUFFER);
	for (n = 0; n < MAX_BUFFER; n++)
	    *(testDataBuffer + n) = n;
	return TEST_OK;
}

int drvSerialTestDebug (int level)
{
	drvSerialTestDebugLevel = level;
	return TEST_OK;
}

/* drvSerialTestCreateLink -- Routine to test drvSerialCreateLink(). The link id
 * is stored in the global variable linkId for later use.
 */
int drvSerialTestCreateLink (char *port)
{
	int			status;
	struct privStruct	priv, *ppriv = &priv;

	status = drvSerialCreateLink (port, drvSerialTestParser, ppriv, &ppriv->id);
	linkId = ppriv->id;

	if (drvSerialTestDebugLevel > 0)
	    epicsPrintf("linkId=%p\n", linkId);

	return status;
}

/* drvSerialTestAttachLink -- Routine to test drvSerialAttachLink. It will return an
 * error if called before serialTestCreateLink().
 */
int drvSerialTestAttachLink (char *port)
{
	int			status;
	struct privStruct	priv, *ppriv = &priv;

        status = drvSerialAttachLink (port, drvSerialTestParser, (void **) &ppriv);

        return status;
}

/* drvSerialTestSend -- Routine to test drvSerialSendRequest(). It should be called
 * after calling serialTestCreateLink().
 */
int drvSerialTestSend ()
{
	drvSerialRequest	req; 

	if (drvSerialTestDebugLevel > 1)
	    epicsPrintf("sizeof(req)=%lu, sizeof(req.buf)=%lu\n", (long)sizeof(req), (long)sizeof(req.buf));

	if (MAX_BUFFER > sizeof(req.buf)) {
	    epicsPrintf ("Buffer too big!\n");
	    return TEST_ERROR;
	}

	memcpy((char *) req.buf, testDataBuffer, MAX_BUFFER);
	if (drvSerialTestDebugLevel > 1)
	    drvSerialTestPrintBuffer("serialTestSend", req.buf);
        req.bufCount    = MAX_BUFFER;
        req.pCB         = drvSerialTestSendRequest; 
        req.pAppPrivate = NULL; 

        drvSerialSendRequest (linkId, dspLow, &req);

	return 0;
}

/* drvSerialTestReceive -- Routine to test drvSerialNextResponse(). Note that the actual
 * reading of the bytes is done by drvSerialTestParser, which puts them into a temporary buffer
 * inside drvSerial. It should be called after calling serialTestCreateLink().
 */
int drvSerialTestReceive()
{ 
	int			status; 
	drvSerialResponse	resp; 

	status = drvSerialNextResponse (linkId, &resp); 

	if (status == S_drvSerial_noEntry) {
	    if (drvSerialTestDebugLevel > 0)
		epicsPrintf ("No data\n");
	    return TEST_ERROR;
	} else if (status != S_drvSerial_OK) {
	    return TEST_ERROR;
	}

	if (drvSerialTestDebugLevel > 1) {
	    drvSerialTestPrintBuffer("serialTestReceive", resp.buf);
	    epicsPrintf ("bufCount=%d\n", resp.bufCount);
	}

	if (memcmp(resp.buf, testDataBuffer, MAX_BUFFER) != 0) {
	    epicsPrintf ("Buffers don't agree\n");
	    return TEST_ERROR;
	} else {
	    if (drvSerialTestDebugLevel > 0)
		epicsPrintf ("Buffers agree\n");
	    return TEST_OK;
	}

	return TEST_OK;
}

/* drvSserialTestAll -- End to end test. It creates a link, and send and recives
 * date multiple times to check for transmission errors.
 */
int drvSerialTestAll(char *port)
{
	int	n;

	drvSerialTestInitialize();

	if (drvSerialTestCreateLink (port) != TEST_OK) {
	    epicsPrintf ("Failed to open port\n");
	    return TEST_ERROR;
	}

	for (n = 0; n < MAX_ITERATIONS; n++) {
	    epicsPrintf ("-- %d ------------------\n", n);
	    if (drvSerialTestSend() != TEST_OK)
		return TEST_ERROR;
	    //taskDelay(100);
	    epicsThreadSleep(0.5);
	    if (drvSerialTestReceive() != TEST_OK)
		return TEST_ERROR;
	}

    	return TEST_OK;
}





static const iocshArg drvSerialTestDebugArg0 = {"test debug level", iocshArgInt};
static const iocshArg * const drvSerialTestDebugArgs[] = {&drvSerialTestDebugArg0};
static const iocshFuncDef drvSerialTestDebugFuncDef = {"drvSerialTestDebug",  1,  drvSerialTestDebugArgs};
static void drvSerialTestDebugCallFunc (const iocshArgBuf *args)
{
  drvSerialTestDebug(args[0].ival);
  epicsPrintf ("drvSerialTestDebug: Setting debug level %d\n", drvSerialTestDebugLevel);
}


static const iocshArg drvSerialTestArg0 = {"portname", iocshArgString};
static const iocshArg * const drvSerialTestArgs[] = {&drvSerialTestArg0};
static const iocshFuncDef drvSerialTestFuncDef = {"drvSerialTest",  1,  drvSerialTestArgs};
static void drvSerialTestCallFunc(const iocshArgBuf *args )
{
   drvSerialTestAll(args[0].sval);
}

static void drvSerialTestRegisterCommands(void)
{
   static int firstTime = 1;
   if (firstTime) {
      iocshRegister (&drvSerialTestDebugFuncDef, drvSerialTestDebugCallFunc);
      iocshRegister(&drvSerialTestFuncDef, drvSerialTestCallFunc);
      firstTime = 0;
   }
}
epicsExportRegistrar(drvSerialTestRegisterCommands);

