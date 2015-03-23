#!/bin/bash

if (( $# != 2 )); then
	echo "Usage: <p4 or p5> <logfile name>"
	exit 1
fi

LOGFILE_TXT=$2.txt
LOGFILE_SOCK=$2.sock

if [[ $1 == "p4" ]]; then
	PID_WATCHDOG=`ps -aux|grep "[P]assengerWatchdog"|awk '{print $2}'`
	PID_SERVER=`ps -aux|grep "[P]assengerHelperAgent"|awk '{print $2}'`
	PID_LOGGER=`ps -aux|grep "[P]assengerLoggingAgent"|awk '{print $2}'`
	# ?app preloader..
else
	# Passenger 5
	PID_WATCHDOG=`ps -aux|grep "[P]assengerAgent watchdog"|awk '{print $2}'`
	PID_SERVER=`ps -aux|grep "[P]assengerAgent server"|awk '{print $2}'`
	PID_LOGGER=`ps -aux|grep "[P]assengerAgent logger"|awk '{print $2}'`
fi

if  [[ -z ${PID_LOGGER} ]]; then
	echo "One or more Passenger PIDs not found, is it running?"
	exit 1
fi

lsof | grep Passenger | grep "${PID_WATCHDOG}\|${PID_SERVER}\|${PID_LOGGER}" > ${LOGFILE_TXT}
grep "socket" ${LOGFILE_TXT} | wc -l > ${LOGFILE_SOCK}
echo "Logged socket count `cat ${LOGFILE_SOCK}` to ${LOGFILE_SOCK} (full log in ${LOGFILE_TXT}), PIDs were ${PID_WATCHDOG} ${PID_SERVER} ${PID_LOGGER}"
exit 0
