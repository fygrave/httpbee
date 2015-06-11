Script should have form of:

id = "Check XSS stuff"
description= "hallo"
tags = "hallo"

You can keep your globals in global space..
init = function (host, port, request)

 return string
end
request = function (host, port, request)

    requeststring = "blah"; 
    return requeststring; (or "" if you're done)
end
response  = function (host, port, request, response)

    print ("In Response Host"..host.."port"..port.."\n");

    sendmessage(host, port, "this box has hardcore bug");
    return 0;
end


pcre API support:

pcre.new(pattern, flags, locale)
pcre.flags()
pcre_obj:match(string, start, flags)
pcre_obj:exec(string, start, flags)
pcre_obj:gmatch(string, start, flags)

AI support:

fuzzymatch(stringa, stringb) - returns score how similar strings are

Need to think how:
1. Script can keep state and iterate
2. Clarify details to communicate with the exchange server

