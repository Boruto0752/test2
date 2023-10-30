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
#include <ioctl_handler.h>
#include <szs_tracker_module.h>
#include <logging.h>
#include <constants.h>

/////////////////////////////////////////////////////////////////////////////////////////////
long tracker_ioctl( struct file *fp, unsigned int cmd, unsigned long arg )
{
	int ret = 0;
	char blockDevicePath[BLOCK_DEVICE_PATH_LEN];
	switch( cmd )
	{
		case BLOCK_DEVICE_ADD:
			if( copy_from_user( blockDevicePath, ( char * )arg, BLOCK_DEVICE_PATH_LEN ) )
			{
				LOG_ERROR( -EFAULT, "Failed to copy device path from user space." );
				return -EFAULT;
			}
			
			ret = register_block_device_by_path( blockDevicePath );
			break;
		case BLOCK_DEVICE_REMOVE:
			if( copy_from_user( blockDevicePath, ( char * )arg, BLOCK_DEVICE_PATH_LEN ) )
			{
				LOG_ERROR( -EFAULT, "Failed to copy device path from user space." );
				return -EFAULT;
			}

			ret = unregister_block_device_by_path( blockDevicePath );
			break;

		default:
			ret = -EINVAL;
			LOG_ERROR( ret, "Invalid ioctl called." );
			break;
	}

	return ret;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// Local Variables:
// indent-tabs-mode: t
// fill-column: 100
// End: