#ifndef SZS_TRACKER_IOCTL_HANDLER_H
#define SZS_TRACKER_IOCTL_HANDLER_H

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
#include <linux/uaccess.h>

/////////////////////////////////////////////////////////////////////////////////////////////
long tracker_ioctl( struct file *fp, unsigned int cmd, unsigned long arg );

#endif //SZS_TRACKER_IOCTL_HANDLER_H

/////////////////////////////////////////////////////////////////////////////////////////////
// Local Variables:
// indent-tabs-mode: t
// fill-column: 100
// End: