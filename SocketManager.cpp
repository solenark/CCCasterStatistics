#include "SocketManager.h"
#include "Socket.h"
#include "TimerManager.h"
#include "Logger.h"

#include <winsock2.h>
#include <windows.h>

#include <cassert>

using namespace std;

void SocketManager::check ( uint64_t timeout )
{
    if ( !initialized )
        return;

    for ( Socket *socket : allocatedSockets )
    {
        if ( activeSockets.find ( socket ) != activeSockets.end() )
            continue;

        LOG_SOCKET ( socket, "added" );
        activeSockets.insert ( socket );
    }

    for ( auto it = activeSockets.begin(); it != activeSockets.end(); )
    {
        if ( allocatedSockets.find ( *it ) != allocatedSockets.end() )
        {
            ++it;
            continue;
        }

        LOG ( "socket %08x removed", *it ); // Don't log any extra data cus already deleted
        activeSockets.erase ( it++ );
    }

    if ( activeSockets.empty() )
        return;

    fd_set readFds, writeFds;
    FD_ZERO ( &readFds );
    FD_ZERO ( &writeFds );

    for ( Socket *socket : activeSockets )
    {
        if ( socket->isConnecting() && socket->isTCP() )
            FD_SET ( socket->fd, &writeFds );
        else
            FD_SET ( socket->fd, &readFds );
    }

    timeval tv;
    tv.tv_sec = timeout / 1000UL;
    tv.tv_usec = ( timeout * 1000UL ) % 1000000UL;

    int count = select ( 0, &readFds, &writeFds, 0, &tv );

    if ( count == SOCKET_ERROR )
    {
        WindowsError err = WSAGetLastError();
        LOG ( "select failed: %s", err );
        throw err;
    }

    if ( count == 0 )
        return;

    assert ( TimerManager::get().isInitialized() == true );
    TimerManager::get().updateCurrentTime();

    for ( Socket *socket : activeSockets )
    {
        if ( allocatedSockets.find ( socket ) == allocatedSockets.end() )
            continue;

        if ( socket->isConnecting() && socket->isTCP() )
        {
            if ( !FD_ISSET ( socket->fd, &writeFds ) )
                continue;

            LOG_SOCKET ( socket, "connectEvent" );
            socket->connectEvent();
        }
        else
        {
            if ( !FD_ISSET ( socket->fd, &readFds ) )
                continue;

            if ( socket->isServer() && socket->isTCP() )
            {
                LOG_SOCKET ( socket, "acceptEvent" );
                socket->acceptEvent();
            }
            else
            {
                u_long numBytes;

                if ( ioctlsocket ( socket->fd, FIONREAD, &numBytes ) != 0 )
                {
                    WindowsError err = WSAGetLastError();
                    LOG ( "ioctlsocket failed: %s", err );
                    throw err;
                }

                if ( socket->isTCP() && numBytes == 0 )
                {
                    LOG_SOCKET ( socket, "disconnectEvent" );
                    socket->disconnectEvent();
                }
                else
                {
                    LOG_SOCKET ( socket, "readEvent" );
                    socket->readEvent();
                }
            }
        }
    }
}

void SocketManager::add ( Socket *socket )
{
    LOG_SOCKET ( socket, "Added socket" );
    allocatedSockets.insert ( socket );
}

void SocketManager::remove ( Socket *socket )
{
    LOG_SOCKET ( socket, "Removing socket" );
    allocatedSockets.erase ( socket );
}

void SocketManager::clear()
{
    LOG ( "Clearing sockets" );
    activeSockets.clear();
    allocatedSockets.clear();
}

SocketManager::SocketManager() : initialized ( false ) {}

void SocketManager::initialize()
{
    if ( initialized )
        return;

    // Initialize WinSock
    WSADATA wsaData;
    int error = WSAStartup ( MAKEWORD ( 2, 2 ), &wsaData );

    if ( error != NO_ERROR )
    {
        WindowsError err = error;
        LOG ( "WSAStartup failed: %s", err );
        throw err;
    }

    initialized = true;
}

void SocketManager::deinitialize()
{
    if ( !initialized )
        return;

    WSACleanup();
    initialized = false;
}

SocketManager& SocketManager::get()
{
    static SocketManager sm;
    return sm;
}