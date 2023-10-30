#ifndef SZS_TRACKER_LOGGING_H
#define SZS_TRACKER_LOGGING_H

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
#include <error_utils.h>

/////////////////////////////////////////////////////////////////////////////////////////////
// Printing macros
#define LOG_DEBUG( fmt, args... )                                                               \
	do                                                                                      \
	{                                                                                       \
		printk( KERN_DEBUG "SZS Tracker: " fmt "\n", ##args );                          \
	} while( 0 )

#define LOG_WARN( fmt, args... )    printk( KERN_WARNING "SZS Tracker: " fmt "\n", ##args )
#define LOG_EMERG( fmt, args... )   printk( KERN_EMERG "SZS Tracker: " fmt "\n", ##args )
#define LOG_ALERT( fmt, args... )   printk( KERN_ALERT "SZS Tracker: " fmt "\n", ##args )
#define LOG_CRIT( fmt, args... )    printk( KERN_CRIT "SZS Tracker: " fmt "\n", ##args )
#define LOG_NOTICE( fmt, args... )  printk( KERN_NOTICE "SZS Tracker: " fmt "\n", ##args )
#define LOG_INFO( fmt, args... )    printk( KERN_INFO "SZS Tracker: " fmt "\n", ##args )
#define LOG_ERROR( error, fmt, args... )                                                        \
	printk( KERN_ERR "SZS Tracker: " fmt " [ERROR Code: %d] ERROR: %s\n", ##args, error, get_error_message( error ) )

#endif // SZS_TRACKER_LOGGING_H
/////////////////////////////////////////////////////////////////////////////////////////////
// Local Variables:
// indent-tabs-mode: t
// fill-column: 100
// End: