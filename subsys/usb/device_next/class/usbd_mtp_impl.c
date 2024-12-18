/*
 * Copyright (c) 2024 Mohamed ElShahawi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <wctype.h>
#include <stddef.h>

#include <zephyr/net_buf.h>
#include <zephyr/devicetree.h>
#include <zephyr/fs/fs.h>

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
#define MTP_OP_GET_OBJECT_HANDLES           0x1007
#define MTP_OP_GET_OBJECT_INFO              0x1008
#define MTP_OP_GET_OBJECT                   0x1009
#define MTP_OP_DELETE_OBJECT                0x100B
#define MTP_OP_SEND_OBJECT_INFO             0x100C
#define MTP_OP_SEND_OBJECT                  0x100D
#define MTP_OP_GET_DEVICE_PROP_DESC         0x1014 //TODO: Delete it
#define MTP_OP_GET_DEVICE_PROP_VALUE        0x1015 //TODO: Delete it
#define MTP_OP_SET_DEVICE_PROP_VALUE        0x1016 //TODO: Delete it
#define MTP_OP_MOVE_OBJECT                  0x1019
#define MTP_OP_COPY_OBJECT                  0x101A

#define MTP_OP_GET_OBJECT_REFERENCES        0x9810

/* MTP Response Codes */
#define MTP_RESP_OK                         0x2001
#define MTP_RESP_GENERAL_ERROR              0x2002
#define MTP_RESP_SESSION_NOT_OPEN           0x2003
#define MTP_RESP_OPERATION_NOT_SUPPORTED    0x2005
#define MTP_RESP_SESSION_ALREADY_OPEN       0x201E
#define MTP_RESP_INVALID_OBJECT_HANDLE      0x2009

/* MTP Image Formats */
#define MTP_FORMAT_UNDEFINED                0x3000
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

/* MACROS */
#define GEN_INTERNAL_STORAGE_ID(id)     (0x00010000 + id)
#define GEN_REMOVABLE_STORAGE_ID(id)    (0x00020000 + id)

#define MTP_GB(x) (x * 1ULL * 1024 * 1024 * 1024)

#define MTP_STR_LEN(str)    (strlen(str)+1)

#define MTP_CMD(opcode) do {                    \
            mtp_##opcode(mtp_command, payload, buf);    \
            } while(0);

#define MTP_CMD_HANDLER(opcode)					    \
static void mtp_##opcode(struct mtp_container* mtp_command, \
                         struct net_buf *payload, \
                         struct net_buf *buf)

#define FSTAB_NODE DT_PATH(fstab)

#define PROCESS_FSTAB_ENTRY(node_id)					\
	IF_ENABLED(DT_PROP(node_id, mtp_enabled),	\
		   ({.mountpoint = DT_PROP(node_id, mount_point), .files_count = 1},))

#define MAX_PATH_LEN        128
#define MAX_FILES           30  // Define the maximum number of files to store

/* Types */
enum mtp_container_type {
    MTP_CONTAINER_UNDEFINED = 0x00,
    MTP_CONTAINER_COMMAND,
    MTP_CONTAINER_DATA,
    MTP_CONTAINER_RESPONSE,
    MTP_CONTAINER_EVENT,
};
struct mtp_header {
    uint32_t length;  // Total length of the response block
    uint16_t type;    // Should be 0x0002 for Data Block
    uint16_t code;     // MTP response code (e.g., MTP_RESP_OK)
    uint32_t transaction_id;    // Transaction ID of the command being responded to
} __packed;

struct mtp_container {
    struct mtp_header hdr;
    uint32_t param[5];          // Optional Parameter 1 (e.g., session ID)
} __packed;


/* Constants */
static const uint16_t mtp_operations[] = {
    MTP_OP_GET_DEVICE_INFO,
    MTP_OP_OPEN_SESSION,
    MTP_OP_CLOSE_SESSION,
    MTP_OP_GET_STORAGE_IDS,
    MTP_OP_GET_STORAGE_INFO,
    MTP_OP_GET_OBJECT_HANDLES,
    MTP_OP_GET_OBJECT_INFO,
    MTP_OP_GET_OBJECT,
    MTP_OP_DELETE_OBJECT,
    MTP_OP_SEND_OBJECT_INFO,
    MTP_OP_SEND_OBJECT,
    MTP_OP_MOVE_OBJECT,
    MTP_OP_COPY_OBJECT,
    MTP_OP_GET_DEVICE_PROP_DESC,
    MTP_OP_GET_DEVICE_PROP_VALUE,
    MTP_OP_SET_DEVICE_PROP_VALUE,
};

static const uint16_t events_supported[] = {
    MTP_EVENT_OBJECT_ADDED,
    MTP_EVENT_OBJECT_REMOVED,
    MTP_EVENT_DEVICE_PROP_CHANGED,
    MTP_EVENT_OBJECT_INFO_CHANGED
};

static const uint16_t device_properties[] = {
    MTP_DEVICE_PROPERTY_BATTERY_LEVEL
};

static const uint16_t playback_formats[] = {
    MTP_FORMAT_UNDEFINED,
    MTP_FORMAT_ASSOCIATION,
};

char date_created[25];
char date_modified[25];

#define MTP_GET_INPROGRESS 1
#define MTP_SEND_INPROGRESS 1
struct mtp_context {
    uint8_t session_id;
    uint8_t filebuf[512]; /* TODO: should match USB Packet size */
    struct {
        struct fs_file_t file;
        uint32_t total_size;
        uint32_t transferred;
        uint32_t chunks_sent;
        uint32_t storage_id;
    } filestate;
} mtp_ctx;

struct fs_object_t {
    union {
        struct {
            uint8_t object_id;
            uint8_t type;
            uint8_t parent_id;
            uint8_t storage_id;
        };
        uint32_t ID; // Allows access to the entire ID as a single value
    };
    uint32_t size;
    char path[MAX_PATH_LEN];
    char name[MAX_FILE_NAME];
};
struct storage_t {
    const char mountpoint[10];
    struct fs_object_t fileslist[MAX_FILES];
    uint16_t files_count;
};


static struct storage_t available_storages[] = {
    {.mountpoint = "NULL"},
    DT_FOREACH_CHILD(FSTAB_NODE, PROCESS_FSTAB_ENTRY)
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
        case 0x9810: str = "GetObjectReferences"    ; break;
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

static void net_buf_pull_utf16le(struct net_buf *buf, char* strbuf, size_t len)
{
    for (int i=0; i < len; ++i) {
        strbuf[i] = net_buf_pull_u8(buf);
        net_buf_pull_u8(buf);
    }
}


static int mtp_send_confirmation(struct net_buf *buf);
/* ================== Pending packet handling ================ */
typedef int (pending_fn_t)(struct net_buf *buf);
static pending_fn_t* pending_fn = NULL;

static void set_pending_packet(pending_fn_t* pend_fn)
{
    pending_fn = pend_fn;
}

int send_pending_packet(struct net_buf *buf)
{
    if (pending_fn) {
        pending_fn_t* lpfn = pending_fn;
        pending_fn = NULL;
        return lpfn(buf);
    } else {
        return -EINVAL;
    }
}

bool mtp_packet_pending()
{
    return (pending_fn != NULL);
}
/* ===================== Extra Data needed handling =============== */

typedef int (extra_data_fn_t)(struct net_buf *buf, struct net_buf *buf_recv);
static extra_data_fn_t* extra_data_fn = NULL;


static bool more_data_needed = false;
static void set_needs_more_data(bool more_data)
{
    more_data_needed = more_data;
}

bool mtp_needs_more_data(struct net_buf *buf)
{
    bool val = more_data_needed;
    more_data_needed = false;
    return val;
}

int handle_extra_data(struct net_buf *buf, struct net_buf *buf_recv)
{
    if (extra_data_fn) {
        extra_data_fn_t* lpfn = extra_data_fn;
        extra_data_fn = NULL;
        return lpfn(buf, buf_recv);
    } else {
        return -EINVAL;
    }
}

void data_header_push(struct net_buf* buf, struct mtp_container* mtp_command, uint32_t data_len)
{
    /* DATA Block Header */
    struct mtp_header hdr;
    hdr.type = MTP_CONTAINER_DATA;
    hdr.code = mtp_command->hdr.code;
    hdr.transaction_id = mtp_command->hdr.transaction_id;
    hdr.length = (sizeof(struct mtp_header) + data_len);
    net_buf_push_mem(buf, &hdr, sizeof(struct mtp_header));
}

static int dir_traverse(uint8_t storage_id, const char* root_path, uint32_t parent)
{
    char path[MAX_PATH_LEN];
    struct fs_dir_t dir;
    int err;
    struct storage_t* sstorage = &available_storages[storage_id];

    fs_dir_t_init(&dir);

    err = fs_opendir(&dir, root_path);
    if (err) {
        LOG_ERR("Unable to open %s (err %d)", root_path, err);
        return -ENOEXEC;
    }

    while (1) {
        struct fs_dirent entry;

        err = fs_readdir(&dir, &entry);
        if (err) {
            LOG_ERR("Unable to read directory");
            break;
        }

        // Check for end of directory listing
        if (entry.name[0] == '\0') {
            break;
        }

        // Build the full path of the file or directory
        snprintf(path, sizeof(path), "%s/%s%s", root_path, entry.name, (entry.type == FS_DIR_ENTRY_DIR) ? "/" : "");

        // If it's a file, store the path in the array
        if (sstorage->files_count < MAX_FILES) {
            strncpy(sstorage->fileslist[sstorage->files_count].path, path, MAX_PATH_LEN - 1);
            strncpy(sstorage->fileslist[sstorage->files_count].name, entry.name, MAX_PATH_LEN - 1);
            sstorage->fileslist[sstorage->files_count].size = entry.size;
            sstorage->fileslist[sstorage->files_count].type = (entry.type == FS_DIR_ENTRY_DIR ? 1 : 0) ;
            sstorage->fileslist[sstorage->files_count].parent_id = parent;
            sstorage->fileslist[sstorage->files_count].object_id = sstorage->files_count;
            sstorage->fileslist[sstorage->files_count].storage_id = storage_id;
            sstorage->files_count++;

            if (entry.type == FS_DIR_ENTRY_DIR) {
                // Recursive call to traverse subdirectory
                dir_traverse(storage_id, path, sstorage->fileslist[sstorage->files_count-1].object_id);
            }

        } else {
            LOG_ERR("Max file count reached, cannot store more paths.");
            break;
        }

    }

    fs_closedir(&dir);

    return 0;
}

MTP_CMD_HANDLER(MTP_OP_GET_DEVICE_INFO)
{
    const char* manufacturer = "Zephyr";
    const char* model = "ZephyrMTP";
    const char* device_version = "2.0";
    const char* serial_number = "0123456789ABCDEF";

    /* Device Info */
    net_buf_add_le16(buf, 100);    /* standard_version = MTP version 1.00 */
    net_buf_add_le32(buf, 6);      /* vendor_extension_id = MTP standard extension ID (Microsoft) */
    net_buf_add_le16(buf, 100);    /* vendor_extension_version */

    /* No Vendor extension is supported */
    net_buf_add_u8(buf, 0);        /* Unused */

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

    net_buf_add_u8(buf, MTP_STR_LEN(manufacturer));                     /* manufacturer_len */
    net_buf_add_utf16le(buf, manufacturer);                             /* manufacturer[] */

    net_buf_add_u8(buf, MTP_STR_LEN(model));                            /* model_len; */
    net_buf_add_utf16le(buf, model);                                    /* model[] */

    net_buf_add_u8(buf, MTP_STR_LEN(device_version));                   /* device_version_len; */
    net_buf_add_utf16le(buf, device_version);                           /* device_version[] */

    net_buf_add_u8(buf, MTP_STR_LEN(serial_number));                    /* serial_number_len; */
    net_buf_add_utf16le(buf, serial_number);                            /* serial_number[] */

    /* Add the Packet Header */
    data_header_push(buf, mtp_command, buf->len);

    set_pending_packet(mtp_send_confirmation);
}


MTP_CMD_HANDLER(MTP_OP_OPEN_SESSION)
{
    for (int i=1; i < ARRAY_SIZE(available_storages); i++) {
        dir_traverse(i, available_storages[i].mountpoint, 0xFFFFFFFF);
    }

    struct mtp_header mtp_response = {
        .length = sizeof(struct mtp_header),
        .type = MTP_CONTAINER_RESPONSE,
        .code = MTP_RESP_OK,
        .transaction_id = mtp_command->hdr.transaction_id
    };

    net_buf_add_mem(buf, &mtp_response, sizeof(struct mtp_header));
}

MTP_CMD_HANDLER(MTP_OP_CLOSE_SESSION)
{
    for (int i=1; i < ARRAY_SIZE(available_storages); i++) {
        memset(available_storages[i].fileslist, 0x00, sizeof(available_storages[i].fileslist));
        available_storages[i].files_count = 0;
    }

    struct mtp_header mtp_response = {
        .length = sizeof(struct mtp_header),
        .type = MTP_CONTAINER_RESPONSE,
        .code = MTP_RESP_OK,
        .transaction_id = mtp_command->hdr.transaction_id
    };

    net_buf_add_mem(buf, &mtp_response, sizeof(struct mtp_header));
}


MTP_CMD_HANDLER(MTP_OP_GET_STORAGE_INFO)
{
    uint32_t requested_storage_id = mtp_command->param[0] & 0x0F;

    LOG_DBG("\n\t\tStorageID    : 0x%x\n", requested_storage_id);

    if (requested_storage_id == 0){
        LOG_ERR("Unknown Storage ID %x", requested_storage_id);
    }

    struct fs_statvfs stat;
    int err = fs_statvfs(available_storages[requested_storage_id].mountpoint, &stat);
    if (err < 0) {
        LOG_ERR("Failed to statvfs %s (%d)", available_storages[requested_storage_id].mountpoint, err);
        return;
    }

    const char* storage_name = available_storages[requested_storage_id].mountpoint;
    if (storage_name[0] == '/') {
        /* skip the slash */
        storage_name++;
    }

    net_buf_add_le16(buf, STORAGE_TYPE_FIXED_RAM);                  /* type */
    net_buf_add_le16(buf, FS_TYPE_GENERIC_HIERARCHICAL);            /* fs_type */
    net_buf_add_le16(buf, OBJECT_PROTECTION_NO);                    /* access_caps */
    net_buf_add_le64(buf, (stat.f_blocks * stat.f_frsize));         /* max_capacity */
    net_buf_add_le64(buf, (stat.f_bfree * stat.f_frsize));          /* free_space */
    net_buf_add_le32(buf, 0xFFFFFFFF);                              /* free_space_obj */
    net_buf_add_u8(buf, MTP_STR_LEN(storage_name));                 /* storage_desc_len */
    net_buf_add_utf16le(buf, storage_name);                         /* storage_desc[] */
    net_buf_add_u8(buf, 0);                                         /* volume_id_len, Unused */

    /* Add the Packet Header */
    data_header_push(buf, mtp_command, buf->len);


    set_pending_packet(mtp_send_confirmation);
}

MTP_CMD_HANDLER(MTP_OP_GET_STORAGE_IDS)
{
    net_buf_add_le32(buf, ARRAY_SIZE(available_storages)-1); /* Number of Storages */
    for (int i=1; i < ARRAY_SIZE(available_storages); i++)
    {
        net_buf_add_le32(buf, GEN_INTERNAL_STORAGE_ID(i)); /* Use array index as Storage ID, 0x00 can't be used */
    }

    /* Add the Packet Header */
    data_header_push(buf, mtp_command, buf->len);

    set_pending_packet(mtp_send_confirmation);
}

#define GET_OBJECT_ID(objID)   ((uint8_t)(objID & 0xFF))
#define GET_TYPE(objID)        ((uint8_t)((objID >> 8) & 0xFF))
#define GET_PARENT_ID(objID)   ((uint8_t)((objID >> 16) & 0xFF))
#define GET_STORAGE_ID(objID)  ((uint8_t)((objID >> 24) & 0xFF))

MTP_CMD_HANDLER(MTP_OP_GET_OBJECT_HANDLES)
{
    uint32_t storage_id = mtp_command->param[0] & 0x0F;
    uint32_t obj_format_code = mtp_command->param[1];
    uint32_t obj_handle = mtp_command->param[2];

    LOG_DBG("\n\t\tStorageID    : 0x%x"
            "\n\t\tObjFormatCode: 0x%x"
            "\n\t\tObjHandle    : 0x%x \n",
                storage_id, obj_format_code, obj_handle);

    uint32_t found_files = 0;
    uint32_t parent_id = (obj_handle == 0xffffffff ? 0xff : GET_OBJECT_ID(obj_handle));
    for (int i=0;i<available_storages[storage_id].files_count;++i) {
        //LOG_DBG("Comparing Req:%x %x", parent_id, available_storages[storage_id].fileslist[i].parent_id);
        if (available_storages[storage_id].fileslist[i].parent_id == parent_id) {
            net_buf_add_le32(buf, available_storages[storage_id].fileslist[i].ID);
            found_files++;
        }
    }
    net_buf_push_mem(buf, &found_files, sizeof(uint32_t));

    /* Add the Packet Header */
    data_header_push(buf, mtp_command, buf->len);

    set_pending_packet(mtp_send_confirmation);
}


MTP_CMD_HANDLER(MTP_OP_GET_OBJECT_INFO)
{
    uint32_t obj_handle = mtp_command->param[0];
    uint8_t storage_id = GET_STORAGE_ID(obj_handle);
    uint8_t object_id =  GET_OBJECT_ID(obj_handle);
    LOG_DBG("\n\t\tObjHandle: 0x%x, SID: %x, OID: %x", mtp_command->param[0], storage_id, object_id);

    if (available_storages[storage_id].fileslist[object_id].ID == obj_handle) {
        char* filename = available_storages[storage_id].fileslist[object_id].name;
        char* data_created = "20241001T220015";
        char* data_modified = "20241011T125813";

        net_buf_add_le32(buf, 0x00010001);                  /* StorageID */

        if (available_storages[storage_id].fileslist[object_id].type == 1) {
            net_buf_add_le16(buf, MTP_FORMAT_ASSOCIATION);  /* ObjectFormat */
        } else {
            net_buf_add_le16(buf, MTP_FORMAT_UNDEFINED);    /* ObjectFormat */
        }

        net_buf_add_le16(buf, OBJECT_PROTECTION_NO);        /* ProtectionStatus */

        if (available_storages[storage_id].fileslist[object_id].type == 1){
            net_buf_add_le32(buf, 0xFFFFFFFFUL);            /* ObjectCompressedSize */
        } else {
            net_buf_add_le32(buf, available_storages[storage_id].fileslist[object_id].size);
        }
        net_buf_add_le16(buf, 0);                           /* ThumbFormat */
        net_buf_add_le32(buf, 0);                           /* ThumbCompressedSize */
        net_buf_add_le32(buf, 0);                           /* ThumbPixWidth */
        net_buf_add_le32(buf, 0);                           /* ThumbPixHeight */
        net_buf_add_le32(buf, 0);                           /* ImagePixWidth */
        net_buf_add_le32(buf, 0);                           /* ImagePixHeight */
        net_buf_add_le32(buf, 0);                           /* ImageBitDepth */
        if (available_storages[storage_id].fileslist[object_id].parent_id == 0xff) {
            LOG_DBG("%s in root", available_storages[storage_id].fileslist[object_id].name);
            net_buf_add_le32(buf, 0xFFFFFFFFUL);            /* ParentObject (0xFF if Object in Root) */
        } else {
            LOG_DBG("%s in parent %x",
                    available_storages[storage_id].fileslist[object_id].name,
                    available_storages[storage_id].fileslist[object_id].parent_id);
            net_buf_add_le32(buf, available_storages[storage_id].fileslist[object_id].parent_id);                    /* ParentObject */
        }
        net_buf_add_le16(buf, 0x0001);                      /* AssociationType */
        net_buf_add_le32(buf, 0);                           /* AssociationDesc */
        net_buf_add_le32(buf, 0);                           /* SequenceNumber */

        net_buf_add_u8(buf, MTP_STR_LEN(filename) );        /* FileNameLength */
        net_buf_add_utf16le(buf, filename);                 /* FileName */

        net_buf_add_u8(buf, MTP_STR_LEN(data_created));     /* DateCreatedLength */
        net_buf_add_utf16le(buf, data_created);             /* DateCreated */

        net_buf_add_u8(buf, MTP_STR_LEN(data_modified));    /* DateModifiedLength */
        net_buf_add_utf16le(buf, data_modified);            /* DateModified */

        net_buf_add_u8(buf, 0);                             /* KeywordsLength, always 0 unused */

    } else {
        LOG_ERR("Unknown Error ID %08x", obj_handle);
    }

    /* Add the Packet Header */
    data_header_push(buf, mtp_command, buf->len);

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

        net_buf_add_mem(buf,&prop,sizeof(struct mtp_object_property_u8));

        /* Add the Packet Header */
        data_header_push(buf, mtp_command, sizeof (struct mtp_object_property_u8));
    }

    set_pending_packet(mtp_send_confirmation);
}


#define MAX_PACKET_SIZE 512  /* Get it in a usb complaint way */


static int continue_get_object(struct net_buf *buf)
{
    int len = 0;
    int total_chunks = (mtp_ctx.filestate.total_size / MAX_PACKET_SIZE);
    memset(mtp_ctx.filebuf, 0x00, 512);

    if (mtp_ctx.filestate.transferred < mtp_ctx.filestate.total_size) {
        len = MIN(MAX_PACKET_SIZE, (mtp_ctx.filestate.total_size - mtp_ctx.filestate.transferred));

        int read = fs_read(&mtp_ctx.filestate.file, mtp_ctx.filebuf, len);
        if (read <= 0) {
            LOG_ERR("Failed to read file content %d", read);
        }
        net_buf_add_mem(buf, mtp_ctx.filebuf, read);

        mtp_ctx.filestate.transferred += len;
        mtp_ctx.filestate.chunks_sent++;
        LOG_DBG("sent [%u of %u]: %u, remaining %u",
                        mtp_ctx.filestate.chunks_sent,total_chunks,
                        mtp_ctx.filestate.transferred,
                        (mtp_ctx.filestate.total_size-mtp_ctx.filestate.transferred));

        if (mtp_ctx.filestate.transferred >= mtp_ctx.filestate.total_size){
            LOG_DBG("Done (%u), CONFIRMING", read);
            fs_close(&mtp_ctx.filestate.file);
            memset(&mtp_ctx.filestate, 0x00, sizeof(mtp_ctx.filestate));
            set_pending_packet(mtp_send_confirmation);
        } else {
            LOG_DBG("Continue (%u) Next", read);
            set_pending_packet(continue_get_object);
        }
    } else {
        LOG_ERR("shouldn't happen !!!!!!!!!");
    }
    return 0;
}


MTP_CMD_HANDLER(MTP_OP_GET_OBJECT)
{
    LOG_DBG("\n\t\tParam0: 0x%x"
            "\n\t\tParam1: 0x%x"
            "\n\t\tParam2: 0x%x",
            mtp_command->param[0], mtp_command->param[1], mtp_command->param[2]);

    uint32_t obj_handle = mtp_command->param[0];
    uint8_t storage_id = GET_STORAGE_ID(obj_handle);
    uint8_t object_id =  GET_OBJECT_ID(obj_handle);
    const char* path = available_storages[storage_id].fileslist[object_id].path;

    fs_file_t_init(&mtp_ctx.filestate.file);
	int err = fs_open(&mtp_ctx.filestate.file, path, FS_O_READ);
	if (err) {
		LOG_ERR("Failed to open %s (%d)", path, err);
		return;
	}

    uint32_t filesize = available_storages[storage_id].fileslist[object_id].size;
    uint32_t available_buf_len = MAX_PACKET_SIZE-sizeof(struct mtp_header);

    LOG_DBG("Sending file: %s size: %u",
     available_storages[storage_id].fileslist[object_id].path,
     available_storages[storage_id].fileslist[object_id].size);

    /* Add the Packet Header */
    data_header_push(buf, mtp_command, available_storages[storage_id].fileslist[object_id].size);

    if (available_storages[storage_id].fileslist[object_id].size > available_buf_len) {
        int read = fs_read(&mtp_ctx.filestate.file, mtp_ctx.filebuf, available_buf_len);
        if (read <= 0) {
            LOG_ERR("Failed to read file content %d", read);
        }
        net_buf_add_mem(buf, mtp_ctx.filebuf, read);

        mtp_ctx.filestate.total_size = filesize;
        mtp_ctx.filestate.transferred = read;
        set_pending_packet(continue_get_object);
    } else {
        int read = fs_read(&mtp_ctx.filestate.file, mtp_ctx.filebuf, filesize);
        if (read <= 0) {
            LOG_ERR("Failed to read file content %d", read);
        }
        net_buf_add_mem(buf, mtp_ctx.filebuf, read);
	    fs_close(&mtp_ctx.filestate.file);
        memset(&mtp_ctx.filestate, 0x00, sizeof(mtp_ctx.filestate));
        set_pending_packet(mtp_send_confirmation);
    }
}

MTP_CMD_HANDLER(MTP_OP_SEND_OBJECT_INFO)
{
    static struct fs_object_t *fs_obj = NULL;

    /* first packet received from Host contains only, destination storageID and Destination ParentID */
    if (fs_obj == NULL) {
        LOG_DBG("\n\t\tDest StorageID: 0x%x"
                "\n\t\tDest ParentHandle: 0x%x, resolved 0x%x",
                mtp_command->param[0],
                mtp_command->param[1],
                GET_OBJECT_ID(mtp_command->param[1]));

        uint32_t dest_storage_id = mtp_command->param[0] & 0x0F;
        uint32_t dest_parent_handle = mtp_command->param[1];

        if (dest_storage_id != 0 && dest_storage_id < ARRAY_SIZE(available_storages)) {
            if ((available_storages[dest_storage_id].files_count + 1) <  MAX_FILES)
            {
                uint32_t new_obj_id = available_storages[dest_storage_id].files_count++;
                fs_obj = &available_storages[dest_storage_id].fileslist[new_obj_id];

                fs_obj->object_id = new_obj_id;
                fs_obj->type = 0;
                fs_obj->parent_id = (dest_parent_handle == 0xffffffff ? 0xff : GET_OBJECT_ID(dest_parent_handle));
                fs_obj->storage_id = dest_storage_id;

                LOG_INF("New ObjID:  0x%08x", fs_obj->ID);
                set_needs_more_data(true);
            } else {
                LOG_ERR("No file handle avaiable %u", available_storages[dest_storage_id].files_count);
            }
        } else {
            LOG_ERR("Unkown storage id %x", dest_storage_id);
        }
    } else { /* Host sent more info */
        char* filepath = fs_obj->path;
        char* filename = fs_obj->name;

        uint8_t str_len = 0;

        net_buf_pull(payload, sizeof(struct mtp_header));                  /* SKIP the header */
        net_buf_pull_le32(payload);                                        /* StorageID, always 0 ignore */
        uint16_t ObjectFormat = net_buf_pull_le16(payload);                /* ObjectFormat */
        if (ObjectFormat == MTP_FORMAT_ASSOCIATION) {
            fs_obj->type = 1;
        }
        net_buf_pull_le16(payload);                                        /* ProtectionStatus */
        fs_obj->size = net_buf_pull_le32(payload);                         /* ObjectCompressedSize */

        net_buf_pull_le16(payload);                                        /* ThumbFormat */
        net_buf_pull_le32(payload);                                        /* ThumbCompressedSize */
        net_buf_pull_le32(payload);                                        /* ThumbPixWidth */
        net_buf_pull_le32(payload);                                        /* ThumbPixHeight */
        net_buf_pull_le32(payload);                                        /* ImagePixWidth */
        net_buf_pull_le32(payload);                                        /* ImagePixHeight */
        net_buf_pull_le32(payload);                                        /* ImageBitDepth */
        uint32_t ParentObject = net_buf_pull_le32(payload);                /* ParentObject (0xFFFF if Object in Root) */
        net_buf_pull_le16(payload);                                        /* AssociationType */
        net_buf_pull_le32(payload);                                        /* AssociationDesc */
        net_buf_pull_le32(payload);                                        /* SequenceNumber */
        str_len = net_buf_pull_u8(payload);                                /* FileNameLength */
        net_buf_pull_utf16le(payload, filename, str_len);                  /* FileName */

        str_len = net_buf_pull_u8(payload);                                /* DateCreatedLength */
        net_buf_pull_utf16le(payload, date_created, str_len);              /* DateCreated */

        str_len = net_buf_pull_u8(payload);                                /* DateModifiedLength */
        net_buf_pull_utf16le(payload, date_modified, str_len);             /* DateModified */
        net_buf_pull_u8(payload);                                          /* KeywordsLength, always 0 unused */

        if (fs_obj->parent_id==0xff){
            snprintf(filepath, MAX_PATH_LEN, "%s/%s", available_storages[fs_obj->storage_id].mountpoint, filename);
        } else {
            printk("mnt: %s\n", available_storages[fs_obj->storage_id].mountpoint);
            printk("fname: %s\n",filename);
            printk("parentID: %u\n",fs_obj->parent_id);
            printf("parentPath:%s\n",
                    available_storages[fs_obj->storage_id].fileslist[fs_obj->parent_id].name);

            snprintf(filepath, MAX_PATH_LEN, "%s/%s/%s",
                    available_storages[fs_obj->storage_id].mountpoint,
                    available_storages[fs_obj->storage_id].fileslist[fs_obj->parent_id].name,
                    filename);
        }

        printk("\noFormat: %x, size: %x, parent: %x\n",
                ObjectFormat, fs_obj->size, ParentObject);
        printk("\nfname: %s\n",filename);
        printk("\ncreated_on: %s\n", date_created);
        printk("\nmodified_on: %s\n", date_modified);
        printk("\npath: %s ID:%x\n", filepath, fs_obj->ID);

        fs_file_t_init(&mtp_ctx.filestate.file);
        int ret = fs_open(&mtp_ctx.filestate.file, filepath, FS_O_CREATE | FS_O_WRITE);
        if (ret) {
            LOG_ERR("Open file failed, %d", ret);
            //TODO: Respond with error
        }
        mtp_ctx.filestate.total_size = fs_obj->size;

        struct mtp_container mtp_response = {
            .hdr = {
                .length = 24,
                .type = MTP_CONTAINER_RESPONSE,
                .code = MTP_RESP_OK,
                .transaction_id = mtp_command->hdr.transaction_id,
            },
            .param[0] = GEN_INTERNAL_STORAGE_ID(fs_obj->storage_id),
            .param[1] = (fs_obj->parent_id == 0xff ? 0xffffffff : available_storages[fs_obj->storage_id].fileslist[fs_obj->parent_id].ID),
            .param[2] = fs_obj->ID
        };
        LOG_INF("Sent info: \n\tSID: %x\n\tPID: %x\n\tOID: %x",
            mtp_response.param[0],mtp_response.param[1],mtp_response.param[2]);
        net_buf_add_mem(buf, &mtp_response, 24);
        fs_obj = NULL;
    }
}

int extra_data_handler(struct net_buf* buf,struct net_buf* buf_recv)
{
    mtp_ctx.filestate.transferred += buf_recv->len;
    mtp_ctx.filestate.chunks_sent++;

    fs_write(&mtp_ctx.filestate.file, buf_recv->data, buf_recv->len);
    LOG_INF("EXTRA: Data len: %u out of %u", mtp_ctx.filestate.transferred, mtp_ctx.filestate.total_size);
    if (mtp_ctx.filestate.transferred >= mtp_ctx.filestate.total_size) {
        fs_close(&mtp_ctx.filestate.file);
        LOG_INF("Sending Confirmation after reciving data (Total len: %u)", mtp_ctx.filestate.transferred);
        LOG_INF("Old filecount %u", available_storages[1].files_count);

        mtp_ctx.filestate.chunks_sent=0;
        mtp_ctx.filestate.transferred=0;
        mtp_ctx.filestate.total_size=0;

        mtp_send_confirmation(buf);
    } else {
        extra_data_fn = extra_data_handler;
        set_needs_more_data(true);
    }
    return 0;
}

MTP_CMD_HANDLER(MTP_OP_SEND_OBJECT)
{
    if (mtp_command->hdr.type == MTP_CONTAINER_COMMAND) {
        LOG_INF("COMMAND RECEIVED len: %u", payload->len);
    } else if (mtp_command->hdr.type == MTP_CONTAINER_DATA) {
        LOG_INF("DATA RECEIVED len: %u", payload->len); /* SKIP The header */
        net_buf_pull_mem(payload,sizeof(struct mtp_header));
        fs_write(&mtp_ctx.filestate.file, payload->data, payload->len);
        extra_data_fn = extra_data_handler;
        set_needs_more_data(true);
        mtp_ctx.filestate.chunks_sent++;
        mtp_ctx.filestate.transferred += payload->len;
        LOG_INF("SEND_OBJECT: Data len: %u out of %u", mtp_ctx.filestate.transferred, mtp_ctx.filestate.total_size);
    }

    return;
}

MTP_CMD_HANDLER(MTP_OP_DELETE_OBJECT)
{
    uint32_t storage_id = GET_STORAGE_ID(mtp_command->param[0]);
    uint32_t object_id = GET_OBJECT_ID(mtp_command->param[0]);

    char* filepath = available_storages[storage_id].fileslist[object_id].path;
    fs_unlink(filepath);

    struct mtp_header mtp_response = {
        .length = sizeof(struct mtp_header),
        .type = MTP_CONTAINER_RESPONSE,
        .code = MTP_RESP_OK,
        .transaction_id = mtp_command->hdr.transaction_id
    };

    net_buf_add_mem(buf, &mtp_response, sizeof(struct mtp_header));
}

MTP_CMD_HANDLER(MTP_OP_GET_OBJECT_REFERENCES)
{
    uint32_t objcount = 0;

    for (int i=0; i < available_storages[1].files_count; i++) {
        if (available_storages[1].fileslist[i].parent_id == 0xff) {
            objcount++;
            net_buf_add_le32(buf, available_storages[1].fileslist[i].ID);
        }
    }

    net_buf_push_le32(buf, objcount);

    data_header_push(buf, mtp_command, buf->len);

    set_pending_packet(mtp_send_confirmation);
}

int mtp_commands_handler(struct net_buf *buf_in, struct net_buf *buf)
{
    if (buf == NULL){
        LOG_ERR("%s: NULL Buffer", __func__);
        return -EINVAL;
    }

    if (handle_extra_data(buf, buf_in) == 0) {
        return buf->len;
    }
    //FIXME: Send mtp_header or mtp_container ? some commands has 0 params, some has 3 params
    //Leave skipping up to the command handler since we can't anticipate how many params should
    //be skipped
    //net_buf_pull_mem(buf_in, sizeof(struct mtp_header));
    struct mtp_header* mtp_command = (struct mtp_header*)buf_in->data;
    struct net_buf *payload = buf_in;

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
        MTP_CMD(MTP_OP_CLOSE_SESSION);
    break;
    case MTP_OP_GET_STORAGE_IDS:
        MTP_CMD(MTP_OP_GET_STORAGE_IDS);
    break;
    case MTP_OP_GET_STORAGE_INFO:
        MTP_CMD(MTP_OP_GET_STORAGE_INFO);
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
    case MTP_OP_DELETE_OBJECT:
        MTP_CMD(MTP_OP_DELETE_OBJECT);
        break;
    case MTP_OP_SEND_OBJECT_INFO:
        MTP_CMD(MTP_OP_SEND_OBJECT_INFO);
        break;
    case MTP_OP_SEND_OBJECT:
        MTP_CMD(MTP_OP_SEND_OBJECT);
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
    case MTP_OP_MOVE_OBJECT:
        LOG_ERR("MTP_OP_MOVE_OBJECT Not Implemented!");
        break;
    case MTP_OP_COPY_OBJECT:
        LOG_ERR("MTP_OP_COPY_OBJECT Not Implemented!");
        break;
    case MTP_OP_GET_OBJECT_REFERENCES:
        MTP_CMD(MTP_OP_GET_OBJECT_REFERENCES);
        break;
    default:
        LOG_ERR("Unknown cmd 0x%x!", mtp_command->code);
    break;
    }

    return buf->len;
}

static int mtp_send_confirmation(struct net_buf *buf)
{
    if (buf == NULL){
        LOG_ERR("%s: Null Buffer!", __func__);
        return -EINVAL;
    }

    struct mtp_container* mtp_command = (struct mtp_container*)buf->data;
    struct mtp_header mtp_response = {
        .length = sizeof(struct mtp_header),
        .type = MTP_CONTAINER_RESPONSE,
        .code = MTP_RESP_OK,
        .transaction_id = mtp_command->hdr.transaction_id
    };

    net_buf_add_mem(buf, &mtp_response, sizeof(struct mtp_header));

    return 0;
}

int mtp_init()
{
#if 0
    fs_unlink("/lfs1/desktop.ini");
    LOG_INF("Found %u storages", ARRAY_SIZE(available_storages)-1);

    for (int i=1; i < ARRAY_SIZE(available_storages); i++) {
        if (available_storages[i].files_count != 1){
            return 0;
        }
        LOG_INF("Storage %u: %s", i, available_storages[i].mountpoint);

        struct fs_statvfs stat;
        int err = fs_statvfs(available_storages[i].mountpoint, &stat);
        if (err < 0) {
            LOG_ERR("Failed to statvfs %s (%d)", available_storages[i].mountpoint, err);
            return -ENOEXEC;
        }

        LOG_INF("Max capacity %lu, freesize %lu, blocks %lu, bfree %lu\n",
             stat.f_blocks * stat.f_frsize, stat.f_frsize * stat.f_bfree, stat.f_blocks, stat.f_bfree);

        dir_traverse(i, available_storages[i].mountpoint, 0xFFFFFFFF);
    }

    for (int storageIdx=1; storageIdx< ARRAY_SIZE(available_storages); storageIdx++){
        LOG_INF("File list Storage %s", available_storages[storageIdx].mountpoint);
        for (int i=0;i<available_storages[storageIdx].files_count;i++)
        {
            LOG_INF("ID: 0x%08x S: %02x, P: %02x, T:%s, O: %02x : %s",
                        available_storages[storageIdx].fileslist[i].ID,
                        available_storages[storageIdx].fileslist[i].storage_id,
                        available_storages[storageIdx].fileslist[i].parent_id,
                        (available_storages[storageIdx].fileslist[i].type == 1 ? "D" : "f"),
                        available_storages[storageIdx].fileslist[i].object_id,
                        available_storages[storageIdx].fileslist[i].path);
        }
        LOG_INF("\n\n");
    }
#endif
    return 0;
}
