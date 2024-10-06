/* mtp_device.c */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/usb/usb_device.h>
#include <usb_descriptor.h>
#include <zephyr/logging/log.h>

#include <usb_work_q.h>

LOG_MODULE_REGISTER(mtp_device, 4);

/* Define USB Base Class Code for Image devices */
#define USB_BCC_IMAGE         0x06

/* Endpoint addresses */
#define MTP_IN_EP_ADDR        0x81  /* Bulk IN */
#define MTP_OUT_EP_ADDR       0x01  /* Bulk OUT */
#define MTP_INTR_EP_ADDR      0x82  /* Interrupt IN */

/* Maximum packet size */
#define MTP_BULK_EP_MPS       64
#define MTP_INTR_EP_MPS       16

/* MTP Container Types */
#define MTP_CONTAINER_UNDEFINED 0x0000
#define MTP_CONTAINER_COMMAND   0x0001
#define MTP_CONTAINER_DATA      0x0002
#define MTP_CONTAINER_RESPONSE  0x0003
#define MTP_CONTAINER_EVENT     0x0004

/* MTP Operation Codes */
#define MTP_OP_GET_DEVICE_INFO      0x1001
#define MTP_OP_OPEN_SESSION         0x1002
#define MTP_OP_CLOSE_SESSION        0x1003
#define MTP_OP_GET_STORAGE_IDS      0x1004
#define MTP_OP_GET_STORAGE_INFO     0x1005
#define MTP_OP_GET_NUM_OBJECTS      0x1006
#define MTP_OP_GET_OBJECT_HANDLES   0x1007
#define MTP_OP_GET_OBJECT_INFO      0x1008
#define MTP_OP_GET_OBJECT           0x1009
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
/* Add more operation codes as needed */

/* MTP Response Codes */
#define MTP_RESP_OK                 0x2001
#define MTP_RESP_GENERAL_ERROR      0x2002
#define MTP_RESP_SESSION_NOT_OPEN   0x2003
#define MTP_RESP_SESSION_ALREADY_OPEN 0x201E
#define MTP_RESP_INVALID_OBJECT_HANDLE 0x2009
/* Add more response codes as needed */

/* MTP Image Formats */
#define MTP_FORMAT_ASSOCIATION                     0x3001
#define MTP_FORMAT_TEXT                            0x3004

/* MTP Event Codes */
#define MTP_EVENT_OBJECT_ADDED                      0x4002
#define MTP_EVENT_OBJECT_REMOVED                    0x4003
#define MTP_EVENT_STORE_ADDED                       0x4004
#define MTP_EVENT_STORE_REMOVED                     0x4005
#define MTP_EVENT_DEVICE_PROP_CHANGED               0x4006
#define MTP_EVENT_OBJECT_INFO_CHANGED               0x4007

/* MTP Device properties */
#define MTP_DEVICE_PROPERTY_BATTERY_LEVEL           0x5001

/* Object Properties */
#define MTP_PROPERTY_STORAGE_ID                     0xDC01
#define MTP_PROPERTY_OBJECT_FORMAT                  0xDC02
#define MTP_PROPERTY_PROTECTION_STATUS              0xDC03
#define MTP_PROPERTY_OBJECT_SIZE                    0xDC04
#define MTP_PROPERTY_OBJECT_FILE_NAME               0xDC07
#define MTP_PROPERTY_DATE_MODIFIED                  0xDC09
#define MTP_PROPERTY_PARENT_OBJECT                  0xDC0B
#define MTP_PROPERTY_PERSISTENT_UID                 0xDC41
#define MTP_PROPERTY_NAME                           0xDC44
#define MTP_PROPERTY_DISPLAY_NAME                   0xDCE0
#define MTP_PROPERTY_FAX_NUMBER_BUSINESS            0xDD16

/* MTP Class-Specific Request Codes */
#define MTP_REQUEST_CANCEL            0x64
#define MTP_REQUEST_GET_DEVICE_STATUS 0x67
#define MTP_REQUEST_DEVICE_RESET      0x66

/* MTP Command Block (Host Request) */
struct mtp_command_block {
    uint32_t container_length;  // Total length of the command block
    uint16_t container_type;    // Should be 0x0001 for Command Block
    uint16_t operation_code;    // MTP operation code (e.g., MTP_OP_OPEN_SESSION)
    uint32_t transaction_id;    // Transaction ID to track the command
    uint32_t param1;            // Optional Parameter 1 (e.g., session ID)
    uint32_t param2;            // Optional Parameter 2 (depends on the command)
    uint32_t param3;            // Optional Parameter 3 (depends on the command)
} __packed;

/* MTP Response Block (Device Response) */
struct mtp_response_block {
    uint32_t container_length;  // Total length of the response block
    uint16_t container_type;    // Should be 0x0003 for Response Block
    uint16_t response_code;     // MTP response code (e.g., MTP_RESP_OK)
    uint32_t transaction_id;    // Transaction ID of the command being responded to
    uint32_t param1;            // Optional Parameter 1 (varies by response)
    uint32_t param2;            // Optional Parameter 2 (depends on the response)
} __packed;

struct mtp_data_block {
    uint32_t container_length;  // Total length of the response block
    uint16_t container_type;    // Should be 0x0002 for Data Block
    uint16_t response_code;     // MTP response code (e.g., MTP_RESP_OK)
    uint32_t transaction_id;    // Transaction ID of the command being responded to
} __packed;


struct usb_mtp_config {
	struct usb_if_descriptor if0;
	struct usb_ep_descriptor if0_out_ep;
	struct usb_ep_descriptor if0_in_ep;
	struct usb_ep_descriptor if0_intr_ep;
} __packed;

struct mtp_device_info {
    uint16_t standard_version;
    uint32_t vendor_extension_id;
    uint16_t vendor_extension_version;
    uint8_t  vendor_extension_desc_len;
    uint16_t vendor_extension_desc[38];  // Vendor extension description in UTF-16LE
    uint16_t functional_mode;
    uint32_t operations_supported_count;
    uint16_t operations_supported[27];  // Adjust size as needed
    uint32_t events_count;
    uint16_t events_supported[6];       // Adjust size as needed
    uint32_t device_properties_count;
    uint16_t device_properties_supported[1];  // Adjust size as needed
    uint32_t formats_count;
    uint32_t image_formats_count;
    uint16_t image_formats[2];
    uint8_t manufacturer_len;
    uint16_t manufacturer[8];   // Manufacturer name (UTF-16LE, null-terminated)
    uint8_t model_len;
    uint16_t model[9];          // Model name (UTF-16LE, null-terminated)
    uint8_t device_version_len;
    uint16_t device_version[4]; // Device version (UTF-16LE, null-terminated)
    uint8_t serial_number_len;
    uint16_t serial_number[17];  // Serial number (UTF-16LE, null-terminated)
} __packed;

static struct mtp_device_info device_info = {
    .standard_version = 100,            // MTP version 1.00
    .vendor_extension_id = 6,  // MTP standard extension ID (Microsoft)
    .vendor_extension_version = 100,    // Vendor extension version
    .vendor_extension_desc_len = 38,    // Length in bytes, not characters
    .vendor_extension_desc = { 'm', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't', '.', 'c', 'o', 'm', ':', ' ', '1', '.', '0', ';',' ','a','n','d','r','o','i','d','.','c','o','m',':',' ','1','.','0',';', '\0' },  // "microsoft.com: 1.0;" in UTF-16LE
    .functional_mode = 0,               // Standard mode
    .operations_supported_count = 27,
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
        MTP_OP_GET_THUMB,
        MTP_OP_DELETE_OBJECT,
        MTP_OP_SEND_OBJECT_INFO,
        MTP_OP_SEND_OBJECT,
        MTP_OP_RESET_DEVICE,
        MTP_OP_GET_DEVICE_PROP_DESC,
        MTP_OP_GET_DEVICE_PROP_VALUE,
        MTP_OP_SET_DEVICE_PROP_VALUE,
        MTP_OP_RESET_DEVICE_PROP_VALUE,
        MTP_OP_MOVE_OBJECT,
        MTP_OP_COPY_OBJECT,
        MTP_OP_GET_PARTIAL_OBJECT,
        MTP_OP_GET_OBJECT_PROPS_SUPPORTED,
        MTP_OP_GET_OBJECT_PROP_DESC,
        MTP_OP_GET_OBJECT_PROP_VALUE,
        MTP_OP_SET_OBJECT_PROP_VALUE,
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
    .manufacturer_len = 8,               // "My Manufacturer" is 14 characters
    .manufacturer = { 'S', 'A', 'M', 'S', 'U', 'N', 'G', '\0' },
    .model_len = 9,                       // "My Model" is 8 characters
    .model = { 'M', 'y', ' ', 'M', 'o', 'd', 'e', 'l' , '\0'},
    .device_version_len = 4,              // "1.0" is 3 characters
    .device_version = { '1', '.', '0' , '\0'},
    .serial_number_len = 17,              // Serial number must be 32 characters in UTF-16LE
    .serial_number = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A','B','C','D','E','F','\0'},
};

USBD_CLASS_DESCR_DEFINE(primary, 0) struct usb_mtp_config mtp_cfg = {
    .if0 = {
        .bLength            = sizeof(struct usb_if_descriptor),
        .bDescriptorType    = USB_DESC_INTERFACE,
        .bInterfaceNumber   = 0x00,
        .bAlternateSetting  = 0x00,
        .bNumEndpoints      = 0x03,
        .bInterfaceClass    = USB_BCC_IMAGE,
        .bInterfaceSubClass = 0x01,  /* Still Image Capture */
        .bInterfaceProtocol = 0x01,  /* Picture Transfer Protocol */
        .iInterface         = 0x00,
    },

    .if0_in_ep = {
        .bLength          = sizeof(struct usb_ep_descriptor),
        .bDescriptorType  = USB_DESC_ENDPOINT,
        .bEndpointAddress = MTP_IN_EP_ADDR,
        .bmAttributes     = USB_DC_EP_BULK,
        .wMaxPacketSize   = sys_cpu_to_le16(MTP_BULK_EP_MPS),
        .bInterval        = 0x00,
    },

    .if0_out_ep = {
        .bLength          = sizeof(struct usb_ep_descriptor),
        .bDescriptorType  = USB_DESC_ENDPOINT,
        .bEndpointAddress = MTP_OUT_EP_ADDR,
        .bmAttributes     = USB_DC_EP_BULK,
        .wMaxPacketSize   = sys_cpu_to_le16(MTP_BULK_EP_MPS),
        .bInterval        = 0x00,
    },

    .if0_intr_ep = {
        .bLength          = sizeof(struct usb_ep_descriptor),
        .bDescriptorType  = USB_DESC_ENDPOINT,
        .bEndpointAddress = MTP_INTR_EP_ADDR,
        .bmAttributes     = USB_DC_EP_INTERRUPT,
        .wMaxPacketSize   = sys_cpu_to_le16(MTP_INTR_EP_MPS),
        .bInterval        = 0x06,
    }
};


static const char* ep_status_code_str[] = {
	/** SETUP received */
	"SETUP received",
	/** Out transaction on this EP, data is available for read */
	"data is available for read",
	/** In transaction done on this EP */
	"In transaction done"
};

static void mtp_bulk_in_cb(uint8_t ep, enum usb_dc_ep_cb_status_code ep_status);
static void mtp_intr_in_cb(uint8_t ep, enum usb_dc_ep_cb_status_code ep_status);
static void mtp_bulk_out_cb(uint8_t ep, enum usb_dc_ep_cb_status_code ep_status);

/* Endpoint Configuration */
static struct usb_ep_cfg_data mtp_ep_cfg[] = {
    {
        .ep_cb   = mtp_bulk_in_cb,
        .ep_addr = MTP_IN_EP_ADDR,
    },
    {
        .ep_cb   = mtp_bulk_out_cb,
        .ep_addr = MTP_OUT_EP_ADDR,
    },
    {
        .ep_cb   = mtp_intr_in_cb,
        .ep_addr = MTP_INTR_EP_ADDR,
    }
};


struct k_work_delayable tx_work;
uint8_t mtp_usb_buf[1024] = {0};
static void mtp_bulk_out_cb(uint8_t ep, enum usb_dc_ep_cb_status_code ep_status)
{
	uint32_t bytes_to_read;

    LOG_DBG("==============================");
	usb_read(ep, NULL, 0, &bytes_to_read);
	LOG_DBG("%s: ep 0x%x, bytes to read %d ", __func__, ep, bytes_to_read);
    usb_read(ep, mtp_usb_buf, bytes_to_read, NULL);
    LOG_HEXDUMP_DBG(mtp_usb_buf, bytes_to_read, "Out from [HOST]: dump");

    if (bytes_to_read < sizeof(struct mtp_command_block)) {
        struct mtp_command_block* mtp_cmd = (struct mtp_command_block*)&mtp_usb_buf;
        switch(mtp_cmd->operation_code){
            case MTP_OP_GET_DEVICE_INFO:
                LOG_DBG("MTP_OP_GET_DEVICE_INFO Submit reply!");
            	k_work_schedule_for_queue(&USB_WORK_Q, &tx_work, K_NO_WAIT);
            break;
            case MTP_OP_OPEN_SESSION:
                LOG_DBG("MTP_OP_OPEN_SESSION Submit reply!");
            	k_work_schedule_for_queue(&USB_WORK_Q, &tx_work, K_NO_WAIT);
            break;
            case MTP_OP_GET_OBJECT_PROPS_SUPPORTED:
                LOG_DBG("MTP_OP_GET_OBJECT_PROPS_SUPPORTED Submit reply!");
            	k_work_schedule_for_queue(&USB_WORK_Q, &tx_work, K_NO_WAIT);
            break;
            case MTP_OP_CLOSE_SESSION:
                LOG_ERR("MTP_OP_CLOSE_SESSION not implemented!");
            break;
            case MTP_OP_GET_STORAGE_IDS:
                LOG_DBG("MTP_OP_GET_STORAGE_IDS Submit reply!");
                k_work_schedule_for_queue(&USB_WORK_Q, &tx_work, K_NO_WAIT);
            break;
            case MTP_OP_GET_STORAGE_INFO:
                LOG_DBG("MTP_OP_GET_STORAGE_INFO Submit reply!");
                k_work_schedule_for_queue(&USB_WORK_Q, &tx_work, K_NO_WAIT);
            break;
            case MTP_OP_GET_NUM_OBJECTS:
                LOG_ERR("MTP_OP_GET_NUM_OBJECTS not implemented!");
            break;
            case MTP_OP_GET_OBJECT_HANDLES:
                LOG_ERR("MTP_OP_GET_OBJECT_HANDLES not implemented!");
            break;
            case MTP_OP_GET_OBJECT_INFO:
                LOG_ERR("MTP_OP_GET_OBJECT_INFO not implemented!");
            break;
            case MTP_OP_GET_OBJECT:
                LOG_ERR("MTP_OP_GET_OBJECT not implemented!");
            break;
            default:
                LOG_ERR("Unknown cmd 0x%x!", mtp_cmd->operation_code);
            break;
        }
    }

}




bool confirm_compeletion = false;
uint32_t confirm_compeletion_command = 0;

static void mtp_bulk_in_cb(uint8_t ep, enum usb_dc_ep_cb_status_code ep_status)
{
	LOG_DBG("ep 0x%x status: %d (%s)", ep, ep_status, ep_status_code_str[ep_status]);
#if 0
    if (confirm_compeletion_command){
        LOG_WRN("Submitting Confirmation");
        k_work_schedule_for_queue(&USB_WORK_Q, &tx_work, K_NO_WAIT);
    }
#endif
}

static void mtp_intr_in_cb(uint8_t ep, enum usb_dc_ep_cb_status_code ep_status)
{
    LOG_WRN("INTR ep 0x%x status: %d (%s)", ep, ep_status, ep_status_code_str[ep_status]);
}

static void mtp_status_cb(struct usb_cfg_data *cfg,
				   enum usb_dc_status_code status,
				   const uint8_t *param)
{
    LOG_DBG("cfg %p status %d param %x", cfg, status, *param);

    /* Handle USB status events */
    switch (status) {
	case USB_DC_ERROR:
		LOG_DBG("USB device error");
		break;
	case USB_DC_RESET:
		LOG_DBG("USB device reset detected");
		break;
	case USB_DC_CONNECTED:
		LOG_DBG("USB device connected");
		break;
	case USB_DC_CONFIGURED:
		LOG_DBG("USB device configured");
		break;
	case USB_DC_DISCONNECTED:
		LOG_DBG("USB device disconnected");
		break;
	case USB_DC_SUSPEND:
		LOG_DBG("USB device suspended");
		break;
	case USB_DC_RESUME:
		LOG_DBG("USB device resumed");
		break;
	case USB_DC_INTERFACE:
		LOG_DBG("USB interface selected");
		break;
	case USB_DC_SOF:
		break;
	case USB_DC_UNKNOWN:
	default:
		LOG_ERR("USB unknown state");
		break;
	}
}

struct mtp_device_status {
            uint16_t  wLength;
            uint16_t  wCode;
        };

static int mtp_class_handle_req(struct usb_setup_packet *setup,
                                int32_t *len, uint8_t **data)
{
    uint8_t dir = USB_REQTYPE_GET_DIR(setup->bmRequestType);
    if (dir){
        LOG_DBG("Request from Device -> Host");
    } else {
        LOG_DBG("Request from Host -> Device");
    }

    LOG_DBG("Class request: bmRequestType 0x%02x, bRequest 0x%02x",
            setup->bmRequestType, setup->bRequest);
/**************************** */
    if (setup->bRequest == MTP_REQUEST_GET_DEVICE_STATUS) {
        LOG_DBG(">MTP_REQUEST_GET_DEVICE_STATUS (IS THIS WORKING ?)");
        static struct mtp_device_status mtp_status = {
            .wLength = 4,
            .wCode = MTP_RESP_OK
        };

        *data = (uint8_t *)&mtp_status;
        *len = 4;
        return 0;
    }
/**************************** */
    if (setup->bRequest == MTP_REQUEST_DEVICE_RESET) {
        LOG_DBG(">MTP Device Reset Requested");


        // Perform any necessary reset actions here

        // Acknowledge the request (no data needs to be sent in response)
        *len = 0;
        return 0;
    }
/**************************** */
    static const uint8_t mtp_storage_ids[] = {
        0x01, 0x00, 0x00, 0x00, // Number of storage IDs (1 in this case)
        0x01, 0x00, 0x00, 0x00  // Storage ID for the device's internal storage
    };

    if (setup->bRequest == MTP_OP_GET_STORAGE_IDS) {
        LOG_DBG(">MTP_OP_GET_STORAGE_IDS");
        *data = (uint8_t *)mtp_storage_ids;
        *len = sizeof(mtp_storage_ids);
        return 0;
    }
    return 0;
}

uint8_t buf[1024];
static void tx_work_handler(struct k_work *work)
{

	if (usb_transfer_is_busy(mtp_ep_cfg[0].ep_addr)) {
		LOG_WRN("Transfer is ongoing");
		return;
	}

    struct mtp_command_block* mtp_cmd = (struct mtp_command_block*)&mtp_usb_buf;

    LOG_DBG("Op: 0x%x, Transaction ID: %d", mtp_cmd->operation_code, mtp_cmd->transaction_id);
    int ret = 0;


    static struct mtp_response_block mtp_response_ok;
    static struct mtp_data_block data_block;


    memset(buf,0,1024);



    if (confirm_compeletion_command){
        struct mtp_command_block* mtp_cmd = (struct mtp_command_block*)&mtp_usb_buf;
        static struct mtp_response_block mtp_response_ok;

        LOG_DBG(">Confirm to HOST<");
        mtp_response_ok.container_length = 12;          // Total length of the response block
        mtp_response_ok.container_type = 0x0003;        // Should be 0x0002 for Response Block
        mtp_response_ok.response_code = MTP_RESP_OK;    // MTP response code (e.g., MTP_RESP_OK)
        mtp_response_ok.transaction_id = mtp_cmd->transaction_id;    // Transaction ID of the command being responded to

        k_sleep(K_MSEC(100));
        int ret = usb_write(mtp_ep_cfg[0].ep_addr, (const uint8_t *)&mtp_response_ok, 12, NULL);
        if (ret)
        {
            LOG_ERR("WRITE_TO_HOST ERR: %d", ret);
        }
        if (confirm_compeletion_command == mtp_cmd->operation_code){
            confirm_compeletion_command = 0x00;
            LOG_WRN("No New command, return!");
            return;
        } else {
            //countine to process the new command received
        }
    }


    switch(mtp_cmd->operation_code)
    {
        case MTP_OP_GET_DEVICE_INFO:
            data_block.container_length = (sizeof(struct mtp_data_block) + sizeof(struct mtp_device_info) );
            data_block.container_type = 0x0002;
            data_block.response_code = mtp_cmd->operation_code;
            data_block.transaction_id = mtp_cmd->transaction_id;


            memcpy(&buf[0],&data_block, sizeof(struct mtp_data_block));
            memcpy(&buf[sizeof(struct mtp_data_block)],&device_info, sizeof(struct mtp_device_info));

            //LOG_HEXDUMP_DBG(&data_block, sizeof(struct mtp_data_block), "DATA_BLOCK");
            //LOG_HEXDUMP_DBG(&buf[sizeof(struct mtp_data_block)], sizeof(struct mtp_device_info), "<COPIED> DEVICE_INFO");

            ret = usb_write(mtp_ep_cfg[0].ep_addr, (const uint8_t *)buf, data_block.container_length, NULL);
            if (ret)
            {
                LOG_ERR("WRITE_TO_HOST ERR: %d", ret);
                return;
            }

            //confirm_compeletion = true;
            confirm_compeletion_command = mtp_cmd->operation_code;
            break;

        case MTP_OP_OPEN_SESSION:
            LOG_DBG("Opening MTP session with ID %d", 0);
            mtp_response_ok.container_length = 12;          // Total length of the response block
            mtp_response_ok.container_type = 0x0003;        // Should be 0x0002 for Response Block
            mtp_response_ok.response_code = MTP_RESP_OK;    // MTP response code (e.g., MTP_RESP_OK)
            mtp_response_ok.transaction_id = mtp_cmd->transaction_id;    // Transaction ID of the command being responded to

            ret = usb_write(mtp_ep_cfg[0].ep_addr, (const uint8_t *)&mtp_response_ok, 12, NULL);
            if (ret)
            {
                LOG_ERR("WRITE_TO_HOST ERR: %d", ret);
                return;
            }
            break;

        case MTP_OP_GET_OBJECT_PROPS_SUPPORTED:
            LOG_DBG("MTP_OP_GET_OBJECT_PROPS_SUPPORTED");
            data_block.container_type = 0x0002;
            data_block.response_code = mtp_cmd->operation_code;
            data_block.transaction_id = mtp_cmd->transaction_id;

            uint32_t props_count = 11;
            uint16_t props[] = {MTP_PROPERTY_STORAGE_ID,
                MTP_PROPERTY_OBJECT_FORMAT,
                MTP_PROPERTY_PROTECTION_STATUS,
                MTP_PROPERTY_OBJECT_SIZE,
                MTP_PROPERTY_OBJECT_FILE_NAME,
                MTP_PROPERTY_DATE_MODIFIED,
                MTP_PROPERTY_PARENT_OBJECT,
                MTP_PROPERTY_PERSISTENT_UID,
                MTP_PROPERTY_NAME,
                MTP_PROPERTY_DISPLAY_NAME,
                MTP_PROPERTY_FAX_NUMBER_BUSINESS
            };

            data_block.container_length = (sizeof(struct mtp_data_block) + sizeof (uint32_t) + sizeof(props));
            memcpy(&buf[0],&data_block, sizeof(struct mtp_data_block));
            memcpy(&buf[sizeof(struct mtp_data_block)],&props_count, sizeof(uint32_t));
            memcpy(&buf[sizeof(struct mtp_data_block)+sizeof(uint32_t)],&props, sizeof(props));

            ret = usb_write(mtp_ep_cfg[0].ep_addr, (const uint8_t *)buf, data_block.container_length, NULL);
            if (ret)
            {
                LOG_ERR("WRITE_TO_HOST ERR: %d", ret);
                return;
            }
            confirm_compeletion_command = mtp_cmd->operation_code;
            break;

        case MTP_OP_GET_STORAGE_IDS:
            LOG_DBG("MTP_OP_GET_OBJECT_PROPS_SUPPORTED");
            data_block.container_type = 0x0002;
            data_block.response_code = mtp_cmd->operation_code;
            data_block.transaction_id = mtp_cmd->transaction_id;

            uint32_t storage_ids_count = 2;
            const uint32_t storage_ids[] = {
                0x00010001,  // Storage ID for internal memory
                0x00020001   // Storage ID for external SD card
            };

            data_block.container_length = (sizeof(struct mtp_data_block) + sizeof (uint32_t) + sizeof(storage_ids));
            memcpy(&buf[0],&data_block, sizeof(struct mtp_data_block));
            memcpy(&buf[sizeof(struct mtp_data_block)],&storage_ids_count, sizeof(uint32_t));
            memcpy(&buf[sizeof(struct mtp_data_block)+sizeof(uint32_t)],&storage_ids, sizeof(storage_ids));

            ret = usb_write(mtp_ep_cfg[0].ep_addr, (const uint8_t *)buf, data_block.container_length, NULL);
            if (ret)
            {
                LOG_ERR("WRITE_TO_HOST ERR: %d", ret);
                return;
            }
            confirm_compeletion_command = mtp_cmd->operation_code;
            break;

        case MTP_OP_GET_STORAGE_INFO:
            LOG_DBG("MTP_OP_GET_STORAGE_INFO (%x)", mtp_cmd->param1);
            data_block.container_type = 0x0002;
            data_block.response_code = mtp_cmd->operation_code;
            data_block.transaction_id = mtp_cmd->transaction_id;

#define STORAGE_TYPE_FIXED_ROM  0x0001
#define STORAGE_TYPE_REMOVABLE_ROM  0x0002
#define STORAGE_TYPE_FIXED_RAM  0x0003
#define STORAGE_TYPE_REMOVABLE_RAM  0x0004


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

            if (mtp_cmd->param1 == 0x20001){
                storage_info = &storage_info2;
            }


            data_block.container_length = (sizeof(struct mtp_data_block) + sizeof(struct storage_info_t));
            memcpy(&buf[0],&data_block, sizeof(struct mtp_data_block));
            memcpy(&buf[sizeof(struct mtp_data_block)],storage_info, sizeof(struct storage_info_t));

            ret = usb_write(mtp_ep_cfg[0].ep_addr, (const uint8_t *)buf, data_block.container_length, NULL);
            if (ret)
            {
                LOG_ERR("WRITE_TO_HOST ERR: %d", ret);
                return;
            }
            confirm_compeletion_command = mtp_cmd->operation_code;
            break;

        default:
            LOG_ERR("Command: %x, Not implemented!", mtp_cmd->operation_code);
            break;
    }

    if (confirm_compeletion_command != 0)
    {
        LOG_WRN("Schedule confirmation");
        k_work_schedule_for_queue(&USB_WORK_Q, &tx_work, K_NO_WAIT);
    }
}

static void mtp_interface_config(struct usb_desc_header *head,
				      uint8_t bInterfaceNumber)
{
	ARG_UNUSED(head);
    LOG_INF("mtp_interface_config");
	mtp_cfg.if0.bInterfaceNumber = bInterfaceNumber;
    k_work_init_delayable(&tx_work, tx_work_handler);
}



/* USB Configuration Data */
USBD_DEFINE_CFG_DATA(mtp_config) = {
    .usb_device_description = NULL,
    .interface_config = mtp_interface_config,
    .interface_descriptor = &mtp_cfg.if0,
    .cb_usb_status = mtp_status_cb,
    .interface = {
        .class_handler = mtp_class_handle_req,
        .custom_handler = NULL,
        .vendor_handler = NULL,
    },
    .num_endpoints = ARRAY_SIZE(mtp_ep_cfg),
    .endpoint = mtp_ep_cfg,
};
