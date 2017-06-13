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

            
            //EV << "New message arrived ";
            //tmp->showMessage();
            //EV << "Size of list: " << signalsList.size() << endl;

            if (bool(tmp->value) == true) 
            {
                signalsList.push_back(tmp);
                /*
                EV << endl << "List after push" << endl;
                for(auto it = signalsList.begin(); it != signalsList.end(); it++)
                {   
                    (*it)->showMessage();
                }
                */
            } 
            else 
            {   
                /*
                EV << endl << "List before erasement" << endl;
                for(auto it = signalsList.begin(); it != signalsList.end(); it++)
                {   
                    (*it)->showMessage();
                }
                */

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
                /*
                EV << endl << "Erased list" << endl << endl;
                for(auto it = signalsList.begin(); it != signalsList.end(); it++)
                {   
                    (*it)->showMessage();
                }
                */
            } 

            EV << endl << "List after recived signal" << endl;
            for(auto it = signalsList.begin(); it != signalsList.end(); it++)
            {   
                (*it)->showMessage();
            }
        }

        virtual bool channelBusyness()
        {
            if(signalsList.size() == 0) 
            { return true; }
            else 
            { return false; }
        }

        virtual bool isCorrupted()
        {
            return corrupted;
        }

        virtual void resetCorruption()
        {   
            //if(signalsList.size() == 0) 
            {
                EV << "corruption reset" << endl;   
                corrupted = false;
            }
        }
};

class Host : public cSimpleModule {
    protected:
        virtual void initialize()
        {
            srand(time(NULL));

            txRate = par("txRate");
            radioDelay = &par("radioDelay");
            iaTime = &par("iaTime");
            pkLenBits = &par("pkLenBits");
            difs = par("DIFS").doubleValue();
            sifs = par("SIFS").doubleValue();
            slot = par("slotTime").doubleValue();
            cw_min = static_cast<int>(par("CWmin").longValue());
            cw_max = static_cast<int>(par("CWmax").longValue());
            cw = cw_min;
            retryLimit = 0;
            timeout = new cMessage("timeout!");
            
            difs = sifs + (slot*2.0);

            scheduleAt(simTime() + difs, timeout);
            state = DIFS;
            n_errors = 0;

            channelWasBusy = false;
            resend = false;

            data_frame = getFirstDataFrame();

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


        }

        char* hostNameGen(int i) 
        {
            std::string tmp = "DCF.host[" + std::to_string(i) + "]";
            char *cstr = new char[tmp.length() + 1];
            strcpy(cstr, tmp.c_str());
            return cstr;
        }

        virtual void finish() 
        {
            recordScalar("duration", simTime());
        }

        virtual void handleMessage(cMessage *msg)
        {
            if (msg == timeout)
            {
                if (state == DIFS)
                {

                    if (!channelListener->channelBusyness()) 
                    {   
                        backoff = intuniform(0, pow(2, cw) - 1);
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
                    state = DIFS;
                    if(!resend) waitStartTime = simTime();
                    resend = true;

                    if (cw < cw_max) {
                        cw += 1;
                    } else if (retryLimit < RETRY_LIMIT) {
                        retryLimit++;
                    } else {
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

                    sendDirect(getACK(), radioDelay->doubleValue(), pkLenBits->doubleValue() / txRate, senderModule->gate("in"));
                    channelWasBusy = false;
                    scheduleAt(simTime() + slot, timeout);
                }
                else if (state == DIFS_WAIT_RECEIVE)
                {
                    state = DIFS;

                    simtime_t dt = simTime() - recvStartTime;
                    EV << dt << endl;
                    emit(receiveSignal, dt.dbl());
                    recvStartTime = 0;

                    emit(channelIsBusy, newSignal(true, getId(), simTime()));

                    sendDirect(getACK(), radioDelay->doubleValue(), pkLenBits->doubleValue() / txRate, senderModule->gate("in"));
                    channelWasBusy = false;
                    state = ACK;
                    scheduleAt(simTime() + difs, timeout);
                }
                else if (state == ACK)
                {   
                    if(!channelListener->isCorrupted()){
                        bubble("Transmitting of message finished succesfully");

                        simtime_t dt = simTime() - sendStartTime;
                        emit(sendSignal, dt.dbl());
                        sendStartTime = 0;

                        numOfSendedPackets++;
                        signalMessage* signalMsg = (signalMessage*)malloc(sizeof(signalMessage));
                        emit(channelIsBusy, newSignal(false, getId(), simTime()));
                        data_frame = getNextDataFrame();
                        state = DIFS;
                        channelWasBusy = false;
                        cw = 1;
                        retryLimit = 0;
                        scheduleAt(simTime() + difs + iaTime->doubleValue(), timeout);
                    } 
                    else
                    {
                        state = SIFS; //like resend
                        scheduleAt(simTime() + slot, timeout);
                    }
                }
                else if (state = RESEND)
                {

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
                        }/*
                        else if (state == SIFS)
                        {
                            bubble("Collision!");
                            cancelEvent(timeout);
                            scheduleAt(simTime() + getDataDuration(msg), timeout);
                            delete msg;
                        }*/
                    }
                    else
                    {
                        bubble("Collision!");
                        cancelEvent(timeout);
                        scheduleAt(simTime() + getDataDuration(msg), timeout);
                        delete msg;
                    }
                }
                else if (isAckFrame(msg))
                {
                    bubble("Recieving ACK");
                    if (state == SIFS)
                    {   
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
                        /*
                        signalMessage* signalMsg = (signalMessage*)malloc(sizeof(signalMessage));
                        signalMsg->value = true;
                        signalMsg->hostId = getId();
                        signalMsg->sendingTime = simTime();
                        emit(channelIsBusy, signalMsg);

                        state = DIFS; //like resend
                        scheduleAt(simTime() + getDataDuration(msg), timeout);
                        */
                        delete msg;
                    }
                }
            }
        }

        void sendData(cMessage *data_frame)
        {
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
            Frame* newMsg = new Frame("first data_frame");
            newMsg->setFrameDuration(pkLenBits->doubleValue() / txRate);
            return dynamic_cast<cMessage*>(newMsg);
        }

        cMessage *getNextDataFrame()
        {
            Frame* newMsg = new Frame("data_frame");
            newMsg->setFrameDuration(pkLenBits->doubleValue() / txRate);
            return dynamic_cast<cMessage*>(newMsg);
        }

        cPacket *getACK() 
        {
            cPacket* ACK = new cPacket("ACK");
            ACK->setDuration(pkLenBits->doubleValue() / txRate);
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
        cPar *iaTime;
        cPar *pkLenBits;
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

        enum State {
            IDLE,
            DIFS,
            BACKOFF,
            DATA,
            SIFS,
            ACK,
            BACKOFF_WAIT_RECEIVE,
            DIFS_WAIT_RECEIVE,
            RESEND
        };
        State state = IDLE;
};

Define_Module(Host);
