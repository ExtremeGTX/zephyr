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
#define MTP_REQUEST_CANCEL                  0x64U
#define MTP_REQUEST_GET_DEVICE_STATUS       0x67U
#define MTP_REQUEST_DEVICE_RESET            0x66U

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

/* MTP File system types */
#define FS_TYPE_GENERIC_HIERARCHICAL        0x0002

/* Object Protection */
#define OBJECT_PROTECTION_NO	            0x0000
#define OBJECT_PROTECTION_READ_ONLY	        0x0001
#define OBJECT_PROTECTION_READ_ONLY_DATA	0x8002
#define OBJECT_PROTECTION_NON_TRANSFERRABLE 0x8003

#define MTP_GB(x) (x * 1ULL * 1024 * 1024 * 1024)

#define MTP_STR_LEN(str)    (strlen(str)+1)

#define MTP_CMD(opcode) do {                    \
            mtp_##opcode(bufp, mtp_command);    \
            } while(0);

#define MTP_CMD_HANDLER(opcode)					    \
static void mtp_##opcode(struct net_buf *buf,       \
                        struct mtp_container* mtp_command)

/* Types */
enum mtp_container_type {
    MTP_CONTAINER_UNDEFINED = 0x00,
    MTP_CONTAINER_COMMAND,
    MTP_CONTAINER_DATA,
    MTP_CONTAINER_RESPONSE,
    MTP_CONTAINER_EVENT,
};

struct mtp_container {
    uint32_t length;            // Total length of the command block
    uint16_t type;              // Should be 0x0001 for Command Block
    uint16_t code;              // MTP operation code (e.g., MTP_OP_OPEN_SESSION)
    uint32_t transaction_id;    // Transaction ID to track the command
    uint32_t param[5];          // Optional Parameter 1 (e.g., session ID)
} __packed;

struct mtp_data_block {
    uint32_t container_length;  // Total length of the response block
    uint16_t container_type;    // Should be 0x0002 for Data Block
    uint16_t response_code;     // MTP response code (e.g., MTP_RESP_OK)
    uint32_t transaction_id;    // Transaction ID of the command being responded to
} __packed;

/* Constants */
static const uint16_t mtp_operations[] = {
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
};

static const uint16_t events_supported[] = {
    MTP_EVENT_OBJECT_ADDED,
    MTP_EVENT_OBJECT_REMOVED,
    MTP_EVENT_STORE_ADDED,
    MTP_EVENT_STORE_REMOVED,
    MTP_EVENT_DEVICE_PROP_CHANGED,
    MTP_EVENT_OBJECT_INFO_CHANGED
};

static const uint16_t device_properties[] = {
    MTP_DEVICE_PROPERTY_BATTERY_LEVEL
};

static const uint16_t playback_formats[] = {
    MTP_FORMAT_ASSOCIATION,
    MTP_FORMAT_TEXT
};

/********************************************************************* */

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

/* Copy and convert ASCII-7 string descriptor to UTF16-LE */
static void net_buf_add_utf16le(struct net_buf *buf, const char* str)
{
    uint16_t len = strlen(str) + 1; /* we need the null terminator */

	for (int i = 0; i < len; i++) {
		__ASSERT(ascii7_str[i] > 0x1F && ascii7_str[i] < 0x7F,
			 "Only printable ascii-7 characters are allowed in USB "
			 "string descriptors");
		net_buf_add_le16(buf, str[i]);
	}
}

static int mtp_send_confirmation(struct net_buf *buf);

typedef int (pending_fn_t)(struct net_buf *buf);
static pending_fn_t* pending_fn = NULL;

static void clear_pending_packet()
{
    pending_fn = NULL;
}

static void set_pending_packet(pending_fn_t* pend_fn)
{
    pending_fn = pend_fn;
}

int send_pending_packet(struct net_buf *buf)
{
    if (pending_fn) {
        //pending_fn_t* lpfn = pending_fn;
        //clear_pending_packet();
        return pending_fn(buf);
    } else {
        return -EINVAL;
    }
}

bool mtp_packet_pending()
{
    //LOG_DBG("Pending check");
    return (pending_fn != NULL);
}

MTP_CMD_HANDLER(MTP_OP_GET_DEVICE_INFO)
{
    /* DATA Block Header */
    struct mtp_data_block data_block;
    data_block.container_type = MTP_CONTAINER_DATA;
    data_block.response_code = mtp_command->code;
    data_block.transaction_id = mtp_command->transaction_id;

    //net_buf_add_mem(buf, &data_block, sizeof(struct mtp_data_block));

    const char* vendor_extension_desc = "microsoft.com: 1.0; android.com: 1.0;";
    const char* manufacturer = "Zephyr";
    const char* model = "ZephyrMTP";
    const char* device_version = "2.0";
    const char* serial_number = "0123456789ABCDEF";

    /* Device Info */
    net_buf_add_le16(buf, 100);    /* standard_version = MTP version 1.00 */
    net_buf_add_le32(buf, 6);      /* vendor_extension_id = MTP standard extension ID (Microsoft) */
    net_buf_add_le16(buf, 100);    /* vendor_extension_version */

    /* Vendor extension description in UTF-16LE */
    net_buf_add_u8(buf, MTP_STR_LEN(vendor_extension_desc));   /* length */
    net_buf_add_utf16le(buf, vendor_extension_desc);        /* string value */

    /* functional_mode; */
    net_buf_add_le16(buf, 0);

    /* operations supported */
    net_buf_add_le32(buf, ARRAY_SIZE(mtp_operations));                  /* count */
    net_buf_add_mem(buf, mtp_operations, sizeof(mtp_operations));       /* operations_supported[] */

    /* events supported */
    net_buf_add_le32(buf, ARRAY_SIZE(events_supported));                /* count */
    net_buf_add_mem(buf, events_supported, sizeof(events_supported));   /* events_supported[] */

    /* Device properties supported */
    net_buf_add_le32(buf, ARRAY_SIZE(device_properties));               /* count */
    net_buf_add_mem(buf, device_properties, sizeof(device_properties)); /* device_properties_supported[] */

    /* Capture formats count */
    net_buf_add_le32(buf, 0);

    /* Playback formats supported */
    net_buf_add_le32(buf, ARRAY_SIZE(playback_formats));                /* count */
    net_buf_add_mem(buf, playback_formats, sizeof(playback_formats));   /* playback_formats[] */

    net_buf_add_u8(buf, MTP_STR_LEN(manufacturer));             /* manufacturer_len */
    net_buf_add_utf16le(buf, manufacturer);                     /* manufacturer[] */

    net_buf_add_u8(buf, MTP_STR_LEN(model));                    /* model_len; */
    net_buf_add_utf16le(buf, model);                            /* model[] */

    net_buf_add_u8(buf, MTP_STR_LEN(device_version));           /* device_version_len; */
    net_buf_add_utf16le(buf, device_version);                   /* device_version[] */

    net_buf_add_u8(buf, MTP_STR_LEN(serial_number));            /* serial_number_len; */
    net_buf_add_utf16le(buf, serial_number);                    /* serial_number[] */

    /* Add the Packet Header */
    data_block.container_length = (sizeof(struct mtp_data_block) + buf->len);
    net_buf_push_mem(buf, &data_block, sizeof(struct mtp_data_block));

    set_pending_packet(mtp_send_confirmation);
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
    uint32_t requested_storage_id = mtp_command->param[0];

    LOG_DBG("\n\t\tStorageID    : 0x%x\n", requested_storage_id);

    struct mtp_data_block data_block;
    data_block.container_type = MTP_CONTAINER_DATA;
    data_block.response_code = mtp_command->code;
    data_block.transaction_id = mtp_command->transaction_id;

    if (requested_storage_id == 0x10001) {
        char* storage_desc = "Internal Storage";
        char* volumeID = "AABBCCDD";
        net_buf_add_le16(buf, STORAGE_TYPE_FIXED_RAM);          /* type */
        net_buf_add_le16(buf, FS_TYPE_GENERIC_HIERARCHICAL);    /* fs_type */
        net_buf_add_le16(buf, OBJECT_PROTECTION_READ_ONLY);     /* access_caps */
        net_buf_add_le64(buf, MTP_GB(16));                      /* max_capacity */
        net_buf_add_le64(buf, MTP_GB(8));                       /* free_space */
        net_buf_add_le32(buf, 0xFFFFFFFF);                      /* free_space_obj */
        net_buf_add_u8(buf, MTP_STR_LEN(storage_desc));         /* storage_desc_len */
        net_buf_add_utf16le(buf, storage_desc);                 /* storage_desc[] */
        net_buf_add_u8(buf, MTP_STR_LEN(volumeID));             /* volume_id_len */
        net_buf_add_utf16le(buf, volumeID);                     /* volume_id_desc[] */
    } else if (requested_storage_id == 0x20001) {
        char* storage_desc = "SD Card";
        char* volumeID = "ABCDEFG";
        net_buf_add_le16(buf, STORAGE_TYPE_REMOVABLE_RAM);
        net_buf_add_le16(buf, FS_TYPE_GENERIC_HIERARCHICAL);
        net_buf_add_le16(buf, OBJECT_PROTECTION_READ_ONLY);
        net_buf_add_le64(buf, MTP_GB(32));
        net_buf_add_le64(buf, MTP_GB(10));
        net_buf_add_le32(buf, 0xFFFFFFFF);
        net_buf_add_u8(buf, MTP_STR_LEN(storage_desc));
        net_buf_add_utf16le(buf, storage_desc);
        net_buf_add_u8(buf, MTP_STR_LEN(volumeID));
        net_buf_add_utf16le(buf, volumeID);
    }

    /* Add the Packet Header */
    data_block.container_length = (sizeof(struct mtp_data_block) + buf->len);
    net_buf_push_mem(buf, &data_block, sizeof(struct mtp_data_block));

    set_pending_packet(mtp_send_confirmation);
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

    set_pending_packet(mtp_send_confirmation);
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

    set_pending_packet(mtp_send_confirmation);


}


MTP_CMD_HANDLER(MTP_OP_GET_OBJECT_INFO)
{
    struct mtp_data_block data_block;

    data_block.container_type = MTP_CONTAINER_DATA;
    data_block.response_code =  mtp_command->code;
    data_block.transaction_id = mtp_command->transaction_id;

    uint32_t obj_handle = mtp_command->param[0];
    LOG_DBG("\n\t\tObjHandle: 0x%x\n", mtp_command->param[0]);

    if (obj_handle == 0x1) {
        char* filename = "Pictures";
        char* data_created = "20241001T220015";
        char* data_modified = "20241011T125813";
        net_buf_add_le32(buf, 0x00010001);                  /* StorageID */
        net_buf_add_le16(buf, MTP_FORMAT_ASSOCIATION);      /* ObjectFormat */
        net_buf_add_le16(buf, OBJECT_PROTECTION_NO);        /* ProtectionStatus */
        net_buf_add_le32(buf, 0xFFFFFFFFUL);                /* ObjectCompressedSize */
        net_buf_add_le16(buf, 0);                           /* ThumbFormat */
        net_buf_add_le32(buf, 0);                           /* ThumbCompressedSize */
        net_buf_add_le32(buf, 0);                           /* ThumbPixWidth */
        net_buf_add_le32(buf, 0);                           /* ThumbPixHeight */
        net_buf_add_le32(buf, 0);                           /* ImagePixWidth */
        net_buf_add_le32(buf, 0);                           /* ImagePixHeight */
        net_buf_add_le32(buf, 0);                           /* ImageBitDepth */
        net_buf_add_le32(buf, 0xFFFFFFFFUL); /* Object in Root */                   /* ParentObject */
        net_buf_add_le16(buf, 0x0001);                      /* AssociationType */
        net_buf_add_le32(buf, 0);                           /* AssociationDesc */
        net_buf_add_le32(buf, 0);                           /* SequenceNumber */

        net_buf_add_u8(buf, MTP_STR_LEN(filename) ); /* FileNameLength */
        net_buf_add_utf16le(buf, filename); /* FileName */

        net_buf_add_u8(buf, MTP_STR_LEN(data_created)); /* DateCreatedLength */
        net_buf_add_utf16le(buf, data_created); /* DateCreated */

        net_buf_add_u8(buf, MTP_STR_LEN(data_modified)); /*  DateModifiedLength */
        net_buf_add_utf16le(buf, data_modified); /* DateModified */

        net_buf_add_u8(buf, 0); /*  KeywordsLength, always 0 unused */
    } else if (obj_handle == 0x2) {
        char* filename = "Test.txt";
        char* data_created = "20241001T220015";
        char* data_modified = "20241011T125813";
        net_buf_add_le32(buf, 0x00010001);                  /* StorageID */
        net_buf_add_le16(buf, MTP_FORMAT_TEXT);      /* ObjectFormat */
        net_buf_add_le16(buf, OBJECT_PROTECTION_NO);        /* ProtectionStatus */
        net_buf_add_le32(buf, KB(2));                /* ObjectCompressedSize */
        net_buf_add_le16(buf, 0);                           /* ThumbFormat */
        net_buf_add_le32(buf, 0);                           /* ThumbCompressedSize */
        net_buf_add_le32(buf, 0);                           /* ThumbPixWidth */
        net_buf_add_le32(buf, 0);                           /* ThumbPixHeight */
        net_buf_add_le32(buf, 0);                           /* ImagePixWidth */
        net_buf_add_le32(buf, 0);                           /* ImagePixHeight */
        net_buf_add_le32(buf, 0);                           /* ImageBitDepth */
        net_buf_add_le32(buf, 0xFFFFFFFFUL); /* Object in Root */                   /* ParentObject */
        net_buf_add_le16(buf, 0x0001);                      /* AssociationType */
        net_buf_add_le32(buf, 0);                           /* AssociationDesc */
        net_buf_add_le32(buf, 0);                           /* SequenceNumber */

        net_buf_add_u8(buf, MTP_STR_LEN(filename) ); /* FileNameLength */
        net_buf_add_utf16le(buf, filename); /* FileName */

        net_buf_add_u8(buf, MTP_STR_LEN(data_created)); /* DateCreatedLength */
        net_buf_add_utf16le(buf, data_created); /* DateCreated */

        net_buf_add_u8(buf, MTP_STR_LEN(data_modified)); /*  DateModifiedLength */
        net_buf_add_utf16le(buf, data_modified); /* DateModified */

        net_buf_add_u8(buf, 0); /*  KeywordsLength, always 0 unused */
    } else {
        LOG_ERR("Unknown file handle 0x%x", obj_handle);
    }

    /* Add the Packet Header */
    data_block.container_length = (sizeof(struct mtp_data_block) + buf->len);
    net_buf_push_mem(buf, &data_block, sizeof(struct mtp_data_block));

    set_pending_packet(mtp_send_confirmation);
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

    set_pending_packet(mtp_send_confirmation);
}

struct getfilestate_t{
    uint32_t total_size;
    uint32_t sent;
};


#define TEST_FILE_SIZE 2048
#define MAX_PACKET_SIZE 512

struct getfilestate_t filestate;
uint8_t filebuf[512];

static int continue_get_object(struct net_buf *buf)
{
    int len = 0;
    int total_chunks = (filestate.total_size / MAX_PACKET_SIZE);
    static int chunks_sent = 0;
    memset(filebuf,(filebuf[0]+1), 512);
    if (filestate.sent < filestate.total_size) {
        len = MIN(MAX_PACKET_SIZE, (filestate.total_size - filestate.sent));

        net_buf_add_mem(buf, filebuf, len);
        filestate.sent += len;
        chunks_sent++;
        LOG_DBG("sent [%u of %u]: %u, remaining %u",chunks_sent,total_chunks, filestate.sent, (filestate.total_size-filestate.sent));
        if (filestate.sent >= filestate.total_size){
            filestate.total_size = 0;
            filestate.sent = 0;
            LOG_DBG("Done, CONFIRMING");
            set_pending_packet(mtp_send_confirmation);
        } else {
            LOG_DBG("Continue Next");
            set_pending_packet(continue_get_object);
        }
    }
    return 0;
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

    memset(filebuf, 'A', MAX_PACKET_SIZE);
    //HelloWorld!
    char* s = "HelloWorld!";
    data_block.container_length = (sizeof(struct mtp_data_block) + TEST_FILE_SIZE);

    filestate.total_size = TEST_FILE_SIZE;
    filestate.sent = 11;
    net_buf_add_mem(buf,&data_block, sizeof(struct mtp_data_block));
    net_buf_add_mem(buf,s, strlen(s));

    net_buf_add_mem(buf, filebuf, (MAX_PACKET_SIZE-sizeof(struct mtp_data_block)-strlen(s)));
    filestate.sent = strlen(s)+(MAX_PACKET_SIZE-sizeof(struct mtp_data_block)-strlen(s));
    LOG_DBG("File Content Sent %u", filestate.sent);

    set_pending_packet(continue_get_object);
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


static int mtp_send_confirmation(struct net_buf *buf)
{
    if (buf == NULL){
        LOG_ERR("%s: Null Buffer!", __func__);
        return -EINVAL;
    }

    struct mtp_container* mtp_command = (struct mtp_container*)buf->data;
    struct mtp_container mtp_response = {
        .length = 12,
        .type = MTP_CONTAINER_RESPONSE,
        .code = MTP_RESP_OK,
        .transaction_id = mtp_command->transaction_id
    };
    net_buf_add_mem(buf, &mtp_response, 12);

    clear_pending_packet();

    return 0;
}
