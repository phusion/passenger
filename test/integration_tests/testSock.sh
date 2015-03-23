#!/bin/bash

if (( $# != 3 )); then
        echo "Usage: <p4 or p5> <test url> <test name>"
        exit 1
fi

PASSENGER_VERSION=$1
TEST_URL=$2
TEST_NAME_PRE=$3_pre
TEST_NAME_POST=$3_post

./logSock.sh ${PASSENGER_VERSION} ${TEST_NAME_PRE} 
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

ab -c 50 -n 10000 -C NO_CACHE=1 ${TEST_URL}
sleep 3
./logSock.sh ${PASSENGER_VERSION} ${TEST_NAME_POST}
rc=$?; if [[ $rc != 0 ]]; then exit $rc; fi

echo "--- TEST RESULT --------------------------------"
COUNT_PRE=`cat ${TEST_NAME_PRE}.sock`
COUNT_POST=`cat ${TEST_NAME_POST}.sock`
DELTA=`expr ${COUNT_POST} - ${COUNT_PRE}`

if [ ${DELTA} -eq 0 ]; then
	echo "SUCCESS: socket count stable at ${COUNT_PRE}"
	exit 0
else
	echo "FAIL: socket count delta ${DELTA} (from ${COUNT_PRE} to ${COUNT_POST})"
	exit 1
fi
