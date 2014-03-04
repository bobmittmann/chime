#!/bin/sh

BKPDIR=`cd ..; pwd`

SRCDIR=`pwd`
BASENAME=`basename ${SRCDIR}`
BKPNAME=${BASENAME}-`date +%Y-%m-%d-%H`.tar.gz
BKPPATH=${BKPDIR}/${BKPNAME}
EXCLUDE="${BASENAME}/tmp* ${BASENAME}/*/.deps"
EXCLUDE=${EXCLUDE}" ${BASENAME}/*/*.elf ${BASENAME}/*/*.exe" 
EXCLUDE=${EXCLUDE}" ${BASENAME}/*/*.o ${BASENAME}/*/*.a" 

echo 
echo Backup file: ${BKPPATH}
echo ------------
echo 

cd ..

for p in ${EXCLUDE} ; do
	TAR_ARGS="${TAR_ARGS} --exclude=$p"
done

tar -vzc --exclude-vcs --exclude="*~" ${TAR_ARGS} -f ${BKPPATH} ${BASENAME} 

echo 
echo ${BKPPATH}
echo 

