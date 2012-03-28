#!/bin/bash
#
# Simple script to convert a directory of jpg images
# to pgm (Portable Grascale Map) binary images. (P5)
#
# Requires the 'convert' command to be installed (ImageMagick)
#
echo "Converting from JPG to PGM"

# Loop through the jpg images in the current directory
# and convert to ppm files
for f in `ls *.jpg`
do
    echo "Processing $f"
    convert $f ${f%jpg}pgm
done
echo "Done"

# Exit
exit 0;


