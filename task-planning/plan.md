
# Selected for implementation

## Over the Air (OTA) updates

#### Problem

this would allow users to update the firmware without having to use additional USB cable and eases quick fixes

#### Solution

- Implement OTA updates via HTTP
- using plain old HTTP POST form submission format, to ease use of eg. `curl` and allow use in a simple web form 
- Additionally: provide a simple user-friendly interface for initiating updates
- Use checksums to ensure data integrity during the update process

----------------------------------

## Discoverable devices

#### Problem

How to discover devices, which get IP address assigned via DHCP?
Ease of use in different networks/environments, from desktop development to installation on a larger network

#### Solution

- Use DHCP to get an IP
- Use UPNP to broadcast the device's presence on the network
    - broadcast NOTIFY alive messages every 45 seconds
    - announce the device's presence on the network
    - use some kind of special identifier to identify the device, which must be UPNP standard compliant and should not do harm to other services listening
    - ideally, there's an identifier used, which marks the device belongs to a device group but also has it's own unique name and/or ID
- Being able to react on M-SEARCH find devices request
- Use string concatenation and formatstring instead of full flavored XML library
- provide simple UPNP XML description, with minimal content

UPNP technical details:
* https://embeddedinn.wordpress.com/tutorials/upnp-device-architecture/
* https://upnp.org/resources/documents/UPnP_UDA_tutorial_July2014.pdf
* https://upnp.org/specs/arch/UPnP-arch-DeviceArchitecture-v2.0.pdf
