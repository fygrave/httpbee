id = "Check XSS stuff"
description= "xss"
tags = "xss"

counter = 20;

init = function (host, port, request) 
    print ("called init\n");
    return 0
end

request = function (host, port, request)
    requeststring = "GET /blah" .. counter .. ".html HTTP/1.0";
    counter = counter -1;

    if (counter == 0) then return "" else return requeststring end
end
response  = function (host, port, request, response)

    rg = pcre.new("/404/",0, "" )
    a, b, c = rg.match(response, 0, 1)
    
    if ( c) then print("File not found\n") end

    print ("Got response from "..host..":"..port.."\n"..response);


    return 0;
end

fini = function()
    print(id.." script execution completed\n")

end
