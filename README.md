This is the drvSerial EPICS support module.

To get the latest sources, follow this procedure 
(from https://epics.anl.gov/tech-talk/2001/msg00662.php):

To access the keck ftp site:

	ftp ftp.keck.hawaii.edu
	login as anonymous
	cd outgoing/epics/drivers
	you would need to pull files out of the subdirectories (ls not dir)
	drvAscii and drvSerial


Note that there is a demonstration EPICS database which uses a VME
processor's console port (you need to tie tx to rx to play with it).

I you are going to connect your remote devices to a terminal server
then you will also need the tnet driver, which is somewhere in the
EPICS distribution.

