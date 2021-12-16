#ifndef MNENGINE_H
#define MNENGINE_H

// pool states for mixing
#define POOL_STATUS_UNKNOWN                    0 // waiting for update
#define POOL_STATUS_IDLE                       1 // waiting for update
#define POOL_STATUS_QUEUE                      2 // waiting in a queue
#define POOL_STATUS_ACCEPTING_ENTRIES          3 // accepting entries
#define POOL_STATUS_FINALIZE_TRANSACTION       4 // master node will broadcast what it accepted
#define POOL_STATUS_SIGNING                    5 // check inputs/outputs, sign final tx
#define POOL_STATUS_TRANSMISSION               6 // transmit transaction
#define POOL_STATUS_ERROR                      7 // error
#define POOL_STATUS_SUCCESS                    8 // success

// status update message constants
#define MASTERNODE_ACCEPTED                    1
#define MASTERNODE_REJECTED                    0
#define MASTERNODE_RESET                       -1

#define MNengine_QUEUE_TIMEOUT                 30
#define MNengine_SIGNING_TIMEOUT               15

// used for anonymous relaying of inputs/outputs/sigs
#define MNengine_RELAY_IN                 1
#define MNengine_RELAY_OUT                2
#define MNengine_RELAY_SIG                3

void ThreadCheckMNenginePool();

#endif // MNENGINE_H
