// engine_spidermonkey.cpp

#include "engine.h"

#ifdef _WIN32
#define XP_WIN
#else
#define XP_UNIX
#endif

#ifdef MOZJS
#include "mozjs/jsapi.h"
#else
#include "js/jsapi.h"
#endif

#include "../client/dbclient.h"

extern const char * jsconcatcode;

namespace mongo {

    class SMScope;

    
    void dontDeleteScope( SMScope * s ){}
    boost::thread_specific_ptr<SMScope> currentScope( dontDeleteScope );


    JSBool resolveBSONField( JSContext *cx, JSObject *obj, jsval id, uintN flags, JSObject **objp );
    void errorReporter( JSContext *cx, const char *message, JSErrorReport *report );

    static JSClass bson_ro_class = {
        "bson_object" , JSCLASS_HAS_PRIVATE | JSCLASS_NEW_RESOLVE , 
        JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_PropertyStub,
        JS_EnumerateStub, (JSResolveOp)(&resolveBSONField) , JS_ConvertStub, JS_FinalizeStub,
        JSCLASS_NO_OPTIONAL_MEMBERS
    };

    
    class Convertor {
    public:
        Convertor( JSContext * cx ){
            _context = cx;
        }
        
        string toString( JSString * so ){
            jschar * s = JS_GetStringChars( so );
            size_t srclen = JS_GetStringLength( so );
            
            size_t len = srclen * 2;
            char * dst = (char*)malloc( len );
            assert( JS_EncodeCharacters( _context , s , srclen , dst , &len) );
            
            string ss( dst , len );
            free( dst );
            return ss;
        }

        string toString( jsval v ){
            return toString( JS_ValueToString( _context , v ) );            
        }

        // ---------- to spider monkey ---------
        
        jsval toval( double d ){
            jsval val;
            assert( JS_NewNumberValue( _context, d , &val ) );
            return val;
        }

        jsval toval( const char * c ){
            JSString * s = JS_NewStringCopyZ( _context , c );
            return STRING_TO_JSVAL( s );
        }

        JSObject * toObject( const BSONObj * obj , bool readOnly=true ){
            // TODO: make a copy and then delete it
            JSObject * o = JS_NewObject( _context , &bson_ro_class , NULL, NULL);
            JS_SetPrivate( _context , o , (void*)obj );
            return o;
        }
        
        jsval toval( const BSONObj* obj , bool readOnly=true ){
            JSObject * o = toObject( obj , readOnly );
            return OBJECT_TO_JSVAL( o );
        }
        
        jsval toval( const BSONElement& e ){

            switch( e.type() ){
            case EOO:
                return JSVAL_NULL;
            case NumberDouble:
            case NumberInt:
                return toval( e.number() );
            case String:
                return toval( e.valuestr() );
            default:
                log() << "resolveBSONField can't handle type: " << (int)(e.type()) << endl;
            }
            
            uassert( "not done" , 0 );
            return 0;
        }
        
    private:
        JSContext * _context;
    };

    static JSClass global_class = {
        "global", JSCLASS_GLOBAL_FLAGS,
        JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_PropertyStub,
        JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, JS_FinalizeStub,
        JSCLASS_NO_OPTIONAL_MEMBERS
    };    

    // --- global helpers ---

    JSBool native_print( JSContext * cx , JSObject * obj , uintN argc, jsval *argv, jsval *rval ){
        Convertor c( cx );
        for ( uintN i=0; i<argc; i++ ){
            if ( i > 0 )
                cout << " ";
            cout << c.toString( argv[i] );
        }
        cout << endl;
        return JS_TRUE;
    }

    JSFunctionSpec globalHelpers[] = { 
        {  "print" , &native_print , 0 , 0 , 0 } , 
        { 0 , 0 , 0 , 0 , 0 } 
    };

    // ----END global helpers ----

    
    JSBool resolveBSONField( JSContext *cx, JSObject *obj, jsval id, uintN flags, JSObject **objp ){
        Convertor c( cx );
        
        BSONObj * o = (BSONObj*)(JS_GetPrivate( cx , obj ));
        string s = c.toString( id );
       
        BSONElement e = (*o)[ s.c_str() ];

        if ( e.type() == EOO ){
            *objp = 0;
            return JS_TRUE;
        }
        
        jsval val = c.toval( e );

        assert( JS_SetProperty( cx , obj , s.c_str() , &val ) );
        *objp = obj;
        return JS_TRUE;
    }
    

    class SMScope;
    
    class SMEngine : public ScriptEngine {
    public:
        
        SMEngine(){
            _runtime = JS_NewRuntime(8L * 1024L * 1024L);
            uassert( "JS_NewRuntime failed" , _runtime );

        }

        ~SMEngine(){
            JS_DestroyRuntime( _runtime );
            JS_ShutDown();
        }

        Scope * createScope();
        
        void runTest();
        
    private:
        JSRuntime * _runtime;
        friend class SMScope;
    };
    
    SMEngine * globalSMEngine;


    void ScriptEngine::setup(){
        globalSMEngine = new SMEngine();
        globalScriptEngine = globalSMEngine;
    }

    // ------ mongo stuff ------

    JSBool mongo_constructor( JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval ){
        uassert( "mongo_constructor not implemented yet" , 0 );
        throw -1;
    }
    
    JSBool mongo_local_constructor( JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval ){
        DBClientBase * client = createDirectClient();
        cout << "client c: " << client << endl;
        JS_SetPrivate( cx , obj , (void*)client );
        return JS_TRUE;
    }

    void mongo_finalize( JSContext * cx , JSObject * obj ){
        DBClientBase * client = (DBClientBase*)JS_GetPrivate( cx , obj );
        if ( client ){
            cout << "client x: " << client << endl;
            cout << "Addr in finalize: " << client->getServerAddress() << endl;
            delete client;
        }
    }

    static JSClass mongo_class = {
        "Mongo" , JSCLASS_HAS_PRIVATE | JSCLASS_NEW_RESOLVE ,
        JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_PropertyStub,
        JS_EnumerateStub, JS_ResolveStub , JS_ConvertStub, mongo_finalize,
        0 , 0 , 0 , 
        mongo_constructor , 
        0 , 0 , 0 , 0
    };

    static JSClass mongo_local_class = {
        "Mongo" , JSCLASS_HAS_PRIVATE | JSCLASS_NEW_RESOLVE ,
        JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_PropertyStub,
        JS_EnumerateStub, JS_ResolveStub , JS_ConvertStub, mongo_finalize,
        0 , 0 , 0 , 
        mongo_local_constructor , 
        0 , 0 , 0 , 0
    };

    JSBool object_id_constructor( JSContext *cx, JSObject *obj, uintN argc, jsval *argv, jsval *rval ){

        OID oid;
        if ( argc == 0 ){
            oid.init();
        }
        else {
            uassert( "not done yet" , 0 );
        }
        
        Convertor c( cx );
        jsval v = c.toval( oid.str().c_str() );
        assert( JS_SetProperty( cx , obj , "str" , &v  ) );

        return JS_TRUE;
    }

    static JSClass object_id_class = {
        "ObjectId" , JSCLASS_HAS_PRIVATE ,
        JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_PropertyStub,
        JS_EnumerateStub, JS_ResolveStub , JS_ConvertStub, JS_FinalizeStub,
        0 , 0 , 0 , 
        object_id_constructor , 
        0 , 0 , 0 , 0
    };

    // ------ scope ------


    class SMScope : public Scope {
    public:
        SMScope(){
            _context = JS_NewContext( globalSMEngine->_runtime , 8192 );
            _convertor = new Convertor( _context );
            massert( "JS_NewContext failed" , _context );
            
            JS_SetOptions( _context , JSOPTION_VAROBJFIX);
            //JS_SetVersion( _context , JSVERSION_LATEST); TODO
            JS_SetErrorReporter( _context , errorReporter );
            
            _global = JS_NewObject( _context , &global_class, NULL, NULL);
            massert( "JS_NewObject failed for global" , _global );
            massert( "js init failed" , JS_InitStandardClasses( _context , _global ) );
            
            JS_DefineFunctions( _context , _global , globalHelpers );

            _this = 0;
        }

        ~SMScope(){
            uassert( "deleted SMScope twice?" , _convertor );
            
            if ( _convertor ){
                delete _convertor;
                _convertor = 0;
            }

            if ( _context ){
                JS_DestroyContext( _context );
                _context = 0;
            }
        }
        
        void reset(){
            massert( "not implemented yet" , 0 );
        }
        
        void init( BSONObj * data ){
            massert( "not implemented yet" , 0 );            
        }

        void localConnect( const char * dbName ){
            jsval mongo = OBJECT_TO_JSVAL( JS_NewObject( _context , &mongo_local_class , 0 , 0 ) );
            assert( JS_SetProperty( _context , _global , "Mongo" , &mongo ) );
            
            jsval objectid = OBJECT_TO_JSVAL( JS_NewObject( _context , &object_id_class , 0 , 0 ) );
            assert( JS_SetProperty( _context , _global , "ObjectId" , &objectid ) );
            exec( jsconcatcode );
        }

        // ----- getters ------
        double getNumber( const char *field ){
            jsval val;
            assert( JS_GetProperty( _context , _global , field , &val ) );
            return toNumber( val );
        }
        
        double toNumber( jsval v ){
            double d;
            uassert( "not a number" , JS_ValueToNumber( _context , v , &d ) );
            return d;
        }

        string getString( const char *field ){
            jsval val;
            assert( JS_GetProperty( _context , _global , field , &val ) );
            JSString * s = JS_ValueToString( _context , val );
            return _convertor->toString( s );
        }

        bool getBoolean( const char *field ){
            jsval val;
            assert( JS_GetProperty( _context , _global , field , &val ) );
            
            JSBool b;
            assert( JS_ValueToBoolean( _context, val , &b ) );

            return b;
        }
        
        BSONObj getObject( const char *field ){
            massert( "not implemented yet: getObject()" , 0 ); throw -1;  
        }

        int type( const char *field ){
            jsval val;
            assert( JS_GetProperty( _context , _global , field , &val ) );
            
            switch ( JS_TypeOfValue( _context , val ) ){
            case JSTYPE_VOID: return Undefined;
            case JSTYPE_NULL: return jstNULL;
            case JSTYPE_OBJECT:	return Object;
            case JSTYPE_FUNCTION: return Code;
            case JSTYPE_STRING: return String;
            case JSTYPE_NUMBER: return NumberDouble;
            case JSTYPE_BOOLEAN: return Bool;
            default:
                uassert( "unknown type" , 0 );
            }
            return 0;
        }

        // ----- setters ------
        
        void setNumber( const char *field , double val ){
            jsval v = _convertor->toval( val );
            assert( JS_SetProperty( _context , _global , field , &v ) );
        }

        void setString( const char *field , const char * val ){
            jsval v = _convertor->toval( val );
            assert( JS_SetProperty( _context , _global , field , &v ) );
        }

        void setObject( const char *field , const BSONObj& obj ){
            jsval v = _convertor->toval( &obj );
            JS_SetProperty( _context , _global , field , &v );
        }

        void setBoolean( const char *field , bool val ){
            jsval v = BOOLEAN_TO_JSVAL( val );
            assert( JS_SetProperty( _context , _global , field , &v ) );            
        }

        void setThis( const BSONObj * obj ){
            _this = _convertor->toObject( obj );
        }

        // ---- functions -----
        
        bool hasFunctionIdentifier( const string& code ){
            return 
                code.find( "function(" ) == 0 ||
                code.find( "function (" ) ==0 ;
        }

        JSFunction * compileFunction( const char * code ){
            if ( ! hasFunctionIdentifier( code ) ){
                string s = code;
                if ( strstr( code , "return" ) == 0 )
                    s = "return " + s;
                return JS_CompileFunction( _context , 0 , "anonymous" , 0 , 0 , s.c_str() , strlen( s.c_str() ) , "nofile" , 0 );
            }
            
            // TODO: there must be a way in spider monkey to do this - this is a total hack

            string s = "return ";
            s += code;
            s += ";";

            JSFunction * func = JS_CompileFunction( _context , 0 , "anonymous" , 0 , 0 , s.c_str() , strlen( s.c_str() ) , "nofile" , 0 );
            jsval ret;
            JS_CallFunction( _context , 0 , func , 0 , 0 , &ret );
            return JS_ValueToFunction( _context , ret );
        }

        ScriptingFunction createFunction( const char * code ){
            return (ScriptingFunction)compileFunction( code );
        }

        void precall(){
            _error = "";
            currentScope.reset( this );
        }
        
        void exec( const char * code ){
            precall();
            jsval ret;
            assert( JS_EvaluateScript( _context , _global , code , strlen( code ) , "anon" , 0 , &ret ) );
        }

        int invoke( JSFunction * func , const BSONObj& args ){
            precall();
            jsval rval;
            
            int nargs = args.nFields();
            jsval smargs[nargs];
            
            BSONObjIterator it( args );
            for ( int i=0; i<nargs; i++ )
                smargs[i] = _convertor->toval( it.next() );
            
            if ( ! JS_CallFunction( _context , _this , func , nargs , smargs , &rval ) ){
                return -3;
            }
            
            assert( JS_SetProperty( _context , _global , "return" , &rval ) );
            return 0;
        }

        int invoke( ScriptingFunction funcAddr , const BSONObj& args ){
            return invoke( (JSFunction*)funcAddr , args );
        }

        void gotError( string s ){
            _error = s;
        }
        
        string getError(){
            return _error;
        }

    private:
        JSContext * _context;
        Convertor * _convertor;

        JSObject * _global;
        JSObject * _this;

        string _error;
    };

    void errorReporter( JSContext *cx, const char *message, JSErrorReport *report ){
        stringstream ss;
        ss << "JS Error: " << message;
        
        if ( report ){
            ss << " " << report->filename << ":" << report->lineno;
        }
        
        log() << ss.str() << endl;
        currentScope->gotError( ss.str() );
        
        // TODO: send to Scope
    }



    void SMEngine::runTest(){
        // this is deprecated
    }

    Scope * SMEngine::createScope(){
        return new SMScope();
    }
    
    
}