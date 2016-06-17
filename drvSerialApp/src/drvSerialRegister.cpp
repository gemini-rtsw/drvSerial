/*drvSerialRegister.c */

#include <iocsh.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* drvSerialInit registration setup */

extern int drvSerialInit(void);
static const iocshFuncDef InitDef = {"drvSerialInit",0,0};
static void InitCall(const iocshArgBuf *arg)
{
    drvSerialInit();
}

/* drvSerialReport registration setup */
extern long  drvSerialReport(int level);
static const iocshArg ReportArg0 = {"level",iocshArgInt};
static const iocshArg *ReportArgs[1] = {&ReportArg0};
static const iocshFuncDef ReportDef = {"drvSerialReport",1,ReportArgs};
static void ReportCall(const iocshArgBuf *args)
{
    drvSerialReport(args[0].ival);
}


/* register  */
void drvSerialRegisterCommands()
{
    static int firstTime = 1;
    if(!firstTime) return;
    firstTime=0;
    iocshRegister(&InitDef,InitCall);
    iocshRegister(&ReportDef,ReportCall);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

class serialSioInit {
public:
    serialSioInit() {drvSerialRegisterCommands();}
};

static serialSioInit serialSioInit;

