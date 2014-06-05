#include "Socket.h"
#include "Log.h"

using namespace std;

#define LISTEN_INTERVAL 1000

BlockingQueue<shared_ptr<Socket::ConnectThread>> Socket::connectingThreads;

Socket::ReaperThread Socket::reaperThread;

Socket::Socket()
    : socketAcceptCmd ( *this )
    , socketDisconnectCmd ( *this )
    , socketReadCmd ( *this )
    , listenThread ( *this )
{
}

Socket::~Socket()
{
    disconnect();
}

void Socket::Accept::exec ( NL::Socket *serverSocket, NL::SocketGroup *, void * )
{
    NL::Socket *socket = serverSocket->accept();
    uint32_t id = ( uint32_t ) socket;

    LOG ( "id=%08x", id );

    Lock lock ( context.mutex );

    context.acceptedSockets[id] = shared_ptr<NL::Socket> ( socket );
    context.socketGroup->add ( socket );

    LOG ( "Socket::accepted ( %08x )", id );
    context.accepted ( id );
}

void Socket::Disconnect::exec ( NL::Socket *socket, NL::SocketGroup *, void * )
{
    uint32_t id = ( uint32_t ) socket;

    LOG ( "id=%08x", id );

    Lock lock ( context.mutex );

    context.socketGroup->remove ( socket );
    context.acceptedSockets.erase ( id );
    if ( context.tcpSocket.get() == socket )
        context.tcpSocket.reset();

    LOG ( "Socket::disconnected ( %08x )", id );
    context.disconnected ( id );
}

void Socket::Read::exec ( NL::Socket *socket, NL::SocketGroup *, void * )
{
    uint32_t id = ( uint32_t ) socket;

    LOG ( "id=%08x", id );

    Lock lock ( context.mutex );

    LOG ( "Socket::received ( %08x, bytes, len )", id );
    // context.received
}

void Socket::ListenThread::start()
{
    if ( isListening )
        return;

    isListening = true;
    Thread::start();
}

void Socket::ListenThread::join()
{
    if ( !isListening )
        return;

    isListening = false;
    Thread::join();
}

void Socket::ListenThread::run()
{
    while ( isListening )
    {
        try
        {
            context.socketGroup->listen ( LISTEN_INTERVAL );
        }
        catch ( const NL::Exception& e )
        {
            LOG ( "[%d] %s", e.nativeErrorCode(), e.what() );
            break;
        }
    }
}

void Socket::ConnectThread::start()
{
    reaperThread.start();
    Thread::start();
}

void Socket::ConnectThread::run()
{
    shared_ptr<NL::Socket> socket;
    try
    {
        socket.reset ( new NL::Socket ( addr, port ) );
    }
    catch ( const NL::Exception& e )
    {
        LOG ( "[%d] %s", e.nativeErrorCode(), e.what() );
    }

    uint32_t id = ( uint32_t ) socket.get();
    if ( !id )
        return;

    Lock lock ( context.mutex );
    if ( !context.tcpSocket )
    {
        context.tcpSocket = socket;
        context.addSocketToGroup ( socket );
    }

    LOG ( "Socket::connected ( %08x )", id );
    context.connected ( id );
}

void Socket::ReaperThread::run()
{
    for ( ;; )
    {
        shared_ptr<ConnectThread> connectThread = connectingThreads.pop();

        if ( connectThread )
            connectThread->join();
        else
            break;
    }
}

void Socket::ReaperThread::join()
{
    connectingThreads.push ( shared_ptr<ConnectThread>() );
    Thread::join();
}

void Socket::addSocketToGroup ( const shared_ptr<NL::Socket>& socket )
{
    LOG ( "id=%08x", ( uint32_t ) socket.get() );

    if ( !socketGroup )
    {
        socketGroup.reset ( new NL::SocketGroup() );
        socketGroup->setCmdOnAccept ( &socketAcceptCmd );
        socketGroup->setCmdOnDisconnect ( &socketDisconnectCmd );
        socketGroup->setCmdOnRead ( &socketReadCmd );
    }

    socketGroup->add ( socket.get() );

    listenThread.start();
}

void Socket::listen ( int port )
{
    LOG ( "port=%d", port );

    serverSocket.reset ( new NL::Socket ( port ) );
    udpSocket.reset ( new NL::Socket ( port, NL::UDP ) );

    addSocketToGroup ( serverSocket );
    addSocketToGroup ( udpSocket );
}

void Socket::connect ( string addr, int port )
{
    LOG ( "addr=%s, port=%d", addr.c_str(), port );

    shared_ptr<ConnectThread> connectThread ( new ConnectThread ( *this, addr, port ) );
    connectThread->start();
    connectingThreads.push ( connectThread );
}

void Socket::disconnect ( uint32_t id )
{
    LOG ( "id=%08x", id );

    LOCK ( mutex );

    if ( id == 0 || tcpSocket.get() )
    {
        listenThread.join();

        acceptedSockets.clear();
        socketGroup.reset();
        udpSocket.reset();
        tcpSocket.reset();
        serverSocket.reset();
    }
    else
    {
        auto it = acceptedSockets.find ( id );
        if ( it != acceptedSockets.end() && it->second )
        {
            socketGroup->remove ( it->second.get() );
            acceptedSockets.erase ( it );
        }
    }
}

bool Socket::isServer() const
{
    LOCK ( mutex );
    return ( ( bool ) serverSocket && !tcpSocket );
}

bool Socket::isConnected() const
{
    LOCK ( mutex );
    return tcpSocket.get();
}

string Socket::localAddr() const
{
    LOCK ( mutex );

    if ( tcpSocket )
        return tcpSocket->hostFrom();

    if ( serverSocket )
        return serverSocket->hostTo();

    return "";
}

string Socket::remoteAddr ( uint32_t id ) const
{
    LOCK ( mutex );

    if ( tcpSocket )
        return tcpSocket->hostTo();

    auto it = acceptedSockets.find ( id );
    if ( it != acceptedSockets.end() && it->second )
        return it->second->hostTo();

    return "";
}

void Socket::send ( uint32_t id, char *bytes, size_t len )
{
    LOCK ( mutex );
}

void Socket::release()
{
    LOG ( "ReaperThread::release()" );
    reaperThread.release();
}
