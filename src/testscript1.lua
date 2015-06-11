id = "Check XSS stuff"
description= "filename bruteforce script"
tags = "bruteforce "

counter = 200;

init = function (host, port, request) 
    return 0
end
fini = function()
    print("Script execution completed\n")
    return 0;
end

request = function (host, port, request)
    counter = counter -1;
    requeststring = "GET /blah" .. counter .. ".html HTTP/1.0";

    if (counter == 0) then return "" else return requeststring end
end
response  = function (host, port, request, response)



    return 0;
end

