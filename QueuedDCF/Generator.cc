#include <omnetpp.h>
#include <string>
#include <time.h>
#include <stdlib.h>
#include <list>
#include <iostream>
#include "messages_m.h"

using namespace omnetpp;

class Generator : public cSimpleModule {
	protected:

        virtual void initialize()
        {
            iaTime = &par("iaTime");
            pkLenBits = &par("pkLenBits");
            txRate = par("txRate");

            timeout = new cMessage("timeout!");

            //scheduleAt(simTime() + iaTime->doubleValue(), timeout);
            scheduleAt(simTime(), timeout);
        }

        virtual void handleMessage(cMessage *msg)
        {
        	newPacket = new Frame("data_frame");
        	newPacket->setKind(KIND_DATA);
        	newPacket->setFrameDuration(pkLenBits->doubleValue() / txRate);
        	send(newPacket, "out");

        	scheduleAt(simTime() + iaTime->doubleValue(), timeout);
        }

    private:
        cPar *iaTime;
        cPar *pkLenBits;
        double txRate;
        cMessage *timeout = nullptr;
        Frame* newPacket = nullptr;
};

Define_Module(Generator);
