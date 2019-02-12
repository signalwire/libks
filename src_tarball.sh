#!/bin/bash
VERSION=`cat CMakeLists.txt | grep "project(" | grep -oP '(\d.)+'`
#Trim variable
VERSION=`echo $VERSION`
echo $VERSION
ORIGDIR=`pwd`
TMPDIR=libks.$$

mkdir -p ../${TMPDIR}

cd ..
cp -a libks ${TMPDIR}/libks-$VERSION
cd ${TMPDIR}
rm -rf libks-$VERSION/.git*
tar zcvf libks-$VERSION.tar.gz libks-$VERSION
mv libks-$VERSION.tar.gz ${ORIGDIR}/.

cd ${ORIGDIR}
rm -rf ../${TMPDIR}