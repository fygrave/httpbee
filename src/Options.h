#ifndef OPTIONS_H
#define OPTIONS_H

#include <string>

class Options {
    public:
        std::string target; // target host
        std::string script; // scripts directory (or file)
        std::string request; // optional parameter passed to each script
        bool verbose; // shall we be verbose
        int port; // target host
        int threads; // number of threads to spawn
        int delay; // specify delay between each request
        bool daemon; // Daemon mode (true or false)
        Options() {
            target = "";
            script = "./";
            request = "";
            port = 80;
            verbose = false;
            daemon = false;
        }
};
#endif
