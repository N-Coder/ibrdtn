#!/bin/sh

echo "Cleaning Android.mk files..."
echo "----------------------------"
for SOURCE in "ibrcommon" "ibrdtn" "dtnd"
do
	echo ""
	echo "Cleaning Android.mk files for $SOURCE"
	echo "-------------------------------------"
	cd $SOURCE
    make clean
	cd ..
done

echo ""
echo "Cleaning SWIG wrapper classes..."
echo "--------------------------------"
rm -Rf ../java/de/tubs/ibr/dtn/swig
