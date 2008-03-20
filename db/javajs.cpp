// java.cpp

#include "stdafx.h"
#include <iostream>
#include <map>
#include <list>

#undef yassert
#include <boost/filesystem/convenience.hpp>
#undef assert
#define assert xassert
#define yassert 1

using namespace boost::filesystem;          

#include "javajs.h"

#ifdef J_USE_OBJ
#include "jsobj.h"
#warning including jsobj.h
#endif

using namespace std;

JavaJSImpl * JavaJS = 0;

JavaJSImpl::JavaJSImpl(){
  _jvm = 0; 
  _env = 0;
  _dbhook = 0;

  char * ed = findEd();
  stringstream ss;

#if defined(_WIN32)
  char colon = ';';
#else
  char colon = ':';
#endif

  ss << "-Djava.class.path=.";
  ss << colon << ed << "/build/";
  
  {
    path includeDir(ed);
    includeDir /= "include";
    directory_iterator end;
    directory_iterator i(includeDir);
    while( i != end ) {
      path p = *i;
      ss << colon << p.string();
      i++;
    }
  }

#if defined(_WIN32)
  ss << colon << "C:\\Program Files\\Java\\jdk1.6.0_05\\lib\\tools.jar";
#else
  ss << colon << "/opt/java/lib/tools.jar";
#endif

  if( getenv( "CLASSPATH" ) )
    ss << colon << getenv( "CLASSPATH" );

  string s = ss.str();
  char * p = (char *)malloc( s.size() * 4 );
  strcpy( p , s.c_str() );
  char *q = p;
#if defined(_WIN32)
  while( *p ) {
    if( *p == '/' ) *p = '\\';
    p++;
  }
#endif

  cout << "using classpath: " << q << endl;

  JavaVMOption * options = new JavaVMOption[3];
  options[0].optionString = q;
  options[1].optionString = "-Djava.awt.headless=true";
  options[2].optionString = "-Xmx300m";
  
  JavaVMInitArgs * vm_args = new JavaVMInitArgs();
  vm_args->version = JNI_VERSION_1_4;
  vm_args->options = options;
  vm_args->nOptions = 3;
  //vm_args->ignoreUnrecognized = JNI_TRUE;

  cerr << "Creating JVM" << endl;
  jint res = JNI_CreateJavaVM( &_jvm, (void**)&_env, vm_args);
  jassert ( res == 0 );

  jassert( _jvm );

  _dbhook = findClass( "ed/js/DBHook" );
  jassert( _dbhook );

  _scopeCreate = _env->GetStaticMethodID( _dbhook , "scopeCreate" , "()J" );
  _scopeReset = _env->GetStaticMethodID( _dbhook , "scopeReset" , "(J)Z" );
  _scopeFree = _env->GetStaticMethodID( _dbhook , "scopeFree" , "(J)V" );

  _scopeGetNumber = _env->GetStaticMethodID( _dbhook , "scopeGetNumber" , "(JLjava/lang/String;)D" );
  _scopeGetString = _env->GetStaticMethodID( _dbhook , "scopeGetString" , "(JLjava/lang/String;)Ljava/lang/String;" );
  _scopeGetBoolean = _env->GetStaticMethodID( _dbhook , "scopeGetBoolean" , "(JLjava/lang/String;)Z" );
  _scopeGetType = _env->GetStaticMethodID( _dbhook , "scopeGetType" , "(JLjava/lang/String;)B" );
  _scopeGetObject = _env->GetStaticMethodID( _dbhook , "scopeGetObject" , "(JLjava/lang/String;Ljava/nio/ByteBuffer;)I" );
  _scopeGuessObjectSize = _env->GetStaticMethodID( _dbhook , "scopeGuessObjectSize" , "(JLjava/lang/String;)J" );
  
  _scopeSetNumber = _env->GetStaticMethodID( _dbhook , "scopeSetNumber" , "(JLjava/lang/String;D)Z" );
  _scopeSetBoolean = _env->GetStaticMethodID( _dbhook , "scopeSetBoolean" , "(JLjava/lang/String;Z)Z" );
  _scopeSetString = _env->GetStaticMethodID( _dbhook , "scopeSetString" , "(JLjava/lang/String;Ljava/lang/String;)Z" );
  _scopeSetObject = _env->GetStaticMethodID( _dbhook , "scopeSetObject" , "(JLjava/lang/String;Ljava/nio/ByteBuffer;)Z" );

  _functionCreate = _env->GetStaticMethodID( _dbhook , "functionCreate" , "(Ljava/lang/String;)J" );
  _invoke = _env->GetStaticMethodID( _dbhook , "invoke" , "(JJ)I" );

  jassert( _scopeCreate );  
  jassert( _scopeReset );
  jassert( _scopeFree );

  jassert( _scopeGetNumber );
  jassert( _scopeGetString );
  jassert( _scopeGetObject );
  jassert( _scopeGetBoolean );
  jassert( _scopeGetType );
  jassert( _scopeGuessObjectSize );

  jassert( _scopeSetNumber );
  jassert( _scopeSetBoolean );
  jassert( _scopeSetString );
  jassert( _scopeSetObject );

  jassert( _functionCreate );
  jassert( _invoke );

//  javajstest();  
}

JavaJSImpl::~JavaJSImpl(){
  if ( _jvm ){
    _jvm->DestroyJavaVM();
    cerr << "Destroying JVM" << endl;
  }
}

// scope

jlong JavaJSImpl::scopeCreate(){ 
  return _env->CallStaticLongMethod( _dbhook , _scopeCreate );
}

jboolean JavaJSImpl::scopeReset( jlong id ){
  return _env->CallStaticBooleanMethod( _dbhook , _scopeReset );
}

void JavaJSImpl::scopeFree( jlong id ){
  _env->CallStaticVoidMethod( _dbhook , _scopeFree );
}

// scope setters

int JavaJSImpl::scopeSetNumber( jlong id , char * field , double val ){
  return _env->CallStaticBooleanMethod( _dbhook , _scopeSetNumber , id , _env->NewStringUTF( field ) , val );
}

int JavaJSImpl::scopeSetString( jlong id , char * field , char * val ){
  return _env->CallStaticBooleanMethod( _dbhook , _scopeSetString , id , _env->NewStringUTF( field ) , _env->NewStringUTF( val ) );
}

#ifdef J_USE_OBJ
int JavaJSImpl::scopeSetObject( jlong id , char * field , JSObj * obj ){
  jobject bb = 0;
  if ( obj ){
    //cout << "from c : " << obj->toString() << endl;
    bb = _env->NewDirectByteBuffer( (void*)(obj->objdata()) , (jlong)(obj->objsize()) );
    jassert( bb );
  }

  return _env->CallStaticBooleanMethod( _dbhook , _scopeSetObject , id , _env->NewStringUTF( field ) , bb );
}
#endif

// scope getters

double JavaJSImpl::scopeGetNumber( jlong id , char * field ){
  return _env->CallStaticDoubleMethod( _dbhook , _scopeGetNumber , id , _env->NewStringUTF( field ) );
}

char * JavaJSImpl::scopeGetString( jlong id , char * field ){
  jstring s = (jstring)_env->CallStaticObjectMethod( _dbhook , _scopeGetString , id , _env->NewStringUTF( field ) );
  if ( ! s )
    return 0;
  
  const char * c = _env->GetStringUTFChars( s , 0 );
  char * buf = new char[ strlen(c) + 1 ];
  strcpy( buf , c );
  _env->ReleaseStringUTFChars( s , c );

  return buf;
}

#ifdef J_USE_OBJ
JSObj * JavaJSImpl::scopeGetObject( jlong id , char * field ){

  int guess = _env->CallStaticIntMethod( _dbhook , _scopeGuessObjectSize , id , _env->NewStringUTF( field ) );

  char * buf = new char[ guess ];
  jobject bb = _env->NewDirectByteBuffer( (void*)buf , guess );
  jassert( bb );
  
  int len = _env->CallStaticIntMethod( _dbhook , _scopeGetObject , id , _env->NewStringUTF( field ) , bb );
  //cout << "len : " << len << endl;
  assert( len > 0 && len < guess ); 

  JSObj * obj = new JSObj( buf , true );
  //cout << "going to print" << endl;
  //cout << obj->toString() << endl;
  return obj;
}
#endif

// other

jlong JavaJSImpl::functionCreate( const char * code ){
  jstring s = _env->NewStringUTF( code );  
  jassert( s );
  jlong id = _env->CallStaticLongMethod( _dbhook , _functionCreate , s );
  return id;
}
 
int JavaJSImpl::invoke( jlong scope , jlong function ){
  return _env->CallStaticIntMethod( _dbhook , _invoke , scope , function );
}

// --- fun run method

void JavaJSImpl::run( char * js ){
  jclass c = findClass( "ed/js/JS" );
  jassert( c );
  
  jmethodID m = _env->GetStaticMethodID( c , "eval" , "(Ljava/lang/String;)Ljava/lang/Object;" );
  jassert( m );
  
  jstring s = _env->NewStringUTF( js );
  cout << _env->CallStaticObjectMethod( c , m , s ) << endl;
}

void JavaJSImpl::printException(){
  jthrowable exc = _env->ExceptionOccurred();
  if ( exc ){
    _env->ExceptionDescribe();
    _env->ExceptionClear();
  }

}

void jasserted(const char *msg, const char *file, unsigned line) { 
  cout << "jassert failed " << msg << " " << file << " " << line << endl;
  if ( JavaJS ) JavaJS->printException();
  throw AssertionException();
}


char * findEd(){

#if defined(_WIN32)
  return "c:/l/ed/ed";
#else

  static list<char*> possibleEdDirs;
  if ( ! possibleEdDirs.size() ){
    possibleEdDirs.push_back( "../../ed/ed/" ); // this one for dwight dev box
    possibleEdDirs.push_back( "../ed/" );
    possibleEdDirs.push_back( "../../ed/" );
  }

  for ( list<char*>::iterator i = possibleEdDirs.begin() ; i != possibleEdDirs.end(); i++ ){
    char * temp = *i;
    DIR * test = opendir( temp );
    if ( ! test )
      continue;
    
    closedir( test );
    cout << "found ed at : " << temp << endl;
    return temp;
  }
  
  return 0;
#endif
};

// ----

int javajstest() {

  int testObject = 1;
  int debug = 0;

  JavaJSImpl& JavaJS = *::JavaJS;
  
  if ( debug ) cout << "about to create scope" << endl;
  jlong scope = JavaJS.scopeCreate();

  jlong func1 = JavaJS.functionCreate( "foo = 5.6; bar = \"eliot\"; abc = { foo : 517 }; " );
  jassert( ! JavaJS.invoke( scope , func1 ) );
  
  assert( 5.6 == JavaJS.scopeGetNumber( scope , "foo" ) );
  assert( ((string)"eliot") == JavaJS.scopeGetString( scope , "bar" ) );
  
  if ( debug ) cout << "func2 start" << endl;
  jassert( JavaJS.scopeSetNumber( scope , "a" , 5.17 ) );
  jassert( JavaJS.scopeSetString( scope , "b" , "eliot" ) );
  jlong func2 = JavaJS.functionCreate( "assert( 5.17 == a ); assert( \"eliot\" == b );" );
  jassert( ! JavaJS.invoke( scope , func2 ) );
  if ( debug ) cout << "func2 end" << endl;

  if ( debug ) cout << "func3 start" << endl;
  jlong func3 = JavaJS.functionCreate( "function(){ z = true; } " );
  jassert( func3 );
  jassert( ! JavaJS.invoke( scope , func3 ) );
  jassert( JavaJS.scopeGetBoolean( scope , "z" ) );
  if ( debug ) cout << "func3 done" << endl;

#ifdef J_USE_OBJ  
  if ( testObject ){

    if ( debug ) cout << "going to get object" << endl;
    JSObj * obj = JavaJS.scopeGetObject( scope , "abc" );
    if ( debug ) cout << "done gettting object" << endl;

    if ( obj && debug ){
      cout << "obj : " << obj->toString() << endl;
    }

    if ( debug ) cout << "func4 start" << endl;    
    JavaJS.scopeSetObject( scope , "obj" , obj );
    if ( debug ) cout << "\t here 1" << endl;
    jlong func4 = JavaJS.functionCreate( "print( tojson( obj ) );" );
    if ( debug ) cout << "\t here 2" << endl;
    jassert( ! JavaJS.invoke( scope , func4 ) );
    if ( debug ) cout << "func4 end" << endl;
    
    if ( debug ) cout << "func5 start" << endl;
    jassert( JavaJS.scopeSetObject( scope , "c" , obj ) );
    jlong func5 = JavaJS.functionCreate( "print( \"setObject : 517 == \" + c.foo );" );
    jassert( func5 );
    jassert( ! JavaJS.invoke( scope , func5 ) );
    if ( debug ) cout << "func5 done" << endl;
  }
#endif

  if ( debug ) cout << "func6 start" << endl;
  for ( int i=0; i<100; i++ ){
    double val = i + 5;
    JavaJS.scopeSetNumber( scope , "zzz" , val );
    jlong func6 = JavaJS.functionCreate( " xxx = zzz; " );
    jassert( ! JavaJS.invoke( scope , func6 ) );
    jassert( val == JavaJS.scopeGetNumber( scope , "xxx" ) );
  }
  if ( debug ) cout << "func6 done" << endl;

  return 0;

}

#if defined(_MAIN)
int main() { return javajstest(); }
#endif