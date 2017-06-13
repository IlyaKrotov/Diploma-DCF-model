network Aloha
{
    parameters:
        int numHosts;  // number of hosts
        double txRate @unit(bps);  // transmission rate
        double slotTime @unit(ms);  // zero means no slots (pure Aloha)
        @display("bgi=background/terrain,s");
    submodules:
        host[numHosts]: Host {
            txRate = txRate;
            slotTime = slotTime;
        }
   // connections allowunconnected:
   //     for i=0..numHosts-1 {
   //         host[i].out --> server.in;
   //     }
}