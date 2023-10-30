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
#include <szs_tracker_module.h>
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/version.h>
#include <linux/ftrace.h>
#include <linux/time.h>
#include <linux/net.h>
#include <linux/inet.h>

#include <logging.h>
#include <constants.h>
#include <socketpool.h>
#include <ioctl_handler.h>

/////////////////////////////////////////////////////////////////////////////////////////////
// Basic Information
/////////////////////////////////////////////////////////////////////////////////////////////
MODULE_LICENSE     ( SZS_TRACKER_LICENSE_TYPE );
MODULE_AUTHOR      ( SZS_TRACKER_AUTHOR       );
MODULE_DESCRIPTION ( SZS_TRACKER_DESCRIPTION  );
MODULE_VERSION     ( SZS_TRACKER_VERSION      );

/////////////////////////////////////////////////////////////////////////////////////////////
// Directives based on kernel versions
/////////////////////////////////////////////////////////////////////////////////////////////
#ifdef LINUX_VERSION_CODE

// From kernel version 5.9, make_request_fn is removed from request_queue
// Thus the interception happens differently from this version 
// https://lore.kernel.org/lkml/20200629193947.2705954-17-hch@lst.de/
#if LINUX_VERSION_CODE >= KERNEL_VERSION( 5, 9, 0 )
    #define KERNEL_VERSION_5_9_OR_NEWER 
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION( 5, 12, 0 )
    #define USE_BI_BDEV
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION( 6, 2, 0 )
    #define USE_SET_INSTRUCTION_POINTER
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION( 5, 11, 0 )
    #define HAS_FTRACE_REGS
#endif

#else
#error "LINUX_VERSION_CODE is not defined."
#endif

/////////////////////////////////////////////////////////////////////////////////////////////
#pragma pack(push, 1)
struct block_device_change_metadata
{
	char deviceName[BLOCK_DEVICE_NAME_LEN];  //32 Bytes
	struct timespec64 timestamp;             //16 Bytes
	uint64_t startingSector;                 // 8 Bytes
	uint64_t dataSize;                       // 8 Bytes
};
#pragma pack(pop)

/////////////////////////////////////////////////////////////////////////////////////////////
struct block_device_node
{
	struct block_device *blockDevice;
	struct list_head list;

	#ifndef KERNEL_VERSION_5_9_OR_NEWER
	blk_qc_t (*original_make_request_fn)( struct request_queue*, struct bio* );
	#endif
};

/////////////////////////////////////////////////////////////////////////////////////////////
static struct file_operations trackerControlFops =
{
	.unlocked_ioctl = tracker_ioctl,
	.owner = THIS_MODULE
};
static struct miscdevice trackerControlDevice =
{
	.minor = MISC_DYNAMIC_MINOR,
	.name = SZS_TRACKER_CONTROL_DEVICE_NAME,
	.fops = &trackerControlFops
};

/////////////////////////////////////////////////////////////////////////////////////////////
static struct socket_pool socketPool;
static struct list_head deviceList;
bool socketPoolInitialized = false;
unsigned short port = 1234; //TO DO: Add a way to customize socket port, min/max socket config 

/////////////////////////////////////////////////////////////////////////////////////////////
// This function adds the given block device to the tracked device list.
// Note: For kernel version < 5.9.0, the original make_request_fn function of the block device
// will be stored in block_device_node struct 
/////////////////////////////////////////////////////////////////////////////////////////////
#ifdef KERNEL_VERSION_5_9_OR_NEWER
static int add_block_device_to_devicelist( struct block_device *blockDevice )
#else
static int add_block_device_to_devicelist( struct block_device *blockDevice, blk_qc_t (*original_make_request_fn)( struct request_queue*, struct bio* ) )
#endif
{
	struct block_device_node *node = NULL, *temp = NULL;
	list_for_each_entry_safe( node, temp, &deviceList, list )
	{
		if( node->blockDevice == blockDevice )
		{
			LOG_ERROR( -1, "Block device is already being tracked." );
			return -1;
		}
	}

	struct block_device_node *newNode = kmalloc( sizeof( *newNode ), GFP_KERNEL );
	if( !newNode )
	{
		LOG_ERROR( -ENOMEM, "Failed to allocate memory for block device node." );
		return -ENOMEM;
	}

	newNode->blockDevice = blockDevice;

	#ifndef KERNEL_VERSION_5_9_OR_NEWER
	newNode->original_make_request_fn = original_make_request_fn;
	#endif

	list_add( &newNode->list, &deviceList );

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// This function removes the given block device from the tracked device list
/////////////////////////////////////////////////////////////////////////////////////////////
static void remove_block_device_from_devicelist( struct block_device *blockDevice )
{
	struct block_device_node *node = NULL, *temp = NULL;

	list_for_each_entry_safe( node, temp, &deviceList, list )
	{
		if( node->blockDevice == blockDevice )
		{
			#ifdef KERNEL_VERSION_5_9_OR_NEWER
			blkdev_put( blockDevice, FMODE_READ );
			#endif

			list_del( &node->list );
			kfree( node );
			
			return;
		}
	}

	LOG_WARN( "Didn't found block device for removal" );
	return;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// This function writes data to a specified socket.
/////////////////////////////////////////////////////////////////////////////////////////////
static int write_to_socket( char *data, size_t len, struct socket* socket )
{
	struct msghdr msg;
	struct kvec vec;
	int result, written = 0;

	memset( &msg, 0, sizeof( struct msghdr ) );
	memset( &vec, 0, sizeof( struct kvec ) );

repeat_send:
	vec.iov_base = ( void * )data + written;
	vec.iov_len = len;
	result = kernel_sendmsg( socket, &msg, &vec, 1, len );
    
	if( result < 0 )
	{
		// Fatal error, tracking should stop here
		LOG_ERROR( result, "Failed to write data to socket." );
		return result;
	}
	else
	{
		written = written + result;
		len = len - result;
		if( len )
		{
			LOG_DEBUG( "Partially sent, sending remaining data ..." );
			goto repeat_send;
		}
	}

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// This function populates a struct 'block_device_change_metadata' with information extracted
// from a BIO.
/////////////////////////////////////////////////////////////////////////////////////////////

static void populate_block_device_change_metadata( struct bio* bio, struct block_device_change_metadata* blockDeviceChangeMetadata )
{
	blockDeviceChangeMetadata->startingSector  = bio->bi_iter.bi_sector;
	blockDeviceChangeMetadata->dataSize        = bio_sectors( bio )* BIO_SECTOR_SIZE;
	ktime_get_real_ts64( &blockDeviceChangeMetadata->timestamp );
	
	#ifdef USE_BI_BDEV
	strcpy( blockDeviceChangeMetadata->deviceName, bio->bi_bdev->bd_disk->disk_name );
	#else
	strcpy( blockDeviceChangeMetadata->deviceName, bio->bi_disk->disk_name );
	#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////
// This function writes actual block change data from a BIO to a specified socket.
// It iterates through bio segments, maps the page where block data exists, copies the
// data and unmaps the page. The copied data is then written to the specified socket.
/////////////////////////////////////////////////////////////////////////////////////////////
static void write_actual_block_change_to_socket( struct bio* bio, struct socket* socket )
{
	struct bio_vec bvec;
	struct bvec_iter bvecItr;

	bio_for_each_segment( bvec, bio, bvecItr )
	{
		char* data = kmap_atomic( bio_iter_page( bio, bvecItr ) );
		char* dst  = kmalloc( bio_iter_len( bio, bvecItr ), GFP_KERNEL);
		
		if( dst == NULL )
		{
			// Fatal error, tracking should stop here
			LOG_ERROR( -ENOMEM, "Failed to allocate memory for block data." );
			kunmap_atomic( data );
			return;
		}

		memcpy( dst, data + bio_iter_offset( bio, bvecItr ), bio_iter_len( bio, bvecItr ) );
		kunmap_atomic( data );
		
		write_to_socket( dst, bio_iter_len( bio, bvecItr ), socket );
		
		kfree( dst );
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////
// This function obtains a free socket from a socket pool, processes a specified BIO,
// populates block device change metadata, and writes both metadata and actual block change data to the socket.
/////////////////////////////////////////////////////////////////////////////////////////////
static void write_bio_to_socket( struct bio* bio )
{
	struct socket *socket = get_free_socket( &socketPool );
	if( socket == NULL )
	{
		// Fatal error, tracking should stop here
		LOG_ERROR( -1 , "Failed to get free socket from socket pool." );
		return;
	}

	struct block_device_change_metadata blockDeviceChangeMetadata;
	populate_block_device_change_metadata( bio, &blockDeviceChangeMetadata );
	write_to_socket( (char*)&blockDeviceChangeMetadata, sizeof( blockDeviceChangeMetadata ), socket );
	write_actual_block_change_to_socket( bio, socket );
	put_socket( &socketPool, socket );
}

/////////////////////////////////////////////////////////////////////////////////////////////
// This function iterates through a linked list of BIOs and process it.

// Important struct bio attributes - 
// bi_sector   : Indicates starting sector of I/O op
// bi_rw       : Indicates type of op (read/write)
// bi_size     : Size of data involved in op in bytes
// *bio_io_vec : Pointer to array of struct bio_vec
// *bi_next    : Points to next struct bio in the list

// Attributs of struct bio_vec - 
// *bv_page    : Points to page in physical memory that has data for op
// bv_offset   : Indicates byte offset within the page where data resides
// bv_len      : Indicate length of data in the physical page

// Note: We won't access the above fields directly due to variation across kernel versions
// Instead we will use predefined directives/functions to access the data from bio struct
// to ensure compatibility .
/////////////////////////////////////////////////////////////////////////////////////////////
static void extract_bios( struct bio* bio )
{
	while( bio != NULL ) 
	{       
		if( bio_has_data( bio ) )
		{
			write_bio_to_socket( bio );
		}
		else
		{
			LOG_DEBUG( "Bio struct has no data" );
		}

		bio = bio->bi_next;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////
#ifdef KERNEL_VERSION_5_9_OR_NEWER
// To avoid a recursive call to the hook function, we will jump
// over the ftrace call
void (*submit_bio_noacct_passthrough)( struct bio * ) =
	( void(*)( struct bio * ) )( ( unsigned long )( submit_bio_noacct ) +
	MCOUNT_INSN_SIZE );

/////////////////////////////////////////////////////////////////////////////////////////////
// For kernel version 5.9.0 and later, this function will be hooked to submit_bio_no_acct function
// It extracts the information from block I/O requests and then calls the original 
// submit_bio_no_acct function. 
/////////////////////////////////////////////////////////////////////////////////////////////
void tracing_fn( struct bio* bio ) 
{
	struct bio* initialBio = bio;
	struct block_device_node *node = NULL, *temp = NULL;
	struct block_device *blockDevice = NULL;
	list_for_each_entry_safe( node, temp, &deviceList, list )
	{
		blockDevice = node->blockDevice;
		#ifdef USE_BI_BDEV
		if( bio->bi_bdev == blockDevice )
		#else
		// Assuming that there will be no partitions for the disk
		// Note: This assumption can be removed by making use of
		// block device minor number
		if( bio->bi_disk == blockDevice->bd_disk )
		#endif
		{
			if( bio_data_dir( bio ) == WRITE )
			{
				extract_bios( bio );
			}
		}
	}

	submit_bio_noacct_passthrough( initialBio );
}
#else
/////////////////////////////////////////////////////////////////////////////////////////////
// For kernel version < 5.9, this function will replace the make_request_fn function of the 
// block device. It extracts the information from Block I/O request and calls the original
// make_request_fn function.
/////////////////////////////////////////////////////////////////////////////////////////////
blk_qc_t misc_make_request_fn( struct request_queue *requestQueue, struct bio* bio ) 
{
	struct bio* initialBio = bio;
	if( bio_data_dir( bio ) == WRITE )
	{
		extract_bios( bio );
	}

	struct block_device_node *node = NULL, *temp = NULL;
	list_for_each_entry_safe( node, temp, &deviceList, list )
	{
		// Assuming that there will be no partitions for the disk
		// Note: This assumption can be removed by making use of
		// block device minor number
		if( initialBio->bi_disk == node->blockDevice->bd_disk )
		{
			return node->original_make_request_fn( requestQueue, initialBio );
		}
	}

	return BLK_QC_T_NONE;
}
#endif

#ifdef KERNEL_VERSION_5_9_OR_NEWER
#ifdef HAS_FTRACE_REGS
static void notrace ftrace_handler_submit_bio_noacct( unsigned long ip,
						      unsigned long parent_ip,
						      struct ftrace_ops *fops,
						      struct ftrace_regs *fregs )
{
	#ifdef USE_SET_INSTRUCTION_POINTER
	ftrace_regs_set_instruction_pointer( fregs, ( unsigned long ) tracing_fn );
	#else
	ftrace_instruction_pointer_set( fregs, ( unsigned long ) tracing_fn );
	#endif
}
#else
static void notrace ftrace_handler_submit_bio_noacct( unsigned long ip,
						      unsigned long parent_ip,
						      struct ftrace_ops *fops,
						      struct pt_regs *fregs )
{
	fregs->ip = ( unsigned long )tracing_fn;
}
#endif

unsigned char* funcname_submit_bio_noacct = "submit_bio_noacct";

struct ftrace_ops opsSubmitBioNoacct =
{
	.func = ftrace_handler_submit_bio_noacct,
	.flags = FTRACE_OPS_FL_SAVE_REGS | FTRACE_OPS_FL_PERMANENT | FTRACE_OPS_FL_IPMODIFY
};

bool tracerRegistered = false;
/////////////////////////////////////////////////////////////////////////////////////////////
// This function sets up and registers a tracer filter for submit_bio_noacct function
/////////////////////////////////////////////////////////////////////////////////////////////
int register_tracer_filter( void )
{
	int ret = ftrace_set_filter( &opsSubmitBioNoacct,
				     funcname_submit_bio_noacct,
				     strlen( funcname_submit_bio_noacct ),
				     /*reset*/ 0 );
	
	if( ret )
	{
		LOG_ERROR( ret, "Failed to set ftrace filter." );
		return ret;
	}

	ret = register_ftrace_function( &opsSubmitBioNoacct );
	if( ret )
	{
		LOG_ERROR( ret, "Failed to register ftrace function." );
		return ret;
	}

	tracerRegistered = true;
	return ret;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// This function unregisters a tracer filter that was previously registered
/////////////////////////////////////////////////////////////////////////////////////////////
void unregister_tracer_filter( void )
{
	if( tracerRegistered )
	{
		int ret = unregister_ftrace_function( &opsSubmitBioNoacct );
		if( ret )
		{
			LOG_WARN( "Failed to unregister tracer filter. ERR : %d", ret );
		}
	}
}
#endif

/////////////////////////////////////////////////////////////////////////////////////////////
// This function registers the block device specified by its path for tracking.  
/////////////////////////////////////////////////////////////////////////////////////////////
int register_block_device_by_path( char *blockDevicePath )
{
	int ret = 0;
	if( !blockDevicePath )
	{
		ret = -1;
		LOG_ERROR( ret, "Block device path is empty." );
		return ret;
	}

	struct block_device* blockDevice = NULL;
	#ifdef KERNEL_VERSION_5_9_OR_NEWER
	blockDevice = blkdev_get_by_path( blockDevicePath, FMODE_READ, /*holder*/ NULL );
	#else
	blockDevice = lookup_bdev( blockDevicePath );
	#endif

	if( blockDevice == NULL || IS_ERR( blockDevice ) )
	{
		ret = -ENODEV;
		LOG_ERROR( ret, "Block device %s not found.", blockDevicePath );
		return ret;
	}

	if( !socketPoolInitialized )
	{
		ret = socket_pool_init( &socketPool, LOCALHOST, port );
		if( ret )
		{
			LOG_ERROR( ret, "Error creating socket pool." );
			return ret;
		}

		socketPoolInitialized = true;
	}

	#ifndef KERNEL_VERSION_5_9_OR_NEWER
	// For kernel version < 5.9.0, get the block device queue and replace
	// the make_request_fn function by misc_make_request_fn
	struct request_queue *blockDeviceQueue = NULL;
	blockDeviceQueue = blockDevice->bd_queue;

	// This should never happen
	if( !blockDeviceQueue )
	{
		ret = -1;
		LOG_ERROR( ret, "Block device queue is NULL.");
		return ret; 
	}

	blk_qc_t (*original_make_request_fn)(struct request_queue*, struct bio*) = blockDeviceQueue->make_request_fn;
	blockDeviceQueue->make_request_fn = misc_make_request_fn;
	#endif

	#ifdef KERNEL_VERSION_5_9_OR_NEWER
	ret = add_block_device_to_devicelist( blockDevice );
	#else
	ret = add_block_device_to_devicelist( blockDevice, original_make_request_fn );
	#endif

	if( ret )
	{
		LOG_ERROR( ret, "Failed add block device to device list." );
		return ret;
	}

	return ret;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// This function unregisters the specified block device from tracking  
/////////////////////////////////////////////////////////////////////////////////////////////
static void unregister_block_device( struct block_device *blockDevice )
{
	#ifndef KERNEL_VERSION_5_9_OR_NEWER
	// For kernel version < 5.9.0, replace back make_request_fn function
	// by device's original make_request_fn.
	struct block_device_node *node = NULL, *temp = NULL;
	struct request_queue *blockDeviceQueue = blockDevice->bd_queue;
	
	list_for_each_entry_safe( node, temp, &deviceList, list )
	{
		if( node->blockDevice == blockDevice )
		{
			blockDeviceQueue->make_request_fn = node->original_make_request_fn;
		}
	}
	#endif

	remove_block_device_from_devicelist( blockDevice );

	return;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// This function unregisters the block device specified by its path from tracking.  
/////////////////////////////////////////////////////////////////////////////////////////////
int unregister_block_device_by_path( char *blockDevicePath )
{
	int ret = 0;
	if( !blockDevicePath )
	{
		ret = -1;
		LOG_ERROR( ret, "Block device path is empty." );
		return ret;
	}

	struct block_device* blockDevice = NULL;
	#ifdef KERNEL_VERSION_5_9_OR_NEWER
	blockDevice = blkdev_get_by_path( blockDevicePath, FMODE_READ, /*holder*/ NULL );
	#else
	blockDevice = lookup_bdev( blockDevicePath );
	#endif

	if( blockDevice == NULL || IS_ERR( blockDevice ) )
	{
		ret = -ENODEV;
		LOG_ERROR( ret, "Block device %s not found.", blockDevicePath );
		return ret;
	}

	unregister_block_device( blockDevice );
	
	#ifdef KERNEL_VERSION_5_9_OR_NEWER
	blkdev_put( blockDevice, FMODE_READ );
	#endif

	return ret;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// This function unregisters all tracked block devices.  
/////////////////////////////////////////////////////////////////////////////////////////////
static void unregister_block_devices( void )
{
	struct block_device_node *node = NULL, *temp = NULL;
	list_for_each_entry_safe( node, temp, &deviceList, list )
	{
		unregister_block_device( node->blockDevice );
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////
static int register_ioctl_control_interface( void )
{
	return misc_register( &trackerControlDevice );
}

/////////////////////////////////////////////////////////////////////////////////////////////
static void unregister_ioctl_control_interface( void )
{
	misc_deregister( &trackerControlDevice );
}

/////////////////////////////////////////////////////////////////////////////////////////////
void tracker_exit( void )
{
	LOG_INFO( "Unloading module..." );

	unregister_ioctl_control_interface();
	
	#ifdef KERNEL_VERSION_5_9_OR_NEWER
	unregister_tracer_filter();
	#endif
 
	unregister_block_devices();

	socket_pool_cleanup( &socketPool );

	LOG_INFO( "Module unloaded." );
}

/////////////////////////////////////////////////////////////////////////////////////////////
static int __init tracker_init( void )
{
	LOG_INFO( "Initializing module..." );

	int ret = 0;
	ret = register_ioctl_control_interface();
	if( ret )
	{
		LOG_ERROR( ret, "Error registering SZS control device." );
		goto error;
	}

	INIT_LIST_HEAD( &deviceList );

	#ifdef KERNEL_VERSION_5_9_OR_NEWER
	ret = register_tracer_filter();
	if( ret )
	{
		LOG_ERROR( ret, "Error registering tracer filter." );
		goto error;
	}
	#endif

	LOG_INFO( "Module initialized successfully." );
	return ret;

error:
	tracker_exit();
	return ret;
}

/////////////////////////////////////////////////////////////////////////////////////////////
module_init( tracker_init );
module_exit( tracker_exit );

/////////////////////////////////////////////////////////////////////////////////////////////
// Local Variables:
// indent-tabs-mode: t
// fill-column: 100
// End:
