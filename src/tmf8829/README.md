
This firmware is downloaded from https://ams-osram.com/tmf8829
First, fownload and unzip TMF8829_Driver_Arduino_v1.2.3.zip

To convert the `tmf8829_application.hex` file into C-source file, 
use the tool http://srecord.sourceforge.net/
`srec_cat -o tmf8829_image.c -C-Array tmf8829_image -INClude tmf8829_application.hex -Intel`
