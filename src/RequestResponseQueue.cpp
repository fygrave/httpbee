#include "RequestResponseQueue.h"
#include <iostream>
using namespace std;

RequestResponseQueue * RequestResponseQueue::m_instance = NULL;


RequestResponseQueue::RequestResponseQueue() {
}

RequestResponseQueue::~RequestResponseQueue() {
}

/* static */ RequestResponseQueue *RequestResponseQueue::getInstance() {
    if (!m_instance) {
        m_instance = new RequestResponseQueue();
    }
    return (m_instance);
}

void RequestResponseQueue::addRequest(std::string &request) {
    requestMutex.Lock();
    requests.push(request);
    requestMutex.Unlock();
}
std::string RequestResponseQueue::getRequest(void) {
    std::string r;
    requestMutex.Lock();
    r = requests.front();
    requests.pop();
    requestMutex.Unlock();
    return r;
}

void RequestResponseQueue::addResponse(std::string &request, std::string &response) {
    RequestResponse rq = RequestResponse(request, response);
    responseMutex.Lock();
    responses.push(rq);
    responseMutex.Unlock();
}
    
RequestResponse RequestResponseQueue::getResponse(void) {
    RequestResponse rq = RequestResponse();
    responseMutex.Lock();
    rq = responses.front();
    responses.pop(); // how the hell I do it within one-liner.. freakin stl..
    responseMutex.Unlock();
    return rq;
}
int RequestResponseQueue::getRequestQueueSize(void) {
    return (requests.size());
}

int RequestResponseQueue::getResponseQueueSize(void) {
    return (responses.size());
}

void RequestResponseQueue::incPendingQuery(void) {
    queryMutex.Lock();
    queryNumber++;
    queryMutex.Unlock();
}

void RequestResponseQueue::decPendingQuery(void) {
    queryMutex.Lock();
    queryNumber--;
    queryMutex.Unlock();
}

int RequestResponseQueue::getPendingQueries(void) {
   // cout << "pending queries " << queryNumber << " " << requests.size() << " " << responses.size()  << "\n";
    return queryNumber;
}
    
