#ifndef SZS_TRACKER_CONSTANTS_H
#define SZS_TRACKER_CONSTANTS_H

/*******************************************************************/
/*                                                                 */
/*                  Copyright 2023 RackWare, Inc.                  */
/*                                                                 */
/*  This is an unpublished work, is confidential and proprietary   */
/*  to RackWare as a trade secret and is not to be used or         */
/*  disclosed except and to the extent expressly permitted in an   */
/*  applicable RackWare license agreement.                         */
/*                                                                 */
/*******************************************************************/
#define SOCKET_POOL_MAX_SOCKETS 10
#define SOCKET_POOL_MIN_SOCKETS 2

#define SZS_TRACKER_VERSION "1.0.0"
#define SZS_TRACKER_LICENSE_TYPE "GPL"
#define SZS_TRACKER_AUTHOR "Chetan Atole"
#define SZS_TRACKER_DESCRIPTION "SZS Tracker Module"

#define SZS_TRACKER_CONTROL_DEVICE_NAME "szs_tracker-ctl"

#define BLOCK_DEVICE_NAME_LEN 32
#define BLOCK_DEVICE_PATH_LEN 256
#define BIO_SECTOR_SIZE 512

#define LOCALHOST "127.0.0.1"

#define SZS_TRACKER_IOCTL_MAGIC 0x91

// IOCTL cmd
#define BLOCK_DEVICE_ADD    _IOW( SZS_TRACKER_IOCTL_MAGIC, 1, char * )
#define BLOCK_DEVICE_REMOVE _IOW( SZS_TRACKER_IOCTL_MAGIC, 2, char * )

#endif // SZS_TRACKER_CONSTANTS_H
/////////////////////////////////////////////////////////////////////////////////////////////
// Local Variables:
// indent-tabs-mode: t
// fill-column: 100
// End: