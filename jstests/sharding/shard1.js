/**
* this tests some of the ground work
*/

s = new ShardingTest( "shard1" , 2 );

db = s.getDB( "test" );
db.foo.insert( { num : 1 , name : "eliot" } );
db.foo.insert( { num : 2 , name : "sara" } );
db.foo.insert( { num : -1 , name : "joe" } );
assert.eq( 3 , db.foo.find().length() );

shardCommand = { shard : "test.foo" , key : { num : 1 } };

assert.throws( function(){ s.adminCommand( shardCommand ); } );

s.adminCommand( { partition : "test" } );
assert.eq( 3 , db.foo.find().length() , "after partitioning count failed" );

s.adminCommand( shardCommand );
dbconfig = s.config.databases.findOne( { name : "test" } );
assert.eq( dbconfig.sharded["test.foo"] , { num : 1 } , "Sharded content" );

assert.eq( 1 , s.config.shard.count() );
si = s.config.shard.findOne();
assert( si );
assert.eq( si.ns , "test.foo" );

assert.eq( 3 , db.foo.find().length() , "after sharding, no split count failed" );


s.stop();
