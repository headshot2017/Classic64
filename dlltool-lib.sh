#!/bin/sh

if [ $# -eq 0 ]
then
	echo "usage: dlltool-lib [path to ClassiCube.exe]"
	exit
fi

noext=$(basename "$1" .exe)

gendef $1
dlltool -d ${noext}.def -l lib${noext}.a -D $1
