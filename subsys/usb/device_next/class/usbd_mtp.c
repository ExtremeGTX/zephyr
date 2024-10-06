/*
 * Copyright (c) 2024 Mohamed ElShahawi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/usb/udc.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(usb_mtp, 4); //CONFIG_USBD_MTP_LOG_LEVEL

/* Endpoint addresses */
#define MTP_IN_EP_ADDR                  0x81  /* Bulk IN */
#define MTP_OUT_EP_ADDR                 0x01  /* Bulk OUT */
#define MTP_INTR_EP_ADDR                0x82  /* Interrupt IN */

/* MTP Class-Specific Request Codes */
#define MTP_REQUEST_CANCEL              0x64U
#define MTP_REQUEST_GET_DEVICE_STATUS   0x67U
#define MTP_REQUEST_DEVICE_RESET        0x66U

/* MTP Response Codes */
#define MTP_RESP_OK                 0x2001

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





/* MTP Image Formats */
#define MTP_FORMAT_ASSOCIATION                     0x3001
#define MTP_FORMAT_TEXT                            0x3004

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

#if 0
static const struct usbd_cctx_vendor_req mtp_vregs =
	USBD_VENDOR_REQ(MTP_REQUEST_GET_DEVICE_STATUS,
                    MTP_REQUEST_DEVICE_RESET,
                    MTP_REQUEST_CANCEL);
#endif

//void (*const destroy)(struct net_buf *buf);
int allocated_bufs = 0;
void buf_destroyed(struct net_buf *buf)
{
    allocated_bufs--;
    struct udc_buf_info *bi = udc_get_buf_info(buf);
    net_buf_destroy(buf);
    LOG_WRN("BUF <Destroyed> %p EP: 0x%x (Allocated bufs: %d)", buf, bi->ep, allocated_bufs);
}

UDC_BUF_POOL_DEFINE(mtp_ep_pool, 2, 512, sizeof(struct udc_buf_info), buf_destroyed);

struct k_work mtp_workq;

enum mtp_container_type {
    MTP_CONTAINER_UNDEFINED = 0x00,
    MTP_CONTAINER_COMMAND,
    MTP_CONTAINER_DATA,
    MTP_CONTAINER_RESPONSE,
    MTP_CONTAINER_EVENT,
};

struct mtp_desc {

	/* Full Speed Descriptors */
    struct usb_if_descriptor if0;
	struct usb_ep_descriptor if0_out_ep;
	struct usb_ep_descriptor if0_in_ep;
	struct usb_ep_descriptor if0_int_in_ep;

	/* High Speed Descriptors */
	struct usb_ep_descriptor if0_hs_out_ep;
	struct usb_ep_descriptor if0_hs_in_ep;

        /* Termination descriptor */
	struct usb_desc_header nil_desc;
};

struct mtp_data {
	struct mtp_desc *const desc;
	const struct usb_desc_header **const fs_desc;
	const struct usb_desc_header **const hs_desc;
	atomic_t state;
};


static void mtp_update(struct usbd_class_data *c_data,
		      uint8_t iface, uint8_t alternate)
{
	LOG_DBG("Instance %p, interface %u alternate %u changed",
		c_data, iface, alternate);
}

static uint8_t mtp_get_bulk_in(struct usbd_class_data *const c_data)
{
	struct usbd_context *uds_ctx = usbd_class_get_ctx(c_data);
	const struct device *dev = usbd_class_get_private(c_data);
	struct mtp_data *data = dev->data;
	struct mtp_desc *desc = data->desc;

    LOG_WRN("DEV: %s", dev->name);
    LOG_WRN("FS EP IN: 0x%x, HS EP IN: 0x%x",
            desc->if0_hs_in_ep.bEndpointAddress,
            desc->if0_in_ep.bEndpointAddress);

    if (usbd_bus_speed(uds_ctx) == USBD_SPEED_HS) {
		return 0x82;
	}

	return 0x82;
}

struct net_buf *mtp_buf_alloc(const uint8_t ep)
{
	struct net_buf *buf = NULL;
	struct udc_buf_info *bi;

	buf = net_buf_alloc(&mtp_ep_pool, K_NO_WAIT);
	if (!buf) {
		return NULL;
	}

	bi = udc_get_buf_info(buf);
	memset(bi, 0, sizeof(struct udc_buf_info));
	bi->ep = ep;
    allocated_bufs++;
    LOG_WRN("Buf >Allocated<: %p EP: 0x%x (Allocated bufs: %d)",buf, ep, allocated_bufs);
	return buf;
}

static int mtp_control_to_host(struct usbd_class_data *c_data,
			      const struct usb_setup_packet *const setup,
			      struct net_buf *const buf)
{
	LOG_ERR("%s: Class request 0x%x (Recipient: %x) is not Implemented",
                        __func__,
                        setup->bRequest,
                        setup->RequestType.recipient);

#if 0
    if (buf->len < MTP_RX_BUF_SIZE) {
        memcpy(mtp_usb_buf, buf->data, buf->len);
    } else {
        LOG_WRN("Data dropped! len: %u", buf->len);
    }

    if (setup->bRequest == MTP_REQUEST_GET_DEVICE_STATUS) {
        LOG_DBG(">MTP_REQUEST_GET_DEVICE_STATUS");
        static struct mtp_device_status mtp_status = {
            .wLength = 6,
            .wCode = MTP_RESP_OK
        };

        //memcpy(buf->data, &mtp_status, sizeof(mtp_status));
        //buf->len = sizeof(mtp_status);
        //net_buf_add_mem(buf, &mtp_status, sizeof(mtp_status));
        struct net_buf *bufp = mtp_buf_alloc(0x81);
        if (bufp == NULL){
            LOG_ERR("Buffer allocation failed!");
            return 0;
        }

        mtp_get_bulk_in(c_data);
        net_buf_add_mem(bufp, &mtp_status, sizeof(struct mtp_device_status));
        LOG_HEXDUMP_WRN(bufp->data, sizeof(struct mtp_device_status), "Data to be sent");
        //memcpy(bufp,&mtp_status, sizeof(struct mtp_device_status));
        //net_buf_add(bufp, sizeof(struct mtp_device_status));

        int ret = usbd_ep_enqueue(c_data, bufp);
        if (ret) {
            LOG_ERR("Failed to enqueue net_buf %d", ret);
            net_buf_unref(bufp);
        }
        return 0;
    }
#endif
	return 0;
}

static int mtp_control_to_dev(struct usbd_class_data *c_data,
			     const struct usb_setup_packet *const setup,
			     const struct net_buf *const buf)
{
	LOG_ERR("%s: Class request 0x%x (Recipient: %x) is not Implemented",
                        __func__,
                        setup->bRequest,
                        setup->RequestType.recipient);


	return 0;
}

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

static int confirm = 0;
static struct net_buf* mtp_commands_handler(struct net_buf *buf, struct net_buf *buf_out)
{
#if 0
    static struct net_buf *bufp = NULL;
    if (bufp == NULL){
        bufp = mtp_buf_alloc(0x81);
    } else {
        LOG_WRN("REF COUNT %u", bufp->ref);
        net_buf_unref(bufp);
        net_buf_reset(bufp);
        bufp = mtp_buf_alloc(0x81);
        if (bufp == NULL){
            LOG_ERR("%s: Buffer allocation failed!", __func__);
            LOG_ERR("REF COUNT %u", bufp->ref);
            return NULL;
        }
    }
#endif
    struct net_buf *bufp = buf_out;
    if (bufp == NULL){
        LOG_ERR("%s: NULL Buffer", __func__);
        return NULL;
    }

        LOG_WRN("REF COUNT %u", bufp->ref);

        if (buf->len <= sizeof(struct mtp_container)) {
        struct mtp_container* mtp_command = (struct mtp_container*)buf->data;
        switch(mtp_command->code){
            case MTP_OP_GET_DEVICE_INFO:
                LOG_DBG("MTP_OP_GET_DEVICE_INFO Submit reply!");
                confirm = 1;
                static struct mtp_data_block data_block;
                data_block.container_length = (sizeof(struct mtp_data_block) + sizeof(struct mtp_device_info) );
                data_block.container_type = 0x0002;
                data_block.response_code = mtp_command->code;
                data_block.transaction_id = mtp_command->transaction_id;

                //bufp = mtp_buf_alloc(0x81);
                if (bufp == NULL){
                            LOG_ERR("%s: Buffer allocation failed! 1", __func__);
                            return 0;
                }
                net_buf_add_mem(bufp, &data_block, sizeof(struct mtp_data_block));
                net_buf_add_mem(bufp, &device_info, sizeof(struct mtp_device_info));
            break;
            case MTP_OP_OPEN_SESSION:
                LOG_DBG("MTP_OP_OPEN_SESSION Submit reply!");
                struct mtp_container mtp_response = {
                    .length = 12,
                    .type = MTP_CONTAINER_RESPONSE,
                    .code = MTP_RESP_OK,
                    .transaction_id = mtp_command->transaction_id
                };

                //bufp = mtp_buf_alloc(0x81);
                if (bufp == NULL){
                            LOG_ERR("%s: Buffer allocation failed! 2", __func__);
                            return 0;
                }
                net_buf_add_mem(bufp, &mtp_response, 12);
                break;

            case MTP_OP_GET_OBJECT_PROPS_SUPPORTED:
                LOG_DBG("MTP_OP_GET_OBJECT_PROPS_SUPPORTED Submit reply!");
                data_block.container_type = 0x0002;
                data_block.response_code = mtp_command->code;
                data_block.transaction_id = mtp_command->transaction_id;

                uint32_t props_count = 11;
                uint16_t props[] = {
                    MTP_PROPERTY_STORAGE_ID,
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
                //bufp = mtp_buf_alloc(0x81);
                if (bufp == NULL){
                            LOG_ERR("%s: Buffer allocation failed! 3", __func__);
                            return 0;
                }
                net_buf_add_mem(bufp, &data_block, sizeof(struct mtp_data_block));
                net_buf_add_mem(bufp, &props_count, sizeof(uint32_t));
                net_buf_add_mem(bufp, &props, sizeof(props));

                confirm = 1;
            break;
            case MTP_OP_CLOSE_SESSION:
                LOG_ERR("MTP_OP_CLOSE_SESSION not implemented!");
            break;
            case MTP_OP_GET_STORAGE_IDS:
                LOG_DBG("MTP_OP_GET_STORAGE_IDS Submit reply!");
            break;
            case MTP_OP_GET_STORAGE_INFO:
                LOG_DBG("MTP_OP_GET_STORAGE_INFO Submit reply!");
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
                LOG_ERR("Unknown cmd 0x%x!", mtp_command->code);
            break;
        }
    }
    return bufp;
}

static void mtp_enable(struct usbd_class_data *const c_data);

static int mtp_request_handler(struct usbd_class_data *c_data,
			      struct net_buf *buf, int err)
{
        struct usbd_context *uds_ctx = usbd_class_get_ctx(c_data);
        struct udc_buf_info *bi =  (struct udc_buf_info *)net_buf_user_data(buf);
        int ret = 0;

        struct net_buf* buf_resp = NULL;

        if (bi->ep == 0x01){
            LOG_INF("=================START=================");
            LOG_INF("%s: %p -> ep 0x%02x, buf: %p len %u, err %d",__func__, c_data, bi->ep, buf, buf->len, err);
            LOG_HEXDUMP_INF(buf->data, buf->len, "mtp_request_handler");
            buf_resp = mtp_buf_alloc(0x81);
            if (buf_resp == NULL){
                LOG_ERR("%s: Buffer allocation failed!", __func__);
                LOG_ERR("REF COUNT %u", buf_resp->ref);
                return -1;
            }
#if 0
            /* Allocate buffer for sending data */
            if (buf_resp == NULL){
                buf_resp = mtp_buf_alloc(0x81);
            } else {
                LOG_WRN("REF COUNT %u", buf_resp->ref);
                net_buf_unref(buf_resp);
                net_buf_reset(buf_resp);
                buf_resp = mtp_buf_alloc(0x81);
                if (buf_resp == NULL){
                    LOG_ERR("%s: Buffer allocation failed!", __func__);
                    LOG_ERR("REF COUNT %u", buf_resp->ref);
                    return -1;
                }
            }
#endif
            buf_resp = mtp_commands_handler(buf, buf_resp);
            ret = usbd_ep_enqueue(c_data, buf_resp);
            if (ret) {
                LOG_ERR("Failed to enqueue net_buf %d", ret);
                net_buf_unref(buf_resp);
            }
            LOG_INF("[Sent DONE]");
        } else {
            LOG_WRN("Discard event EP: %x (buf %p, len: %u)", bi->ep, buf, buf->len);
            if (confirm){
                LOG_INF("Confirm to HOST");
                buf_resp = mtp_buf_alloc(0x81);
                if (buf_resp == NULL){
                    LOG_ERR("%s: Buffer allocation failed 4!", __func__);
                    LOG_ERR("REF COUNT %u", buf_resp->ref);
                    return -1;
                }

                struct mtp_container* mtp_command = (struct mtp_container*)buf->data;
                confirm = 0;
                struct mtp_container mtp_response = {
                    .length = 12,
                    .type = MTP_CONTAINER_RESPONSE,
                    .code = MTP_RESP_OK,
                    .transaction_id = mtp_command->transaction_id
                };
                net_buf_add_mem(buf_resp, &mtp_response, 12);
                ret = usbd_ep_enqueue(c_data, buf_resp);
                if (ret) {
                    LOG_ERR("Failed to enqueue net_buf %d", ret);
                    net_buf_unref(buf_resp);
                }
                LOG_INF("CONFIRMATION DONE");
            } else {
                mtp_enable(c_data);
            }
            LOG_INF("================= END =================");
        }
        return usbd_ep_buf_free(uds_ctx, buf);
}

/* Class associated configuration is selected */
static void mtp_enable(struct usbd_class_data *const c_data)
{
	struct mtp_data *data = usbd_class_get_private(c_data);

    struct net_buf *bufp = mtp_buf_alloc(0x01);
    if (bufp == NULL){
        LOG_ERR("%s: Buffer allocation failed! 5", __func__);
        return;
    }

    int ret = usbd_ep_enqueue(c_data, bufp);
    if (ret) {
        LOG_ERR("Init Failed to enqueue net_buf %d", ret);
        net_buf_unref(bufp);
    }

	LOG_INF("Ready to receive from HOST");
}

/* Class associated configuration is disabled */
static void mtp_disable(struct usbd_class_data *const c_data)
{
	struct mtp_data *data = usbd_class_get_private(c_data);

	LOG_ERR("**************Disable**************");
}

static void *mtp_get_desc(struct usbd_class_data *const c_data,
			 const enum usbd_speed speed)
{
	struct mtp_data *data = usbd_class_get_private(c_data);

	if (speed == USBD_SPEED_HS) {
		return data->hs_desc;
	}

	return data->fs_desc;
}

static int mtp_init(struct usbd_class_data *c_data)
{
        LOG_DBG("Init class instance %p", c_data);

        return 0;
}

struct usbd_class_api mtp_api = {
    .update = mtp_update,
	.control_to_dev = mtp_control_to_dev,
	.control_to_host = mtp_control_to_host,
	.request = mtp_request_handler,
	.enable = mtp_enable,
    .disable = mtp_disable,
    .get_desc = mtp_get_desc,
	.init = mtp_init,
};

static struct mtp_desc mtp_desc_0 = {
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

    .if0_int_in_ep = {
        .bLength          = sizeof(struct usb_ep_descriptor),
        .bDescriptorType  = USB_DESC_ENDPOINT,
        .bEndpointAddress = MTP_INTR_EP_ADDR,
        .bmAttributes     = USB_EP_TYPE_INTERRUPT,
        .wMaxPacketSize   = sys_cpu_to_le16(16U),
        .bInterval        = 0x06,
    },

    /* Full Speed*/
    .if0_in_ep = {
        .bLength          = sizeof(struct usb_ep_descriptor),
        .bDescriptorType  = USB_DESC_ENDPOINT,
        .bEndpointAddress = MTP_IN_EP_ADDR,
        .bmAttributes     = USB_EP_TYPE_BULK,
        .wMaxPacketSize   = sys_cpu_to_le16(64U),
        .bInterval        = 0x00,
    },

    .if0_out_ep = {
        .bLength          = sizeof(struct usb_ep_descriptor),
        .bDescriptorType  = USB_DESC_ENDPOINT,
        .bEndpointAddress = MTP_OUT_EP_ADDR,
        .bmAttributes     = USB_EP_TYPE_BULK,
        .wMaxPacketSize   = sys_cpu_to_le16(64U),
        .bInterval        = 0x00,
    },

    /* High Speed */
    .if0_hs_in_ep = {
        .bLength          = sizeof(struct usb_ep_descriptor),
        .bDescriptorType  = USB_DESC_ENDPOINT,
        .bEndpointAddress = MTP_IN_EP_ADDR,
        .bmAttributes     = USB_EP_TYPE_BULK,
        .wMaxPacketSize   = sys_cpu_to_le16(512U),
        .bInterval        = 0x00,
    },

    .if0_hs_out_ep = {
        .bLength          = sizeof(struct usb_ep_descriptor),
        .bDescriptorType  = USB_DESC_ENDPOINT,
        .bEndpointAddress = MTP_OUT_EP_ADDR,
        .bmAttributes     = USB_EP_TYPE_BULK,
        .wMaxPacketSize   = sys_cpu_to_le16(512U),
        .bInterval        = 0x00,
    },

    .nil_desc = {
        .bLength = 0,
        .bDescriptorType = 0,
     },
};

const static struct usb_desc_header *mtp_fs_desc_0[] = {
	(struct usb_desc_header *) &mtp_desc_0.if0,
	(struct usb_desc_header *) &mtp_desc_0.if0_in_ep,
	(struct usb_desc_header *) &mtp_desc_0.if0_out_ep,
	(struct usb_desc_header *) &mtp_desc_0.if0_int_in_ep,
	(struct usb_desc_header *) &mtp_desc_0.nil_desc,
};
const static struct usb_desc_header *mtp_hs_desc_0[] = {
	(struct usb_desc_header *) &mtp_desc_0.if0,
	(struct usb_desc_header *) &mtp_desc_0.if0_hs_in_ep,
	(struct usb_desc_header *) &mtp_desc_0.if0_hs_out_ep,
	(struct usb_desc_header *) &mtp_desc_0.if0_int_in_ep,
	(struct usb_desc_header *) &mtp_desc_0.nil_desc,
};

static struct mtp_data mtp_data0 = {
        .desc = &mtp_desc_0,
        .fs_desc = mtp_fs_desc_0,
        .hs_desc = mtp_hs_desc_0,
};

USBD_DEFINE_CLASS(mtp, &mtp_api, &mtp_data0, /*&mtp_vregs*/ NULL);