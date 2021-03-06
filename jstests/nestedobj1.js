//SERVER-5127, SERVER-5036
//assert(false, "nestedobj1 test disabled, causes weird crash when destroying FieldRangeSet");

if ( typeof _threadInject == "undefined" ) { // SERVER-6448

function makeNestObj(depth){
    toret = { a : 1};

    for(i = 1; i < depth; i++){
        toret = {a : toret};
    }

    return toret;
}

t = db.objNestTest;
t.drop();
t.ensureIndex({a:1});

nestedObj = makeNestObj(500);

t.insert( { tst : "test1", a : nestedObj }, true );
t.insert( { tst : "test2", a : nestedObj }, true );
t.insert( { tst : "test3", a : nestedObj }, true );

assert.eq(3, t.count(), "records in collection");
assert.eq(1, t.find({tst : "test2"}).count(), "find test");

//make sure index insertion succeeded (these keys are not too big for TokuDB)
assert.eq(3, t.find().hint({a:1}).explain().n, "index not empty");
print("Test succeeded!")
}
