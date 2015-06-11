print( "Testing the C++ LUA Wrapper" );


print( getenv( "HOME" ) );

system( "/usr/bin/id" );

print ( "Launched process .." );

os.execute( "id > t");


--
--  Called from C++
--
function test( string )
	print ("String is : " .. string );
    fh = io.open("/etc/passwd", "rt")
    while 1 do
        local ln = fh:read("*l")
        if (ln) then print (ln) else break end
    end
    fh:close()
	return 32;
end
