network QueuedDCF
{
    parameters:
        int numHosts;  // number of hosts
        double txRate @unit(bps);  // transmission rate
        double slotTime @unit(ms);
        double DIFS @unit(s);
        double SIFS @unit(s);
        int CWmin;
        int CWmax;
        @display("bgi=background/terrain,s");
        //int capactity;
    submodules:
        host[numHosts]: Host {
            txRate = txRate;
            slotTime = slotTime;
            DIFS = DIFS;
            SIFS = SIFS;
            CWmin = CWmin;
            CWmax= CWmax;
        }
        generator[numHosts]: Generator;
    connections:
        for i=0..numHosts-1 {
            generator[i].out --> host[i].packetsIn;
        }
}
