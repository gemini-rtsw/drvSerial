
/* drvSerial.h */
/* $Id: drvSerial.h,v 1.1.1.1 2005/01/11 02:38:31 wdahl Exp $ */
/*
 *     	Serial Port EPICS Driver Support
 *
 *      Author:         Jeffrey O. Hill
 *      Date:           6-6-95
 *
 * Modification Log:
 * -----------------
 * $Log: drvSerial.h,v $
 * Revision 1.1.1.1  2005/01/11 02:38:31  wdahl
 * unixIoc for EPICS 3.14.6
 *
 * Revision 1.1  2003/04/02 02:56:38  ktsubota
 * Initial insertion
 *
 * Revision 1.1  2003/01/14 00:24:04  ktsubota
 * Initial insertion
 *
 * Revision 1.2  2002/09/05 21:07:18  ahoney
 * Most recent version updated by J. Hill.
 *
 * Revision 1.5  1999/12/23 16:55:39  hill
 * fixed warnings drvAbdf1.c
 *
 * Revision 1.4  1998/02/11 23:26:09  aptdvl
 * 1st production release
 *
 * Revision 1.2  1995/09/01  07:44:31  jhill
 * parens in macro
 *
 * Revision 1.1  1995/08/24  07:12:43  jhill
 * installed into cvs
 *
 *
 * NOTES:
 */






/*
 * convenience routines which set/get the baud rate from the vxWorks shell
 * (called before iocInit). pName specifies the device or file name.
 */
#if  0
/* These don't seem to be implemented */
int drvSerialSetBaudRate (const char *pName, unsigned rate);
int drvSerialGetBaudRate (const char *pName);
#endif



/*
 * Buffer large enough to hold the largest response
 */
typedef struct
{
	void *pAppPrivate;
	unsigned bufCount;
	char	buf[0x100];
} drvSerialResponse;

typedef void *drvSerialLinkId;

/*
 * returns number of bytes added to pResp or EOF 
 * (called by the in task)
 */
typedef int drvSerialParseInput(FILE *pf, 
	drvSerialResponse *pResp, void *pAppPrivate);

long
drvSerialCreateLink(
		    const char *pDeviceName,	/* STDIO file name */
		    drvSerialParseInput *pAppParser,
		    void *pAppPrivate,
		    drvSerialLinkId * pId
);

long
drvSerialAttachLink(
                    const char *pName,
                    drvSerialParseInput *pParser,
                    void **ppAppPrivate
);


/*
 * returns the file pointer for this serial link
 * if the link is established (otherwise it returns EOF).
 */
FILE *drvSerialGetFile (drvSerialLinkId id, long direction);

/*
 * Pull the next response off the response queue
 * (return error if none are present)
 */
long
drvSerialNextResponse(
		      drvSerialLinkId id,
		      drvSerialResponse * pResponse);

/*
 * Buffer large enough to hold the largest request
 *
 * The send call back is called to deliver the frame
 * to the stream by the send task when the
 * frame reaches the top of the queue
 *
 * returns 0 on success or EOF
 */
typedef struct drvSerialRequestTag
{
	int (*pCB)(FILE *pf, struct drvSerialRequestTag *pReq);
	void *pAppPrivate;
	unsigned bufCount;
	char buf[0x100];
}drvSerialRequest;
typedef int drvSerialSendCB (FILE *pf, drvSerialRequest *pReq);

/*
 * Place a request in the request queue
 *
 * The events callback is called immediately after sending
 */
typedef enum
{
	dspLow, dspMed, dspHigh
} drvSerialPriority;
#define dspLowest dspLow
#define dspHighest dspHigh

long
drvSerialSendRequest(
		     drvSerialLinkId id,
		     drvSerialPriority pri,
		     const drvSerialRequest * pRequest);

/*
 * call this to reserve a slot in the request queue
 * so that we are guaranteed to have queue space
 * available in the future
 *
 * (returns NULL if no queue space available)
 */
drvSerialRequest *
drvSerialCreateReservedRequest(
				drvSerialLinkId id,
				drvSerialPriority pri);

/*
 * call this to send a request reserved with
 * drvSerialCreateRequest() above
 */
long
drvSerialSendReservedRequest(
				drvSerialLinkId id,
				const drvSerialRequest * pRequest);

/*
 * Usually it is ok for the "drvSerialParseInput" function
 * to be only called when there is a free slot in the
 * input queue (the default). In certain special situations it is desirable
 * for all input to be discarded when the input queue
 * is full, and for a special action to be taken when
 * an input frame is discarded. If your application requires
 * this behavior, then test to see if there is at least
 * one free slot in the input queue with the following funtion
 * prior to returning from the "drvSerialParseInput" routine,
 * and thereby avoid returning an input message to drvSerial if the
 * input queue is full.
 *
 * this function returns TRUE/FALSE
 */
int
drvSerialInputQueueIsFull(drvSerialLinkId id);

/*
 * need to register this in errMdef.h
 */
#define M_drvSerialLib (1000<<16)
#define S_drvSerial_OK 0
#define S_drvSerial_noEntry (M_drvSerialLib | 1) /* no response on the queue*/
#define S_drvSerial_badParam (M_drvSerialLib | 2)	/* unable to set option */
#define S_drvSerial_paramConflict (M_drvSerialLib | 3)	/* requested options conflict between applications */
#define S_drvSerial_noInit (M_drvSerialLib | 4)	/* drvSerial has not been initialized */
#define S_drvSerial_linkInUse (M_drvSerialLib | 5)	/* serial device is in use */
#define S_drvSerial_noParser (M_drvSerialLib | 6)	/* no parser */
#define S_drvSerial_EOF (M_drvSerialLib | 7)		/* link down */
#define S_drvSerial_OVF (M_drvSerialLib | 8)		/* no msg term before buf ovf */
#define S_drvSerial_queueFull (M_drvSerialLib | 9)	/* queue quota exceeded */
#define S_drvSerial_invalidArg (M_drvSerialLib | 10)	/* invalid argument */
#define S_drvSerial_noDevRead (M_drvSerialLib | 11)	/* open for read failed */
#define S_drvSerial_noDevWrite (M_drvSerialLib | 12)	/* open for write failed */
#define S_drvSerial_linkDown (M_drvSerialLib | 13)	/* serial link is down */
#define S_drvSerial_noMemory (M_drvSerialLib | 14)	/* out of dynamic memory*/
#define S_drvSerial_noneAttached (M_drvSerialLib | 15)	/* no app is using the link*/
#define S_drvSerial_internal (M_drvSerialLib | 16)	/* internal */
