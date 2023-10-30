#ifndef SZS_TRACKER_SOCKETPOOL_H
#define SZS_TRACKER_SOCKETPOOL_H

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
// #include <includes.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <constants.h>

/////////////////////////////////////////////////////////////////////////////////////////////
struct socket_pool_entry
{
	struct socket* socket;
	bool inUse;
};

/////////////////////////////////////////////////////////////////////////////////////////////
struct socket_pool
{
	struct socket_pool_entry entries[SOCKET_POOL_MAX_SOCKETS];
	unsigned int size;
	unsigned int minSize;
	unsigned int maxSize;
	char* ip;
	unsigned short port;
	spinlock_t socketPoolSpinlock;
};

/////////////////////////////////////////////////////////////////////////////////////////////
int socket_pool_init( struct socket_pool* socketPool, const char* ip, unsigned short port );
void socket_pool_cleanup( struct socket_pool* socketPool );
struct socket* get_free_socket( struct socket_pool* socketPool );
void put_socket( struct socket_pool* socketPool, struct socket* sock );

#endif // SZS_TRACKER_SOCKETPOOL_H

/////////////////////////////////////////////////////////////////////////////////////////////
// Local Variables:
// indent-tabs-mode: t
// fill-column: 100
// End: