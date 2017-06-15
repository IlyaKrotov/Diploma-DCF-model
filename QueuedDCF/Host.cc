#include <omnetpp.h>
#include <string>
#include <time.h>
#include <stdlib.h>
#include <list>
#include <iostream>
#include "messages_m.h"

#define RETRY_LIMIT 3
#define HOSTS_NUMBER 3

using namespace omnetpp;

class Queue : public cObject {
    public:
        Queue(int newCapacity)
        {   
            capacity = newCapacity;
            currentNumberOfFrames = 0;
        }

        bool push(cMessage* pkg) 
        {   
            auto pkt = dynamic_cast<cPacket*>(pkg);
            if(currentNumberOfFrames < capacity) 
            {
                queue.push_back(pkt);
                currentNumberOfFrames++;
                return true;
            } else {
                return false;
            }
        }

        cPacket* pop() 
        {
            if(currentNumberOfFrames > 0)
            {   
                currentNumberOfFrames--;
                auto msg = queue.front();
                queue.pop_front();
                return msg;
            } else 
            {
                return nullptr;
            }
        }

        int currentOccupancy()
        {
            return currentNumberOfFrames;
        }
        
    private:
        std::list<cPacket*> queue;
        int capacity;
        int currentNumberOfFrames;
};

class signalMessage: public cObject 
{
    public:
        bool value;
        int hostId;
        simtime_t sendingTime;

        void showMessage()
        {
            EV << "Host id: " << hostId << ", Value: " << value << ", Time: " << sendingTime << endl;
        }
};
Register_Class(signalMessage);

class ChannelListener : public cListener, cSimpleModule 
{
    public:
        bool channelBusy;
        std::list<signalMessage*> signalsList; 
        bool corrupted;

        ChannelListener()
        {
            corrupted = false;
        }

        virtual void receiveSignal(cComponent *src, simsignal_t id, cObject* value, cObject *details) 
        {
            signalMessage* tmp = (signalMessage*)malloc(sizeof(signalMessage));
            tmp = static_cast<signalMessage*>(value);

            if (bool(tmp->value) == true) 
            {
                signalsList.push_back(tmp);

                if (signalsList.size() > 1)
                {
                    corrupted = true;
                    EV << "corruption set" << endl << "number of frames in collision: " << signalsList.size() << endl;
                }
            } 
            else 
            {   
                if (signalsList.size() > 1)
                {
                    corrupted = true;
                    EV << "corruption set" << endl << "number of frames in collision: " << signalsList.size() << endl;
                }

                for(auto it = signalsList.begin(); it != signalsList.end(); it++)
                {
                    if((*it)->hostId == tmp->hostId)
                    {
                        it = signalsList.erase(it);
                    }
                } 

                if (signalsList.size() == 0)
                {
                    EV << "corruption reset" << endl;   
                    corrupted = false;
                }
            } 

            EV << endl << "List after recived signal" << endl;
            for(auto it = signalsList.begin(); it != signalsList.end(); it++)
            {   
                (*it)->showMessage();
            }
        }

        virtual bool channelBusyness()
        {
            return signalsList.size() == 0 ? false : true;
        }

        virtual bool isCorrupted()
        {
            if (signalsList.size() > 1)
            {
                corrupted = true;
                EV << "corruption set" << endl << "number of frames in collision: " << signalsList.size() << endl;
            }
            return corrupted;
        }

        virtual void resetCorruption()
        {   
            for(auto it = signalsList.begin(); it != signalsList.end(); it++)
            {
                it = signalsList.erase(it);
            } 

            EV << "corruption reset" << endl;   
            corrupted = false;
        }

        virtual bool succesfullTransmition()
        {
            return signalsList.size() == 1 ? true : false;
        }
};

class Host : public cSimpleModule {
    protected:
        virtual void initialize()
        {
            srand(time(NULL));

            radioDelay = &par("radioDelay");
            difs = par("DIFS").doubleValue();
            sifs = par("SIFS").doubleValue();
            slot = par("slotTime").doubleValue();
            cw_min = static_cast<int>(par("CWmin").longValue());
            cw_max = static_cast<int>(par("CWmax").longValue());
            cw = cw_min;
            retryLimit = 0;
            timeout = new cMessage("timeout!");
            
            difs = sifs + (slot*2.0);
            pkLenBits = 0.001;

            scheduleAt(simTime() + difs, timeout);
            state = DIFS;
            n_errors = 0;

            channelWasBusy = false;
            resend = false;

            queue = new Queue(10);
            data_frame = getFirstDataFrame();
            if(data_frame == nullptr)
            {   
                state = WAIT_FOR_MESSAGE;
            }


            channelIsBusy = registerSignal("channelIsBusy");
            channelListener = new ChannelListener();
            getSimulation()->getSystemModule()->subscribe("channelIsBusy", channelListener); 

            WATCH(backoff);
            WATCH(resend);
            WATCH(n_errors);
            WATCH(numOfSendedPackets);
            
            for(int i = 0; i < HOSTS_NUMBER; i++) 
            {   
                hosts[i] = getSimulation()->getModuleByPath(hostNameGen(i));
            }

            EV  << "My ID: " << getId() << endl;

            //stats
            sendSignal = registerSignal("send");
            waitSignal = registerSignal("waitBegin");
            receiveSignal = registerSignal("receive");
            collisionSignal = registerSignal("collision");
            ACKWaitSignal = registerSignal("ACKWaitLength");

            gate("in")->setDeliverOnReceptionStart(true);
        }

        char* hostNameGen(int i) 
        {
            std::string tmp = "QueuedDCF.host[" + std::to_string(i) + "]";
            char *cstr = new char[tmp.length() + 1];
            strcpy(cstr, tmp.c_str());
            return cstr;
        }

        virtual void finish() 
        {   
            recordScalar("number of errors", n_errors);
            recordScalar("number of transmitted packets", numOfSendedPackets);
            recordScalar("duration", simTime());
        }

        virtual void handleMessage(cMessage *msg)
        {   
            if(msg->getArrivalGateId() == gate("packetsIn")->getId())
            {
                EV << "Message to queue is arrived, id:" << getId() << endl;
                if(queue->push(msg))
                {
                    EV << "Succesful push, current occupancy: " << queue->currentOccupancy() << endl;
                }
                else
                {
                    EV << "Queue is overloaded! " << endl;
                }
            }
            else if (msg == timeout)
            {   
                if (state == WAIT_FOR_MESSAGE)
                {
                    data_frame = getNextDataFrame();
                    if(data_frame == nullptr)
                    {   
                        cancelEvent(timeout);
                        scheduleAt(simTime() + slot, timeout);
                    }
                    else 
                    {
                        state = DIFS;
                        cancelEvent(timeout);
                        scheduleAt(simTime() + slot, timeout);
                    }
                }
                else if (state == DIFS)
                {
                    EV << "DIFS: before new boff, " << channelListener->channelBusyness()  << endl;
                    if (!channelListener->channelBusyness()) 
                    {   
                        backoff = intuniform(0, pow(2, cw) - 1);
                        EV << "New backoff: " << backoff << ", host ID: " << getId() << endl;
                    }

                    if (backoff <= 0 && channelWasBusy == false)
                    {   
                        sendStartTime = simTime();

                        sendData(data_frame);

                        ACKWaitStartTime = simTime();
                    }
                    else if (channelWasBusy == false) 
                    {
                        state = BACKOFF;
                        scheduleAt(simTime() + slot, timeout);
                    }
                    else 
                    {
                        channelWasBusy = true;
                        state = DIFS;
                        scheduleAt(simTime() + difs, timeout);
                    }

                }
                else if (state == BACKOFF)
                {

                    if (!channelListener->channelBusyness()) backoff--;

                    if (backoff <= 0 && channelWasBusy == false)
                    {
                        sendStartTime = simTime();

                        sendData(data_frame);

                        ACKWaitStartTime = simTime();
                    }
                    else if (channelWasBusy == false) 
                    {
                        scheduleAt(simTime() + slot, timeout);
                    }
                    else 
                    {
                        channelWasBusy = true;
                        state = DIFS;
                        scheduleAt(simTime() + difs, timeout);
                    }
                }
                else if (state == DATA)
                {
                    /* handle data transmission finished, start waiting SIFS + eps */
                    bubble("Sending finished");
                    if(channelListener->succesfullTransmition())
                    {
                        emit(channelIsBusy, newSignal(false, getId(), simTime()));
                    }

                    if(!channelListener->isCorrupted())
                    {
                        state = SIFS;
                        scheduleAt(simTime() + sifs * 1.2, timeout);
                    }
                    else
                    {
                        state = SIFS;
                        scheduleAt(simTime() + slot, timeout);
                    }
                }
                else if (state == SIFS)
                {
                    /* no ACK received, failed to send data */
                    bubble("Sending failed");
                    n_errors++;
                    emit(channelIsBusy, newSignal(false, getId(), simTime()));
                    channelListener->resetCorruption();

                    state = DIFS;
                    if(!resend) waitStartTime = simTime();
                    resend = true;

                    if (cw < cw_max) {
                        cw += 1;
                        EV << "New cw: " << cw << endl; 
                    } else if (retryLimit < RETRY_LIMIT) {
                        EV << "New retry limit: " << retryLimit << endl; 
                        retryLimit++;
                    } else {
                        EV << "Drop old package and setting new frame" << endl; 
                        cw = 1;
                        retryLimit = 0;
                        data_frame = getNextDataFrame();
                    }

                    channelWasBusy = false;
                    cancelEvent(timeout);
                    scheduleAt(simTime() + difs, timeout);
                    /* keep current frame, so no call to getNextDataFrame() here */
                }
                else if (state == BACKOFF_WAIT_RECEIVE)
                {
                    state = BACKOFF;

                    simtime_t dt = simTime() - recvStartTime;
                    emit(receiveSignal, dt.dbl());
                    EV << dt << endl;
                    recvStartTime = 0;

                    emit(channelIsBusy, newSignal(true, getId(), simTime()));

                    sendDirect(getACK(), radioDelay->doubleValue(), pkLenBits, senderModule->gate("in"));
                    channelWasBusy = false;
                    scheduleAt(simTime() + slot, timeout);
                }
                else if (state == DIFS_WAIT_RECEIVE)
                {
                    state = DIFS;

                    simtime_t dt = simTime() - recvStartTime;
                    emit(receiveSignal, dt.dbl());
                    EV << dt << endl;
                    recvStartTime = 0;

                    emit(channelIsBusy, newSignal(true, getId(), simTime()));

                    sendDirect(getACK(), radioDelay->doubleValue(), pkLenBits, senderModule->gate("in"));
                    channelWasBusy = false;
                    scheduleAt(simTime() + slot, timeout);
                }
                else if (state == ACK)
                {   
                    if(channelListener->succesfullTransmition())
                    {
                        emit(channelIsBusy, newSignal(false, getId(), simTime()));
                    }
                    if(!channelListener->isCorrupted()){
                        bubble("Transmitting of message finished succesfully");

                        simtime_t dt = simTime() - sendStartTime;
                        emit(sendSignal, dt.dbl());
                        sendStartTime = 0;

                        numOfSendedPackets++;

                        emit(channelIsBusy, newSignal(false, getId(), simTime()));

                        data_frame = getNextDataFrame();
                        state = DIFS;
                        channelWasBusy = false;
                        cw = 1;
                        retryLimit = 0;
                        cancelEvent(timeout);
                        scheduleAt(simTime() + difs, timeout);
                    } 
                    else
                    {
                        state = SIFS; //like resend
                        scheduleAt(simTime() + slot, timeout);
                    }
                }
                else
                {
                    throw cRuntimeError("unexpected timeout in state = %d", state);
                }
            }
            else
            {
                /* If here, some foreign message came in! */                
                if (isDataFrame(msg))
                {
                    if(!channelListener->isCorrupted()) {
                        bubble("Recieving message");

                        emit(channelIsBusy, newSignal(false, getId(), simTime()));
                        if(channelListener->succesfullTransmition())
                        {
                            emit(channelIsBusy, newSignal(false, msg->getSenderModule()->getId(), simTime()));
                        }

                        if (state == BACKOFF || state == DIFS)
                        {   
                            recvStartTime = simTime();
                            senderModule = msg->getSenderModule();
                            cancelEvent(timeout); // no timeout now!
                            state = state == BACKOFF ? BACKOFF_WAIT_RECEIVE : DIFS_WAIT_RECEIVE;
                            scheduleAt(simTime() + getDataDuration(msg), timeout);
                        }
                        else if (state == ACK || state == SIFS)
                        {
                            /* this is strange, but... */
                            recvStartTime = simTime();
                            senderModule = msg->getSenderModule();
                            cancelEvent(timeout);
                            state = DIFS_WAIT_RECEIVE;
                            scheduleAt(simTime() + getDataDuration(msg), timeout);
                        }
                    }
                    else
                    {   
                        //channelListener->resetCorruption();
                        emit(channelIsBusy, newSignal(false, msg->getSenderModule()->getId(), simTime()));

                        bubble("Collision!");
                        cancelEvent(timeout);
                        scheduleAt(simTime() + getDataDuration(msg), timeout);
                        delete msg;
                    }
                }
                else if (isAckFrame(msg))
                {
                    if(!channelListener->isCorrupted()) {
                        if (state == SIFS)
                        {   
                            bubble("Recieving ACK");

                            emit(channelIsBusy, newSignal(false, getId(), simTime()));
                            if(channelListener->succesfullTransmition())
                            {
                                emit(channelIsBusy, newSignal(false, msg->getSenderModule()->getId(), simTime()));
                            }

                            simtime_t dt = simTime() - ACKWaitStartTime;
                            emit(ACKWaitSignal, dt.dbl());

                            cancelEvent(timeout);
                            auto ack = dynamic_cast<cPacket*>(msg);
                            if(resend) {
                                simtime_t dt = simTime() - waitStartTime;
                                emit(waitSignal, dt.dbl());
                            }
                            resend = false;
                            state = ACK;
                            scheduleAt(simTime() + ack->getDuration(), timeout);
                        }
                        else
                        {
                            bubble("Ignoring ACK");
                            delete msg;
                        }
                    }
                    else
                    {   
                        //channelListener->resetCorruption();
                        emit(channelIsBusy, newSignal(false, msg->getSenderModule()->getId(), simTime()));

                        bubble("Collision!");
                        cancelEvent(timeout);
                        //scheduleAt(simTime() + getDataDuration(msg), timeout);
                        scheduleAt(simTime() + slot, timeout);
                        delete msg;
                    }
                }
            }
        }

        void sendData(cMessage *data_frame)
        {   
            if (data_frame == nullptr)
            {
                state = WAIT_FOR_MESSAGE;
                cancelEvent(timeout);
                scheduleAt(simTime() + slot, timeout);
                return;
            }
            bubble("Sending data frame");
            ASSERT2(!timeout->isScheduled(), "Sending data while timeout is already scheduled");
            channelWasBusy = false;
            state = DATA;
            scheduleAt(simTime() + getDataDuration(data_frame), timeout);

            emit(channelIsBusy, newSignal(true, getId(), simTime()));

            // did in that way, bcs id num != i
            if(resend == false) {
                recieverNumber = rand() % (HOSTS_NUMBER);
                if (recieverNumber + 2 == getId()) recieverNumber = (recieverNumber + 1) % HOSTS_NUMBER;
            }
            
            EV << "Reciever ID: " << recieverNumber + 2 << ", My ID: " << getId() << endl;

            cPacket* sendingPacket = dynamic_cast<cPacket*>(data_frame);
            sendingPacket->setKind(KIND_DATA);
            sendDirect(sendingPacket->dup(), radioDelay->doubleValue(), getDataDuration(data_frame), hosts[recieverNumber]->gate("in"));
        }

        signalMessage* newSignal(bool value, int hostId, simtime_t sendingTime)
        {
            signalMessage* signalMsg = (signalMessage*)malloc(sizeof(signalMessage));
            signalMsg->value = value;
            signalMsg->hostId = hostId;
            signalMsg->sendingTime = sendingTime;
            return signalMsg;
        }

        simtime_t getDataDuration(cMessage *msg)
        {   
            return dynamic_cast<Frame*>(msg)->getFrameDuration();
        }

        cMessage *getFirstDataFrame()
        {
            auto newMsg = queue->pop();
            return dynamic_cast<cMessage*>(newMsg);
        }

        cMessage *getNextDataFrame()
        {
            auto newMsg = queue->pop();
            if(newMsg == nullptr)
            {
                state = WAIT_FOR_MESSAGE;
                cancelEvent(timeout);
                scheduleAt(simTime() + slot, timeout);
            }
            return dynamic_cast<cMessage*>(newMsg);
        }

        cPacket *getACK() 
        {
            cPacket* ACK = new cPacket("ACK");
            ACK->setDuration(pkLenBits);
            ACK->setKind(KIND_ACK);
            return ACK;
        }

        bool isDataFrame(cMessage *msg)
        {
            return msg->getKind() == KIND_DATA;
        }

        bool isAckFrame(cMessage *msg)
        {
            return msg->getKind() == KIND_ACK;
        }

    private:
        simtime_t sifs;
        simtime_t difs;
        simtime_t slot;
        int backoff = 0;
        int cw_min = 0;
        int cw_max = 0;
        int cw = 0;
        int n_errors = 0;
        int retryLimit = 0;
        cMessage *timeout = nullptr;
        cMessage *data_frame = nullptr;

        cPar* radioDelay;
        double txRate;
        double pkLenBits;
        int numHosts;
        cModule* hosts[HOSTS_NUMBER];
        cModule* senderModule;

        ChannelListener* channelListener;
        simsignal_t channelIsBusy;

        bool channelWasBusy;
        bool resend;
        int recieverNumber;

        //stats
        simsignal_t waitSignal;
        simsignal_t sendSignal;
        simsignal_t receiveSignal;
        simsignal_t ACKWaitSignal;
        simsignal_t collisionSignal;
        int numOfSendedPackets = 0;
        simtime_t recvStartTime;
        simtime_t sendStartTime;
        simtime_t ACKWaitStartTime;
        simtime_t waitStartTime;

        Queue* queue;

        enum State {
            IDLE,
            DIFS,
            BACKOFF,
            DATA,
            SIFS,
            ACK,
            BACKOFF_WAIT_RECEIVE,
            DIFS_WAIT_RECEIVE, 
            WAIT_FOR_MESSAGE
        };
        State state = IDLE;
};

Define_Module(Host);

