TODO:
## Progress:
Stage 1:
- Windows can identify the device as MTP device !YAY!
- Next ? respond to Windows request (OPEN_SESSION) command ?
  - can we use usb workqueue ? or create our own queue ? or a separate thread
  - usb_write vs usb_transfer ?
  - when `mtp_bulk_in_cb` gets triggered ?



Stage 2:
- Thanks to Allah, it is now in "My Computer"
- Learned points:
  - Always check if data is sent correctly using the "free device monitor" tool
  - Communication with Host always ends with Response Block (RESP_OK or Error)
  - So send data + OK is a must otherwise host will be waiting for more data
- Review points:
  - is it mandatory to have microsoft.com and/or android.com
  - check if all Object operations are really needed
  - check how to send confirmation packet after data packet without raising stm32 usb driver Error
  - Can i replace Samsung manufacture, VID/PID values ?


Stage 3:
- Thanks to Allah, it can now show 2 storage partitions
- ~~Partitions info neeeds to be checked ?!~~
- ~~Commit current state~~
- Refactor and optimize before continuing
