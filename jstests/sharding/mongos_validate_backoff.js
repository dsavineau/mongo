//
// Ensures that single mongos shard-key errors are fast, but slow down when many are triggered  
//

var st = new ShardingTest({ shards : 1, mongos : 1 })

var mongos = st.s0
var admin = mongos.getDB( "admin" )
var coll = mongos.getCollection( "foo.bar" )

printjson( admin.runCommand({ enableSharding : coll.getDB() + "" }) )

coll.ensureIndex({ shardKey : 1 }, {clustering: true})
printjson( admin.runCommand({ shardCollection : coll + "", key : { shardKey : 1 } }) )

var timeBadInsert = function(){

    var start = new Date().getTime()

    // Bad insert, no shard key
    coll.insert({ hello : "world" })
    assert.neq( null, coll.getDB().getLastError() )

    var end = new Date().getTime()    

    return end - start
}

// Loop over this test a few times, to ensure that the error counters get reset if we don't have
// bad inserts over a long enough time.
for( var test = 0; test < 3; test++ ){
    
    var firstWait = timeBadInsert()
    var lastWait = 0
    
    for( var i = 0; i < 20; i++ ){
        printjson( lastWait = timeBadInsert() )
    }
    
    // Kind a heuristic test, we want to make sure that the error wait after sleeping is much less
    // than the error wait after a lot of errors
    assert.gt( lastWait, firstWait * 2 * 2 )
    
    // Sleeping for long enough to reset our exponential counter
    sleep( 3000 )
}

jsTest.log( "DONE!" )

st.stop()
