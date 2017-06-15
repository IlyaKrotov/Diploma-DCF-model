network DCF
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
    submodules:
        host[numHosts]: Host {
            txRate = txRate;
            slotTime = slotTime;
            DIFS = DIFS;
            SIFS = SIFS;
            CWmin = CWmin;
            CWmax= CWmax;
        }
}