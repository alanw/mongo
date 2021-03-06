// dbshell.cpp

#include <stdio.h>

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "../scripting/engine.h"
#include "../client/dbclient.h"
#include "utils.h"

extern const char * jsconcatcode;


void shellHistoryInit(){
#ifdef USE_READLINE
    using_history();
    read_history( ".dbshell" );
#else
    cout << "type \"exit\" to exit" << endl;
#endif
}
void shellHistoryDone(){
#ifdef USE_READLINE
    write_history( ".dbshell" );
#endif
}
void shellHistoryAdd( const char * line ){
    if ( strlen(line) == 0 )
        return;
#ifdef USE_READLINE
    add_history( line );
#endif
}
char * shellReadline( const char * prompt ){
#ifdef USE_READLINE
  return readline( "> " );
#else
  printf( "> " );
  char * buf = new char[1024];
  char * l = fgets( buf , 1024 , stdin );
  int len = strlen( buf );
  buf[len-1] = 0;
  return l;
#endif
}

#if !defined(_WIN32)
#include <execinfo.h>
#include <string.h>

void quitNicely( int sig ){
    if ( sig == SIGPIPE )
        mongo::rawOut( "mongo got signal SIGPIPE\n" );
    shellHistoryDone();
    exit(0);
}

void quitAbruptly( int sig ) {
    ostringstream ossSig;
    ossSig << "mongo got signal " << sig << " (" << strsignal( sig ) << "), stack trace: " << endl;
    mongo::rawOut( ossSig.str() );
    
    ostringstream ossBt;
    mongo::printStackTrace( ossBt );
    mongo::rawOut( ossBt.str() );
    
    mongo::shellUtils::KillMongoProgramInstances();
    exit(14);    
}

void setupSignals() {
    signal( SIGINT , quitNicely );
    signal( SIGTERM , quitNicely );
    signal( SIGPIPE , quitNicely ); // Maybe just log and continue?
    signal( SIGABRT , quitAbruptly );
    signal( SIGSEGV , quitAbruptly );
    signal( SIGBUS , quitAbruptly );
    signal( SIGFPE , quitAbruptly );
}
#else
inline void setupSignals() {}
#endif

string fixHost( string url , string host , string port ){
    if ( host.size() == 0 && port.size() == 0 ){
        if ( url.find( "/" ) == string::npos && url.find( "." ) != string::npos )
            return url + "/test";
        return url;
    }
    
    if ( url.find( "/" ) != string::npos ){
        cerr << "url can't have host or port if you specify them individually" << endl;
        exit(-1);
    }
    
    if ( host.size() == 0 )
        host = "127.0.0.1";

    string newurl = host;
    if ( port.size() > 0 )
        newurl += ":" + port;
    
    newurl += "/" + url;
    
    return newurl;
}

int main(int argc, char* argv[]) {
    setupSignals();
    
    mongo::shellUtils::RecordMyLocation( argv[ 0 ] );

    mongo::ScriptEngine::setup();
    auto_ptr< mongo::Scope > scope( mongo::globalScriptEngine->createScope() );
    
    string url = "test";
    string dbhost;
    string port;
    
    string username;
    string password;

    bool runShell = false;
    bool nodb = false;
    
    string script;
    
    int argNumber = 1;
    for ( ; argNumber < argc; argNumber++) {
        const char* str = argv[argNumber];
        
        if (strcmp(str, "--shell") == 0) {
            runShell = true;
            continue;
        } 
        
        if (strcmp(str, "--nodb") == 0) {
            nodb = true;
            continue;
        } 

        if ( strcmp( str , "--port" ) == 0 ){
            port = argv[argNumber+1];
            argNumber++;
            continue;
        }

        if ( strcmp( str , "--host" ) == 0 ){
            dbhost = argv[argNumber+1];
            argNumber++;
            continue;
        }

        if ( strcmp( str , "--eval" ) == 0 ){
            script = argv[argNumber+1];
            argNumber++;
            continue;
        }
        
        if ( strcmp( str , "-u" ) == 0 ){
            username = argv[argNumber+1];
            argNumber++;
            continue;
        }

        if ( strcmp( str , "-p" ) == 0 ){
            password = argv[argNumber+1];
            argNumber++;
            continue;
        }

        if ( strstr( str , "-p" ) == str ){
            password = str;
            password = password.substr( 2 );
            continue;
        }

        if ( strcmp(str, "--help") == 0 || 
             strcmp(str, "-h" ) == 0 ) {

            cout 
                << "usage: " << argv[0] << " [options] [db address] [file names]\n" 
                << "db address can be:\n"
                << "   foo   =   foo database on local machine\n" 
                << "   192.169.0.5/foo   =   foo database on 192.168.0.5 machine\n" 
                << "   192.169.0.5:9999/foo   =   foo database on 192.168.0.5 machine on port 9999\n"
                << "options\n"
                << " --shell run the shell after executing files\n"
                << " -u <username>\n"
                << " -p<password> - notice no space\n"
                << " --host <host> - server to connect to\n"
                << " --port <port> - port to connect to\n"
                << " --nodb don't connect to mongo program on startup.  No 'db address' arg expected.\n"
                << " --eval <script> evaluate javascript.\n"
                << "file names: a list of files to run.  will exit after unless --shell is specified\n"
                ;
            
            return 0;
        } 

        if (strcmp(str, "-f") == 0) {
            continue;
        } 
        
        if (strncmp(str, "--", 2) == 0) {
            printf("Warning: unknown flag %s.\nTry --help for options\n", str);
            continue;
        } 
        
        if ( nodb )
            break;

        const char * last = strstr( str , "/" );
        if ( last )
            last++;
        else
            last = str;
        
        if ( ! strstr( last , "." ) ){
            url = str;
            continue;
        }
        
        if ( strstr( last , ".js" ) )
            break;

        path p( str );
        if ( ! boost::filesystem::exists( p ) ){
            url = str;
            continue;
        }
            

        break;
    }
    
    scope->externalSetup();
    mongo::shellUtils::installShellUtils( *scope );

    if ( !nodb ) { // connect to db
        cout << "url: " << url << endl;
        string setup = (string)"db = connect( \"" + fixHost( url , dbhost , port ) + "\")";
        if ( ! scope->exec( setup , "(connect)" , false , true , false ) )
            return -1;
        
        if ( username.size() && password.size() ){
            stringstream ss;
            ss << "if ( ! db.auth( \"" << username << "\" , \"" << password << "\" ) ){ throw 'login failed'; }";

            if ( ! scope->exec( ss.str() , "(auth)" , true , true , false ) ){
                cout << "login failed" << endl;
                return -1;
            }

        }

    }    
    
    if ( !script.empty() ) {
        script = "function() { " + script + " }";
        mongo::shellUtils::MongoProgramScope s;
        if ( scope->invoke( script.c_str(), mongo::BSONObj() ) )
            return -4;
    }
    
    int numFiles = 0;
    
    for ( ; argNumber < argc; argNumber++) {
        const char* str = argv[argNumber];

        mongo::shellUtils::MongoProgramScope s;

        if ( ! scope->execFile( str , false , true , false ) ){
            return -3;
        }
        
        numFiles++;
    }
    
    if ( numFiles == 0 && script.empty() )
        runShell = true;

    if ( runShell ){
        
        mongo::shellUtils::MongoProgramScope s;

        shellHistoryInit();
        
        cout << "type \"help\" for help" << endl;
        
        //v8::Handle<v8::Object> shellHelper = baseContext_->Global()->Get( v8::String::New( "shellHelper" ) )->ToObject();
        
        while ( 1 ){
            
            char * line = shellReadline( "> " );
            
            if ( ! line || ( strlen(line) == 4 && strstr( line , "exit" ) ) ){
                cout << "bye" << endl;
                break;
            }
            
            string code = line;
            if ( code == "exit" ){
                break;
            }
            
            bool wascmd = false;
            {
                string cmd = line;
                if ( cmd.find( " " ) > 0 )
                    cmd = cmd.substr( 0 , cmd.find( " " ) );
                
                scope->exec( (string)"__iscmd__ = shellHelper[\"" + cmd + "\"];" , "(shellhelp1)" , false , true , true );
                if ( scope->getBoolean( "__iscmd__" )  ){
                    scope->exec( (string)"shellHelper( \"" + cmd + "\" , \"" + code.substr( cmd.size() ) + "\");" , "(shellhelp2)" , false , true , false );
                    wascmd = true;
                }
                
            }
            
            if ( ! wascmd ){
                scope->exec( code.c_str() , "(shell)" , false , true , false );
                scope->exec( "shellPrintHelper( __lastres__ );" , "(shell2)" , true , true , false );
            }
            
            
            shellHistoryAdd( line );
        }
        
        shellHistoryDone();
    }
    
    return 0;
}


namespace mongo {
    DBClientBase * createDirectClient(){
        uassert( "no createDirectClient in shell" , 0 );
        return 0;
    }
}

