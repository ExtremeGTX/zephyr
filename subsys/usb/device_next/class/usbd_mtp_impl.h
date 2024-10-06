#ifndef _USBD_MTP_IMPL_H_
#define _USBD_MTP_IMPL_H_

bool mtp_packet_pending();
bool mtp_packet_pending();
bool mtp_needs_more_data(struct net_buf *buf);
int mtp_commands_handler(struct net_buf *buf_in, struct net_buf *buf);
int send_pending_packet(struct net_buf *buf);


#endif