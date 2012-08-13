#/bin/sh

OUTFILE="$1"

rm -rf $OUTFILE

function printit()
{
  local typ="$1"
  local src="$2"
  local ext="$3"
  local files=(`find $src -type f -name "$ext" | "xargs"`)
  local file_count=${#files[@]}
  for ((i=0; i<${file_count}; i++));
  do
      echo "$typ += ../${files[$i]}" >> $OUTFILE
  done
}

printit HEADERS src "*.h"
printit HEADERS src "*.hpp"
printit SOURCES src "*.cpp"
printit SOURCES src "*.c"
