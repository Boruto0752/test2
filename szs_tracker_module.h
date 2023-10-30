#ifndef SZS_TRACKER_MODULE_H
#define SZS_TRACKER_MODULE_H

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
int register_block_device_by_path  ( char *blockDevicePath );
int unregister_block_device_by_path( char *blockDevicePath );

#endif // SZS_TRACKER_MODULE_H
/////////////////////////////////////////////////////////////////////////////////////////////
// Local Variables:
// indent-tabs-mode: t
// fill-column: 100
// End: