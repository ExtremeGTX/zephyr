TODO:
## Progress:
### Stage 1:
- Windows can identify the device as MTP device !YAY!
  - All Endpoints must be decalred otherwise identification will fail.
    - Example: defined IN, OUT EPs only -> FAIL
    - Example: defined IN, OUT, Interrupt EPs -> Success
  - Also the sequence of identifying the EPs is important
    - Example: defined Interrupt, IN, OUT EPs -> FAIL
    - Example: defined IN, OUT, Interrupt EPs -> Success

- Next ? respond to Windows request (OPEN_SESSION) command ?
  - can we use usb workqueue ? or create our own queue ? or a separate thread
  - usb_write vs usb_transfer ?
  - when `mtp_bulk_in_cb` gets triggered ?

### Stage 2:
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

### Stage 3:
- Thanks to Allah, it can now show 2 storage partitions
- ~~Partitions info neeeds to be checked ?!~~
- ~~Commit current state~~
- Refactor and optimize before continuing

### Stage 4:
- Thanks to Allah, it can now shows a folder and a file in the first partition
- HHD Software sent me a license valid for 6 months for Device monitoring studio (Ultimate edition).

### Stage 5:
- Thanks to Allah, it can show files/folders on 2 volumes
- User can copy from device to host, or double click to view
- [x] Commit work after structs removal
- [x] Query files from flash file system (littlefs) and show them to the host.
  - |StorageID|ParentID|Type|ObjectID|
  - Think about using fsdirn_t struct

## Stage 6
- Back to parsing issue in SEND_OBJECT_INFO Command and why it is wrong
- SEND_OBJECT is DONE ^_^ yay! Thanks to Allah (21/11/2024), it can handle write a file and read it back!

## Next
- Use type

## Ideas/work:
Operations to be implemented:
- [x] Read a file  (Copy from device -> Host)
  - [x] First packet is fine
  - [X] Subsequents packets still not sent properly
    - [x] You must fill up the 1st packet to the max before sending the subsequenet packets otherwise it won't work
      - Example (file size = 1KB): 1st packet (Header + 10 bytes)
      - Rest of bytes in next bytes
      - FAIL
      - Example (file size = 1KB): 1st packet (Header + (filling the rest of 512 bytes of buf) bytes)
      - Rest of bytes in next bytes
      - SUCCESS!

- [x] Write a file (Copy from Host -> device)
  - [x] I faced a problem when host after sending `SEND_OBJECT_INFO` it sends `GET_STORAGE_INFO` instead of `SEND_OBJECT`
        the reason was i sent wrong new_object_id which i think was `0x000000000` it was a pointer access problem
- [x] Delete a file

- [ ] Implement open/close session and allow only one session (this is the widely used setup)

# Fixes/Issues
- [ ] Fix battery level
- [ ] Support custom icon ?
- [ ] Fix storage_id, Support REMOVABLE_RAM for SDCard, all storages now is advertised as FIXED_RAM
- [ ] Check if a partition is ready-only and update protection status accordingly
- [ ] Get Manufacturere and Serial number from Defined configs

# Configs
- CONFIG_MAX_OBJECT_HANDLES: Number of maxium object handles allocated for (files, folders)
  including the objects which will be written from Host
- READ_ONLY_MTP



## Notes
Mount SD Card
`fs mount fat /SD:`



No MTP
Memory region         Used Size  Region Size  %age Used
           FLASH:      137360 B         2 MB      6.55%
             RAM:      106664 B       384 KB     27.13%

with MTP
Memory region         Used Size  Region Size  %age Used
           FLASH:      164712 B         2 MB      7.85%
             RAM:      128912 B       384 KB     32.78%

Flash usage: ~27KB
RAM usage: ~22KB