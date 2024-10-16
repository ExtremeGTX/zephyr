/*
 * Copyright (c) 2024 Mohamed ElShahawi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <wctype.h>
#include <stddef.h>

#include <zephyr/net_buf.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(usb_mtp_impl, 4); //CONFIG_USBD_MTP_LOG_LEVEL

/* MTP Class-Specific Request Codes */
#define MTP_REQUEST_CANCEL              0x64U
#define MTP_REQUEST_GET_DEVICE_STATUS   0x67U
#define MTP_REQUEST_DEVICE_RESET        0x66U

/* MTP Operation Codes */
#define MTP_OP_GET_DEVICE_INFO              0x1001
#define MTP_OP_OPEN_SESSION                 0x1002
#define MTP_OP_CLOSE_SESSION                0x1003
#define MTP_OP_GET_STORAGE_IDS              0x1004
#define MTP_OP_GET_STORAGE_INFO             0x1005
#define MTP_OP_GET_NUM_OBJECTS              0x1006
#define MTP_OP_GET_OBJECT_HANDLES           0x1007
#define MTP_OP_GET_OBJECT_INFO              0x1008
#define MTP_OP_GET_OBJECT                   0x1009
#define MTP_OP_GET_THUMB                    0x100A
#define MTP_OP_DELETE_OBJECT                0x100B
#define MTP_OP_SEND_OBJECT_INFO             0x100C
#define MTP_OP_SEND_OBJECT                  0x100D
#define MTP_OP_RESET_DEVICE                 0x1010
#define MTP_OP_GET_DEVICE_PROP_DESC         0x1014
#define MTP_OP_GET_DEVICE_PROP_VALUE        0x1015
#define MTP_OP_SET_DEVICE_PROP_VALUE        0x1016
#define MTP_OP_RESET_DEVICE_PROP_VALUE      0x1017
#define MTP_OP_MOVE_OBJECT                  0x1019
#define MTP_OP_COPY_OBJECT                  0x101A
#define MTP_OP_GET_PARTIAL_OBJECT           0x101B
#define MTP_OP_GET_OBJECT_PROPS_SUPPORTED   0x9801
#define MTP_OP_GET_OBJECT_PROP_DESC         0x9802
#define MTP_OP_GET_OBJECT_PROP_VALUE        0x9803
#define MTP_OP_SET_OBJECT_PROP_VALUE        0x9804
#define MTP_OP_SET_OBJECT_REFERENCES        0x9811
#define MTP_OP_SKIP                         0x9820

/* MTP Response Codes */
#define MTP_RESP_OK                         0x2001
#define MTP_RESP_GENERAL_ERROR              0x2002
#define MTP_RESP_SESSION_NOT_OPEN           0x2003
#define MTP_RESP_SESSION_ALREADY_OPEN       0x201E
#define MTP_RESP_INVALID_OBJECT_HANDLE      0x2009

/* MTP Image Formats */
#define MTP_FORMAT_ASSOCIATION              0x3001
#define MTP_FORMAT_TEXT                     0x3004

/* MTP Event Codes */
#define MTP_EVENT_OBJECT_ADDED              0x4002
#define MTP_EVENT_OBJECT_REMOVED            0x4003
#define MTP_EVENT_STORE_ADDED               0x4004
#define MTP_EVENT_STORE_REMOVED             0x4005
#define MTP_EVENT_DEVICE_PROP_CHANGED       0x4006
#define MTP_EVENT_OBJECT_INFO_CHANGED       0x4007

/* MTP Device properties */
#define MTP_DEVICE_PROPERTY_BATTERY_LEVEL   0x5001

/* Object Properties */
#define MTP_PROPERTY_STORAGE_ID             0xDC01
#define MTP_PROPERTY_OBJECT_FORMAT          0xDC02
#define MTP_PROPERTY_PROTECTION_STATUS      0xDC03
#define MTP_PROPERTY_OBJECT_SIZE            0xDC04
#define MTP_PROPERTY_OBJECT_FILE_NAME       0xDC07
#define MTP_PROPERTY_DATE_MODIFIED          0xDC09
#define MTP_PROPERTY_PARENT_OBJECT          0xDC0B
#define MTP_PROPERTY_PERSISTENT_UID         0xDC41
#define MTP_PROPERTY_NAME                   0xDC44
#define MTP_PROPERTY_DISPLAY_NAME           0xDCE0
#define MTP_PROPERTY_FAX_NUMBER_BUSINESS    0xDD16

/* Storage Types */
#define STORAGE_TYPE_FIXED_ROM              0x0001
#define STORAGE_TYPE_REMOVABLE_ROM          0x0002
#define STORAGE_TYPE_FIXED_RAM              0x0003
#define STORAGE_TYPE_REMOVABLE_RAM          0x0004

/* Object Protection */
#define OBJECT_PROTECTION_NO	                0x0000
#define OBJECT_PROTECTION_READ_ONLY	            0x0001
#define OBJECT_PROTECTION_READ_ONLY_DATA	    0x8002
#define OBJECT_PROTECTION_NON_TRANSFERRABLE     0x8003

#define MTP_CMD(opcode) do {                    \
            mtp_##opcode(bufp, mtp_command);    \
            } while(0);

#define MTP_CMD_HANDLER(opcode)					    \
static void mtp_##opcode(struct net_buf *buf,       \
                        struct mtp_container* mtp_command)

enum mtp_container_type {
    MTP_CONTAINER_UNDEFINED = 0x00,
    MTP_CONTAINER_COMMAND,
    MTP_CONTAINER_DATA,
    MTP_CONTAINER_RESPONSE,
    MTP_CONTAINER_EVENT,
};

struct mtp_container {
    uint32_t length;  // Total length of the command block
    uint16_t type;    // Should be 0x0001 for Command Block
    uint16_t code;    // MTP operation code (e.g., MTP_OP_OPEN_SESSION)
    uint32_t transaction_id;    // Transaction ID to track the command
    uint32_t param[5];            // Optional Parameter 1 (e.g., session ID)
} __packed;

struct mtp_data_block {
    uint32_t container_length;  // Total length of the response block
    uint16_t container_type;    // Should be 0x0002 for Data Block
    uint16_t response_code;     // MTP response code (e.g., MTP_RESP_OK)
    uint32_t transaction_id;    // Transaction ID of the command being responded to
} __packed;

struct mtp_device_info {
    uint16_t standard_version;
    uint32_t vendor_extension_id;
    uint16_t vendor_extension_version;
    uint8_t  vendor_extension_desc_len;
    uint16_t vendor_extension_desc[38];  // Vendor extension description in UTF-16LE
    uint16_t functional_mode;
    uint32_t operations_supported_count;
    uint16_t operations_supported[18];
    uint32_t events_count;
    uint16_t events_supported[6];
    uint32_t device_properties_count;
    uint16_t device_properties_supported[1];
    uint32_t formats_count;
    uint32_t image_formats_count;
    uint16_t image_formats[2];
    uint8_t manufacturer_len;
    uint16_t manufacturer[7];
    uint8_t model_len;
    uint16_t model[10];
    uint8_t device_version_len;
    uint16_t device_version[4];
    uint8_t serial_number_len;
    uint16_t serial_number[17];
} __packed;

struct storage_info_t{
    uint16_t type;
    uint16_t fs_type;
    uint16_t access_caps;
    uint64_t max_capacity;
    uint64_t free_space;
    uint32_t free_space_obj;
    uint8_t storage_desc_len;
    uint16_t storage_desc[17];
    uint8_t  volume_id_len;
    uint16_t volume_id_desc[9];
} __packed;

struct mtp_object_info {
    uint32_t StorageID;                 // Example: 0x00010001 (Storage ID 1)
    uint16_t ObjectFormat;              // Example: 0x3001 (Association/Folders)
    uint16_t ProtectionStatus;          // Example: 0x0000 (No protection)
    uint32_t ObjectCompressedSize;      // Example: 0x0000000000010000 (64KB)
    uint16_t ThumbFormat;               // Example: 0x3801 (JPEG Thumbnail)
    uint32_t ThumbCompressedSize;       // Example: 0x00002000 (8KB)
    uint32_t ThumbPixWidth;             // Example: 128 pixels
    uint32_t ThumbPixHeight;            // Example: 128 pixels
    uint32_t ImagePixWidth;             // Example: 1920 pixels (Full Image Width)
    uint32_t ImagePixHeight;            // Example: 1080 pixels (Full Image Height)
    uint32_t ImageBitDepth;             // Example: 24-bit color depth
    uint32_t ParentObject;              // Example: 0x00000000 (No parent)
    uint16_t AssociationType;           // Example: 0x0001 (Folder)
    uint32_t AssociationDesc;           // Example: 0x00000000 (No association desc)
    uint32_t SequenceNumber;            // Example: 0x00000001 (First object)

    uint8_t FileNameLength;
    uint16_t FileName[9];               // Example: "SampleFile.jpg"

    uint8_t DateCreatedLength;
    uint16_t DateCreated[16];               // Example: 0x000000007FF00000 (Timestamp)

    uint8_t DateModifiedLength;
    uint16_t DateModified[16];              // Example: 0x000000007FF01000 (Timestamp)

    uint8_t KeywordsLength;
    uint16_t Keywords[0];               // Example: "Sample, Image"
} __packed;

static struct mtp_device_info device_info = {
    .standard_version = 100,            // MTP version 1.00
    .vendor_extension_id = 6,  // MTP standard extension ID (Microsoft)
    .vendor_extension_version = 100,    // Vendor extension version
    .vendor_extension_desc_len = 38,    // Length in bytes, not characters
    .vendor_extension_desc = { 'm', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't', '.', 'c', 'o', 'm', ':', ' ', '1', '.', '0', ';',' ','a','n','d','r','o','i','d','.','c','o','m',':',' ','1','.','0',';', '\0' },  // "microsoft.com: 1.0;" in UTF-16LE
    .functional_mode = 0,               // Standard mode
    .operations_supported_count = 18,
    .operations_supported = {
        MTP_OP_GET_DEVICE_INFO,
        MTP_OP_OPEN_SESSION,
        MTP_OP_CLOSE_SESSION,
        MTP_OP_GET_STORAGE_IDS,
        MTP_OP_GET_STORAGE_INFO,
        MTP_OP_GET_NUM_OBJECTS,
        MTP_OP_GET_OBJECT_HANDLES,
        MTP_OP_GET_OBJECT_INFO,
        MTP_OP_GET_OBJECT,
        MTP_OP_DELETE_OBJECT,
        MTP_OP_SEND_OBJECT_INFO,
        MTP_OP_SEND_OBJECT,
        MTP_OP_RESET_DEVICE,
        MTP_OP_MOVE_OBJECT,
        MTP_OP_COPY_OBJECT,
        MTP_OP_GET_PARTIAL_OBJECT,
        MTP_OP_SET_OBJECT_REFERENCES,
        MTP_OP_SKIP
    },
    .events_count = 6,
    .events_supported = {
        MTP_EVENT_OBJECT_ADDED,
        MTP_EVENT_OBJECT_REMOVED,
        MTP_EVENT_STORE_ADDED,
        MTP_EVENT_STORE_REMOVED,
        MTP_EVENT_DEVICE_PROP_CHANGED,
        MTP_EVENT_OBJECT_INFO_CHANGED
    },
    .device_properties_count = 1,
    .device_properties_supported = {
        MTP_DEVICE_PROPERTY_BATTERY_LEVEL
    },
    .formats_count = 0,
    .image_formats_count = 2,
    .image_formats = {
        MTP_FORMAT_ASSOCIATION,
        MTP_FORMAT_TEXT
    },
    .manufacturer_len = 7,
    .manufacturer = { 'Z', 'e', 'p', 'h', 'y', 'r', '\0' },
    .model_len = 10,                       // "My Model" is 8 characters
    .model = { 'Z', 'e', 'p', 'h', 'y', 'r', 'M', 'T' , 'P', '\0'},
    .device_version_len = 4,              // "1.0" is 3 characters
    .device_version = { '1', '.', '0' , '\0'},
    .serial_number_len = 17,              // Serial number must be 32 characters in UTF-16LE
    .serial_number = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A','B','C','D','E','F','\0'},
};

#define RESET   "\033[0m"
#define GREEN   "\033[32m"      /* Green */
#define BLUE    "\033[34m"      /* Blue */
#define MAGENTA "\033[35m"      /* Magenta */
#define CYAN    "\033[36m"      /* Cyan */
const char* mtp_code_to_string(uint16_t code)
{
    char* str = NULL;
    switch (code) {
        // MTP Operation Codes
        case 0x1001: str = "GetDeviceInfo"          ; break;
        case 0x1002: str = "OpenSession"            ; break;
        case 0x1003: str = "CloseSession"           ; break;
        case 0x1004: str = "GetStorageIDs"          ; break;
        case 0x1005: str = "GetStorageInfo"         ; break;
        case 0x1006: str = "GetNumObjects"          ; break;
        case 0x1007: str = "GetObjectHandles"       ; break;
        case 0x1008: str = "GetObjectInfo"          ; break;
        case 0x1009: str = "GetObject"              ; break;
        case 0x100A: str = "GetThumb"               ; break;
        case 0x100B: str = "DeleteObject"           ; break;
        case 0x100C: str = "SendObjectInfo"         ; break;
        case 0x100D: str = "SendObject"             ; break;
        case 0x1010: str = "ResetDevice"            ; break;
        case 0x1014: str = "GetDevicePropDesc"      ; break;
        case 0x1015: str = "GetDevicePropValue"     ; break;
        case 0x1016: str = "SetDevicePropValue"     ; break;
        case 0x1017: str = "ResetDevicePropValue"   ; break;
        case 0x1019: str = "MoveObject"             ; break;
        case 0x101A: str = "CopyObject"             ; break;
        case 0x101B: str = "GetPartialObject"       ; break;
        case 0x9801: str = "GetObjectPropsSupported"; break;
        case 0x9802: str = "GetObjectPropDesc"      ; break;
        case 0x9803: str = "GetObjectPropValue"     ; break;
        case 0x9804: str = "SetObjectPropValue"     ; break;
        case 0x9811: str = "SetObjectReferences"    ; break;
        case 0x9820: str = "Skip"                   ; break;

        // MTP Response Codes
        case 0x2001: str = "OK"                 ; break;
        case 0x2002: str = "GeneralError"       ; break;
        case 0x2003: str = "SessionNotOpen"     ; break;
        case 0x2009: str = "InvalidObjectHandle"; break;
        case 0x201E: str = "SessionAlreadyOpen" ; break;

        // MTP Image Formats
        case 0x3001: str = "Association"; break;
        case 0x3004: str = "Text"; break;

        // MTP Event Codes
        case 0x4002: str = "ObjectAdded"; break;
        case 0x4003: str = "ObjectRemoved"; break;
        case 0x4004: str = "StoreAdded"; break;
        case 0x4005: str = "StoreRemoved"; break;
        case 0x4006: str = "DevicePropChanged"; break;
        case 0x4007: str = "ObjectInfoChanged"; break;

        // MTP Device Properties
        case 0x5001: str = "BatteryLevel"; break;

        // Object Properties
        case 0xDC01: str = "StorageID"; break;
        case 0xDC02: str = "ObjectFormat"; break;
        case 0xDC03: str = "ProtectionStatus"; break;
        case 0xDC04: str = "ObjectSize"; break;
        case 0xDC07: str = "ObjectFileName"; break;
        case 0xDC09: str = "DateModified"; break;
        case 0xDC0B: str = "ParentObject"; break;
        case 0xDC41: str = "PersistentUID"; break;
        case 0xDC44: str = "Name"; break;
        case 0xDCE0: str = "DisplayName"; break;
        case 0xDD16: str = "FaxNumberBusiness"; break;

        // Storage Types
        case 0x0001: str = "FixedROM"; break;
        case 0x0002: str = "RemovableROM"; break;
        case 0x0003: str = "FixedRAM"; break;
        case 0x0004: str = "RemovableRAM"; break;

        // Default case if code is not recognized
        default:
            str = "Unknown Code";
            LOG_WRN("Unknown Code 0x%x", code);
            break;
    }

    return str;
}

static int confirm_msg_compeletion = 0;

static void set_confirmation_needed(bool set)
{
    confirm_msg_compeletion = set ? 1 : 0;
}

bool mtp_confirmation_needed()
{
    return confirm_msg_compeletion;
}

MTP_CMD_HANDLER(MTP_OP_GET_DEVICE_INFO)
{
    struct mtp_data_block data_block;
    data_block.container_length = (sizeof(struct mtp_data_block) + sizeof(struct mtp_device_info) );
    data_block.container_type = MTP_CONTAINER_DATA;
    data_block.response_code = mtp_command->code;
    data_block.transaction_id = mtp_command->transaction_id;

    net_buf_add_mem(buf, &data_block, sizeof(struct mtp_data_block));
    net_buf_add_mem(buf, &device_info, sizeof(struct mtp_device_info));
    set_confirmation_needed(true);
}


MTP_CMD_HANDLER(MTP_OP_OPEN_SESSION)
{
    struct mtp_container mtp_response = {
        .length = 12,
        .type = MTP_CONTAINER_RESPONSE,
        .code = MTP_RESP_OK,
        .transaction_id = mtp_command->transaction_id
    };

    net_buf_add_mem(buf, &mtp_response, 12);
}


MTP_CMD_HANDLER(MTP_OP_GET_STORAGE_INFO)
{
    struct mtp_data_block data_block;

    LOG_DBG("\n\t\tStorageID    : 0x%x\n", mtp_command->param[0]);
    data_block.container_type = MTP_CONTAINER_DATA;
    data_block.response_code = mtp_command->code;
    data_block.transaction_id = mtp_command->transaction_id;


    struct storage_info_t* storage_info;
    struct storage_info_t storage_info1 = {
            .type = STORAGE_TYPE_FIXED_RAM,              // Fixed ROM (internal memory)
            .fs_type = 0x0002,                       // Generic hierarchical file system
            .access_caps = 0x0001,                   // Read/write access
            .max_capacity = 16ULL * 1024 * 1024 * 1024, // 16 GB
            .free_space = 8ULL * 1024 * 1024 * 1024,   // 8 GB free
            .free_space_obj = 0xFFFFFFFF,                     // Free space in objects (not used)
            .storage_desc_len = 17,                  // Length of "Internal Storage" string
            .storage_desc = { 'I', 'n', 't', 'e', 'r', 'n', 'a', 'l', ' ', 'S', 't', 'o', 'r', 'a', 'g', 'e', '\0' }, // UTF-16LE encoded "Internal Storage"
            .volume_id_len = 9,                      // Length of "Internal" string
            .volume_id_desc = { 'A', 'A', 'B', 'B', 'C', 'C', 'D', 'D', '\0' } // UTF-16LE encoded "Internal"
    };

    struct storage_info_t storage_info2 = {
            .type = STORAGE_TYPE_REMOVABLE_RAM,                          // Removable RAM (external memory like SD card)
            .fs_type = 0x0002,                       // Generic hierarchical file system
            .access_caps = 0x0001,                   // Read/write access
            .max_capacity = 32ULL * 1024 * 1024 * 1024, // 32 GB
            .free_space = 10ULL * 1024 * 1024 * 1024,  // 10 GB free
            .free_space_obj = 0xFFFFFFFF,                     // Free space in objects (not used)
            .storage_desc_len = 17,                   // Length of "SD Card" string
            .storage_desc = { 'S', 'D', ' ', 'C', 'a', 'r', 'd','\0','\0','\0','\0','\0','\0','\0','\0','\0','\0' },  // UTF-16LE encoded "SD Card"
            .volume_id_len = 9,                      // Length of "SD Card" string
            .volume_id_desc = { 'A', 'B', 'C', 'D', 'E', 'F', 'G','\0','\0' } // UTF-16LE encoded "SD Card"
    };

    storage_info = &storage_info1;

    if (mtp_command->param[0] == 0x20001){
        storage_info = &storage_info2;
    }


    data_block.container_length = (sizeof(struct mtp_data_block) + sizeof(struct storage_info_t));
    net_buf_add_mem(buf,&data_block, sizeof(struct mtp_data_block));
    net_buf_add_mem(buf,storage_info, sizeof(struct storage_info_t));

    set_confirmation_needed(true);
}


MTP_CMD_HANDLER(MTP_OP_GET_STORAGE_IDS)
{
    struct mtp_data_block data_block;

    data_block.container_type = MTP_CONTAINER_DATA;
    data_block.response_code =  mtp_command->code;
    data_block.transaction_id = mtp_command->transaction_id;

    uint32_t storage_ids_count = 2;
    const uint32_t storage_ids[] = {
        0x00010001,  // Storage ID for internal memory
        0x00020001   // Storage ID for external SD card
    };

    data_block.container_length = (sizeof(struct mtp_data_block) + sizeof (uint32_t) + sizeof(storage_ids));

    net_buf_add_mem(buf,&data_block, sizeof(struct mtp_data_block));
    net_buf_add_mem(buf,&storage_ids_count, sizeof(uint32_t));
    net_buf_add_mem(buf,&storage_ids, sizeof(storage_ids));

    set_confirmation_needed(true);
}

MTP_CMD_HANDLER(MTP_OP_GET_OBJECT_HANDLES)
{
    struct mtp_data_block data_block;

    data_block.container_type = MTP_CONTAINER_DATA;
    data_block.response_code =  mtp_command->code;
    data_block.transaction_id = mtp_command->transaction_id;

    uint32_t storage_id = mtp_command->param[0];
    uint32_t obj_format_code = mtp_command->param[1];
    uint32_t obj_handle = mtp_command->param[2];

    uint32_t object_handles[] = { 0x00000001, 0x00000002,/*0x00000003*/ };  // Example object handles
    uint32_t num_handles = sizeof(object_handles) / sizeof(object_handles[0]);

    LOG_DBG("\n\t\tStorageID    : 0x%x"
            "\n\t\tObjFormatCode: 0x%x"
            "\n\t\tObjHandle    : 0x%x \n",
                storage_id, obj_format_code, obj_handle);

    data_block.container_length = (sizeof(struct mtp_data_block) + sizeof (uint32_t) + sizeof(object_handles));
    net_buf_add_mem(buf,&data_block, sizeof(struct mtp_data_block));
    net_buf_add_mem(buf,&num_handles,sizeof(uint32_t));
    net_buf_add_mem(buf,object_handles, sizeof(object_handles));

    if (buf->len != data_block.container_length) {
        LOG_ERR("Buf len: %u, container_len: %u", buf->len, data_block.container_length);
    }

    set_confirmation_needed(true);
}


MTP_CMD_HANDLER(MTP_OP_GET_OBJECT_INFO)
{
    struct mtp_data_block data_block;

    data_block.container_type = MTP_CONTAINER_DATA;
    data_block.response_code =  mtp_command->code;
    data_block.transaction_id = mtp_command->transaction_id;

    uint32_t obj_handle = mtp_command->param[0];
    LOG_DBG("\n\t\tObjHandle: 0x%x\n", mtp_command->param[0]);

    if (obj_handle == 0x1)
    {
        struct mtp_object_info file0 = {
            .StorageID      = 0x00010001,
            .ObjectFormat   = MTP_FORMAT_ASSOCIATION,
            .ProtectionStatus = OBJECT_PROTECTION_NO,
            .ObjectCompressedSize = 0xFFFFFFFFUL,
            .ThumbFormat  = 0,
            .ThumbCompressedSize = 0,
            .ThumbPixWidth = 0,
            .ThumbPixHeight = 0,
            .ImagePixWidth = 0,
            .ImagePixHeight = 0,
            .ImageBitDepth = 0,
            .ParentObject = 0xFFFFFFFFUL,
            .AssociationType = 0x0001,
            .AssociationDesc = 0,
            .SequenceNumber = 0,
            .FileNameLength = 9,
            .FileName = {'P','i','c','t','u','r','e','s','\0'},
            .DateCreatedLength = 16,
            .DateCreated = {'2', '0', '2', '4', '1', '0', '0', '1', 'T', '2', '2', '0', '0', '1', '5', '\0'},
            .DateModifiedLength = 16,
            .DateModified  = {'2', '0', '2', '4', '1', '0', '1', '1', 'T', '1', '2', '5', '8', '1', '3', '\0'},
            .KeywordsLength = 0
        };

        data_block.container_length = (sizeof(struct mtp_data_block) + sizeof(struct mtp_object_info));
        net_buf_add_mem(buf,&data_block, sizeof(struct mtp_data_block));
        net_buf_add_mem(buf,&file0, sizeof(struct mtp_object_info));
    } else if (obj_handle == 0x2) {
            struct mtp_object_info file1 = {
            .StorageID      = 0x00010001,
            .ObjectFormat   = MTP_FORMAT_TEXT,
            .ProtectionStatus = OBJECT_PROTECTION_NO,
            .ObjectCompressedSize = 11,
            .ThumbFormat  = 0,
            .ThumbCompressedSize = 0,
            .ThumbPixWidth = 0,
            .ThumbPixHeight = 0,
            .ImagePixWidth = 0,
            .ImagePixHeight = 0,
            .ImageBitDepth = 0,
            .ParentObject = 0xFFFFFFFF,
            .AssociationType = 0x0000,
            .AssociationDesc = 0,
            .SequenceNumber = 0,
            .FileNameLength = 9,
            .FileName = {'T','e','s','t','.','t','x','t','\0'},
            .DateCreatedLength = 16,
            .DateCreated = {'2', '0', '2', '4', '1', '0', '0', '1', 'T', '2', '2', '0', '0', '1', '5', '\0'},
            .DateModifiedLength = 16,
            .DateModified  = {'2', '0', '2', '4', '1', '0', '1', '1', 'T', '1', '2', '5', '8', '1', '3', '\0'},
            .KeywordsLength = 0
        };

        data_block.container_length = (sizeof(struct mtp_data_block) + sizeof(struct mtp_object_info));
        net_buf_add_mem(buf,&data_block, sizeof(struct mtp_data_block));
        net_buf_add_mem(buf,&file1, sizeof(struct mtp_object_info));
    } else {
        LOG_ERR("Unknown file handle 0x%x", obj_handle);
    }
    set_confirmation_needed(true);
}

#define MTP_DATA_TYPE_UINT8  0x0002

struct mtp_object_property_u8 {
    uint16_t code;
    uint16_t datatype;
    uint8_t get_set;
    uint8_t default_value;
    uint32_t group_code;
    uint8_t formflag;
} __packed;


MTP_CMD_HANDLER(MTP_OP_GET_DEVICE_PROP_DESC)
{
    LOG_DBG("\n\t\tParam0: 0x%x"
            "\n\t\tParam1: 0x%x"
            "\n\t\tParam2: 0x%x",
            mtp_command->param[0], mtp_command->param[1], mtp_command->param[2]);

    struct mtp_data_block data_block;

    data_block.container_type = MTP_CONTAINER_DATA;
    data_block.response_code =  mtp_command->code;
    data_block.transaction_id = mtp_command->transaction_id;

    /* although packet is correct but windows doesn't show the right battery level */
    if (mtp_command->param[0] == MTP_DEVICE_PROPERTY_BATTERY_LEVEL)
    {
        struct mtp_object_property_u8 prop = {
            .code = MTP_DEVICE_PROPERTY_BATTERY_LEVEL,
            .datatype = MTP_DATA_TYPE_UINT8,
            .get_set = 0,
            .default_value = 0,
            .group_code = 0,
            .formflag = 0x00
        };

        data_block.container_length = (sizeof(struct mtp_data_block) + sizeof (struct mtp_object_property_u8));

        net_buf_add_mem(buf,&data_block, sizeof(struct mtp_data_block));
        net_buf_add_mem(buf,&prop,sizeof(struct mtp_object_property_u8));
    }

    set_confirmation_needed(true);
}

MTP_CMD_HANDLER(MTP_OP_GET_OBJECT)
{
    LOG_DBG("\n\t\tParam0: 0x%x"
            "\n\t\tParam1: 0x%x"
            "\n\t\tParam2: 0x%x",
            mtp_command->param[0], mtp_command->param[1], mtp_command->param[2]);

    struct mtp_data_block data_block;

    data_block.container_type = MTP_CONTAINER_DATA;
    data_block.response_code =  mtp_command->code;
    data_block.transaction_id = mtp_command->transaction_id;

    //HelloWorld!
    char* s = "HelloWorld!";
    data_block.container_length = (sizeof(struct mtp_data_block) + strlen(s));

    net_buf_add_mem(buf,&data_block, sizeof(struct mtp_data_block));
    net_buf_add_mem(buf,s, strlen(s));
    set_confirmation_needed(true);
}

int mtp_commands_handler(struct net_buf *buf, struct net_buf *bufp)
{
    if (bufp == NULL){
        LOG_ERR("%s: NULL Buffer", __func__);
        return -EINVAL;
    }

    if (buf->len > sizeof(struct mtp_container)) {
        LOG_ERR("%s: Malformed data, discarded", __func__);
        return -EINVAL;
    }


    struct mtp_container* mtp_command = (struct mtp_container*)buf->data;
    LOG_DBG(GREEN "[%s]" RESET, mtp_code_to_string(mtp_command->code));

    switch(mtp_command->code)
    {
    case MTP_OP_GET_DEVICE_INFO:
        MTP_CMD(MTP_OP_GET_DEVICE_INFO);
    break;

    case MTP_OP_OPEN_SESSION:
        MTP_CMD(MTP_OP_OPEN_SESSION);
        break;
    case MTP_OP_CLOSE_SESSION:
        LOG_ERR("MTP_OP_CLOSE_SESSION not implemented!");
    break;
    case MTP_OP_GET_STORAGE_IDS:
        MTP_CMD(MTP_OP_GET_STORAGE_IDS);
    break;
    case MTP_OP_GET_STORAGE_INFO:
        MTP_CMD(MTP_OP_GET_STORAGE_INFO);
    break;
    case MTP_OP_GET_NUM_OBJECTS:
        LOG_ERR("MTP_OP_GET_NUM_OBJECTS not implemented!");
    break;
    case MTP_OP_GET_OBJECT_HANDLES:
        MTP_CMD(MTP_OP_GET_OBJECT_HANDLES);
    break;
    case MTP_OP_GET_OBJECT_INFO:
        MTP_CMD(MTP_OP_GET_OBJECT_INFO);
    break;
    case MTP_OP_GET_OBJECT:
        MTP_CMD(MTP_OP_GET_OBJECT);
    break;
    case MTP_OP_GET_THUMB:
        LOG_ERR("MTP_OP_GET_THUMB Not Implemented!");
        break;
    case MTP_OP_DELETE_OBJECT:
        LOG_ERR("MTP_OP_DELETE_OBJECT Not Implemented!");
        break;
    case MTP_OP_SEND_OBJECT_INFO:
        LOG_ERR("MTP_OP_SEND_OBJECT_INFO Not Implemented!");
        break;
    case MTP_OP_SEND_OBJECT:
        LOG_ERR("MTP_OP_SEND_OBJECT Not Implemented!");
        break;
    case MTP_OP_RESET_DEVICE:
        LOG_ERR("MTP_OP_RESET_DEVICE Not Implemented!");
        break;
    case MTP_OP_GET_DEVICE_PROP_DESC:
        MTP_CMD(MTP_OP_GET_DEVICE_PROP_DESC);
        break;
    case MTP_OP_GET_DEVICE_PROP_VALUE:
        LOG_ERR("MTP_OP_GET_DEVICE_PROP_VALUE Not Implemented!");
        break;
    case MTP_OP_SET_DEVICE_PROP_VALUE:
        LOG_ERR("MTP_OP_SET_DEVICE_PROP_VALUE Not Implemented!");
        break;
    case MTP_OP_RESET_DEVICE_PROP_VALUE:
        LOG_ERR("MTP_OP_RESET_DEVICE_PROP_VALUE Not Implemented!");
        break;
    case MTP_OP_MOVE_OBJECT:
        LOG_ERR("MTP_OP_MOVE_OBJECT Not Implemented!");
        break;
    case MTP_OP_COPY_OBJECT:
        LOG_ERR("MTP_OP_COPY_OBJECT Not Implemented!");
        break;
    case MTP_OP_GET_PARTIAL_OBJECT:
        LOG_ERR("MTP_OP_GET_PARTIAL_OBJECT Not Implemented!");
        break;
    case MTP_OP_GET_OBJECT_PROPS_SUPPORTED:
        LOG_ERR("MTP_OP_GET_OBJECT_PROPS_SUPPORTED Not Implemented!");
        break;
    case MTP_OP_GET_OBJECT_PROP_DESC:
        LOG_ERR("MTP_OP_GET_OBJECT_PROP_DESC Not Implemented!");
        break;
    case MTP_OP_GET_OBJECT_PROP_VALUE:
        LOG_ERR("MTP_OP_GET_OBJECT_PROP_VALUE Not Implemented!");
        break;
    case MTP_OP_SET_OBJECT_PROP_VALUE:
        LOG_ERR("MTP_OP_SET_OBJECT_PROP_VALUE Not Implemented!");
        break;
    case MTP_OP_SET_OBJECT_REFERENCES:
        LOG_ERR("MTP_OP_SET_OBJECT_REFERENCES Not Implemented!");
        break;
    case MTP_OP_SKIP:
        LOG_ERR("MTP_OP_SKIP Not Implemented!");
        break;
    default:
        LOG_ERR("Unknown cmd 0x%x!", mtp_command->code);
    break;
    }

    return 0;
}


int mtp_send_confirmation(struct net_buf *buf)
{
    if (buf == NULL){
        LOG_ERR("%s: Null Buffer!", __func__);
        return -EINVAL;
    }

    struct mtp_container* mtp_command = (struct mtp_container*)buf->data;
    confirm_msg_compeletion = 0;
    struct mtp_container mtp_response = {
        .length = 12,
        .type = MTP_CONTAINER_RESPONSE,
        .code = MTP_RESP_OK,
        .transaction_id = mtp_command->transaction_id
    };
    net_buf_add_mem(buf, &mtp_response, 12);
    set_confirmation_needed(false);

    return 0;
}
