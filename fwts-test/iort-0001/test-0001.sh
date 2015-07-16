#!/bin/bash
#
TEST="Test apci table against IORT"
NAME=test-0001.sh
TMPLOG=$TMP/iort.log.$$

$FWTS --log-format="%line %owner " -w 80 --dumpfile=$FWTSTESTDIR/iort-0001/acpidump-0001.log iort - | cut -c7- | grep "^iort" > $TMPLOG
diff $TMPLOG $FWTSTESTDIR/iort-0001/iort-0001.log >> $FAILURE_LOG
ret=$?
if [ $ret -eq 0 ]; then
	echo PASSED: $TEST, $NAME
else
	echo FAILED: $TEST, $NAME
fi

rm $TMPLOG
exit $ret
