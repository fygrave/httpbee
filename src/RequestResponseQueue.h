#ifndef REQUESTRESPONSEQUEUE_H
#define REQUESTRESPONSEQUEUE_H
#include <queue>
#include <string>
#include "Mutex.h"

class RequestResponse {
    public:
        std::string request;
        std::string response;
        RequestResponse() {
            request = "";
            response = "";
        }
        RequestResponse(std::string &_req, std::string &_res) {
            request = _req;
            response = _res;
        }
        RequestResponse& operator=(RequestResponse &o) {
            request = o.request;
            response = o.response;
            return *this;
        }
};

class RequestResponseQueue {
    public:
    /**
     * this is singleton. getInstance is used to get instance of the class
     */
        static RequestResponseQueue *getInstance();
    /**
     * Destructor
     */
        ~RequestResponseQueue();
     /**
      * Thread-safe addRequest
      * @param request string (full, like GET / HTTP/1.0\nHost: blargh)
      */
        void addRequest(std::string&);
        /**
         * pops up request from the queue
         */
        std::string getRequest(void);
        /**
         * adds response to the queue (thread-safe)
         * @param request (that produced response)
         * @param response
         */
        void addResponse(std::string&, std::string&);
        /**
         * Returns request/response pair from a queue. pops-up
         */
        RequestResponse getResponse(void);
        /**
         * Returns RequestQueueSize
         */
        int getRequestQueueSize(void);
        /**
         * Returns ResponseQueueSize
         */
        int getResponseQueueSize(void);
        /**
         * Increase number of currently processing queries
         */
        void incPendingQuery(void);
        /**
         * Decease number of pending queries
         */
        void decPendingQuery(void);
        /**
         * Return number of currently active queries.
         */
        int getPendingQueries(void);
    protected:
        RequestResponseQueue();
        
    private:

        static RequestResponseQueue *m_instance;

        std::queue<std::string> requests;
        std::queue<RequestResponse> responses;
        Mutex requestMutex;
        Mutex responseMutex;
        Mutex queryMutex;
        int queryNumber;



};
#endif
