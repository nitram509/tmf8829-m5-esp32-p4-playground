
# Ideas to be considered

## Self-contained web application

* the M5 ESP32 P4 module shall run a web server, which serves a small web application, which allows users to use the DToF sensor straight away, without installing anything

* useful pre-conditions
  * UPNP support
    * so people can easily find the device, when plugin into PoE network
    * if somehow possible, it would be great to make it visible in FritzBox or in OSX network environment view in Finder
  * Over the Air (OTA) updates
    * this would allow users to update the firmware without having to use additional USB cable and eases quick fixes

## Secured endpoints, gRPC

to prevent malicious use and access
Open Question: is this worth the effort - for the Nation installation?