#include "Logger.h"
#include "EventManager.h"
#include "TimerManager.h"
#include "SocketManager.h"
#include "ControllerManager.h"
#include "GameManager.h"
#include "TcpSocket.h"
#include "UdpSocket.h"
#include "Timer.h"
#include "Test.h"
#include "Messages.h"
#include "Constants.h"

#include <optionparser.h>
#include <windows.h>

#include <cassert>

using namespace std;
using namespace option;


#define LOG_FILE FOLDER "debug.log"

enum optionIndex { UNKNOWN, HELP, GTEST, STDOUT, PLUS };


struct Main
        : public Socket::Owner
        , public Timer::Owner
        , public ControllerManager::Owner
        , public Controller::Owner
        , public GameManager::Owner
{
    GameManager gm;
    Timer timer;
    Controller *controller;

    void doneMapping ( Controller *controller, uint32_t key ) override
    {
        assert ( controller == this->controller );
    }

    void attachedJoystick ( Controller *controller ) override
    {
        this->controller = controller;

        controller->startMapping ( this, 0x10 );
    }

    void detachedJoystick ( Controller *controller ) override
    {
        if ( this->controller == controller )
            this->controller = 0;
    }

    void readEvent ( Socket *socket, const MsgPtr& msg, const IpAddrPort& address ) override
    {
        LOG ( "Got %s from '%s'", msg, address );
    }

    void timerExpired ( Timer *timer ) override
    {
        assert ( timer == &this->timer );

        gm.closeGame();
        EventManager::get().stop();
    }

    void gameOpened() override
    {
        LOG ( "Game opened" );
    }

    void gameClosed() override
    {
        LOG ( "Game closed" );

        EventManager::get().stop();
    }

    Main ( Option opt[] ) : gm ( this ), timer ( this )
    {
        if ( opt[STDOUT] )
            Logger::get().initialize();
        else
            Logger::get().initialize ( LOG_FILE );
        TimerManager::get().initialize();
        SocketManager::get().initialize();
        ControllerManager::get().initialize ( this );

        gm.openGame();

        // timer.start ( 5000 );
    }

    ~Main()
    {
        gm.closeGame();

        ControllerManager::get().deinitialize();
        SocketManager::get().deinitialize();
        TimerManager::get().deinitialize();
        Logger::get().deinitialize();
    }
};

static void signalHandler ( int signum )
{
    LOG ( "Interupt signal %d received", signum );
    EventManager::get().release();
}

static BOOL WINAPI consoleCtrl ( DWORD ctrl )
{
    LOG ( "Console ctrl %d received", ctrl );
    EventManager::get().release();
    return TRUE;
}

int main ( int argc, char *argv[] )
{
    static const Descriptor options[] =
    {
        { UNKNOWN, 0,  "",        "", Arg::None, "Usage: " BINARY " [options]\n\nOptions:" },
        { HELP,    0, "h",    "help", Arg::None, "  --help, -h    Print usage and exit." },
        { GTEST,   0,  "",   "gtest", Arg::None, "  --gtest       Run unit tests and exit." },
        { STDOUT,  0,  "",  "stdout", Arg::None, "  --stdout      Output logs to stdout." },
        { PLUS,    0, "p",    "plus", Arg::None, "  --plus, -p    Increment count." },
        {
            UNKNOWN, 0, "",  "", Arg::None,
            "\nExamples:\n"
            "  " BINARY " --unknown -- --this_is_no_option\n"
            "  " BINARY " -unk --plus -ppp file1 file2\n"
        },
        { 0, 0, 0, 0, 0, 0 }
    };

    // Skip program name argv[0] if present, because optionparser doesn't like it
    argc -= ( argc > 0 );
    argv += ( argc > 0 );

    Stats stats ( options, argc, argv );
    Option opt[stats.options_max], buffer[stats.buffer_max];
    Parser parser ( options, argc, argv, opt, buffer );

    if ( parser.error() )
        return -1;

    if ( opt[HELP] )
    {
        printUsage ( cout, options );
        return 0;
    }

    signal ( SIGABRT, signalHandler );
    signal ( SIGINT, signalHandler );
    signal ( SIGTERM, signalHandler );
    SetConsoleCtrlHandler ( consoleCtrl, TRUE );

    if ( opt[GTEST] )
    {
        Logger::get().initialize();
        int result = RunAllTests ( argc, argv );
        Logger::get().deinitialize();
        return result;
    }

    for ( Option *it = opt[UNKNOWN]; it; it = it->next() )
        cout << "Unknown option: " << it->name << endl;

    for ( int i = 0; i < parser.nonOptionsCount(); ++i )
        cout << "Non-option #" << i << ": " << parser.nonOption ( i ) << endl;

    Main main ( opt );
    EventManager::get().start();
    return 0;
}
