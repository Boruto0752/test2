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
#include <socketpool.h>
#include <logging.h>

/////////////////////////////////////////////////////////////////////////////////////////////
static int create_socket( struct socket** sock )
{
	int kernelSocketCreateRC = sock_create_kern( &init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, sock );
	if( kernelSocketCreateRC < 0 )
	{
		return kernelSocketCreateRC;
	}

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////
static int connect( struct socket* sock, const char *ip, unsigned short port )
{
	struct sockaddr_in address;
	int kernelSocketConnectRC = 0;
	memset( &address, 0, sizeof( struct sockaddr_in ) );
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = in_aton( ip );
	address.sin_port = htons( port );

	kernelSocketConnectRC = sock->ops->connect( sock, ( struct sockaddr * )&address, sizeof( struct sockaddr_in ), O_WRONLY );
	if( kernelSocketConnectRC < 0 )
	{
		sock_release( sock );
		return kernelSocketConnectRC;
	}

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////
static void destroy_socket( struct socket* sock )
{
	if ( sock != NULL )
	{
		sock_release( sock );
		sock = NULL;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////
// This function initializes a socket pool with SOCKET_POOL_MIN_SOCKETS number of sockets connected
// to the specified IP and port.
/////////////////////////////////////////////////////////////////////////////////////////////
int socket_pool_init( struct socket_pool* socketPool, const char *ip, unsigned short port )
{
	if( socketPool == NULL )
	{
		LOG_ERROR( -1, "Socket pool is NULL." );
		return -1;
	}

	socketPool->size = SOCKET_POOL_MIN_SOCKETS;
	socketPool->minSize = SOCKET_POOL_MIN_SOCKETS;
	socketPool->maxSize = SOCKET_POOL_MAX_SOCKETS;
	socketPool->socketPoolSpinlock = __SPIN_LOCK_UNLOCKED( socketPool->socketPoolSpinlock );
	socketPool->port = port;
	socketPool->ip = kmalloc( strlen( ip ), GFP_KERNEL);
	if( socketPool->ip == NULL )
	{
		LOG_ERROR( -ENOMEM, "Failed to allocate memory for IP." );
		return -ENOMEM;
	}

	strcpy( socketPool->ip, ip );

	unsigned int index = 0;
	while( index < socketPool->size )
	{
		struct socket* sock = NULL;
		int ret = create_socket( &sock );
		if( ret != 0 )
		{
			LOG_ERROR( ret, "Sock[%u]: Failed to create socket.", index );
			return ret;
		}

		ret = connect( sock, ip, port );
		if( ret != 0 )
		{
			LOG_ERROR( ret, "Sock[%u]: Failed to connect socket.", index );
			return ret;
		}

		socketPool->entries[index].socket = sock;
		socketPool->entries[index].inUse = false;

		index++;
	}

	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// This function cleans up and release resources associated with a socket pool.
/////////////////////////////////////////////////////////////////////////////////////////////
void socket_pool_cleanup( struct socket_pool* socketPool )
{
	if( socketPool == NULL )
	{
		LOG_ERROR( -1, "Socket pool is NULL." );
		return;
	}

	unsigned int index = 0;
	while( index < socketPool->size )
	{
		destroy_socket( socketPool->entries[index].socket );
		index++;
	}

	socketPool->size = 0;
	socketPool->port = 0;
	socketPool->minSize = 0;
	socketPool->maxSize = 0;

	kfree( socketPool->ip );
}

/////////////////////////////////////////////////////////////////////////////////////////////
// This function is used to obtain a free socket from a given socket pool. If a free socket is
// available in the pool, it is returned. If the pool is not full, and no free sockets are available,
// a new socket will be created and returned.
/////////////////////////////////////////////////////////////////////////////////////////////
struct socket* get_free_socket( struct socket_pool* socketPool )
{
	if( socketPool == NULL )
	{
		LOG_ERROR( -1, "Socket pool is NULL." );
		return NULL;
	}

	spin_lock( &socketPool->socketPoolSpinlock );
	struct socket* sock = NULL;

	// Check if any free socket available in pool
	unsigned int index = 0;
	while( index < socketPool->size )
	{
		if( !socketPool->entries[index].inUse )
		{
			socketPool->entries[index].inUse = true;
			sock = socketPool->entries[index].socket;
			break;
		}

		index++;
	}

	// If no free socket availble in pool check if socket pool is full
	if( sock == NULL && socketPool->size >= socketPool->maxSize )
	{
		LOG_ERROR( -1, "Socket pool is full. No more sockets can be created." );
		spin_unlock( &socketPool->socketPoolSpinlock );
		
		return NULL;
	}

	// If no free socket available and socket pool is not full
	// Create and connect new socket.
	if( sock == NULL )
	{
		int ret = create_socket( &sock );
		if ( ret != 0)
		{
			LOG_ERROR( ret, "Failed to create new socket." );
			spin_unlock( &socketPool->socketPoolSpinlock );

			return NULL;
		}

		ret = connect( sock, socketPool->ip, socketPool->port );
		if( ret != 0 )
		{
			LOG_ERROR( ret, "Failed to connect new socket." );
			spin_unlock( &socketPool->socketPoolSpinlock );

			return NULL;
		}

		socketPool->entries[socketPool->size].socket = sock;
		socketPool->entries[socketPool->size].inUse = true;
		socketPool->size++;

		LOG_DEBUG( "New socket added to socket pool, number of sockets in pool : %u", socketPool->size );
	}

	spin_unlock( &socketPool->socketPoolSpinlock );

	return sock;
}

/////////////////////////////////////////////////////////////////////////////////////////////
// This function is used to release a socket back to a given socket pool. It marks the specified
// socket as available for reuse by setting the 'inUse' flag to false within the socket pool's
// entries
/////////////////////////////////////////////////////////////////////////////////////////////
void put_socket( struct socket_pool* socketPool, struct socket* sock )
{
	if( socketPool == NULL )
	{
		LOG_ERROR( -1, "Socket pool is NULL." );
		return;
	}

	if( sock == NULL )
	{
		LOG_ERROR( -1, "Socket is NULL." );
		return;
	}

	unsigned int index = 0;
	while( index < socketPool->size )
	{
		if( socketPool->entries[index].socket == sock )
		{
			socketPool->entries[index].inUse = false;
			break;
		}

		index++;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////
// Local Variables:
// indent-tabs-mode: t
// fill-column: 100
// End: